// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BenchPack.h"

#include "core/Document.h"
#include "core/Json.h"
#include "core/Multires.h"
#include "core/Parts.h"
#include "core/Project.h"
#include "core/Sha256.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace sf {

namespace {

namespace fs = std::filesystem;

fs::path pathOf(const std::string& p) { return fs::path(std::u8string(p.begin(), p.end())); }

std::string utf8Of(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool readAll(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// A file name and nothing else: no folders, no "..", no drive letters.
bool plainFileName(const std::string& s) {
    if (s.empty() || s == "." || s == "..") return false;
    return s.find_first_of("/\\:") == std::string::npos && s.find("..") == std::string::npos;
}

bool plainId(const std::string& s) {
    return !s.empty() && s.size() <= 40 &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '-'; });
}

} // namespace

bool loadBenchPack(const std::string& dir, BenchPack& pack, std::string& error) {
    pack = BenchPack{};
    std::string text;
    const fs::path manifest = pathOf(dir) / "pack.json";
    if (!readAll(manifest, text)) {
        error = "No pack.json in " + dir + " (download the scene pack: docs/benchmark.md)";
        return false;
    }
    Json j;
    if (!Json::parseStrict(text, j, error)) {
        error = "pack.json: " + error;
        return false;
    }
    if (!j.isObject() || j["schema"].asString() != kBenchPackSchema) {
        error = std::string("pack.json: schema is not ") + kBenchPackSchema;
        return false;
    }
    pack.dir = dir;
    pack.name = j["name"].asString();
    pack.version = int(j["version"].asNumber(0));
    pack.strokeSet = j["stroke_set"].asString();
    if (pack.name.empty() || pack.version < 1 || pack.strokeSet.empty()) {
        error = "pack.json: name, version and stroke_set are needed";
        return false;
    }
    const Json& scenes = j["scenes"];
    if (!scenes.isArray() || scenes.size() == 0) {
        error = "pack.json: no scenes";
        return false;
    }
    std::set<std::string> ids;
    Sha256 hash;
    hash.update(text.data(), text.size());
    for (size_t i = 0; i < scenes.size(); ++i) {
        const Json& s = scenes[i];
        BenchPackScene sc;
        sc.id = s["id"].asString();
        sc.title = s["title"].asString();
        sc.file = s["file"].asString();
        sc.tier = s["tier"].asString();
        sc.divide = int(s["divide"].asNumber(-1));
        sc.triangles = s["triangles"].asNumber(0);
        const std::string where = "pack.json scene " + std::to_string(i + 1) + ": ";
        if (!plainId(sc.id) || !ids.insert(sc.id).second) { error = where + "the id must be unique, lowercase letters, digits and dashes"; return false; }
        if (!plainFileName(sc.file) || pathOf(sc.file).extension() != ".sfproj") { error = where + "file must be a plain .sfproj file name"; return false; }
        if (sc.tier != "floor" && sc.tier != "standard" && sc.tier != "high") { error = where + "tier is floor, standard or high"; return false; }
        if (sc.divide < 0 || sc.divide > 8) { error = where + "divide is 0 to 8"; return false; }
        if (sc.triangles < 1) { error = where + "triangles is missing"; return false; }
        std::string data;
        if (!readAll(pathOf(dir) / pathOf(sc.file), data)) { error = where + sc.file + " is missing from the pack"; return false; }
        hash.update(data.data(), data.size());
        pack.scenes.push_back(std::move(sc));
    }
    pack.sha256 = hash.hex();
    return true;
}

bool buildBenchPackScene(const BenchPack& pack, const BenchPackScene& scene, Mesh& out, std::string& error) {
    if (!plainFileName(scene.file)) {
        error = "the scene's file is not a plain file name";
        return false;
    }
    Document doc;
    if (!loadProject(utf8Of(pathOf(pack.dir) / pathOf(scene.file)), doc, error)) return false;
    Mesh m = doc.parts.size() > 1 ? mergedVisibleMesh(doc) : std::move(doc.mesh); // (Parts as they sit in the scene)
    for (int i = 0; i < scene.divide; ++i)
        if (!subdivDivide(m, true, error)) return false;
    const double tris = double(m.triangleCount());
    if (tris != scene.triangles) {
        error = scene.id + ": the mesh has " + std::to_string(size_t(tris)) + " triangles, the manifest says " +
                std::to_string(size_t(scene.triangles)) + " (the pack was changed, or this build divides differently)";
        return false;
    }
    out = std::move(m);
    return true;
}

} // namespace sf
