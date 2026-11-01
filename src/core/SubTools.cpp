// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SubTools.h"

#include "core/Commands.h"
#include "core/Dyntopo.h"
#include "core/Masking.h"
#include "core/Multires.h"
#include "core/Polygroups.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/TextureTransfer.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"

#include <cctype>
#include <algorithm>
#include <filesystem>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

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
        for (const auto& l : m->layers)
            if (layerWeights.emplace(l.name, l.weight).second) layerNames.push_back(l.name);
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
            const uint32_t g = m->polygroupAt(t);
            out.polygroups.push_back(groupOffset + g);
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
    for (auto& g : out.polygroups) ++g;
    if (out.hasQuads()) out.quadPartners.resize(out.triangles.size(), Mesh::kNoQuad);
    out.rebuildTopology();
    if (!out.layers.empty()) out.evaluateAll(); // weights of same-named layers may have differed
    out.recomputeAllNormals();
    return out;
}

Mesh mergedVisibleMesh(const Document& doc) {
    if (doc.subtoolCount() == 1) return doc.mesh;
    std::vector<const Mesh*> meshes;
    for (int i = 0; i < doc.subtoolCount(); ++i)
        if (doc.subtools[size_t(i)].visible && doc.subtoolMesh(i).triangleCount()) meshes.push_back(&doc.subtoolMesh(i));
    if (meshes.empty()) return Mesh();
    if (meshes.size() == 1) return *meshes.front();
    return mergeMeshes(meshes);
}

std::vector<MeshObjectRef> visibleSubtoolObjects(const Document& doc) {
    if (doc.subtoolCount() == 1) {
        if (doc.mesh.triangleCount() == 0) return {};
        return {{doc.assetName(), &doc.mesh}};
    }
    std::vector<MeshObjectRef> out;
    for (int i = 0; i < doc.subtoolCount(); ++i)
        if (doc.subtools[size_t(i)].visible && doc.subtoolMesh(i).triangleCount())
            out.push_back({doc.subtools[size_t(i)].name, &doc.subtoolMesh(i)});
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
    for (size_t i = 1; i < objects.size(); ++i) doc.addSubtool(std::move(objects[i].mesh), nameOf(i), false);
    doc.markClean();
}

