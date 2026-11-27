// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Glass, water and diamond (core/PartGlass.h): the Part's look as data, its presets and Fresnel, the part_glass command and
// its undo, the project file, glTF's KHR_materials_transmission / ior / volume both ways, and the back-to-front order the
// viewport draws see-through Parts in.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/PartGlass.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "io/GltfIO.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace sf;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }

std::string slurp(const std::string& path) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

} // namespace

TEST(glass_presets_and_fresnel) {
    // The indices the presets promise, and the reflectance of a face looking straight at the eye: ((n - 1) / (n + 1))^2.
    CHECK(near(glassPresetIor(GlassPreset::Water), 1.33f));
    CHECK(near(glassPresetIor(GlassPreset::Glass), 1.5f));
    CHECK(near(glassPresetIor(GlassPreset::Diamond), 2.42f));
    CHECK(near(glassF0(1.5f), 0.04f, 1e-5f));
    CHECK(near(glassF0(2.42f), 0.1722f, 1e-3f));
    CHECK(near(glassF0(1.0f), 0.0f)); // (no change of medium: nothing reflects)
    // Schlick: F0 looking straight on, 1 at a grazing angle, rising between; a denser glass reflects more at every angle.
    CHECK(near(glassFresnel(1.5f, 1.0f), glassF0(1.5f)));
    CHECK(near(glassFresnel(1.5f, 0.0f), 1.0f));
    float last = glassFresnel(1.5f, 1.0f);
    for (float c = 0.9f; c >= 0.0f; c -= 0.1f) {
        const float f = glassFresnel(1.5f, c);
        CHECK(f >= last);
        CHECK(glassFresnel(2.42f, c) > glassFresnel(1.33f, c));
        last = f;
    }
    GlassPreset p;
    CHECK(parseGlassPreset("DIAMOND", p) && p == GlassPreset::Diamond);
    CHECK(!parseGlassPreset("plastic", p));
    const PartGlass water = glassFromPreset(GlassPreset::Water), glass = glassFromPreset(GlassPreset::Glass);
    CHECK(water.active() && glass.active() && near(glass.transmission, 1.0f));
    CHECK(glass.tint == glm::vec3(1.0f)); // glass and diamond stay clear
    CHECK(water.tint != glm::vec3(1.0f) && water.tint.z > water.tint.x); // water leans blue-green
}

TEST(glass_json_round_trip_and_old_files) {
    PartGlass g;
    g.transmission = 0.8f;
    g.ior = 2.1f;
    g.tint = glm::vec3(0.2f, 0.5f, 0.9f);
    g.depth = 3.0f;
    PartGlass back;
    CHECK(glassFromJson(glassToJson(g), back));
    CHECK(near(back.transmission, 0.8f) && near(back.ior, 2.1f) && near(back.depth, 3.0f));
    CHECK(near(back.tint.x, 0.2f, 2e-3f) && near(back.tint.y, 0.5f, 2e-3f) && near(back.tint.z, 0.9f, 2e-3f));
    // Nothing there (a file from before glass): not glass, and `out` is left as it was.
    PartGlass untouched;
    CHECK(!glassFromJson(Json(), untouched) && !untouched.active());
    // Out-of-range and broken values are clamped, never trusted.
    Json bad = Json::object();
    bad.set("transmission", 7.0);
    bad.set("ior", -3.0);
    bad.set("depth", 1e9);
    PartGlass clamped;
    CHECK(glassFromJson(bad, clamped));
    CHECK(near(clamped.transmission, 1.0f) && near(clamped.ior, kGlassIorMin) && near(clamped.depth, kGlassDepthMax));
    PartGlass nan;
    nan.ior = std::nanf("");
    CHECK(near(clampedGlass(nan).ior, PartGlass().ior));
}

TEST(glass_draw_order_is_back_to_front) {
    // Distances in front of the eye: the farthest draws first; equal distances keep the scene's order.
    const std::vector<size_t> order = glassDrawOrder({1.0f, 5.0f, 3.0f, 5.0f});
    CHECK(order.size() == 4);
    CHECK(order[0] == 1 && order[1] == 3 && order[2] == 2 && order[3] == 0);
    CHECK(glassDrawOrder({}).empty());
    const float nan = std::nanf("");
    CHECK(glassDrawOrder({nan, 2.0f, nan, 1.0f}).size() == 4); // (a broken distance doesn't break the sort)
}

