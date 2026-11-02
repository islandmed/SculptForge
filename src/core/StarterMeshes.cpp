// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/StarterMeshes.h"

#include "core/Multires.h"
#include "core/Project.h"

#include <stb_image.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

// assets/starter/*.sfproj.z (tools/starter/build.sh), embedded by CMakeLists.txt.
namespace sf::assets {
#define SF_STARTER(sym)                                                                                                \
    extern const unsigned char sym[];                                                                                  \
    extern const unsigned long long sym##_size;
SF_STARTER(starterHumanHead)
SF_STARTER(starterBust)
SF_STARTER(starterFullBody)
SF_STARTER(starterHand)
SF_STARTER(starterFoot)
SF_STARTER(starterEar)
SF_STARTER(starterEye)
SF_STARTER(starterStylisedHead)
SF_STARTER(starterCreatureHead)
SF_STARTER(starterTorso)
SF_STARTER(starterRoundedBox)
SF_STARTER(starterCylinder)
SF_STARTER(starterCapsule)
SF_STARTER(starterRing)
#undef SF_STARTER
} // namespace sf::assets

namespace sf {

namespace {

namespace fs = std::filesystem;

fs::path pathOf(const std::string& p) { return fs::path(std::u8string(p.begin(), p.end())); }
std::string utf8Of(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    return f && f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size())) && f.flush();
}

} // namespace

const std::vector<StarterMesh>& starterMeshes() {
    using namespace assets;
#define SF_DATA(sym) sym, size_t(sym##_size)
    static const std::vector<StarterMesh> all = {
        {"human-head", "Human Head", "A neutral human head and neck with eye and mouth loops, for portraits.", 2, true,
         false, SF_DATA(starterHumanHead)},
        {"bust", "Bust", "Head, neck, shoulders and upper chest.", 2, true, false, SF_DATA(starterBust)},
        {"full-body", "Full Body", "A full human figure in A-pose, 167 cm tall.", 1, true, true, SF_DATA(starterFullBody)},
        {"hand", "Hand", "A left hand, raised with the palm to the front.", 2, false, false, SF_DATA(starterHand)},
        {"foot", "Foot", "A left foot and ankle.", 2, false, false, SF_DATA(starterFoot)},
        {"ear", "Ear", "A left ear with a little of the head round it, facing the front.", 2, false, false,
         SF_DATA(starterEar)},
        {"eye", "Eye", "An eyeball, 24 mm across, with the cornea and rings round the pupil.", 2, true, true,
         SF_DATA(starterEye)},
        {"stylised-head", "Stylised Head", "A cartoon head: big cranium and eyes, small nose and jaw.", 2, true, false,
         SF_DATA(starterStylisedHead)},
        {"creature-head", "Creature Head", "A dragon or creature head blockout: skull, jaw and two horns as Parts.", 2,
         true, true, SF_DATA(starterCreatureHead)},
        {"torso", "Torso", "A human torso from the neck to the upper thighs.", 2, true, false, SF_DATA(starterTorso)},
        {"rounded-box", "Rounded Box", "A 20 cm box with rounded edges.", 2, true, true, SF_DATA(starterRoundedBox)},
        {"cylinder", "Cylinder", "A 20 cm cylinder with bevelled rims and quad caps.", 2, true, true,
         SF_DATA(starterCylinder)},
        {"capsule", "Capsule", "A 30 cm capsule.", 2, true, true, SF_DATA(starterCapsule)},
        {"ring", "Ring", "A ring with a bevelled, squared cross-section.", 2, true, true, SF_DATA(starterRing)},
    };
#undef SF_DATA
    return all;
}

const StarterMesh* findStarterMesh(std::string_view idOrName) {
    const std::string want = lowerAscii(idOrName);
    if (want.empty()) return nullptr;
    for (const StarterMesh& m : starterMeshes())
        if (want == m.id || want == lowerAscii(m.name)) return &m;
    if (want == "head") return findStarterMesh("human-head");
    return nullptr;
}

bool unpackStarterMesh(const StarterMesh& mesh, std::vector<uint8_t>& project, std::string& error) {
    int len = 0;
    std::unique_ptr<char, decltype(&std::free)> out(
        stbi_zlib_decode_malloc(reinterpret_cast<const char*>(mesh.data), int(mesh.size), &len), &std::free);
    if (!out || len <= 0) {
        error = std::string("The starter mesh ") + mesh.name + " is damaged";
        return false;
    }
    project.assign(out.get(), out.get() + len);
    return true;
}

