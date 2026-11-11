// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bug report packs (core/ReproPack.h): the topology class is right, the stand-in matches it, and nothing of the
// artist's work gets in: no vertex position (as bytes or as text), no texture byte, no name, path or link.
#include "TestHarness.h"

#include "core/BrushRecipe.h"
#include "core/Document.h"
#include "core/Primitives.h"
#include "core/ReproPack.h"
#include "core/Texture.h"
#include "io/Zip.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <random>
#include <set>
#include <tuple>

using namespace sf;

namespace {

std::string tempPack(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("sf-repro-") + name + ".zip")).string();
}

std::vector<ZipEntry> readEntries(const std::string& path) {
    std::vector<ZipEntry> entries;
    std::string err;
    readStoredZip(path, entries, err);
    return entries;
}

bool contains(const std::vector<uint8_t>& hay, const void* needle, size_t n) {
    if (n == 0 || hay.size() < n) return false;
    return std::search(hay.begin(), hay.end(), static_cast<const uint8_t*>(needle), static_cast<const uint8_t*>(needle) + n) !=
           hay.end();
}
bool containsText(const std::vector<uint8_t>& hay, const std::string& s) { return contains(hay, s.data(), s.size()); }
bool packHas(const std::vector<ZipEntry>& pack, const std::string& s) {
    for (const ZipEntry& e : pack)
        if (containsText(e.data, s)) return true;
    return false;
}

// The stand-in's vertices (proxy.ply: the header, then x y z floats).
std::vector<glm::vec3> plyVertices(const std::vector<uint8_t>& ply) {
    const std::string text(ply.begin(), ply.end());
    const size_t v = text.find("element vertex "), end = text.find("end_header\n");
    if (v == std::string::npos || end == std::string::npos) return {};
    const size_t n = std::stoul(text.substr(v + 15));
    std::vector<glm::vec3> out(n);
    if (end + 11 + n * 12 > ply.size()) return {};
    std::memcpy(out.data(), ply.data() + end + 11, n * 12);
    return out;
}

// The leaks a pack must never have: in its text files, every position's float bytes and its value printed at 7 and
// 9 digits; in the stand-in, any of the sculpt's vertices; anywhere, the texture's bytes (runs of 16), names, paths.
int leaks(const Document& doc, const std::vector<ZipEntry>& pack, const std::vector<std::string>& secrets) {
    int found = 0;
    std::set<std::tuple<float, float, float>> source;
    for (int i = 0; i < doc.partCount(); ++i)
        for (const glm::vec3& p : doc.partMesh(i).positions) source.insert({p.x, p.y, p.z});
    for (const ZipEntry& e : pack) {
        if (e.name == "proxy.ply") {
            for (const glm::vec3& p : plyVertices(e.data))
                if (source.count({p.x, p.y, p.z})) {
                    std::printf("  leak: proxy.ply has a vertex of the sculpt (%g %g %g)\n", p.x, p.y, p.z);
                    ++found;
                }
            continue;
        }
        for (const auto& [x, y, z] : source)
            for (const float f : {x, y, z}) {
                if (std::fabs(f) < 1e-3f) continue; // (0 and tiny values are in any file)
                if (contains(e.data, &f, 4)) {
                    std::printf("  leak: %s has the bytes of %g\n", e.name.c_str(), double(f));
                    ++found;
                }
                for (int digits : {7, 9}) {
                    char buf[32];
                    std::snprintf(buf, sizeof buf, "%.*g", digits, double(f));
                    if (std::strlen(buf) >= 7 && containsText(e.data, buf)) {
                        std::printf("  leak: %s has the text %s\n", e.name.c_str(), buf);
                        ++found;
                    }
                }
            }
    }
    for (int i = 0; i < doc.partCount(); ++i)
        if (const auto& tex = doc.partMesh(i).texture; tex)
            for (size_t at = 0; at + 16 <= tex->pixels.size(); at += 997)
                for (const ZipEntry& e : pack)
                    if (contains(e.data, tex->pixels.data() + at, 16)) {
                        std::printf("  leak: %s has texture bytes\n", e.name.c_str());
                        ++found;
                    }
    for (const std::string& s : secrets)
        for (const ZipEntry& e : pack)
            if (containsText(e.data, s)) {
                std::printf("  leak: %s has \"%s\"\n", e.name.c_str(), s.c_str());
                ++found;
            }
    return found;
}

