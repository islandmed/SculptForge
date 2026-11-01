// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project files (.sfproj): lossless round trip of the mesh, every optional
// attribute, layers and splats; version handling; dirty tracking; recent files.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Texture.h"
#include "io/Zip.h"

#include <cmath>
#include <filesystem>

using namespace sf;

namespace {

Document makeRichDoc() {
    Mesh m = makeIcosphere(2);
    m.ensureMask();
    m.ensureColors();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        m.mask[v] = float(v % 7) / 6.0f;
        m.colors[v] = glm::vec3(float(v % 3) * 0.5f, 0.25f, 1.0f / float(v + 1));
    }
    m.ensurePolygroups();
    m.ensureHidden();
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        m.polygroups[t] = t % 5 + 100000u;
        m.hidden[t] = t % 11 == 0 ? 1 : 0;
    }
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t i = 0; i < m.uvs.size(); ++i) m.uvs[i] = glm::vec2(float(i) * 0.001f, -float(i) * 0.5f);
    m.addLayer("Smile");
    m.addLayer("Blink L");
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        m.layers[0].delta[v] = glm::vec3(0.01f * float(v % 4), 0, 0);
        m.layers[1].delta[v] = glm::vec3(0, -0.002f * float(v % 9), 0.1f);
    }
    m.setLayerWeight(0, 0.75f);
    m.setLayerWeight(1, -0.3f);

    Document doc = makeDoc(std::move(m));
    doc.activeLayer = 1;
    doc.splats.resize(37, 1);
    for (size_t i = 0; i < doc.splats.size(); ++i) {
        doc.splats.positions[i] = glm::vec3(float(i), -float(i) * 0.5f, 2.0f);
        doc.splats.rotations[i] = glm::normalize(glm::quat(1.0f, 0.1f * float(i), 0.2f, -0.3f));
        doc.splats.scales[i] = glm::vec3(0.01f, 0.02f, 0.03f * float(i + 1));
        doc.splats.opacities[i] = float(i) / 37.0f;
    }
    for (size_t i = 0; i < doc.splats.sh.size(); ++i) doc.splats.sh[i] = glm::vec3(float(i) * 0.01f, 0.5f, -0.25f);
    doc.splats.transform = glm::mat4(2.0f);
    doc.splats.transform[3] = glm::vec4(1, 2, 3, 1);
    doc.splats.name = "Capture";
    BrushRecipe recipe;
    recipe.name = "Test Scales";
    recipe.description = "for the project test";
    doc.activeBrush = doc.addBrush(recipe);
    Json cam = Json::object();
    cam.set("distance", 7.5);
    doc.projectSettings.set("camera", std::move(cam));
    doc.projectSettings.set("future_feature", "kept");
    return doc;
}

std::string readManifest(const std::string& path) {
    std::vector<ZipEntry> entries;
    std::string err;
    if (!readStoredZip(path, entries, err)) return {};
    for (const auto& e : entries)
        if (e.name == "manifest.json") return std::string(e.data.begin(), e.data.end());
    return {};
}

// Rewrites the manifest of an existing project with `edit` applied.
bool editManifest(const std::string& path, const std::function<void(Json&)>& edit) {
    std::vector<ZipEntry> entries;
    std::string err;
    if (!readStoredZip(path, entries, err)) return false;
    for (auto& e : entries) {
        if (e.name != "manifest.json") continue;
        Json j;
        if (!Json::parse(std::string(e.data.begin(), e.data.end()), j, err)) return false;
        edit(j);
        const std::string text = j.dump();
        e.data.assign(text.begin(), text.end());
    }
    return writeStoredZip(path, entries, 1, err);
}

// Applies `edit` to the "mesh" object of the first SubTool in a manifest.
void editFirstMesh(Json& j, const std::function<void(Json&)>& edit) {
    Json subtools = Json::array();
    for (size_t i = 0; i < j["subtools"].size(); ++i) {
        Json st = j["subtools"][i];
        if (i == 0) {
            Json mesh = st["mesh"];
            edit(mesh);
            st.set("mesh", std::move(mesh));
        }
        subtools.push(std::move(st));
    }
    j.set("subtools", std::move(subtools));
}

} // namespace

