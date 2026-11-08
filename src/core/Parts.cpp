// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Parts.h"

#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/Dyntopo.h"
#include "core/Masking.h"
#include "core/Multires.h"
#include "core/PathStroke.h"
#include "core/FaceGroups.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/TextureTransfer.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"

#include <stb_image.h>

#include <cctype>
#include <deque>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <utility>
#include <vector>

// assets/insert-sets/*.obj.z (zlib-compressed OBJ; tools/creature-bones/build.sh), embedded by CMakeLists.txt.
namespace sf::assets {
extern const unsigned char insertSetCreatureBones[];
extern const unsigned long long insertSetCreatureBones_size;
} // namespace sf::assets

namespace sf {

Mesh mergeMeshes(const std::vector<const Mesh*>& meshes) {
    Mesh out;
    bool mask = false, colors = false, hidden = false, uvs = false;
    std::vector<std::string> layerNames;
    std::map<std::string, float> layerWeights;
    for (const Mesh* m : meshes) {
        mask |= m->hasMask();
        colors |= m->hasColors();
        hidden |= m->hasHidden();
        uvs |= m->hasUvs();
        for (size_t i = 0; i < m->layers.size(); ++i) // as each shows (the merged mesh has strength 1, all shown)
            if (layerWeights.emplace(m->layers[i].name, m->layerFactor(i)).second) layerNames.push_back(m->layers[i].name);
    }
    for (const auto& name : layerNames) out.layers.push_back({name, {}, layerWeights[name]});

    uint32_t groupOffset = 0;
    for (const Mesh* m : meshes) {
        const uint32_t first = uint32_t(out.positions.size());
        const size_t nv = m->vertexCount(), nt = m->triangleCount();
        appendQuads(out, out.triangles.size(), *m);
        out.positions.insert(out.positions.end(), m->positions.begin(), m->positions.end());
        for (const auto& t : m->triangles) out.triangles.push_back(t + glm::uvec3(first));
        if (mask) {
            if (m->hasMask()) out.mask.insert(out.mask.end(), m->mask.begin(), m->mask.end());
            else out.mask.resize(out.mask.size() + nv, 0.0f);
        }
        if (colors) {
            if (m->hasColors()) out.colors.insert(out.colors.end(), m->colors.begin(), m->colors.end());
            else out.colors.resize(out.colors.size() + nv, glm::vec3(1.0f));
        }
        if (hidden) {
            if (m->hasHidden()) out.hidden.insert(out.hidden.end(), m->hidden.begin(), m->hidden.end());
            else out.hidden.resize(out.hidden.size() + nt, 0);
        }
        if (uvs) {
            if (m->hasUvs()) out.uvs.insert(out.uvs.end(), m->uvs.begin(), m->uvs.end());
            else out.uvs.resize(out.uvs.size() + nt * 3, glm::vec2(0.0f));
        }
        // Groups stay distinct per input, so "split by groups" gives the pieces back.
        uint32_t maxGroup = 0;
        for (uint32_t t = 0; t < nt; ++t) {
            const uint32_t g = m->facegroupAt(t);
            out.facegroups.push_back(groupOffset + g);
            maxGroup = std::max(maxGroup, g);
        }
        groupOffset += maxGroup + 1;
        if (!layerNames.empty()) {
            const auto& base = m->basePositions();
            out.base.insert(out.base.end(), base.begin(), base.end());
            for (auto& ol : out.layers) {
                auto it = std::find_if(m->layers.begin(), m->layers.end(), [&](const SculptLayer& l) { return l.name == ol.name; });
                if (it != m->layers.end()) ol.delta.insert(ol.delta.end(), it->delta.begin(), it->delta.end());
                else ol.delta.resize(ol.delta.size() + nv, glm::vec3(0.0f));
            }
        }
    }
    // Group ids start at 0 in the first input; shift so ids are 1-based like new groups elsewhere.
    for (auto& g : out.facegroups) ++g;
    if (out.hasQuads()) out.quadPartners.resize(out.triangles.size(), Mesh::kNoQuad);
    out.rebuildTopology();
    if (!out.layers.empty()) out.evaluateAll(); // weights of same-named layers may have differed
    out.recomputeAllNormals();
    return out;
}

Mesh placedMesh(const Mesh& mesh, const glm::mat4& transform) {
    Mesh out = mesh;
    if (transform == glm::mat4(1.0f)) return out;
    const glm::mat3 linear(transform);
    for (auto& p : out.positions) p = glm::vec3(transform * glm::vec4(p, 1.0f));
    for (auto& p : out.base) p = glm::vec3(transform * glm::vec4(p, 1.0f));
    for (auto& l : out.layers)
        for (auto& d : l.delta) d = linear * d;
    out.multires.reset();
    out.topologyDirty = true;
    out.recomputeAllNormals();
    return out;
}

const Mesh& partPlacedMesh(const Document& doc, int i, Mesh& scratch) {
    if (!doc.parts[size_t(i)].moved()) return doc.partMesh(i);
    scratch = placedMesh(doc.partMesh(i), doc.parts[size_t(i)].transform);
    return scratch;
}

Mesh mergedVisibleMesh(const Document& doc) {
    if (doc.partCount() == 1) return placedMesh(doc.mesh, doc.parts[0].transform);
    std::deque<Mesh> placed;
    std::vector<const Mesh*> meshes;
    for (int i = 0; i < doc.partCount(); ++i)
        if (doc.parts[size_t(i)].visible && doc.partMesh(i).triangleCount())
            meshes.push_back(&partPlacedMesh(doc, i, placed.emplace_back()));
    if (meshes.empty()) return Mesh();
    if (meshes.size() == 1) return *meshes.front();
    return mergeMeshes(meshes);
}

std::vector<MeshObjectRef> visiblePartObjects(const Document& doc, bool instances) {
    std::vector<MeshObjectRef> out;
    std::vector<const PartData*> data; // by object, for instanceOf
    auto add = [&](int i, const std::string& name) {
        const Part& p = doc.parts[size_t(i)];
        MeshObjectRef o{name, &doc.partMesh(i)};
        if (instances) {
            o.transform = p.transform;
            const auto first = std::find(data.begin(), data.end(), p.data.get());
            if (first != data.end()) o.instanceOf = int(first - data.begin());
        } else if (p.moved()) {
            o.owned = std::make_shared<const Mesh>(placedMesh(doc.partMesh(i), p.transform));
            o.mesh = o.owned.get();
        }
        data.push_back(p.data.get());
        out.push_back(std::move(o));
    };
    if (doc.partCount() == 1) {
        if (doc.mesh.triangleCount() == 0) return {};
        add(0, doc.assetName());
        return out;
    }
    for (int i = 0; i < doc.partCount(); ++i)
        if (doc.parts[size_t(i)].visible && doc.partMesh(i).triangleCount()) add(i, doc.parts[size_t(i)].name);
    return out;
}

void setSceneObjects(Document& doc, std::vector<MeshObject> objects, const std::string& fallbackName) {
    if (objects.empty()) {
        doc.setMesh(Mesh(), fallbackName);
        return;
    }
    auto nameOf = [&](size_t i) {
        return objects.size() == 1 || objects[i].name.empty() ? fallbackName : objects[i].name;
    };
    doc.setMesh(std::move(objects[0].mesh), nameOf(0));
    for (size_t i = 1; i < objects.size(); ++i) doc.addPart(std::move(objects[i].mesh), nameOf(i), false);
    doc.markClean();
}

std::vector<Mesh> splitByFaceGroups(const Mesh& mesh) {
    std::vector<uint32_t> ids;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) ids.push_back(mesh.facegroupAt(t));
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<Mesh> out;
    if (ids.size() < 2) return out;
    for (uint32_t id : ids) {
        std::vector<uint8_t> keep(mesh.triangleCount());
        for (uint32_t t = 0; t < keep.size(); ++t) keep[t] = mesh.facegroupAt(t) == id;
        out.push_back(extractTriangles(mesh, keep));
    }
    return out;
}