repro::Inputs inputsFor(const Document& doc, const std::vector<std::string>& names) {
    repro::Inputs in;
    in.system = "SculptForge 0.12.7\nOS: Linux 6.9 x86_64\nGPU: NVIDIA GeForce RTX 3060 (4.6.0 NVIDIA 560.35)\n";
    in.settingsJson = R"({"uiScale": 1.25, "libraryFolder": "/home/kira/Studio Assets/troll", "inputStyle": "blender",
                          "recent": ["C:\\Users\\kira\\Desktop\\dragon_final.sfproj"], "brushSet": "Troll Skin Set"})";
    in.log = {"2026-11-11 10:00:00  Opened /mnt/show/troll/troll_head_v12.sfproj",
              "2026-11-11 10:00:01  Exported \"Kira's dragon\" to C:\\Users\\kira\\out\\dragon.fbx",
              "2026-11-11 10:00:02  ComfyUI at https://comfy.acme.internal:8188/prompt answered 500",
              "2026-11-11 10:00:03  Mail kira@acme-animation.com about Dragon Head",
              "2026-11-11 10:00:04  Saved \\\\fileserver\\share\\dragon.obj and ~/sculpts/dragon.ply",
              "2026-11-11 10:00:05  Stroke took 4.2 ms; it's 1/2 done; Part 'Dragon Head' hidden"};
    in.actions = {"open path=<text>", "remesh resolution=320"};
    in.names = names;
    in.doc = &doc;
    return in;
}

const std::vector<std::string> kSecrets = {"kira", "Kira", "troll", "dragon", "Dragon", "acme", "comfy", "fileserver",
                                           "sculpts", "Studio Assets", "Troll Skin", "8188", ".sfproj", ".fbx"};

} // namespace

TEST(repro_redacts_paths_names_links_and_quotes) {
    CHECK(repro::redactText("Opened /home/kira/troll.obj") == "Opened <path>");
    CHECK(repro::redactText("Saved C:\\Users\\kira\\x.sfproj.") == "Saved <path>.");
    CHECK(repro::redactText("see https://comfy.acme.internal:8188/x now") == "see <url> now");
    CHECK(repro::redactText("mail kira@acme.com") == "mail <email>");
    CHECK(repro::redactText("read dragon_final.sfproj (2 MB)") == "read <file> (2 MB)");
    CHECK(repro::redactText("Part \"Dragon Head\" and 'Eye L' hidden") == "Part <text> and <text> hidden");
    CHECK(repro::redactText("it's half done, 1/2 of 4.2 ms") == "it's half done, 1/2 of 4.2 ms");
    CHECK(repro::redactText("remesh resolution=320 preset=game name=Dragon") == "remesh resolution=320 preset=game name=<text>");
    CHECK(repro::redactText("OpenGL 4.6 (Core Profile) Mesa 26.1.8") == "OpenGL 4.6 (Core Profile) Mesa 26.1.8");
    CHECK(repro::redactText("host sw.acme.internal down") == "host <file> down");
    CHECK(repro::redactText("bell\x1b[31mred\x07") == "bell[31mred");
}

TEST(repro_actions_keep_numbers_only) {
    repro::clearActions();
    repro::noteCommand("open", {{"path", std::string("/home/kira/dragon.obj")}, {"unit", 0.01}});
    repro::noteCommand("remesh", {{"resolution", 320.0}, {"keep_parts", true}, {"preset", std::string("game")}});
    repro::noteStroke("clay", 42, 1800);
    const auto a = repro::recentActions();
    CHECK(a.size() == 3);
    CHECK(a[0] == "open path=<text> unit=0.01");
    CHECK(a[1] == "remesh keep_parts=true preset=<text> resolution=320");
    CHECK(a[2] == "stroke clay dabs=42 vertices=1800");
    for (size_t i = 0; i < repro::kActionCount + 50; ++i) repro::noteCommand("undo", {});
    CHECK(repro::recentActions().size() == repro::kActionCount);
    repro::clearActions();
}