TEST(part_glass_command_and_undo) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(1));
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
    CHECK(!doc.parts[0].glass.active() && !doc.parts[1].glass.active());
    CHECK(reg.execute(doc, "part_glass", {{"preset", std::string("diamond")}, {"index", 0.0}}).ok);
    CHECK(doc.parts[0].glass.active() && near(doc.parts[0].glass.ior, 2.42f) && !doc.parts[1].glass.active());
    // An index alone on an ordinary Part makes it glass; a tint is read like every colour argument.
    CHECK(reg.execute(doc, "part_glass", {{"ior", 1.33}, {"tint", std::string("#3399cc")}}).ok);
    CHECK(doc.parts[1].glass.active() && near(doc.parts[1].glass.transmission, 1.0f) && doc.parts[1].glass.tint.z > doc.parts[1].glass.tint.x);
    CHECK(!reg.execute(doc, "part_glass", {{"preset", std::string("plastic")}}).ok);
    CHECK(reg.execute(doc, "part_glass", {{"transmission", 5.0}}).ok && near(doc.parts[1].glass.transmission, 1.0f));
    // One undo step each, in the Part's own history.
    CHECK(reg.execute(doc, "part_glass", {{"preset", std::string("none")}}).ok);
    CHECK(!doc.parts[1].glass.active());
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.parts[1].glass.active());
    CHECK(reg.execute(doc, "redo").ok);
    CHECK(!doc.parts[1].glass.active());
    // A copy of a glass Part is glass.
    CHECK(reg.execute(doc, "part_select", {{"index", 0.0}}).ok);
    CHECK(reg.execute(doc, "part_duplicate").ok);
    CHECK(doc.activePartInfo().glass.active() && near(doc.activePartInfo().glass.ior, 2.42f));
}

TEST(glass_in_the_project_file) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(1));
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
    CHECK(reg.execute(doc, "part_glass", {{"preset", std::string("water")}, {"depth", 2.5}, {"index", 1.0}}).ok);
    const std::string path = tempPath("sf_glass_project.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partCount() == 2);
    CHECK(!back.parts[0].glass.active());
    CHECK(back.parts[1].glass.active() && near(back.parts[1].glass.ior, 1.33f) && near(back.parts[1].glass.depth, 2.5f));
    CHECK(near(back.parts[1].glass.tint.z, doc.parts[1].glass.tint.z, 2e-3f));
    std::filesystem::remove(path);
    // A project with no glass writes no glass key: the files of every scene without it stay as they were.
    Document plain = makeDoc(makeIcosphere(1));
    CHECK(saveProject(path, plain, err));
    Document plainBack;
    CHECK(loadProject(path, plainBack, err) && !plainBack.parts[0].glass.active());
    std::filesystem::remove(path);
}

TEST(glass_in_gltf_round_trip) {
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(2));
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
    CHECK(reg.execute(doc, "part_glass", {{"preset", std::string("water")}, {"depth", 1.5}, {"index", 0.0}}).ok);
    const glm::vec3 tint = doc.parts[0].glass.tint;
    const std::string path = tempPath("sf_glass.glb");
    CHECK(reg.execute(doc, "export_glb", {{"path", path}}).ok);
    const std::string text = slurp(path);
    // The three extensions are used, none required (a viewer without them shows the opaque base), and the Part's material has them.
    CHECK(text.find("\"extensionsUsed\":[\"KHR_materials_ior\",\"KHR_materials_transmission\",\"KHR_materials_volume\"]") != std::string::npos);
    CHECK(text.find("extensionsRequired") == std::string::npos);
    CHECK(text.find("\"KHR_materials_transmission\":{\"transmissionFactor\":1}") != std::string::npos);
    CHECK(text.find("\"KHR_materials_ior\":{\"ior\":1.33") != std::string::npos);
    CHECK(text.find("\"KHR_materials_volume\":{\"thicknessFactor\":") != std::string::npos);
    std::vector<MeshObject> objects;
    std::string err;
    CHECK(loadGltf(path, objects, err) && objects.size() == 2);
    if (objects.size() == 2) {
        const MeshObject& water = objects[0];
        CHECK(water.glass.active() && near(water.glass.transmission, 1.0f) && near(water.glass.ior, 1.33f, 1e-3f));
        CHECK(near(water.glass.depth, 1.5f, 5e-3f)); // (attenuation distance over a quarter of the Part's size)
        CHECK(near(water.glass.tint.x, tint.x, 1e-3f) && near(water.glass.tint.y, tint.y, 1e-3f) && near(water.glass.tint.z, tint.z, 1e-3f));
        CHECK(!objects[1].glass.active());
    }
    // A glass Part appended from the file arrives as glass.
    Document other = makeDoc(makeIcosphere(1));
    CHECK(reg.execute(other, "part_append", {{"path", path}}).ok);
    CHECK(other.partCount() == 3 && other.parts[1].glass.active() && !other.parts[2].glass.active());
    // Opening it puts the glass on the first Part.
    Document opened;
    CHECK(reg.execute(opened, "open", {{"path", path}}).ok);
    CHECK(opened.partCount() == 2 && opened.parts[0].glass.active() && near(opened.parts[0].glass.ior, 1.33f, 1e-3f));
    // A scene with no glass writes none of it.
    Document plain = makeDoc(makeIcosphere(1));
    CHECK(reg.execute(plain, "export_glb", {{"path", path}}).ok);
    const std::string plainText = slurp(path);
    CHECK(plainText.find("KHR_materials") == std::string::npos && plainText.find("extensionsUsed") == std::string::npos);
    std::filesystem::remove(path);
}