TEST(project_round_trip_every_attribute) {
    Document doc = makeRichDoc();
    const std::string path = tempPath("sf_project_roundtrip.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));

    Document back;
    back.splats.resize(3, 0); // replaced by the project's splats
    CHECK(loadProject(path, back, err));
    const Mesh& a = doc.mesh;
    const Mesh& b = back.mesh;
    CHECK(b.positions == a.positions);
    CHECK(b.triangles == a.triangles);
    CHECK(b.base == a.base);
    CHECK(b.mask == a.mask);
    CHECK(b.colors == a.colors);
    CHECK(b.polygroups == a.polygroups);
    CHECK(b.hidden == a.hidden);
    CHECK(b.uvs == a.uvs);
    CHECK(b.layers.size() == 2);
    for (size_t i = 0; i < b.layers.size() && i < a.layers.size(); ++i) {
        CHECK(b.layers[i].name == a.layers[i].name);
        CHECK(b.layers[i].weight == a.layers[i].weight);
        CHECK(b.layers[i].delta == a.layers[i].delta);
    }
    CHECK(b.normals.size() == b.vertexCount());
    CHECK(back.activeLayer == 1);

    const SplatCloud& sa = doc.splats;
    const SplatCloud& sb = back.splats;
    CHECK(sb.size() == sa.size() && sb.shDegree == sa.shDegree);
    CHECK(sb.positions == sa.positions && sb.scales == sa.scales && sb.opacities == sa.opacities && sb.sh == sa.sh);
    bool rotOk = sb.rotations.size() == sa.rotations.size();
    for (size_t i = 0; rotOk && i < sa.rotations.size(); ++i)
        rotOk = sb.rotations[i].x == sa.rotations[i].x && sb.rotations[i].y == sa.rotations[i].y &&
                sb.rotations[i].z == sa.rotations[i].z && sb.rotations[i].w == sa.rotations[i].w;
    CHECK(rotOk);
    CHECK(sb.transform == sa.transform);
    CHECK(sb.name == "Capture");

    CHECK(back.activeBrush >= 0 && back.brushes[size_t(back.activeBrush)].name == "Test Scales");
    CHECK(back.projectSettings["camera"]["distance"].asNumber() == 7.5);
    CHECK(back.projectSettings["future_feature"].asString() == "kept");
    CHECK(back.path == path);
    CHECK(!back.dirty());
    CHECK(!back.undo.canUndo());
    std::filesystem::remove(path);
}

TEST(project_plain_mesh_keeps_attributes_empty) {
    Document doc = makeDoc(makeIcosphere(1));
    const std::string path = tempPath("sf_project_plain.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.mesh.positions == doc.mesh.positions);
    CHECK(!back.mesh.hasMask() && !back.mesh.hasColors() && !back.mesh.hasPolygroups() && !back.mesh.hasHidden() &&
          !back.mesh.hasUvs());
    CHECK(back.mesh.layers.empty() && back.mesh.base.empty() && back.splats.empty());
    std::filesystem::remove(path);
}

TEST(project_commands_and_open) {
    CommandRegistry reg;
    Document doc = makeRichDoc();
    const std::string path = tempPath("sf_project_cmd.sfproj");
    std::filesystem::remove(path);
    CHECK(!reg.execute(doc, "save_project").ok); // no project path yet
    const std::string noExt = path.substr(0, path.size() - std::string(kProjectExtension).size());
    CHECK(reg.execute(doc, "save_project", {{"path", noExt}}).ok);
    CHECK(doc.path == path && std::filesystem::exists(path) && !doc.dirty());
    CHECK(reg.execute(doc, "save_project").ok); // defaults to the project path

    Document other = makeDoc(makeIcosphere(1));
    CommandResult r = reg.execute(other, "open", {{"path", path}});
    CHECK(r.ok);
    CHECK(other.mesh.triangleCount() == doc.mesh.triangleCount() && other.mesh.layers.size() == 2);
    Document third;
    CHECK(reg.execute(third, "open_project", {{"path", path}}).ok);
    CHECK(third.mesh.colors == doc.mesh.colors);
    CHECK(!reg.execute(third, "open_project", {{"path", tempPath("sf_project_missing.sfproj")}}).ok);
    CHECK(isProjectPath("a/b/Scene.SFPROJ") && !isProjectPath("scene.obj"));
    std::filesystem::remove(path);
}

TEST(project_subtool_shading) {
    // Smooth / Flat per SubTool (#11): an optional key, so files without it (and older readers) are unaffected.
    CommandRegistry reg;
    Document doc = makeDoc(makeIcosphere(1));
    CHECK(reg.execute(doc, "subtool_append", {{"type", std::string("cube")}}).ok);
    CHECK(doc.activeSubtoolInfo().shading == Shading::Default);
    CHECK(reg.execute(doc, "subtool_shading", {{"mode", std::string("flat")}}).ok);
    CHECK(reg.execute(doc, "subtool_shading", {{"mode", std::string("Smooth")}, {"index", 0.0}}).ok);
    CHECK(!reg.execute(doc, "subtool_shading", {{"mode", std::string("faceted")}}).ok);
    CHECK(doc.subtools[0].shading == Shading::Smooth && doc.subtools[1].shading == Shading::Flat);
    CHECK(reg.execute(doc, "subtool_duplicate").ok); // the copy keeps it
    CHECK(doc.subtoolCount() == 3 && doc.activeSubtoolInfo().shading == Shading::Flat);

    const std::string path = tempPath("sf_project_shading.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.subtoolCount() == 3);
    CHECK(back.subtools[0].shading == Shading::Smooth && back.subtools[1].shading == Shading::Flat &&
          back.subtools[2].shading == Shading::Flat);

    // Unknown values load as Default rather than failing the file.
    Shading s = Shading::Flat;
    CHECK(!parseShading("phong", s) && s == Shading::Flat);
    CHECK(parseShading("DEFAULT", s) && s == Shading::Default);
    std::filesystem::remove(path);
}

TEST(project_attachments) {
    // Document::projectFiles (an embedded custom MatCap): saved under attachments/ and loaded back byte for byte;
    // names that could escape the folder are not written.
    Document doc = makeDoc(makeIcosphere(1));
    doc.projectFiles["matcaps/Green Gold.png"] = {1, 2, 3, 250};
    doc.projectFiles["../evil.png"] = {9};
    doc.projectFiles["empty.bin"] = {};
    const std::string path = tempPath("sf_project_attachments.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    back.projectFiles["stale"] = {7};
    CHECK(loadProject(path, back, err));
    CHECK(back.projectFiles.size() == 2);
    CHECK(back.projectFiles["matcaps/Green Gold.png"] == std::vector<uint8_t>({1, 2, 3, 250}));
    CHECK(back.projectFiles.count("empty.bin") == 1 && back.projectFiles.count("../evil.png") == 0);
    CHECK(!back.projectSettings.has("attachments")); // a core key, not an app setting
    std::filesystem::remove(path);
}

TEST(project_version_handling) {
    Document doc = makeDoc(makeIcosphere(1));
    const std::string path = tempPath("sf_project_version.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    const std::string manifest = readManifest(path);
    const std::string v = std::to_string(kProjectBaseWriteVersion); // nothing in it needs a newer format
    CHECK(kProjectVersion >= 7);
    CHECK(manifest.find("\"version\": " + v) != std::string::npos || manifest.find("\"version\":" + v) != std::string::npos);
    CHECK(manifest.find("app_version") != std::string::npos);

    // Unknown fields (top level and inside the mesh) are ignored.
    CHECK(editManifest(path, [](Json& j) {
        j.set("shiny_new_thing", Json::object());
        editFirstMesh(j, [](Json& mesh) { mesh.set("creases", "future blob"); });
    }));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.mesh.positions == doc.mesh.positions);

    // Version 2 (SculptForge 0.4, before textures) still loads.
    CHECK(editManifest(path, [](Json& j) { j.set("version", 2); }));
    Document v2;
    CHECK(loadProject(path, v2, err));
    CHECK(v2.mesh.positions == doc.mesh.positions && !v2.mesh.texture);

    // A newer format is refused with a clear message and leaves the document alone.
    CHECK(editManifest(path, [](Json& j) {
        j.set("version", kProjectVersion + 1);
        j.set("app_version", "9.9.9");
    }));
    Document keep = makeDoc(makePlane(4));
    const size_t tris = keep.mesh.triangleCount();
    err.clear();
    CHECK(!loadProject(path, keep, err));
    CHECK(err.find("newer") != std::string::npos && err.find("9.9.9") != std::string::npos);
    CHECK(keep.mesh.triangleCount() == tris);

    // Not a project / corrupt blob.
    CHECK(editManifest(path, [](Json& j) { j.set("format", "something else"); }));
    CHECK(!loadProject(path, keep, err));
    CHECK(saveProject(path, doc, err));
    CHECK(editManifest(path, [](Json& j) {
        editFirstMesh(j, [](Json& mesh) { mesh.set("vertex_count", mesh["vertex_count"].asNumber() + 1); });
    }));
    err.clear();
    CHECK(!loadProject(path, keep, err) && !err.empty());
    CHECK(keep.mesh.triangleCount() == tris);
    std::filesystem::remove(path);
}

TEST(project_dirty_tracking) {
    CommandRegistry reg;
    Document doc;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CHECK(!doc.dirty());
    CHECK(reg.execute(doc, "subdivide").ok);
    CHECK(doc.dirty());
    const std::string path = tempPath("sf_project_dirty.sfproj");
    CHECK(reg.execute(doc, "save_project", {{"path", path}}).ok);
    CHECK(!doc.dirty());
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.dirty());
    CHECK(reg.execute(doc, "redo").ok);
    CHECK(doc.dirty()); // redo is a change too (conservative)
    doc.markClean();
    doc.mesh.ensureMask();
    doc.undo.push(UndoRecord::captureMask(doc.mesh, "Mask All")); // e.g. a command another feature adds
    CHECK(doc.dirty());
    doc.markClean();
    CHECK(reg.execute(doc, "layer_add", {{"name", std::string("L")}}).ok);
    CHECK(doc.dirty());
    doc.markClean();
    CHECK(reg.execute(doc, "layer_set_weight", {{"index", 0.0}, {"weight", 0.5}}).ok);
    CHECK(doc.dirty());
    doc.markClean();
    CHECK(reg.execute(doc, "mesh_to_splats", {{"count", 1000.0}}).ok);
    CHECK(doc.dirty());
    CHECK(reg.execute(doc, "open", {{"path", path}}).ok);
    CHECK(!doc.dirty());
    std::filesystem::remove(path);
}

TEST(project_recent_files_logic) {
    std::vector<std::string> list;
    list = addRecentFile(list, "/a.sfproj");
    list = addRecentFile(list, "/b.obj");
    list = addRecentFile(list, "/a.sfproj"); // moves to the front, no duplicate
    CHECK((list == std::vector<std::string>{"/a.sfproj", "/b.obj"}));
    list = addRecentFile(list, "");
    CHECK(list.size() == 2);
    for (int i = 0; i < 15; ++i) list = addRecentFile(list, "/f" + std::to_string(i));
    CHECK(list.size() == kMaxRecentFiles);
    CHECK(list.front() == "/f14" && list.back() == "/f5");
    CHECK(addRecentFile({"x", "y", "z"}, "w", 2) == (std::vector<std::string>{"w", "x"}));

    auto pruned = pruneRecentFiles({"/keep1", "/gone", "", "/keep1", "/keep2"},
                                   [](const std::string& p) { return p != "/gone"; });
    CHECK((pruned == std::vector<std::string>{"/keep1", "/keep2"}));
    CHECK(pruneRecentFiles({"/x"}, [](const std::string&) { return false; }).empty());
}

TEST(project_save_obj_is_an_export) {
    // OBJ drops the mask, groups, visibility and layers: it must not mark the project saved.
    CommandRegistry reg;
    Document doc = makeRichDoc();
    const std::string proj = tempPath("sf_export_obj.sfproj"), obj = tempPath("sf_export_obj.obj");
    CHECK(reg.execute(doc, "save_project", {{"path", proj}}).ok);
    CHECK(reg.execute(doc, "mask_clear").ok);
    CHECK(doc.dirty());
    CHECK(reg.execute(doc, "save_obj", {{"path", obj}}).ok);
    CHECK(std::filesystem::exists(obj));
    CHECK(doc.dirty() && doc.path == proj);
    CHECK(!reg.execute(doc, "save_obj").ok); // never writes an OBJ over the .sfproj
    std::filesystem::remove(proj);
    std::filesystem::remove(obj);
}

TEST(project_save_adds_extension_to_any_other_extension) {
    CommandRegistry reg;
    Document doc = makeRichDoc();
    const std::string asked = tempPath("sf_project_head.obj");
    std::filesystem::remove(asked);
    CHECK(reg.execute(doc, "save_project", {{"path", asked}}).ok);
    CHECK(!std::filesystem::exists(asked)); // the user's OBJ is untouched
    CHECK(doc.path == asked + kProjectExtension && isProjectPath(doc.path));
    CHECK(std::filesystem::exists(doc.path));
    std::filesystem::remove(doc.path);
}

namespace {

// A plane with planar UVs and a 512 texture (texture tests have their own helpers; this one is enough here).
Document texturedDoc() {
    Mesh m = makePlane(8);
    const AABB b = m.bounds();
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = m.positions[m.triangles[t][c]];
            m.uvs[t * 3 + size_t(c)] = glm::vec2((p.x - b.min.x) / b.extent().x, (p.z - b.min.z) / b.extent().z);
        }
    m.texture = makeTexture(512, 512);
    m.paintToTexture = true;
    return makeDoc(std::move(m));
}

} // namespace

TEST(project_texture_without_material_channels_writes_format_6) {
    Document doc = texturedDoc();
    addTextureLayer(*doc.mesh.texture, "Layer 1");
    const std::string path = tempPath("sf_project_v6_texture.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Json manifest;
    CHECK(Json::parse(readManifest(path), manifest, err));
    CHECK(manifest["version"].asNumber() == 6);
    CHECK(!manifest["subtools"][0]["texture"].has("pbr"));
    // A format 6 texture project loads with no material channels.
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.mesh.texture && back.mesh.texture->layers.size() == 1 && !hasMaterialChannels(*back.mesh.texture));
    std::filesystem::remove(path);
}

TEST(project_material_channels_round_trip_as_format_7) {
    Document doc = texturedDoc();
    PaintTexture& t = *doc.mesh.texture;
    for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::Normal, Channel::Height, Channel::Emissive}) setChannelEnabled(t, c, true);
    t.heightNormalStrength = 2.5f;
    const float rough[1] = {0.125f}, height[1] = {0.700012f};
    for (int y = 100; y < 180; ++y)
        for (int x = 100; x < 180; ++x) {
            t.channels[size_t(Channel::Roughness)].base.setValues(x, y, rough);
            t.channels[size_t(Channel::Height)].base.setValues(x, y, height);
        }
    addTextureLayer(t, "Scratches");
    LayerChannel& ln = t.layers[0].channels[size_t(Channel::Normal)];
    ln.blend = kBlendNormalCombine;
    ln.opacity = 0.75f;
    const float n4[4] = {0.7f, 0.5f, 0.9f, 1.0f};
    ln.image.setValues(300, 300, n4);
    t.layers[0].channels[size_t(Channel::Metallic)].enabled = false;
    t.layers[0].channels[0].enabled = false;
    compositeTextureAll(t);
    const std::string path = tempPath("sf_project_v7_pbr.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Json manifest;
    CHECK(Json::parse(readManifest(path), manifest, err));
    CHECK(manifest["version"].asNumber() == 7);
    CHECK(manifest["subtools"][0]["texture"]["pbr"]["channels"].size() == 5);
    Document back;
    CHECK(loadProject(path, back, err));
    const PaintTexture& b = *back.mesh.texture;
    CHECK(enabledChannelMask(b) == enabledChannelMask(t));
    CHECK(b.heightNormalStrength == 2.5f);
    for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::Normal, Channel::Height, Channel::Emissive})
        CHECK(b.channels[size_t(c)].base.sameTexels(t.channels[size_t(c)].base));
    CHECK(std::abs(b.channels[size_t(Channel::Height)].base.value(150, 150) - 0.700012f) < 2e-5f); // 16 bits kept
    CHECK(b.layers.size() == 1);
    CHECK(b.layers[0].channels[size_t(Channel::Normal)].blend == kBlendNormalCombine);
    CHECK(b.layers[0].channels[size_t(Channel::Normal)].opacity == 0.75f);
    CHECK(!b.layers[0].channels[size_t(Channel::Metallic)].enabled);
    CHECK(!b.layers[0].channels[0].enabled);
    CHECK(b.layers[0].channels[size_t(Channel::Normal)].image.sameTexels(ln.image));
    CHECK(b.layers[0].channels[size_t(Channel::Roughness)].image.empty()); // transparent layer channels store nothing
    CHECK(channelImage(b, Channel::Normal).sameTexels(channelImage(t, Channel::Normal)));
    // Sparse: the unpainted channels cost a few bytes in the file.
    CHECK(std::filesystem::file_size(path) < 400 * 1024);
    // Turning every channel off goes back to format 6.
    for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::Normal, Channel::Height, Channel::Emissive})
        setChannelEnabled(*back.mesh.texture, c, false);
    CHECK(saveProject(path, back, err));
    CHECK(Json::parse(readManifest(path), manifest, err));
    CHECK(manifest["version"].asNumber() == 6);
    std::filesystem::remove(path);
}