TEST(repro_topology_class) {
    const repro::PartStats sphere = repro::partStats(makeIcosphere(3));
    CHECK(sphere.pieces == 1 && sphere.genus == 0 && sphere.openLoops == 0 && sphere.openEdges == 0);
    CHECK(sphere.vertices == 642 && sphere.triangles == 1280);
    const repro::PartStats torus = repro::partStats(makeTorus(24));
    CHECK(torus.pieces == 1 && torus.genus == 1 && torus.openLoops == 0);
    const repro::PartStats plane = repro::partStats(makePlane(8));
    CHECK(plane.pieces == 1 && plane.genus == 0 && plane.openLoops == 1 && plane.openEdges > 0);
    Mesh two = makeIcosphere(2);
    Mesh other = makeTorus(16);
    const uint32_t off = uint32_t(two.positions.size());
    two.positions.insert(two.positions.end(), other.positions.begin(), other.positions.end());
    for (glm::uvec3 t : other.triangles) two.triangles.push_back(t + glm::uvec3(off));
    const repro::PartStats both = repro::partStats(two);
    CHECK(both.pieces == 2 && both.genus == 1);
}

TEST(repro_proxy_matches_counts_not_shapes) {
    repro::SceneStats s;
    s.size = {2.0f, 3.0f, 1.0f};
    s.parts.push_back(repro::partStats(makeIcosphere(4)));
    s.parts.push_back(repro::partStats(makeTorus(32)));
    s.parts.push_back(repro::partStats(makePlane(16)));
    const Mesh proxy = repro::proxyMesh(s);
    uint64_t want = 0;
    for (const auto& p : s.parts) want += p.vertices;
    CHECK(std::fabs(double(proxy.positions.size()) - double(want)) < 0.05 * double(want));
    // Its own topology class: three pieces, one handle, one open loop.
    const repro::PartStats got = repro::partStats(proxy);
    CHECK(got.pieces == 3);
    CHECK(got.genus == 1);
    CHECK(got.openLoops == 1);
    // Inside the scene's size.
    AABB b = proxy.bounds();
    CHECK(b.max.x - b.min.x <= 2.0f + 1e-4f && b.max.y - b.min.y <= 3.0f + 1e-4f && b.max.z - b.min.z <= 1.0f + 1e-4f);
    // A huge scene is scaled down to the cap.
    repro::SceneStats big = s;
    for (auto& p : big.parts) p.vertices *= 1000;
    CHECK(repro::proxyMesh(big, 50000).positions.size() < 60000);
}

TEST(repro_pack_has_no_positions_textures_or_names) {
    Document doc = test::makeDoc(makeIcosphere(4));
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> jitter(-0.37f, 0.41f);
    for (glm::vec3& p : doc.mesh.positions) p = p * 1.7311f + glm::vec3(jitter(rng), jitter(rng), jitter(rng)) * 0.0123f;
    doc.mesh.texture = std::make_shared<PaintTexture>();
    doc.mesh.texture->width = doc.mesh.texture->height = 64;
    doc.mesh.texture->pixels.resize(64 * 64 * 4);
    for (uint8_t& b : doc.mesh.texture->pixels) b = uint8_t(rng());
    doc.activePartInfo().name = "Dragon Head";
    const std::string path = tempPack("leaks");
    std::string err;
    std::vector<repro::Item> items = repro::buildItems(inputsFor(doc, {"Dragon Head", "kira", "troll_head_v12"}));
    CHECK(items.size() == 6);
    CHECK(repro::writePack(path, items, err));
    const std::vector<ZipEntry> pack = readEntries(path);
    CHECK(pack.size() == 7);
    CHECK(leaks(doc, pack, kSecrets) == 0);
    CHECK(packHas(pack, "\"vertices\": 2562"));
    CHECK(packHas(pack, "inputStyle"));     // a preference that's a plain word is kept...
    CHECK(packHas(pack, "\"blender\""));
    CHECK(!packHas(pack, "Troll Skin Set")); // ... names are not
    CHECK(plyVertices(pack.back().data).size() > 2000);
    // An unticked item isn't in the zip, nor in its README.
    for (repro::Item& i : items) i.include = i.id != "log";
    CHECK(repro::writePack(path, items, err));
    std::vector<ZipEntry> entries;
    CHECK(readStoredZip(path, entries, err));
    bool log = false;
    for (const ZipEntry& e : entries) log = log || e.name == "log.txt";
    CHECK(!log && entries.size() == 6 && entries[0].name == "README.txt");
    std::filesystem::remove(path);
}