std::vector<Mesh> splitIntoParts(const Mesh& mesh) {
    // Union-find over vertices joined by triangles.
    std::vector<uint32_t> parent(mesh.vertexCount());
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t v) {
        while (parent[v] != v) v = parent[v] = parent[parent[v]];
        return v;
    };
    for (const auto& t : mesh.triangles) {
        parent[find(t.y)] = find(t.x);
        parent[find(t.z)] = find(t.x);
    }
    std::map<uint32_t, std::vector<uint8_t>> pieces;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        auto& keep = pieces[find(mesh.triangles[t].x)];
        if (keep.empty()) keep.resize(mesh.triangleCount());
        keep[t] = 1;
    }
    std::vector<Mesh> out;
    if (pieces.size() < 2) return out;
    for (const auto& [root, keep] : pieces) out.push_back(extractTriangles(mesh, keep));
    std::stable_sort(out.begin(), out.end(), [](const Mesh& a, const Mesh& b) { return a.triangleCount() > b.triangleCount(); });
    return out;
}

namespace {

std::string fileStem(const std::string& path) {
    const auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

std::string countText(size_t tris) { return std::to_string(tris) + " triangles"; }

// The Part an argument names: index= (0-based) or name=, else the active one. -1 if not found.
int targetPart(const Document& doc, const Args& a) {
    if (a.contains("name")) return doc.findPart(argString(a, "name"));
    if (a.contains("index")) {
        const int i = int(argNumber(a, "index", -1));
        return i >= 0 && i < doc.partCount() ? i : -1;
    }
    return doc.activePart;
}

CommandResult notFound(const Args& a) {
    return {false, a.contains("name") ? "No Part named '" + argString(a, "name") + "'" : std::string("No such Part")};
}

// The folder named by folder= (#96), or -1.
int targetFolder(const Document& doc, const Args& a) { return doc.findPartFolder(argString(a, "folder")); }

CommandResult folderNotFound(const Args& a) { return {false, "No folder named '" + argString(a, "folder") + "'"}; }

// Replaces Part `index` by `pieces`, named after it ("Body 1", "Body 2", ...); the first piece is selected.
void replaceWithPieces(Document& doc, int index, std::vector<Mesh> pieces) {
    notePartLevelsDropped(doc, index); // the pieces are cut from the current level
    const std::string base = doc.parts[size_t(index)].name;
    const bool visible = doc.parts[size_t(index)].visible;
    const uint32_t folder = doc.parts[size_t(index)].folder;
    const glm::mat4 place = doc.parts[size_t(index)].transform;
    for (size_t k = 0; k < pieces.size(); ++k) {
        const int at = doc.addPart(std::move(pieces[k]), base + " " + std::to_string(k + 1), false, index + 1 + int(k));
        doc.parts[size_t(at)].visible = visible;
        doc.parts[size_t(at)].folder = folder;
        doc.parts[size_t(at)].transform = place;
    }
    doc.selectPart(index + 1);
    doc.removePart(index);
}

// Split Hidden / Masked / Unmasked: the triangles in `set` leave the active Part for a new Part just below it
// (named "<Part> <suffix>", shown as the source is, in its folder, with its shading). One undo step: the
// source's old mesh and the new Part go back together. Every attribute extractTriangles keeps carries over (UVs,
// face groups, vertex paint, sculpt layers); subdivision levels can't be cut, so the source's are noted dropped
// and undo restores them. The new Part shows everything (Split Hidden) and starts unmasked (Split Masked).
// With `keep` (Keep original, _tfx on Discord) the triangles are copied instead: the source stays untouched,
// levels and all, and undo just takes the new Part away.
CommandResult splitOffPart(Document& doc, TriangleSet set, const char* label, const char* suffix, const char* what,
                           bool keep) {
    size_t n = 0;
    std::vector<uint8_t> in = trianglesIn(doc.mesh, set, &n);
    const std::string source = doc.activePartInfo().name;
    if (n == 0) return {false, "Nothing in " + source + " is " + what};
    if (!keep && n == doc.mesh.triangleCount())
        return {false, "Everything in " + source + " is " + what + ": nothing would stay"};
    Mesh piece = extractTriangles(doc.mesh, in);
    Mesh rest;
    if (!keep) {
        for (uint8_t& k : in) k = !k;
        rest = extractTriangles(doc.mesh, in);
    }
    in = {};
    if (set == TriangleSet::Hidden) piece.hidden.clear();
    if (set == TriangleSet::Masked) piece.mask.clear();
    const Part& from = doc.activePartInfo();
    const bool visible = from.visible;
    const uint32_t folder = from.folder;
    const Shading shading = from.shading;
    const glm::mat4 place = from.transform; // (a moved linked copy, #94: the piece stays where it was seen)
    const int at = doc.addPart(std::move(piece), doc.uniquePartName(source + " " + suffix), false, doc.activePart + 1);
    Part& added = doc.parts[size_t(at)];
    added.visible = visible;
    added.folder = folder;
    added.shading = shading;
    added.transform = place;
    const uint32_t id = added.id;
    const std::string name = added.name;
    auto step = std::make_shared<PartStep>();
    step->id = id;
    step->at = at;
    if (keep) {
        UndoRecord r; // undo just takes the copy away again
        r.label = std::string(label) + " (keep original)";
        r.part = std::move(step);
        doc.undo.push(std::move(r));
        doc.modified = true;
        return {true, "Copied " + countText(n) + " into " + name + "; " + source + " is unchanged"};
    }
    doc.replaceMeshUndoable(std::move(rest), label);
    if (UndoRecord* r = doc.undo.lastUndo()) r->part = std::move(step);
    return {true, "Split " + countText(n) + " into " + name + ", " + countText(doc.mesh.triangleCount()) + " stay in " + source};
}

// Reads the mesh in a model file for insert_mesh (#34): OBJ, PLY or GLB / glTF through the importers, every
// object in it as one mesh. Nothing but the file itself is read.
bool loadInsertMesh(const std::string& path, Mesh& out, std::string& error) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    for (char& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    bool ok = false;
    if (ext == ".obj") ok = loadObj(path, out, error);
    else if (ext == ".glb" || ext == ".gltf") ok = loadGltf(path, out, error);
    else if (ext == ".ply") {
        SplatCloud splats;
        PlyContent content = PlyContent::Mesh;
        ok = loadPly(path, out, splats, content, error);
        if (ok && content != PlyContent::Mesh) {
            error = path + " holds splats or points, not a mesh";
            return false;
        }
    } else {
        error = "Insert takes .obj, .ply or .glb files";
        return false;
    }
    if (!ok && error.empty()) error = "Couldn't read " + path;
    return ok;
}

std::string lowerExtension(const std::string& path) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    for (char& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return ext;
}
bool insertableExtension(const std::string& ext) { return ext == ".obj" || ext == ".ply" || ext == ".glb" || ext == ".gltf"; }

// A multi-object file's objects (OBJ `o` blocks, glTF nodes); a PLY is one object.
bool loadInsertObjects(const std::string& path, std::vector<MeshObject>& out, std::string& error) {
    const std::string ext = lowerExtension(path);
    out.clear();
    bool ok = false;
    if (ext == ".obj") ok = loadObj(path, out, error);
    else if (ext == ".glb" || ext == ".gltf") ok = loadGltf(path, out, error);
    else {
        MeshObject one;
        ok = loadInsertMesh(path, one.mesh, error);
        if (ok) out.push_back(std::move(one));
    }
    if (!ok && error.empty()) error = "Couldn't read " + path;
    out.erase(std::remove_if(out.begin(), out.end(), [](const MeshObject& o) { return o.mesh.triangleCount() == 0; }), out.end());
    return ok;
}

} // namespace

const std::vector<BuiltinInsertSet>& builtinInsertSets() {
    static const std::vector<BuiltinInsertSet> sets = {{"creature-bones", "Creature bones"}};
    return sets;
}

bool resolveInsertSetPath(const std::string& path, std::string& file, std::string& error) {
    const std::string prefix = kBuiltinInsertSetPrefix;
    if (path.compare(0, prefix.size(), prefix) != 0) {
        file = path;
        return true;
    }
    const std::string id = path.substr(prefix.size());
    const unsigned char* data = nullptr;
    size_t size = 0;
    if (id == "creature-bones") data = assets::insertSetCreatureBones, size = size_t(assets::insertSetCreatureBones_size);
    if (!data) return error = "No built-in insert set '" + id + "'", false;
    // Written once per run (the thumbnails and each insert read the file): the app's copy, unless the file there
    // already has exactly these bytes.
    static std::mutex mutex;
    static std::map<std::string, std::string> written;
    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = written.find(id); it != written.end()) return file = it->second, true;
    int len = 0;
    const std::unique_ptr<char, decltype(&std::free)> obj(
        stbi_zlib_decode_malloc(reinterpret_cast<const char*>(data), int(size), &len), &std::free);
    if (!obj || len <= 0) return error = "The built-in insert set '" + id + "' is damaged", false;
    const std::string_view content(obj.get(), size_t(len));
    std::error_code ec;
    const std::string appData = appDataDir();
    std::vector<std::filesystem::path> dirs;
    if (!appData.empty()) dirs.push_back(std::filesystem::path(std::u8string(appData.begin(), appData.end())) / "insert-sets");
    if (const auto tmp = std::filesystem::temp_directory_path(ec); !ec) dirs.push_back(tmp / "sculptforge-insert-sets");
    for (const auto& dir : dirs) {
        const auto target = dir / (id + ".obj");
        bool same = false;
        if (std::ifstream in(target, std::ios::binary); in) {
            const std::string have((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            same = have == content;
        }
        if (!same) {
            std::filesystem::create_directories(dir, ec);
            auto part = target;
            part += ".part";
            {
                std::ofstream out(part, std::ios::binary | std::ios::trunc);
                if (!out || !out.write(content.data(), std::streamsize(content.size())) || !out.flush()) continue;
            }
            std::filesystem::rename(part, target, ec);
            if (ec) {
                std::filesystem::remove(part, ec);
                continue;
            }
        }
        const auto u8 = target.u8string();
        file = written[id] = std::string(u8.begin(), u8.end());
        return true;
    }
    return error = "Couldn't write the built-in insert set '" + id + "' to the app's data folder", false;
}

bool listInsertSet(const std::string& setPath, std::vector<InsertSetItem>& items, std::string& error) {
    items.clear();
    std::string path;
    if (!resolveInsertSetPath(setPath, path, error)) return false;
    const std::filesystem::path dir(std::u8string(path.begin(), path.end()));
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) {
        std::vector<std::string> files;
        for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator();
             it.increment(ec)) {
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            const auto u8 = it->path().u8string();
            std::string file(u8.begin(), u8.end());
            if (insertableExtension(lowerExtension(file))) files.push_back(std::move(file));
        }
        std::sort(files.begin(), files.end());
        if (files.size() > size_t(kInsertSetMaxItems)) files.resize(size_t(kInsertSetMaxItems));
        for (const std::string& f : files) items.push_back({fileStem(f), f, -1});
        if (items.empty()) error = path + " has no .obj, .ply or .glb files";
        return !items.empty();
    }
    if (!insertableExtension(lowerExtension(path))) {
        error = "An insert set is a folder, or an .obj, .ply or .glb file";
        return false;
    }
    std::vector<MeshObject> objects;
    if (!loadInsertObjects(path, objects, error)) return false;
    const std::string stem = fileStem(path);
    for (size_t i = 0; i < objects.size() && i < size_t(kInsertSetMaxItems); ++i)
        items.push_back({objects[i].name.empty() ? stem + " " + std::to_string(i + 1) : objects[i].name, path, int(i)});
    if (items.empty()) error = path + " has no mesh to insert";
    return !items.empty();
}