bool writeStarterMeshes(const std::string& dir, std::string& error) {
    std::error_code ec;
    fs::create_directories(pathOf(dir), ec);
    if (ec) {
        error = "Cannot create " + dir + ": " + ec.message();
        return false;
    }
    std::vector<uint8_t> data, existing;
    for (const StarterMesh& m : starterMeshes()) {
        if (!unpackStarterMesh(m, data, error)) return false;
        const fs::path p = pathOf(dir) / (std::string(m.id) + kProjectExtension);
        if (readFile(p, existing) && existing == data) continue;
        // Written next to it and renamed, so a thumbnail worker never reads half a file.
        const fs::path tmp = fs::path(p).concat(".part");
        if (!writeFile(tmp, data)) {
            error = "Cannot write " + utf8Of(p);
            return false;
        }
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            error = "Cannot write " + utf8Of(p);
            return false;
        }
    }
    return true;
}

const StarterMesh* starterMeshForPath(const std::string& dir, const std::string& path) {
    if (dir.empty() || path.empty()) return nullptr;
    const fs::path p = pathOf(path);
    if (p.parent_path().lexically_normal() != pathOf(dir).lexically_normal()) return nullptr;
    if (lowerAscii(utf8Of(p.extension())) != kProjectExtension) return nullptr;
    const std::string id = utf8Of(p.stem());
    for (const StarterMesh& m : starterMeshes())
        if (id == m.id) return &m;
    return nullptr;
}

bool loadStarterMesh(const StarterMesh& mesh, Document& doc, int levels, std::string& error) {
    std::vector<uint8_t> data;
    if (!unpackStarterMesh(mesh, data, error)) return false;
    // loadProject reads files: a private temporary copy, removed right after.
    static std::atomic<unsigned> counter{0};
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path tmp = fs::temp_directory_path(ec) / ("sculptforge-starter-" + std::to_string(stamp) + "-" +
                                                        std::to_string(counter++) + kProjectExtension);
    if (ec || !writeFile(tmp, data)) {
        fs::remove(tmp, ec);
        error = "Cannot write a temporary file for the starter mesh";
        return false;
    }
    Document loaded;
    const bool ok = loadProject(utf8Of(tmp), loaded, error);
    fs::remove(tmp, ec);
    if (!ok) return false;

    if (levels < 0) levels = mesh.levels;
    levels = std::clamp(levels, 0, 4);
    struct Part {
        Mesh mesh;
        std::string name;
    };
    std::vector<Part> parts;
    for (int i = 0; i < int(loaded.subtools.size()); ++i) {
        Part p{std::move(loaded.subtoolMesh(i)), loaded.subtools[size_t(i)].name};
        for (int l = 0; l < levels; ++l)
            if (!subdivDivide(p.mesh, true, error)) return false;
        parts.push_back(std::move(p));
    }
    if (parts.empty()) {
        error = std::string("The starter mesh ") + mesh.name + " is empty";
        return false;
    }
    if (parts.size() == 1) parts[0].name = mesh.name; // a one-object OBJ named its Part after the file
    doc.setMesh(std::move(parts[0].mesh), parts[0].name);
    for (size_t i = 1; i < parts.size(); ++i) doc.addSubtool(std::move(parts[i].mesh), parts[i].name, false);
    doc.selectSubtool(0);
    doc.path.clear();
    return true;
}

void registerStarterMeshCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    std::string names;
    for (const StarterMesh& m : starterMeshes()) names += std::string(names.empty() ? "" : ", ") + m.id;
    registry.add({"new_starter_mesh",
                  "New scene from a starter mesh: an all-quad base mesh to sculpt on (centimetres, standing on the "
                  "ground), opened with subdivision levels so you can step down to the base cage. Names: " +
                      names + ".",
                  {{"name", T::String, "Which starter mesh, e.g. human-head, bust, full-body, creature-head.", true},
                   {"levels", T::Number, "Subdivision levels to add (0-4; default: the mesh's own, 1 or 2).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      const std::string want = argString(a, "name");
                      const StarterMesh* m = findStarterMesh(want);
                      if (!m) return {false, "No starter mesh called \"" + want + "\""};
                      std::string error;
                      if (!loadStarterMesh(*m, doc, int(argNumber(a, "levels", -1)), error)) return {false, error};
                      size_t vertices = 0;
                      for (int i = 0; i < int(doc.subtools.size()); ++i) vertices += doc.subtoolMesh(i).vertexCount();
                      const std::string levels = subdivLevelText(doc.mesh);
                      return {true, std::string("New ") + m->name + " (" + std::to_string(vertices) + " vertices" +
                                        (levels.empty() ? std::string() : ", " + levels) + ")"};
                  }});
}

} // namespace sf