// Fuzz: random scenes (several Parts, layers, moved Parts, open and closed pieces, colours, textures) and random
// log text with paths and names; no pack ever holds a position, a texture byte or a name.
TEST(repro_pack_fuzz_never_leaks) {
    std::mt19937 rng(20261111);
    const std::string path = tempPack("fuzz");
    for (int round = 0; round < 24; ++round) {
        std::uniform_real_distribution<float> u(-50.0f, 50.0f);
        auto shape = [&](int kind) {
            switch (kind % 5) {
            case 0: return makeIcosphere(1 + int(rng() % 3), 0.5f + float(rng() % 100) / 37.0f);
            case 1: return makeTorus(8 + int(rng() % 24));
            case 2: return makePlane(2 + int(rng() % 12));
            case 3: return makeCylinder(6 + int(rng() % 20));
            default: return makeCube(1 + int(rng() % 6));
            }
        };
        Document doc = test::makeDoc(shape(int(rng())));
        const glm::vec3 shift{u(rng), u(rng), u(rng)};
        for (glm::vec3& p : doc.mesh.positions) p = p * (1.0f + float(rng() % 1000) / 333.0f) + shift;
        doc.activePartInfo().name = "Dragon Head"; // (the log below names it, as an app's log may)
        std::vector<std::string> named; // what the artist named, said in plain words in the log below
        const int extra = int(rng() % 4);
        for (int i = 0; i < extra; ++i) {
            Mesh m = shape(int(rng()));
            for (glm::vec3& p : m.positions) p += glm::vec3(u(rng), u(rng), u(rng));
            if (rng() % 2) m.ensureColors(glm::vec3(0.3f, 0.6f, 0.9f));
            if (rng() % 3 == 0) {
                m.texture = std::make_shared<PaintTexture>();
                m.texture->width = m.texture->height = 32;
                m.texture->pixels.resize(32 * 32 * 4);
                for (uint8_t& b : m.texture->pixels) b = uint8_t(rng());
            }
            doc.addPart(std::move(m), "Secret Part " + std::to_string(rng() % 100000));
            named.push_back(doc.parts.back().name);
        }
        if (rng() % 2 && doc.partCount() > 1) doc.parts[1].transform[3] = glm::vec4(u(rng), u(rng), u(rng), 1.0f);
        if (rng() % 2) named.push_back(doc.mesh.layers[size_t(doc.mesh.addLayer("Wing Scales " + std::to_string(rng() % 1000)))].name);
        if (rng() % 2) {
            doc.addPartFolder("Hero Props " + std::to_string(rng() % 1000));
            named.push_back(doc.partFolders.back().name);
        }
        // The app adds the installed packs' names (PanelReproPack.cpp), the user's and the computer's.
        const std::string packName = "Acme Props " + std::to_string(rng() % 1000);
        named.push_back(packName);
        named.push_back("Dragon Head");
        std::vector<std::string> names = repro::documentNames(doc);
        names.insert(names.end(), {packName, "kira", "troll_head_v12"});
        repro::Inputs in = inputsFor(doc, names);
        in.log.push_back("Installed (pack " + packName + ")"); // as PanelPacks.cpp logs it
        for (const std::string& n : named) in.log.push_back("Selected " + n + " at /tmp/" + n + ".obj");
        std::vector<repro::Item> items = repro::buildItems(in);
        for (repro::Item& i : items) i.include = rng() % 4 != 0;
        std::string err;
        CHECK(repro::writePack(path, items, err));
        const std::vector<ZipEntry> pack = readEntries(path);
        std::vector<std::string> secrets = kSecrets;
        secrets.insert(secrets.end(), named.begin(), named.end());
        secrets.insert(secrets.end(), {"Secret Part", "Wing Scales", "Hero Props", "Acme Props"});
        CHECK(leaks(doc, pack, secrets) == 0);
    }
    std::filesystem::remove(path);
}

TEST(repro_document_names_cover_what_the_artist_named) {
    Document doc = test::makeDoc(makeIcosphere(2));
    doc.activePartInfo().name = "Troll Body";
    doc.mesh.addLayer("Wrinkles Pass");
    doc.addPartFolder("Hero Props");
    BrushRecipe b;
    b.name = "Acme Skin Pores";
    b.origin.source = BrushSource::User;
    doc.addBrush(b);
    doc.path = "/show/troll/troll_v12.sfproj";
    const std::vector<std::string> n = repro::documentNames(doc);
    for (const char* want : {"Troll Body", "Wrinkles Pass", "Hero Props", "Acme Skin Pores", "troll_v12"})
        CHECK(std::find(n.begin(), n.end(), want) != n.end());
    for (const BrushRecipe& r : doc.brushes) // built-in brush names aren't secrets
        if (r.origin.source == BrushSource::Builtin) CHECK(std::find(n.begin(), n.end(), r.name) == n.end());
}