bool loadInsertSetItem(const std::string& setPath, int index, Mesh& out, std::string& name, std::string& error) {
    std::vector<InsertSetItem> items;
    std::string path;
    if (!resolveInsertSetPath(setPath, path, error)) return false;
    const std::filesystem::path dir(std::u8string(path.begin(), path.end()));
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) {
        if (!listInsertSet(path, items, error)) return false;
        if (index < 0 || index >= int(items.size()))
            return error = "The set has " + std::to_string(items.size()) + " items (index 0-" + std::to_string(items.size() - 1) + ")", false;
        name = items[size_t(index)].name;
        return loadInsertMesh(items[size_t(index)].file, out, error);
    }
    if (!insertableExtension(lowerExtension(path))) {
        error = "An insert set is a folder, or an .obj, .ply or .glb file";
        return false;
    }
    std::vector<MeshObject> objects; // (one read: listing it again would read the file twice)
    if (!loadInsertObjects(path, objects, error)) return false;
    if (index < 0 || index >= int(objects.size()) || index >= kInsertSetMaxItems)
        return error = "The set has " + std::to_string(objects.size()) + " items (index 0-" + std::to_string(int(objects.size()) - 1) + ")", false;
    name = objects[size_t(index)].name.empty() ? fileStem(path) + " " + std::to_string(index + 1) : objects[size_t(index)].name;
    out = std::move(objects[size_t(index)].mesh);
    return true;
}