std::vector<Mesh> splitByPolygroups(const Mesh& mesh) {
    std::vector<uint32_t> ids;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) ids.push_back(mesh.polygroupAt(t));
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<Mesh> out;
    if (ids.size() < 2) return out;
    for (uint32_t id : ids) {
        std::vector<uint8_t> keep(mesh.triangleCount());
        for (uint32_t t = 0; t < keep.size(); ++t) keep[t] = mesh.polygroupAt(t) == id;
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

// The SubTool an argument names: index= (0-based) or name=, else the active one. -1 if not found.
int targetSubtool(const Document& doc, const Args& a) {
    if (a.contains("name")) return doc.findSubtool(argString(a, "name"));
    if (a.contains("index")) {
        const int i = int(argNumber(a, "index", -1));
        return i >= 0 && i < doc.subtoolCount() ? i : -1;
    }
    return doc.activeSubtool;
}

CommandResult notFound(const Args& a) {
    return {false, a.contains("name") ? "No Part named '" + argString(a, "name") + "'" : std::string("No such Part")};
}

// Replaces SubTool `index` by `pieces`, named after it ("Body 1", "Body 2", ...); the first piece is selected.
void replaceWithPieces(Document& doc, int index, std::vector<Mesh> pieces) {
    noteSubtoolLevelsDropped(doc, index); // the pieces are cut from the current level
    const std::string base = doc.subtools[size_t(index)].name;
    const bool visible = doc.subtools[size_t(index)].visible;
    for (size_t k = 0; k < pieces.size(); ++k) {
        const int at = doc.addSubtool(std::move(pieces[k]), base + " " + std::to_string(k + 1), false, index + 1 + int(k));
        doc.subtools[size_t(at)].visible = visible;
    }
    doc.selectSubtool(index + 1);
    doc.removeSubtool(index);
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

// The shape of `source` for inserting: its triangles and quads only (no mask, paint, UVs, layers or levels),
// centred on its bounds and scaled so its largest side is `size`. False if it has no triangles.
bool normalizedPiece(const Mesh& source, float size, Mesh& out) {
    if (source.triangleCount() == 0 || source.vertexCount() == 0) return false;
    out = Mesh();
    out.positions = source.positions;
    out.triangles = source.triangles;
    if (source.hasQuads()) out.quadPartners = source.quadPartners;
    out.polygroups.assign(out.triangles.size(), 0u);
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
// SubTool as one new polygroup, one undo step.
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
    for (uint32_t g : doc.mesh.polygroups) group = std::max(group, g);
    prim.polygroups.assign(prim.triangles.size(), group + 1);
    prim.recomputeAllNormals();
    // Symmetry: a copy for each mirror image and radial turn (about the same centre as the brushes'), in the
    // same polygroup and the same undo step. A copy that would land on one already there (inserted on a plane
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

void registerSubToolCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    const ParamSpec indexParam{"index", T::Number, "Part index (0 = top of the list). Default: the active one.", false};
    const ParamSpec nameParam{"name", T::String, "Part name (instead of index).", false};

    reg.add({"subtool_list", "List the Parts (separate meshes) in the scene.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 std::string out = std::to_string(doc.subtoolCount()) + " Part" + (doc.subtoolCount() == 1 ? "" : "s") + ":";
                 for (int i = 0; i < doc.subtoolCount(); ++i) {
                     const SubTool& s = doc.subtools[size_t(i)];
                     out += "\n" + std::to_string(i) + ". " + s.name + " (" + countText(doc.subtoolMesh(i).triangleCount());
                     if (subdivLevelCount(doc.subtoolMesh(i))) out += ", " + subdivLevelText(doc.subtoolMesh(i));
                     if (i == doc.activeSubtool) out += ", active";
                     if (!s.visible) out += ", hidden";
                     out += ")";
                 }
                 return {true, out};
             }});

    reg.add({"subtool_select", "Make a Part the active one: brushes and commands work on it.", {indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (!a.contains("index") && !a.contains("name")) return {false, "Give index= or name="};
                 const int i = targetSubtool(doc, a);
                 if (i < 0) return notFound(a);
                 doc.selectSubtool(i);
                 doc.subtools[size_t(i)].visible = true; // you can't sculpt what you can't see
                 return {true, "Active Part: " + doc.subtools[size_t(i)].name};
             }});

    reg.add({"subtool_append",
             "Add a new Part: a primitive (sphere, cube, plane) placed beside the visible model, or a model file. "
             "It becomes the active Part.",
             {{"type", T::String, "'sphere' (default), 'cube', 'plane', 'cylinder', 'cone', 'torus', 'capsule', 'icosphere' or 'quadsphere'.", false},
              {"path", T::String, "Model file to append instead of a primitive (.obj, .glb, .fbx, .ply, .stl, .usd...).", false},
              {"name", T::String, "Name for the new Part.", false},
              {"size", T::Number, "Primitive size relative to the largest visible Part (0.05-2). Default 0.35.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh m;
                 std::string name;
                 if (a.contains("path")) {
                     // Load through `open` into a scratch document, so every format and its attributes work;
                     // each object in the file becomes its own SubTool.
                     Document scratch;
                     const std::string path = argString(a, "path");
                     CommandResult r = CommandRegistry().execute(scratch, "open", {{"path", path}});
                     if (!r.ok) return r;
                     if (scratch.subtoolCount() == 1) {
                         if (scratch.mesh.triangleCount() == 0) return {false, path + " has no mesh to append"};
                         m = std::move(scratch.mesh);
                         name = fileStem(path);
                     } else {
                         const int first = doc.subtoolCount();
                         size_t tris = 0;
                         for (int i = 0; i < scratch.subtoolCount(); ++i) {
                             tris += scratch.subtoolMesh(i).triangleCount();
                             doc.addSubtool(std::move(scratch.subtoolMesh(i)), scratch.subtools[size_t(i)].name, false);
                         }
                         doc.selectSubtool(first);
                         return {true, "Appended " + std::to_string(scratch.subtoolCount()) + " Parts from " + path + " (" +
                                           countText(tris) + ")"};
                     }
                 } else {
                     const std::string type = argString(a, "type", "sphere");
                     // Sized from the largest visible SubTool (so repeated appends neither grow nor shrink),
                     // placed beside everything shown.
                     const AABB scene = doc.visibleBounds();
                     float extent = 0.0f;
                     for (int i = 0; i < doc.subtoolCount(); ++i) {
                         const Mesh& sm = doc.subtoolMesh(i);
                         if (!doc.subtools[size_t(i)].visible || !sm.vertexCount()) continue;
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
                 const int i = doc.addSubtool(std::move(m), name);
                 return {true, "Appended Part " + doc.subtools[size_t(i)].name + " (" + countText(tris) + ")"};
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
        {"subtool", T::String, "insert a copy of this Part (by name)", false},
        {"file", T::String, "insert the mesh in this model file (.obj, .ply or .glb)", false}};
    meshParams.insert(meshParams.end(), placeParams.begin(), placeParams.end());
    reg.add({"insert_mesh",
             "Insert your own mesh into the active Part where the surface is (#34), placed like insert_primitive: "
             "a copy of another Part or a model file, scaled so its largest side is `size`, its up (+Y) axis "
             "along the surface normal, as one new face group; one undoable step.",
             meshParams,
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string blocked = dyntopoBlocker(doc.mesh);
                 if (!blocked.empty()) return {false, "Can't insert: " + blocked};
                 if (a.contains("subtool") == a.contains("file")) return {false, "Give subtool= or file="};
                 Mesh source;
                 std::string label;
                 if (a.contains("subtool")) {
                     label = argString(a, "subtool");
                     const int i = doc.findSubtool(label);
                     if (i < 0) return {false, "No Part named '" + label + "'"};
                     source = doc.subtoolMesh(i);
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

    reg.add({"subtool_duplicate", "Copy a Part (with its layers and attributes) and make the copy active.",
             {indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetSubtool(doc, a);
                 if (i < 0) return notFound(a);
                 Mesh copy = doc.subtoolMesh(i);
                 const Shading shading = doc.subtools[size_t(i)].shading;
                 const int added = doc.addSubtool(std::move(copy), doc.subtools[size_t(i)].name + " copy", true, i + 1);
                 doc.subtools[size_t(added)].shading = shading;
                 return {true, "Duplicated as " + doc.subtools[size_t(added)].name};
             }});

    reg.add({"subtool_delete", "Delete a Part (not undoable). The last Part can't be deleted.",
             {indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetSubtool(doc, a);
                 if (i < 0) return notFound(a);
                 if (doc.subtoolCount() == 1) return {false, "The scene needs at least one Part"};
                 const std::string name = doc.subtools[size_t(i)].name;
                 doc.removeSubtool(i);
                 return {true, "Deleted Part " + name};
             }});

    reg.add({"subtool_rename", "Rename a Part.",
             {{"to", T::String, "The new name.", true}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetSubtool(doc, a);
                 if (i < 0) return notFound(a);
                 std::string to = argString(a, "to");
                 if (to.empty()) return {false, "The name can't be empty"};
                 if (doc.subtools[size_t(i)].name != to) to = doc.uniqueSubtoolName(to);
                 doc.subtools[size_t(i)].name = to;
                 doc.modified = true;
                 return {true, "Renamed to " + to};
             }});

    reg.add({"subtool_visibility", "Show or hide a Part. Hidden Parts aren't drawn, picked or exported.",
             {{"visible", T::Bool, "true to show, false to hide. Default: toggle.", false}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetSubtool(doc, a);
                 if (i < 0) return notFound(a);
                 SubTool& s = doc.subtools[size_t(i)];
                 s.visible = argBool(a, "visible", !s.visible);
                 doc.modified = true;
                 return {true, s.name + (s.visible ? " shown" : " hidden")};
             }});

    reg.add({"subtool_shading",
             "Shade a Part smooth or flat (faceted), or as the scene's Flat shading setting says (default).",
             {{"mode", T::String, "default, smooth or flat.", true}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetSubtool(doc, a);
                 if (i < 0) return notFound(a);
                 Shading mode;
                 if (!parseShading(argString(a, "mode"), mode))
                     return {false, "Unknown shading '" + argString(a, "mode") + "' (default, smooth, flat)"};
                 SubTool& s = doc.subtools[size_t(i)];
                 if (s.shading != mode) doc.modified = true;
                 s.shading = mode;
                 return {true, s.name + " shading: " + shadingName(mode)};
             }});

    reg.add({"subtool_solo", "Show only the active Part, or (when it already is the only one shown) show all again.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 bool othersShown = false;
                 for (int i = 0; i < doc.subtoolCount(); ++i)
                     othersShown |= i != doc.activeSubtool && doc.subtools[size_t(i)].visible;
                 for (int i = 0; i < doc.subtoolCount(); ++i)
                     doc.subtools[size_t(i)].visible = i == doc.activeSubtool || !othersShown;
                 doc.modified = true;
                 return {true, othersShown ? "Solo: " + doc.activeSubtoolInfo().name : std::string("All Parts shown")};
             }});

    reg.add({"subtool_show_all", "Show every Part.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 for (auto& s : doc.subtools) s.visible = true;
                 doc.modified = true;
                 return {true, "All Parts shown"};
             }});

    reg.add({"subtool_move", "Move a Part up or down the list.",
             {{"direction", T::String, "'up' or 'down'.", true}, indexParam, nameParam},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = targetSubtool(doc, a);
                 if (i < 0) return notFound(a);
                 const int dir = argString(a, "direction") == "up" ? -1 : 1;
                 if (i + dir < 0 || i + dir >= doc.subtoolCount()) return {false, "Already at the " + std::string(dir < 0 ? "top" : "bottom")};
                 doc.moveSubtool(i, dir);
                 return {true, "Moved " + doc.subtools[size_t(i + dir)].name + (dir < 0 ? " up" : " down")};
             }});

    reg.add({"subtool_merge_down",
             "Merge the active Part with the one below it into one mesh (not undoable). Each keeps its own "
             "face groups, so subtool_split_groups can separate them again. Painted textures are kept in one new "
             "texture on new Auto UVs.",
             {{"keep_texture", T::Bool, "Keep painted textures (Auto UV the result and copy them in). Default true.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = doc.activeSubtool;
                 if (i + 1 >= doc.subtoolCount()) return {false, "There's no Part below " + doc.activeSubtoolInfo().name};
                 Mesh merged = mergeMeshes({&doc.subtoolMesh(i), &doc.subtoolMesh(i + 1)});
                 const std::string texture = carryMergedTexturesNote({&doc.subtoolMesh(i), &doc.subtoolMesh(i + 1)}, merged,
                                                                     argBool(a, "keep_texture", true));
                 for (int k : {i, i + 1}) noteSubtoolLevelsDropped(doc, k); // merged at their current levels
                 const std::string name = doc.subtools[size_t(i)].name, below = doc.subtools[size_t(i + 1)].name;
                 doc.removeSubtool(i + 1);
                 doc.replaceActiveMesh(std::move(merged));
                 return {true, "Merged " + below + " into " + name + " (" + countText(doc.mesh.triangleCount()) + ")" + texture};
             }});

    reg.add({"subtool_merge_visible",
             "Merge every visible Part into one new Part, replacing them (not undoable). Hidden Parts stay "
             "as they are. Painted textures are kept in one new texture on new Auto UVs.",
             {{"keep_texture", T::Bool, "Keep painted textures (Auto UV the result and copy them in). Default true.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<int> visible;
                 for (int i = 0; i < doc.subtoolCount(); ++i)
                     if (doc.subtools[size_t(i)].visible) visible.push_back(i);
                 if (visible.size() < 2) return {false, "Show at least two Parts to merge"};
                 Mesh merged = mergedVisibleMesh(doc);
                 std::vector<const Mesh*> parts; // as mergedVisibleMesh gathers them
                 for (int i : visible)
                     if (doc.subtoolMesh(i).triangleCount()) parts.push_back(&doc.subtoolMesh(i));
                 const std::string texture =
                     parts.size() > 1 ? carryMergedTexturesNote(parts, merged, argBool(a, "keep_texture", true)) : std::string();
                 for (int k : visible) noteSubtoolLevelsDropped(doc, k); // merged at their current levels
                 const size_t tris = merged.triangleCount();
                 const int at = visible.front();
                 doc.addSubtool(std::move(merged), "Merged", true, at);
                 for (auto it = visible.rbegin(); it != visible.rend(); ++it) doc.removeSubtool(*it + 1);
                 return {true, "Merged " + std::to_string(visible.size()) + " Parts into " +
                                   doc.activeSubtoolInfo().name + " (" + countText(tris) + ")" + texture};
             }});

    reg.add({"subtool_split_groups", "Split the active Part into one Part per face group (not undoable).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 auto pieces = splitByPolygroups(doc.mesh);
                 if (pieces.empty()) return {false, doc.activeSubtoolInfo().name + " has only one face group"};
                 const size_t n = pieces.size();
                 replaceWithPieces(doc, doc.activeSubtool, std::move(pieces));
                 return {true, "Split into " + std::to_string(n) + " Parts"};
             }});

    reg.add({"subtool_split_parts", "Split the active Part into one Part per disconnected piece (not undoable).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 auto pieces = splitIntoParts(doc.mesh);
                 if (pieces.empty()) return {false, doc.activeSubtoolInfo().name + " is a single piece"};
                 const size_t n = pieces.size();
                 replaceWithPieces(doc, doc.activeSubtool, std::move(pieces));
                 return {true, "Split into " + std::to_string(n) + " Parts"};
             }});
}

} // namespace sf