namespace {

// The shape of `source` for inserting: its triangles and quads only (no mask, paint, UVs, layers or levels),
// centred on its bounds and scaled so its largest side is `size`. False if it has no triangles.
bool normalizedPiece(const Mesh& source, float size, Mesh& out) {
    if (source.triangleCount() == 0 || source.vertexCount() == 0) return false;
    out = Mesh();
    out.positions = source.positions;
    out.triangles = source.triangles;
    if (source.hasQuads()) out.quadPartners = source.quadPartners;
    out.facegroups.assign(out.triangles.size(), 0u);
    const AABB box = out.bounds();
    const glm::vec3 e = box.extent();
    const float largest = std::max({e.x, e.y, e.z});
    const float scale = largest > 1e-12f ? size / largest : 1.0f;
    const glm::vec3 centre = box.center();
    for (auto& p : out.positions) p = (p - centre) * scale;
    out.sanitizeAttributes();
    return true;
}

// Places `prim` (built around the origin, its axis +Y) where insert_primitive / insert_mesh say: on the surface
// point, +Y along the normal, turned by twist, with mirrored copies for symmetry; merged into the active
// Part as one new face group, one undo step.
CommandResult insertPiece(Document& doc, Mesh prim, const Args& a, const std::string& label) {
    std::vector<SymCopy> mirrors;
    std::string error;
    if (!symmetryCopiesArg(a, doc.mesh, mirrors, error)) return {false, error};
    const float size = float(std::max(argNumber(a, "size", 0.2), 1e-4));
    glm::vec3 n(float(argNumber(a, "nx", 0.0)), float(argNumber(a, "ny", 1.0)), float(argNumber(a, "nz", 0.0)));
    n = glm::length(n) > 1e-8f ? glm::normalize(n) : glm::vec3(0, 1, 0);
    // Rotate +Y onto the normal.
    const glm::vec3 y(0, 1, 0);
    glm::mat3 rot(1.0f);
    const float c = glm::dot(y, n);
    if (c < -0.9999f) rot = glm::mat3(1, 0, 0, 0, -1, 0, 0, 0, -1);
    else if (c < 0.9999f) {
        const glm::vec3 v = glm::cross(y, n);
        const glm::mat3 k(0, v.z, -v.y, -v.z, 0, v.x, v.y, -v.x, 0);
        rot = glm::mat3(1.0f) + k + k * k * (1.0f / (1.0f + c));
    }
    const float twist = glm::radians(float(argNumber(a, "twist", 0.0)));
    if (twist != 0.0f) { // about the primitive's own axis (Y) first, then onto the normal
        const float cs = std::cos(twist), sn = std::sin(twist);
        rot = rot * glm::mat3(cs, 0, -sn, 0, 1, 0, sn, 0, cs);
    }
    const glm::vec3 at(float(argNumber(a, "x", 0.0)), float(argNumber(a, "y", 0.0)), float(argNumber(a, "z", 0.0)));
    for (auto& p : prim.positions) p = at + rot * p;
    uint32_t group = 0;
    for (uint32_t g : doc.mesh.facegroups) group = std::max(group, g);
    prim.facegroups.assign(prim.triangles.size(), group + 1);
    prim.recomputeAllNormals();
    // Symmetry: a copy for each mirror image and radial turn (about the same centre as the brushes'), in the
    // same face group and the same undo step. A copy that would land on one already there (inserted on a plane
    // or the radial axis itself) is left out.
    std::vector<Mesh> copies;
    std::vector<glm::vec3> placed{at};
    for (const SymCopy& m : mirrors) {
        const glm::vec3 centre = m.point(at);
        if (std::any_of(placed.begin(), placed.end(),
                        [&](const glm::vec3& p) { return glm::length(centre - p) < size * 0.25f; }))
            continue;
        placed.push_back(centre);
        Mesh copy = prim;
        for (auto& p : copy.positions) p = m.point(p);
        if (m.handedness() < 0.0f) // a reflection turns the triangles inside out: wind them back
            for (auto& t : copy.triangles) std::swap(t.y, t.z);
        copy.recomputeAllNormals();
        copies.push_back(std::move(copy));
    }
    bool radial = false; // (a copy turned other than by a half turn: not just mirrored)
    for (const SymCopy& m : mirrors)
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) radial = radial || (i != j && m.m[i][j] != 0.0f);
    std::vector<const Mesh*> parts{&doc.mesh, &prim};
    for (const Mesh& c2 : copies) parts.push_back(&c2);
    Mesh merged = mergeMeshes(parts);
    const size_t tris = prim.triangleCount() * (1 + copies.size());
    doc.replaceMeshUndoable(std::move(merged), "Insert " + label);
    return {true, "Inserted " + (copies.empty() ? "a " + label : std::to_string(copies.size() + 1) + " " + label + (radial ? "s (radial)" : "s (mirrored)")) +
                      " (" + std::to_string(tris) + " triangles, face group " + std::to_string(group + 1) + ")"};
}

} // namespace

void registerPartCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    const ParamSpec indexParam{"index", T::Number, "Part index (0 = top of the list). Default: the active one.", false};
    const ParamSpec nameParam{"name", T::String, "Part name (instead of index).", false};

    reg.add({"part_list", "List the Parts (separate meshes) in the scene.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 std::string out = std::to_string(doc.partCount()) + " Part" + (doc.partCount() == 1 ? "" : "s") + ":";
                 for (int i = 0; i < doc.partCount(); ++i) {
                     const Part& s = doc.parts[size_t(i)];
                     out += "\n" + std::to_string(i) + ". " + s.name + " (" + countText(doc.partMesh(i).triangleCount());
                     if (subdivLevelCount(doc.partMesh(i))) out += ", " + subdivLevelText(doc.partMesh(i));
                     if (const int links = doc.linkCount(i); links > 1) out += ", linked x" + std::to_string(links);
                     if (i == doc.activePart) out += ", active";
                     if (!s.visible) out += ", hidden";
                     if (const int f = doc.partFolderIndex(s.folder); f >= 0) out += ", in " + doc.partFolders[size_t(f)].name;
                     out += ")";
                 }
                 for (const PartFolder& f : doc.partFolders) {
                     int n = 0;
                     for (const Part& s : doc.parts) n += s.folder == f.id ? 1 : 0;
                     out += "\nFolder " + f.name + " (" + std::to_string(n) + " Part" + (n == 1 ? "" : "s") +
                            (f.collapsed ? ", collapsed" : "") + ")";
                 }
                 return {true, out};
             }});

    reg.add({"part_select", "Make a Part the active one: brushes and commands work on it.", {indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (!a.contains("index") && !a.contains("name")) return {false, "Give index= or name="};
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 doc.selectPart(i);
                 doc.parts[size_t(i)].visible = true; // you can't sculpt what you can't see
                 return {true, "Active Part: " + doc.parts[size_t(i)].name};
             }});

    reg.add({"part_append",
             "Add a new Part: a primitive (sphere, cube, plane) placed beside the visible model, or a model file. "
             "It becomes the active Part.",
             {{"type", T::String, "'sphere' (default), 'cube', 'plane', 'cylinder', 'cone', 'torus', 'capsule', 'icosphere' or 'quadsphere'.", false},
              {"path", T::String, "Model file to append instead of a primitive (.obj, .glb, .fbx, .ply, .stl, .usd...).", false},
              {"unit", T::String, "Unit of the numbers in an STL, OBJ or PLY file (mm, cm, m, in), as open's.", false},
              {"name", T::String, "Name for the new Part.", false},
              {"size", T::Number, "Primitive size relative to the largest visible Part (0.05-2). Default 0.35.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh m;
                 std::string name;
                 if (a.contains("path")) {
                     // Load through `open` into a scratch document, so every format and its attributes work;
                     // each object in the file becomes its own Part.
                     Document scratch;
                     const std::string path = argString(a, "path");
                     Args openArgs{{"path", path}};
                     if (a.contains("unit")) openArgs["unit"] = argString(a, "unit");
                     CommandResult r = CommandRegistry().execute(scratch, "open", openArgs);
                     if (!r.ok) return r;
                     if (scratch.partCount() == 1) {
                         if (scratch.mesh.triangleCount() == 0) return {false, path + " has no mesh to append"};
                         m = std::move(scratch.mesh);
                         name = fileStem(path);
                     } else {
                         const int first = doc.partCount();
                         size_t tris = 0;
                         for (int i = 0; i < scratch.partCount(); ++i) {
                             tris += scratch.partMesh(i).triangleCount();
                             doc.addPart(std::move(scratch.partMesh(i)), scratch.parts[size_t(i)].name, false);
                         }
                         doc.selectPart(first);
                         return {true, "Appended " + std::to_string(scratch.partCount()) + " Parts from " + path + " (" +
                                           countText(tris) + ")"};
                     }
                 } else {
                     const std::string type = argString(a, "type", "sphere");
                     // Sized from the largest visible Part (so repeated appends neither grow nor shrink),
                     // placed beside everything shown.
                     const AABB scene = doc.visibleBounds();
                     float extent = 0.0f;
                     for (int i = 0; i < doc.partCount(); ++i) {
                         const Mesh& sm = doc.partMesh(i);
                         if (!doc.parts[size_t(i)].visible || !sm.vertexCount()) continue;
                         const glm::vec3 e = sm.bounds().extent();
                         extent = std::max({extent, e.x, e.y, e.z});
                     }
                     if (extent <= 0.0f) extent = 2.0f;
                     const float size = extent * float(std::clamp(argNumber(a, "size", 0.35), 0.05, 2.0));
                     if (type == "sphere") m = makeIcosphere(5, size * 0.5f);
                     else if (type == "cube") m = makeCube(48, size);
                     else if (type == "plane") m = makePlane(128, size);
                     else if (type == "cylinder") m = makeCylinder(48, size);
                     else if (type == "cone") m = makeCone(48, size);
                     else if (type == "torus") m = makeTorus(64, size);
                     else if (type == "capsule") m = makeCapsule(48, size);
                     else if (type == "icosphere") m = makeIcosphere(3, size * 0.5f);
                     else if (type == "quadsphere") m = makeQuadSphere(32, size);
                     else
                         return {false, "Unknown primitive '" + type +
                                            "' (sphere, cube, plane, cylinder, cone, torus, capsule, icosphere, quadsphere)"};
                     // Beside the model on +X, at its middle height.
                     const glm::vec3 at = scene.valid() ? glm::vec3(scene.max.x + size * 0.6f, scene.center().y, scene.center().z)
                                                        : glm::vec3(0.0f);
                     for (auto& p : m.positions) p += at;
                     m.recomputeAllNormals();
                     name = type;
                     if (!name.empty()) name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
                     if (type == "quadsphere") name = "QuadSphere";
                 }
                 if (a.contains("name")) name = argString(a, "name");
                 const size_t tris = m.triangleCount();
                 const int i = doc.addPart(std::move(m), name);
                 return {true, "Appended Part " + doc.parts[size_t(i)].name + " (" + countText(tris) + ")"};
             }});

    const std::vector<ParamSpec> placeParams{
        {"x", T::Number, "surface point x", true}, {"y", T::Number, "surface point y", true}, {"z", T::Number, "surface point z", true},
        {"nx", T::Number, "surface normal x", false}, {"ny", T::Number, "surface normal y", false}, {"nz", T::Number, "surface normal z", false},
        {"size", T::Number, "size in scene units (default 0.2)", false},
        {"twist", T::Number, "turn about the normal in degrees (default 0)", false},
        {"symmetry", T::String, "mirror axes, e.g. \"x\" or \"xz\": a mirrored copy across each plane (#43; default none)", false}};
    std::vector<ParamSpec> primitiveParams{{"type", T::String, "sphere, cube, cylinder, cone, torus, capsule or quadsphere", false}};
    primitiveParams.insert(primitiveParams.end(), placeParams.begin(), placeParams.end());
    reg.add({"insert_primitive",
             "Insert a primitive into the active Part where the surface is (#34): its axis along the surface "
             "normal, its centre on the surface, as one new face group; one undoable step.",
             primitiveParams,
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string blocked = dyntopoBlocker(doc.mesh);
                 if (!blocked.empty()) return {false, "Can't insert: " + blocked};
                 const std::string type = argString(a, "type", "sphere");
                 const float size = float(std::max(argNumber(a, "size", 0.2), 1e-4));
                 Mesh prim;
                 if (type == "sphere") prim = makeIcosphere(3, size * 0.5f);
                 else if (type == "cube") prim = makeCube(8, size);
                 else if (type == "cylinder") prim = makeCylinder(24, size);
                 else if (type == "cone") prim = makeCone(24, size);
                 else if (type == "torus") prim = makeTorus(32, size);
                 else if (type == "capsule") prim = makeCapsule(24, size);
                 else if (type == "quadsphere") prim = makeQuadSphere(8, size);
                 else return {false, "Unknown primitive '" + type + "'"};
                 return insertPiece(doc, std::move(prim), a, type);
             }});

    std::vector<ParamSpec> meshParams{
        {"part", T::String, "insert a copy of this Part (by name)", false},
        {"file", T::String, "insert the mesh in this model file (.obj, .ply or .glb)", false},
        {"set", T::String, "insert one item of this insert set: a folder of model files, a file with several objects, or builtin:creature-bones (the built-in Creature bones)", false},
        {"index", T::Number, "with set=: which item (0 = the first; insert_set_items lists them)", false}};
    meshParams.insert(meshParams.end(), placeParams.begin(), placeParams.end());
    reg.add({"insert_mesh",
             "Insert your own mesh into the active Part where the surface is (#34), placed like insert_primitive: "
             "a copy of another Part, a model file, or one item of an insert set (a folder or a multi-object file), scaled so its largest side is `size`, its up (+Y) axis "
             "along the surface normal, as one new face group; one undoable step.",
             meshParams,
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string blocked = dyntopoBlocker(doc.mesh);
                 if (!blocked.empty()) return {false, "Can't insert: " + blocked};
                 if (int(a.contains("part")) + int(a.contains("file")) + int(a.contains("set")) != 1)
                     return {false, "Give part=, file= or set="};
                 Mesh source;
                 std::string label;
                 if (a.contains("part")) {
                     label = argString(a, "part");
                     const int i = doc.findPart(label);
                     if (i < 0) return {false, "No Part named '" + label + "'"};
                     source = doc.partMesh(i);
                 } else if (a.contains("set")) {
                     std::string error;
                     if (!loadInsertSetItem(argString(a, "set"), int(argNumber(a, "index", 0.0)), source, label, error))
                         return {false, error};
                 } else {
                     const std::string path = argString(a, "file");
                     std::string error;
                     if (!loadInsertMesh(path, source, error)) return {false, error};
                     label = fileStem(path);
                 }
                 Mesh piece;
                 if (!normalizedPiece(source, float(std::max(argNumber(a, "size", 0.2), 1e-4)), piece))
                     return {false, label + " has no mesh to insert"};
                 return insertPiece(doc, std::move(piece), a, label);
             }});

    reg.add({"insert_set_items",
             "List the items of an insert set (a folder of .obj / .ply / .glb files, or one file with several "
             "objects, or builtin:creature-bones), in the order insert_mesh set=<path> index=<n> numbers them.",
             {{"set", T::String, "the folder or file, or builtin:creature-bones", true}},
             [](Document&, const Args& a) -> CommandResult {
                 std::vector<InsertSetItem> items;
                 std::string error;
                 if (!listInsertSet(argString(a, "set"), items, error)) return {false, error};
                 std::string out = std::to_string(items.size()) + " item" + (items.size() == 1 ? "" : "s") + ":";
                 for (size_t i = 0; i < items.size(); ++i) out += "\n" + std::to_string(i) + ". " + items[i].name;
                 return {true, out};
             }});

    reg.add({"part_duplicate", "Copy a Part (with its layers and attributes) and make the copy active.",
             {indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 Mesh copy = doc.partMesh(i);
                 const Shading shading = doc.parts[size_t(i)].shading;
                 const uint32_t folder = doc.parts[size_t(i)].folder;
                 const glm::mat4 place = doc.parts[size_t(i)].transform;
                 const int added = doc.addPart(std::move(copy), doc.parts[size_t(i)].name + " copy", true, i + 1);
                 doc.parts[size_t(added)].shading = shading;
                 doc.parts[size_t(added)].folder = folder; // (right below it: the folder stays together)
                 doc.parts[size_t(added)].transform = place;
                 return {true, "Duplicated as " + doc.parts[size_t(added)].name};
             }});

    reg.add({"part_duplicate_linked",
             "Linked Duplicate: a new Part sharing the Part's mesh, with its own place in the scene (#94). "
             "Sculpting, painting or masking any linked Part changes them all; move each with the Gizmo or "
             "`transform`. One GPU mesh for all of them. Undoable.",
             {indexParam, nameParam,
              {"offset", T::String, "Where the copy goes relative to the Part, x,y,z in world units. Default: beside "
                                    "it on +X.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 if (doc.partMesh(i).triangleCount() == 0) return {false, doc.parts[size_t(i)].name + " is empty"};
                 glm::vec3 offset(doc.partBounds(i).extent().x * 1.15f, 0.0f, 0.0f);
                 if (a.contains("offset") && !parseVec3(argString(a, "offset"), offset))
                     return {false, "Bad offset '" + argString(a, "offset") + "' (expected x,y,z)"};
                 const int added = doc.linkedDuplicate(i, offset);
                 return {true, "Linked duplicate " + doc.parts[size_t(added)].name + " (" +
                                   std::to_string(doc.linkCount(added)) + " linked Parts share the mesh)"};
             }});

    reg.add({"part_make_unique",
             "Make Unique: give a linked Part its own copy of the mesh, so it no longer changes with the others "
             "(#94). It stays where it is; its edit history starts fresh.",
             {indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 if (!doc.makeUnique(i)) return {false, doc.parts[size_t(i)].name + " isn't linked"};
                 return {true, doc.parts[size_t(i)].name + " is unique now"};
             }});

    reg.add({"part_delete", "Delete a Part (not undoable). The last Part can't be deleted.",
             {indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 if (doc.partCount() == 1) return {false, "The scene needs at least one Part"};
                 const std::string name = doc.parts[size_t(i)].name;
                 doc.removePart(i);
                 return {true, "Deleted Part " + name};
             }});

    reg.add({"part_rename", "Rename a Part.",
             {{"to", T::String, "The new name.", true}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 std::string to = argString(a, "to");
                 if (to.empty()) return {false, "The name can't be empty"};
                 if (doc.parts[size_t(i)].name != to) to = doc.uniquePartName(to);
                 doc.parts[size_t(i)].name = to;
                 doc.modified = true;
                 return {true, "Renamed to " + to};
             }});

    reg.add({"part_visibility", "Show or hide a Part. Hidden Parts aren't drawn, picked or exported.",
             {{"visible", T::Bool, "true to show, false to hide. Default: toggle.", false}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 Part& s = doc.parts[size_t(i)];
                 s.visible = argBool(a, "visible", !s.visible);
                 doc.modified = true;
                 return {true, s.name + (s.visible ? " shown" : " hidden")};
             }});

    reg.add({"part_shading",
             "Shade a Part smooth or flat (faceted), or as the scene's Flat shading setting says (default).",
             {{"mode", T::String, "default, smooth or flat.", true}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 Shading mode;
                 if (!parseShading(argString(a, "mode"), mode))
                     return {false, "Unknown shading '" + argString(a, "mode") + "' (default, smooth, flat)"};
                 Part& s = doc.parts[size_t(i)];
                 if (s.shading != mode) doc.modified = true;
                 s.shading = mode;
                 return {true, s.name + " shading: " + shadingName(mode)};
             }});

    // Folders (#96): group Parts in the list; hide, show and move (transform folder=true) a folder at once.
    const ParamSpec folderParam{"folder", T::String, "Folder name.", true};
    reg.add({"part_folder_new", "Make a folder in the Parts list and put a Part in it (the active one by default).",
             {{"folder", T::String, "Its name (default Folder; made unique).", false},
              {"empty", T::Bool, "Make it empty instead. Default false.", false}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 const uint32_t id = doc.addPartFolder(argString(a, "folder", "Folder"));
                 const std::string& folder = doc.partFolders.back().name;
                 if (argBool(a, "empty", false)) return {true, "Made folder " + folder};
                 const int at = doc.setPartFolder(i, id);
                 return {true, "Made folder " + folder + " with " + doc.parts[size_t(at)].name};
             }});

    reg.add({"part_set_folder", "Put a Part into a folder, or take it out (folder empty).",
             {{"folder", T::String, "Folder name; empty = out of its folder.", true}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 const std::string name = doc.parts[size_t(i)].name;
                 if (argString(a, "folder").empty()) {
                     doc.setPartFolder(i, 0);
                     return {true, name + " is out of its folder"};
                 }
                 const int f = targetFolder(doc, a);
                 if (f < 0) return folderNotFound(a);
                 doc.setPartFolder(i, doc.partFolders[size_t(f)].id);
                 return {true, name + " is in " + doc.partFolders[size_t(f)].name};
             }});

    reg.add({"part_folder_rename", "Rename a Part folder.",
             {folderParam, {"to", T::String, "The new name.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int f = targetFolder(doc, a);
                 if (f < 0) return folderNotFound(a);
                 std::string to = argString(a, "to");
                 if (to.empty()) return {false, "The name can't be empty"};
                 PartFolder& folder = doc.partFolders[size_t(f)];
                 if (folder.name != to) to = doc.uniqueFolderName(to);
                 folder.name = to;
                 doc.modified = true;
                 return {true, "Folder renamed to " + to};
             }});

    reg.add({"part_folder_visibility", "Show or hide every Part in a folder.",
             {folderParam, {"visible", T::Bool, "true to show, false to hide. Default: hide if any is shown, else show.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int f = targetFolder(doc, a);
                 if (f < 0) return folderNotFound(a);
                 const PartFolder& folder = doc.partFolders[size_t(f)];
                 bool anyShown = false;
                 for (const Part& s : doc.parts) anyShown |= s.folder == folder.id && s.visible;
                 const bool show = argBool(a, "visible", !anyShown);
                 int n = 0;
                 for (Part& s : doc.parts)
                     if (s.folder == folder.id) {
                         s.visible = show;
                         ++n;
                     }
                 if (n == 0) return {false, folder.name + " has no Parts"};
                 doc.modified = true;
                 return {true, folder.name + (show ? " shown" : " hidden")};
             }});

    reg.add({"part_folder_collapse", "Fold a Part folder in the list (only its header shows), or open it.",
             {folderParam, {"collapsed", T::Bool, "true to fold, false to open. Default: toggle.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int f = targetFolder(doc, a);
                 if (f < 0) return folderNotFound(a);
                 PartFolder& folder = doc.partFolders[size_t(f)];
                 folder.collapsed = argBool(a, "collapsed", !folder.collapsed); // (a view state: not an unsaved change)
                 return {true, folder.name + (folder.collapsed ? " folded" : " opened")};
             }});

    reg.add({"part_folder_delete", "Remove a Part folder. Its Parts stay, outside any folder.", {folderParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int f = targetFolder(doc, a);
                 if (f < 0) return folderNotFound(a);
                 const std::string name = doc.partFolders[size_t(f)].name;
                 doc.removePartFolder(doc.partFolders[size_t(f)].id);
                 return {true, "Removed folder " + name + " (its Parts stay)"};
             }});

    reg.add({"part_solo", "Show only the active Part, or (when it already is the only one shown) show all again.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 bool othersShown = false;
                 for (int i = 0; i < doc.partCount(); ++i)
                     othersShown |= i != doc.activePart && doc.parts[size_t(i)].visible;
                 for (int i = 0; i < doc.partCount(); ++i)
                     doc.parts[size_t(i)].visible = i == doc.activePart || !othersShown;
                 doc.modified = true;
                 return {true, othersShown ? "Solo: " + doc.activePartInfo().name : std::string("All Parts shown")};
             }});

    reg.add({"part_show_all", "Show every Part.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 for (auto& s : doc.parts) s.visible = true;
                 doc.modified = true;
                 return {true, "All Parts shown"};
             }});

    reg.add({"part_move", "Move a Part up or down the list.",
             {{"direction", T::String, "'up' or 'down'.", true}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetPart(doc, a);
                 if (i < 0) return notFound(a);
                 const int dir = argString(a, "direction") == "up" ? -1 : 1;
                 if (i + dir < 0 || i + dir >= doc.partCount()) return {false, "Already at the " + std::string(dir < 0 ? "top" : "bottom")};
                 doc.movePart(i, dir);
                 return {true, "Moved " + doc.parts[size_t(i + dir)].name + (dir < 0 ? " up" : " down")};
             }});

    reg.add({"part_merge_down",
             "Merge the active Part with the one below it into one mesh (not undoable). Each keeps its own "
             "face groups, so part_split_groups can separate them again. Painted textures are kept in one new "
             "texture on new Auto UVs.",
             {{"keep_texture", T::Bool, "Keep painted textures (Auto UV the result and copy them in). Default true.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = doc.activePart;
                 if (i + 1 >= doc.partCount()) return {false, "There's no Part below " + doc.activePartInfo().name};
                 doc.makeUnique(i); // (a linked Part: the merge is its own, the others stay as they are)
                 // Both where they are in the scene (linked copies, #94): the merged Part isn't moved.
                 Mesh placedA, placedB;
                 const Mesh& a0 = partPlacedMesh(doc, i, placedA);
                 const Mesh& b0 = partPlacedMesh(doc, i + 1, placedB);
                 Mesh merged = mergeMeshes({&a0, &b0});
                 const std::string texture = carryMergedTexturesNote({&a0, &b0}, merged, argBool(a, "keep_texture", true));
                 for (int k : {i, i + 1}) notePartLevelsDropped(doc, k); // merged at their current levels
                 const std::string name = doc.parts[size_t(i)].name, below = doc.parts[size_t(i + 1)].name;
                 doc.removePart(i + 1);
                 doc.replaceActiveMesh(std::move(merged));
                 doc.activePartInfo().transform = glm::mat4(1.0f);
                 return {true, "Merged " + below + " into " + name + " (" + countText(doc.mesh.triangleCount()) + ")" + texture};
             }});

    reg.add({"part_merge_visible",
             "Merge every visible Part into one new Part, replacing them (not undoable). Hidden Parts stay "
             "as they are. Painted textures are kept in one new texture on new Auto UVs.",
             {{"keep_texture", T::Bool, "Keep painted textures (Auto UV the result and copy them in). Default true.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<int> visible;
                 for (int i = 0; i < doc.partCount(); ++i)
                     if (doc.parts[size_t(i)].visible) visible.push_back(i);
                 if (visible.size() < 2) return {false, "Show at least two Parts to merge"};
                 Mesh merged = mergedVisibleMesh(doc);
                 std::vector<const Mesh*> parts; // as mergedVisibleMesh gathers them
                 std::deque<Mesh> placed;
                 for (int i : visible)
                     if (doc.partMesh(i).triangleCount()) parts.push_back(&partPlacedMesh(doc, i, placed.emplace_back()));
                 const std::string texture =
                     parts.size() > 1 ? carryMergedTexturesNote(parts, merged, argBool(a, "keep_texture", true)) : std::string();
                 for (int k : visible) notePartLevelsDropped(doc, k); // merged at their current levels
                 const size_t tris = merged.triangleCount();
                 const int at = visible.front();
                 doc.addPart(std::move(merged), "Merged", true, at);
                 for (auto it = visible.rbegin(); it != visible.rend(); ++it) doc.removePart(*it + 1);
                 return {true, "Merged " + std::to_string(visible.size()) + " Parts into " +
                                   doc.activePartInfo().name + " (" + countText(tris) + ")" + texture};
             }});

    reg.add({"part_split_groups", "Split the active Part into one Part per face group (not undoable).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 auto pieces = splitByFaceGroups(doc.mesh);
                 if (pieces.empty()) return {false, doc.activePartInfo().name + " has only one face group"};
                 const size_t n = pieces.size();
                 replaceWithPieces(doc, doc.activePart, std::move(pieces));
                 return {true, "Split into " + std::to_string(n) + " Parts"};
             }});

    reg.add({"part_split_parts", "Split the active Part into one Part per disconnected piece (not undoable).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 auto pieces = splitIntoParts(doc.mesh);
                 if (pieces.empty()) return {false, doc.activePartInfo().name + " is a single piece"};
                 const size_t n = pieces.size();
                 replaceWithPieces(doc, doc.activePart, std::move(pieces));
                 return {true, "Split into " + std::to_string(n) + " Parts"};
             }});

    const ParamSpec keepParam{"keep", T::Bool,
                              "Keep original: copy the triangles into the new Part and leave the source untouched "
                              "(undo takes the copy away). Default false.",
                              false};
    reg.add({"part_split_hidden", "Move the hidden triangles of the active Part into a new Part below it (one undo step).",
             {keepParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 return splitOffPart(doc, TriangleSet::Hidden, "Split Hidden", "hidden", "hidden",
                                     argBool(a, "keep", false));
             }});

    reg.add({"part_split_masked",
             "Move the masked triangles of the active Part (corners' average mask above 0.5) into a new, unmasked "
             "Part below it (one undo step).",
             {keepParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 return splitOffPart(doc, TriangleSet::Masked, "Split Masked", "masked", "masked",
                                     argBool(a, "keep", false));
             }});

    reg.add({"part_split_unmasked",
             "Move the unmasked triangles of the active Part (corners' average mask 0.5 or less) into a new Part "
             "below it; the masked ones stay (one undo step).",
             {keepParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 return splitOffPart(doc, TriangleSet::Unmasked, "Split Unmasked", "unmasked", "unmasked",
                                     argBool(a, "keep", false));
             }});
}

} // namespace sf
