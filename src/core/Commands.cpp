// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Commands.h"
#include "core/ViewCapture.h"

#include "core/Bake.h"
#include "core/Benchmark.h"
#include "core/BrushAlpha.h"
#include "core/BrushLibrary.h"
#include "core/Decimate.h"
#include "core/KeepMasked.h"
#include "core/Macro.h"
#include "core/Pack.h"
#include "core/Dyntopo.h"
#include "core/EnginePresets.h"
#include "core/ExtractMirror.h"
#include "core/Face.h"
#include "core/HardwareProfile.h"
#include "core/ImageTo3D.h"
#include "core/Masking.h"
#include "core/Cut.h"
#include "core/Multires.h"
#include "core/Micro.h"
#include "core/MeshTransform.h"
#include "core/PathStroke.h"
#include "core/Polygroups.h"
#include "core/SelectionSets.h"
#include "core/SubTools.h"
#include "core/Polypaint.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/QRemesh.h"
#include "core/ZSpheres.h"
#include "core/Deform.h"
#include "core/Sculptable.h"
#include "core/Splats.h"
#include "core/StarterMeshes.h"
#include "core/TextureTransfer.h"
#include "core/Timeline.h"
#include "core/Voxel.h"
#include "io/FbxIO.h"
#include "io/GltfIO.h"
#include "io/BlenderCommands.h"
#include "io/GozCommands.h"
#include "io/GozIO.h"
#include "io/MayaCommands.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"
#include "io/StlIO.h"
#include "io/UsdIO.h"

#include <algorithm>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>

namespace sf {

double argNumber(const Args& args, const std::string& key, double fallback) {
    auto it = args.find(key);
    if (it == args.end()) return fallback;
    if (auto* d = std::get_if<double>(&it->second)) return *d;
    if (auto* b = std::get_if<bool>(&it->second)) return *b ? 1.0 : 0.0;
    return fallback;
}

bool argBool(const Args& args, const std::string& key, bool fallback) {
    auto it = args.find(key);
    if (it == args.end()) return fallback;
    if (auto* b = std::get_if<bool>(&it->second)) return *b;
    if (auto* d = std::get_if<double>(&it->second)) return *d != 0.0;
    return fallback;
}

std::string argString(const Args& args, const std::string& key, const std::string& fallback) {
    auto it = args.find(key);
    if (it == args.end()) return fallback;
    if (auto* s = std::get_if<std::string>(&it->second)) return *s;
    return fallback;
}

namespace {

constexpr size_t kMaxSubdivideTriangles = 50'000'000;

std::string lowerExtension(const std::string& path) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

std::string fileStem(const std::string& path) {
    const auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

// " (3 SubTools)" for a multi-object export, else nothing.
std::string objectCountText(const std::vector<MeshObjectRef>& objects) {
    return objects.size() > 1 ? " (" + std::to_string(objects.size()) + " Parts)" : std::string();
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        default: out += c;
        }
    }
    return out;
}

} // namespace

CommandRegistry::CommandRegistry() {
    using T = ParamSpec::Type;

    add({"new_sphere",
         "Replace the scene with a new sphere ready for sculpting.",
         {{"subdivisions", T::Number, "Icosphere subdivision level (0-8). Default 6.", false},
          {"triangles", T::Number,
           "About this many triangles instead (a cube's quads pushed onto the sphere, up to 48M; benchmarks: "
           "tools/bench_strokes.sh).",
           false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (const double t = argNumber(a, "triangles", 0.0); t > 0.0) {
                 const int res = std::clamp(int(std::lround(std::sqrt(t / 12.0))), 1, 2000);
                 Mesh m = makeCube(res, 2.0f);
                 for (glm::vec3& p : m.positions) p = glm::normalize(p);
                 m.recomputeAllNormals();
                 doc.setMesh(std::move(m), "Sphere");
                 doc.path.clear();
                 return {true, "Created sphere with " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
             }
             int level = std::clamp(int(argNumber(a, "subdivisions", 6)), 0, 8);
             doc.setMesh(makeIcosphere(level), "Sphere");
             doc.path.clear();
             return {true, "Created sphere with " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"new_plane",
         "Replace the scene with a flat grid plane.",
         {{"resolution", T::Number, "Grid cells per side (1-2000). Default 128.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             int res = std::clamp(int(argNumber(a, "resolution", 128)), 1, 2000);
             doc.setMesh(makePlane(res), "Plane");
             doc.path.clear();
             return {true, "Created plane with " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"open_obj",
         "Load a Wavefront OBJ file, replacing the current scene.",
         {{"path", T::String, "File path (UTF-8).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path");
             std::vector<MeshObject> objects;
             std::string err;
             if (!loadObj(path, objects, err)) return {false, err};
             const size_t count = objects.size();
             setSceneObjects(doc, std::move(objects), fileStem(path));
             doc.path = path;
             return {true, "Loaded " + path + (count > 1 ? " (" + std::to_string(count) + " Parts)" : "")};
         }});

    add({"open",
         "Open a file: models (.obj, .fbx, .glb, .gltf, .ply, .stl, .usda, .usdz, ZBrush .GoZ) replace the scene; Gaussian splats "
         "(.ply/.splat captures, KHR_gaussian_splatting .glb, USD ParticleField) load as a reference next to the sculpt. "
         "glTF morph targets and FBX/USD blend shapes become sculpt layers. Files with several objects (OBJ `o`, "
         "glTF nodes, FBX models, USD meshes) open as one Part each. SculptForge projects (.sfproj) "
         "restore the whole scene.",
         {{"path", T::String, "File path (UTF-8).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path");
             if (isProjectPath(path)) return openProjectCommand(doc, path);
             const std::string ext = lowerExtension(path);
             std::vector<MeshObject> objects; // one per object in the file (OBJ `o`, glTF node, FBX model, USD prim)
             Mesh m;
             SplatCloud splats;
             std::string err;
             GozInfo goz;
             bool ok;
             std::string ztoolNote;
             if (ext == ".glb" || ext == ".gltf") ok = loadGltf(path, objects, err, &splats);
             else if (ext == ".usda" || ext == ".usd" || ext == ".usdz") ok = loadUsd(path, objects, splats, err);
             else if (ext == ".splat") ok = loadDotSplat(path, splats, err);
             else if (ext == ".fbx") ok = loadFbx(path, objects, err);
             else if (ext == ".goz") ok = loadGoz(path, m, err, &goz);
             else if (ext == ".stl") ok = loadStl(path, m, err);
             else if (ext == ".ztl" || ext == ".sculptforge") {
                 // A ZTool converted in ZBrush by the SculptForge ZTL Export plug-in (GoZ files beside it).
                 if (convertedZToolFiles(path).empty()) return {false, zbrushProprietaryMessage(".ztl")};
                 ok = loadConvertedZTool(path, objects, err, &ztoolNote);
             } else if (ext == ".zpr" || ext == ".zbr") return {false, zbrushProprietaryMessage(ext)};
             else if (ext == ".ply") {
                 PlyContent content;
                 ok = loadPly(path, m, splats, content, err);
             } else ok = loadObj(path, objects, err);
             if (!ok) return {false, err};
             if (m.triangleCount() > 0) objects.push_back({"", std::move(m)});
             std::erase_if(objects, [](const MeshObject& o) { return o.mesh.triangleCount() == 0; });
             std::string msg = "Opened " + std::filesystem::path(path).filename().string();  // file name only: status shows in screen recordings
             const bool hasMesh = !objects.empty();
             if (hasMesh) {
                 size_t tris = 0, quads = 0, layers = 0;
                 for (const auto& o : objects) {
                     tris += o.mesh.triangleCount();
                     quads += o.mesh.quadCount();
                     layers = std::max(layers, o.mesh.layers.size());
                 }
                 const size_t count = objects.size();
                 setSceneObjects(doc, std::move(objects), ext == ".goz" && !goz.name.empty() ? goz.name : fileStem(path));
                 doc.path = ext == ".obj" ? path : std::string(); // other formats are exported explicitly
                 if (ext == ".goz") doc.gozName = goz.name;
                 msg += " (" + (count > 1 ? std::to_string(count) + " Parts, " : std::string()) + std::to_string(tris) +
                        " triangles" + (quads == 0 ? "" : ", " + std::to_string(quads) + " quads") + (layers == 0 ? "" : ", " + std::to_string(layers) + " layers") + ")";
                 if (!ztoolNote.empty()) msg += "; " + ztoolNote;
             }
             if (!splats.empty()) {
                 doc.splats = std::move(splats);
                 doc.splatsChanged();
                 msg += " (" + std::to_string(doc.splats.size()) + " Gaussian splats, SH degree " +
                        std::to_string(doc.splats.shDegree) + ")";
                 if (hasMesh) doc.markClean(); // a model with its own splats is one freshly opened scene
             }
             return {true, msg};
         }});

    add({"remesh",
         "Voxel remesh (like Voxel Remesh): rebuild the sculpt as an even, watertight surface, merging overlapping "
         "parts and fixing broken scans or generated models. Sculpt layers are baked in. A painted texture is "
         "kept (new Auto UVs, colour projected from the old surface) unless keep_texture=false.",
         {{"resolution", T::Number, "Voxels across the longest side (32-512). Default 200; higher keeps more detail.", false},
          {"keep_texture", T::Bool, "Keep a painted texture: Auto UV the result and project the texture onto it. "
                                    "Default true.", false},
          {"keep_masked", T::Bool, "Keep masked: masked areas keep their triangles exactly; the rest is remeshed and "
                                   "joined to them watertight. Default false.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string err;
             const std::vector<uint8_t> keep = argBool(a, "keep_masked", false) ? keepMaskedTriangles(doc.mesh) : std::vector<uint8_t>();
             const bool keeping = std::find(keep.begin(), keep.end(), 1) != keep.end();
             if (keeping && std::find(keep.begin(), keep.end(), 0) == keep.end())
                 return {false, "Remesh: everything is masked, so nothing would change"};
             Mesh m = voxelRemesh(doc.mesh, std::clamp(int(argNumber(a, "resolution", 200)), 32, 512), &err);
             if (m.triangleCount() == 0) return {false, err};
             std::string keepNote;
             if (keeping) {
                 Mesh stitched;
                 if (!stitchKeptTriangles(doc.mesh, keep, m, stitched, err, &keepNote)) return {false, "Remesh (Keep masked): " + err};
                 m = std::move(stitched);
             }
             const std::string texture = carryTextureNote(doc.mesh, m, argBool(a, "keep_texture", true));
             doc.replaceMeshUndoable(std::move(m), "Remesh");
             return {true, "Remeshed to " + std::to_string(doc.mesh.triangleCount()) + " triangles" + texture +
                           (keepNote.empty() ? "" : "; Keep masked: " + keepNote)};
         }});

    add({"image_relief",
         "Turn an image into a sculpted relief: light areas rise, dark areas stay low (logos, coins, emblems, "
         "height maps, texture photos). Replaces the scene.",
         {{"path", T::String, "Image file (png, jpg, bmp, tga...).", true},
          {"depth", T::Number, "Relief height as a fraction of the width (0.01-1). Default 0.12.", false},
          {"resolution", T::Number, "Detail: grid cells along the long side (32-2048). Default 384.", false},
          {"invert", T::Bool, "Make dark areas high instead.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             Image img;
             std::string err;
             if (!Image::load(argString(a, "path"), img, err)) return {false, err};
             ReliefOptions o;
             o.depth = float(std::clamp(argNumber(a, "depth", o.depth), 0.01, 1.0));
             o.resolution = int(std::clamp(argNumber(a, "resolution", o.resolution), 32.0, 2048.0));
             o.invert = argBool(a, "invert", false);
             doc.replaceMeshUndoable(makeImageRelief(img, o), "Image Relief");
             doc.path.clear();
             return {true, "Relief with " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"image_inflate",
         "Inflate a sketch or silhouette (transparent background or plain backdrop) into a rounded, closed 3D base "
         "mesh ready for sculpting. Replaces the scene.",
         {{"path", T::String, "Image file.", true},
          {"thickness", T::Number, "1 = round, lower = flatter (0.1-3). Default 1.", false},
          {"resolution", T::Number, "Detail along the long side (32-512). Default 160.", false},
          {"subdivisions", T::Number, "Smoothing subdivision passes (0-3). Default 1.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             Image img;
             std::string err;
             if (!Image::load(argString(a, "path"), img, err)) return {false, err};
             InflateOptions o;
             o.thickness = float(std::clamp(argNumber(a, "thickness", 1.0), 0.1, 3.0));
             o.resolution = int(std::clamp(argNumber(a, "resolution", o.resolution), 32.0, 512.0));
             o.subdivisions = int(std::clamp(argNumber(a, "subdivisions", 1.0), 0.0, 3.0));
             Mesh m = inflateSilhouette(img, o, &err);
             if (m.triangleCount() == 0) return {false, err};
             doc.replaceMeshUndoable(std::move(m), "Inflate Sketch");
             doc.path.clear();
             return {true, "Inflated sketch into " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"image_project",
         "Stamp an image onto the current model as surface detail, projected from the front (+Z). Light = raised. "
         "Goes into the active sculpt layer.",
         {{"path", T::String, "Image file.", true},
          {"depth", T::Number, "Displacement at full brightness, as a fraction of the model size (0.001-0.5). Default 0.03.", false},
          {"invert", T::Bool, "Make dark areas raised instead.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             Image img;
             std::string err;
             if (!Image::load(argString(a, "path"), img, err)) return {false, err};
             AABB b = doc.mesh.bounds();
             if (!b.valid()) return {false, "Nothing to project onto"};
             const float size = glm::length(b.extent());
             const glm::vec3 eye = b.center() + glm::vec3(0, 0, size * 2.0f);
             const float halfH = std::max(b.extent().x / std::max(img.aspect(), 1e-3f), b.extent().y) * 0.5f;
             const float halfW = halfH * img.aspect();
             glm::mat4 proj = glm::ortho(-halfW, halfW, -halfH, halfH, 0.01f, size * 4.0f);
             glm::mat4 view = glm::lookAt(eye, b.center(), glm::vec3(0, 1, 0));
             ProjectOptions o;
             o.depth = float(std::clamp(argNumber(a, "depth", 0.03), 0.001, 0.5)) * size;
             o.invert = argBool(a, "invert", false);
             o.coverage = 1.0f;
             Mesh m = doc.mesh;
             size_t n = projectImage(m, doc.activeLayer, img, proj * view, img.aspect(), eye, o);
             if (n == 0) return {false, "The image did not land on any front-facing surface"};
             doc.replaceMeshUndoable(std::move(m), "Project Image");
             return {true, "Projected onto " + std::to_string(n) + " vertices"};
         }});

    add({"save_obj",
         "Export the visible Parts as a Wavefront OBJ file (one `o` object each, face groups as `g` groups). OBJ "
         "cannot hold the mask, visibility, sculpt layers or splats, so this is an export: the document keeps its "
         "path and unsaved-changes state. UVs are kept; a textured Part also writes <name>.mtl and its PNG beside "
         "the file.",
         {{"path", T::String, "File path (UTF-8). Defaults to the current document path unless that is a project.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path", isProjectPath(doc.path) ? std::string() : doc.path);
             if (path.empty()) return {false, "No path given"};
             if (isProjectPath(path)) return {false, "Use save_project for .sfproj files"};
             std::string err;
             const auto objects = visibleSubtoolObjects(doc);
             if (objects.empty()) return {false, "Nothing visible to export"};
             if (!saveObj(path, objects, err)) return {false, err};
             return {true, "Saved " + path + objectCountText(objects)};
         }});

    add({"subdivide",
         "Smoothly subdivide the mesh (Loop subdivision). Each level multiplies the triangle count by 4. Unlike "
         "Divide (subdiv_divide), it keeps no lower levels to step back to.",
         {{"levels", T::Number, "Number of subdivision levels to apply (1-4). Default 1.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             int levels = std::clamp(int(argNumber(a, "levels", 1)), 1, 4);
             size_t projected = doc.mesh.triangleCount();
             for (int i = 0; i < levels; ++i) projected *= 4;
             if (projected > kMaxSubdivideTriangles)
                 return {false, "Subdividing would exceed " + std::to_string(kMaxSubdivideTriangles) + " triangles"};
             Mesh m = loopSubdivide(doc.mesh);
             for (int i = 1; i < levels; ++i) m = loopSubdivide(m);
             doc.replaceMeshUndoable(std::move(m), "Subdivide");
             return {true, "Subdivided to " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"layer_add",
         "Add a sculpt layer (blend shape / morph target). New strokes go into it until another layer is made active.",
         {{"name", T::String, "Layer name, e.g. 'Smile' or 'BlinkL'.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             Mesh m = doc.mesh;
             int idx = m.addLayer(argString(a, "name", "Layer " + std::to_string(doc.mesh.layers.size() + 1)));
             doc.replaceMeshUndoable(std::move(m), "Add Layer");
             doc.activeLayer = idx;
             return {true, "Added layer '" + doc.mesh.layers[idx].name + "' (index " + std::to_string(idx) + ")"};
         }});

    add({"layer_remove",
         "Delete a sculpt layer and its offsets.",
         {{"index", T::Number, "Layer index (0-based).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             int idx = int(argNumber(a, "index", -1));
             if (idx < 0 || idx >= int(doc.mesh.layers.size())) return {false, "No layer " + std::to_string(idx)};
             Mesh m = doc.mesh;
             m.removeLayer(idx);
             doc.replaceMeshUndoable(std::move(m), "Remove Layer");
             doc.activeLayer = -1;
             return {true, "Removed layer " + std::to_string(idx)};
         }});

    add({"layer_set_weight",
         "Set how strongly a sculpt layer is applied (0 = off, 1 = as sculpted; negative or >1 exaggerates).",
         {{"index", T::Number, "Layer index (0-based).", true},
          {"weight", T::Number, "Blend weight, typically 0-1.", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             int idx = int(argNumber(a, "index", -1));
             if (idx < 0 || idx >= int(doc.mesh.layers.size())) return {false, "No layer " + std::to_string(idx)};
             doc.mesh.setLayerWeight(idx, float(argNumber(a, "weight", 1.0)));
             doc.bvhDirty = true;
             doc.modified = true;
             return {true, "Layer " + std::to_string(idx) + " weight set"};
         }});

    add({"layer_set_active",
         "Choose which sculpt layer new strokes go into (-1 = base mesh).",
         {{"index", T::Number, "Layer index, or -1 for the base mesh.", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             int idx = int(argNumber(a, "index", -1));
             if (idx < -1 || idx >= int(doc.mesh.layers.size())) return {false, "No layer " + std::to_string(idx)};
             doc.activeLayer = idx;
             return {true, idx < 0 ? "Sculpting on base mesh" : "Sculpting on layer " + doc.mesh.layers[idx].name};
         }});

    add({"decimate",
         "Reduce triangle count while preserving shape (quadric edge collapse). Sculpt layers are kept.",
         {{"ratio", T::Number, "Fraction of triangles to keep (0.01-1). Default 0.5.", false},
          {"triangles", T::Number, "Exact target triangle count (overrides ratio).", false},
          {"keep_masked", T::Bool, "Keep masked: masked vertices aren't moved or removed, so masked areas keep their "
                                   "triangles exactly. Default false.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             size_t target = a.contains("triangles")
                                 ? size_t(std::max(4.0, argNumber(a, "triangles", 4)))
                                 : size_t(double(doc.mesh.triangleCount()) *
                                          std::clamp(argNumber(a, "ratio", 0.5), 0.01, 1.0));
             doc.replaceMeshUndoable(decimate(doc.mesh, target, argBool(a, "keep_masked", false)), "Decimate");
             return {true, "Decimated to " + std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});

    add({"export_glb",
         "Export a game-ready binary glTF (.glb), one node and mesh per visible Part. Sculpt layers become "
         "morph targets (blend shapes). UVs are kept and Part textures are embedded. A baked face take "
         "(face_take_bake) becomes an animation of the morph target weights.",
         {{"path", T::String, "Output .glb path (UTF-8).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path");
             std::string err;
             const auto objects = visibleSubtoolObjects(doc);
             if (objects.empty()) return {false, "Nothing visible to export"};
             MorphAnimation take;
             const bool animated = bakedFaceAnimation(doc, take);
             if (!saveGlb(path, objects, doc.assetName(), err, animated ? &take : nullptr)) return {false, err};
             return {true, "Exported " + path + objectCountText(objects) + (animated ? " with the take '" + take.name + "'" : "")};
         }});

    add({"export_usd",
         "Export OpenUSD (.usda text, or .usdz package for AR Quick Look / Vision Pro), one Mesh prim per visible "
         "Part. Sculpt layers become "
         "UsdSkel blend shapes with their current weights; loaded Gaussian splats are included as a "
         "ParticleField3DGaussianSplat prim. UVs are kept; a Part's texture becomes a UsdPreviewSurface texture "
         "(packed into .usdz, else a PNG beside the file). A baked face take (face_take_bake) becomes time samples "
         "of the blend shape weights.",
         {{"path", T::String, "Output .usda, .usd or .usdz path (UTF-8).", true},
          {"splats", T::Bool, "Include the loaded Gaussian splats. Default true.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string path = argString(a, "path");
             std::string err;
             const SplatCloud* splats = argBool(a, "splats", true) && !doc.splats.empty() ? &doc.splats : nullptr;
             const auto objects = visibleSubtoolObjects(doc);
             MorphAnimation take;
             const bool animated = bakedFaceAnimation(doc, take);
             if (!saveUsd(path, objects, splats, doc.assetName(), err, animated ? &take : nullptr)) return {false, err};
             return {true, "Exported " + path + objectCountText(objects) +
                               (splats ? " (mesh + " + std::to_string(splats->size()) + " splats)" : "") +
                               (animated ? " with the take '" + take.name + "'" : "")};
         }});

    add({"export_fbx",
         "Export binary FBX, one model per visible Part: sculpt layers become blend shapes; with lods > 1 each "
         "Part's LOD chain is written as an FBX LOD group in the same file. UVs are kept; a Part's texture is "
         "written as a PNG beside the file and used by its material.",
         {{"path", T::String, "Output .fbx path (UTF-8).", true},
          {"lods", T::Number, "LOD levels including LOD0 (1-8). Default 1.", false},
          {"ratio", T::Number, "Triangle ratio between LODs (0.1-0.9). Default 0.5.", false},
          {"scale", T::Number, "Multiply positions (FBX units are centimetres). Default 100 (1 unit = 1 m).", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             const int levels = std::clamp(int(argNumber(a, "lods", 1)), 1, 8);
             const auto objects = visibleSubtoolObjects(doc);
             if (objects.empty()) return {false, "Nothing visible to export"};
             // One model per visible SubTool; with lods > 1 each gets its own LOD group (see FbxIO.h).
             std::vector<std::vector<Mesh>> lods(objects.size());
             std::vector<FbxObject> fbxObjects;
             for (size_t i = 0; i < objects.size(); ++i) {
                 FbxObject fo{objects[i].name, {objects[i].mesh}};
                 if (levels > 1) {
                     lods[i] = generateLods(*objects[i].mesh, levels, float(std::clamp(argNumber(a, "ratio", 0.5), 0.1, 0.9)));
                     fo.lods.clear();
                     for (const auto& l : lods[i]) fo.lods.push_back(&l);
                 }
                 fbxObjects.push_back(std::move(fo));
             }
             FbxExportOptions o;
             o.scale = float(argNumber(a, "scale", 100.0));
             std::string err;
             const std::string path = argString(a, "path");
             if (!saveFbx(path, fbxObjects, o, err)) return {false, err};
             return {true, "Exported " + path + objectCountText(objects)};
         }});

    add({"export_unreal",
         "Export an Unreal Engine-ready FBX: centimetres, SM_/SK_ naming, LOD chain packed for automatic LOD "
         "import; a sculpt with layers becomes a skeletal mesh with morph targets. Every visible Part is its own "
         "model (Unreal's Combine Meshes makes them one asset).",
         {{"folder", T::String, "Output folder (UTF-8). The file is named after the asset.", true},
          {"name", T::String, "Asset name (default SM_<document> or SK_<document>).", false},
          {"height_cm", T::Number, "Model height in Unreal, in cm. Default: 1 unit = 1 m.", false},
          {"lods", T::Number, "LOD levels for static meshes (1-8). Default 4.", false},
          {"ratio", T::Number, "Triangle ratio between LODs (0.1-0.9). Default 0.5.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             UnrealExportOptions o;
             o.folder = argString(a, "folder");
             o.assetName = argString(a, "name");
             o.heightCm = float(argNumber(a, "height_cm", 0.0));
             o.lods = int(argNumber(a, "lods", 4));
             o.ratio = float(argNumber(a, "ratio", 0.5));
             UnrealExportResult r;
             std::string err;
             if (!exportForUnreal(doc, o, r, err)) return {false, err};
             size_t morphs = 0;
             for (const auto& obj : visibleSubtoolObjects(doc)) morphs += obj.mesh->layers.size();
             return {true, "Exported " + r.path + " (" + (r.skeletal ? "skeletal mesh, " + std::to_string(morphs) + " morph targets"
                                                                     : "static mesh, " + std::to_string(r.lodCount) + " LODs") + ")"};
         }});

    add({"export_lods",
         "Generate a LOD chain of every visible Part and export each level as <name>_LOD<n>.glb next to `path`. "
         "Blend shapes are kept on every LOD.",
         {{"path", T::String, "Base output path, e.g. /assets/rock.glb.", true},
          {"levels", T::Number, "Number of LODs including LOD0 (1-8). Default 4.", false},
          {"ratio", T::Number, "Triangle ratio between consecutive LODs (0.1-0.9). Default 0.5.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             const std::string pathArg = argString(a, "path");
             std::filesystem::path base(std::u8string(pathArg.begin(), pathArg.end()));
             int levels = std::clamp(int(argNumber(a, "levels", 4)), 1, 8);
             float ratio = float(std::clamp(argNumber(a, "ratio", 0.5), 0.1, 0.9));
             const auto objects = visibleSubtoolObjects(doc);
             if (objects.empty()) return {false, "Nothing visible to export"};
             // Every visible SubTool is decimated on its own; LOD n of each goes into <name>_LOD<n>.glb.
             std::vector<std::vector<Mesh>> lods;
             for (const auto& o : objects) {
                 lods.push_back(generateLods(*o.mesh, levels, ratio));
                 // Decimation keeps the UVs, so every level can carry the SubTool's texture.
                 for (auto& l : lods.back()) l.texture = o.mesh->texture;
             }
             auto stemU8 = base.stem().u8string();
             std::string stem(stemU8.begin(), stemU8.end());
             std::ostringstream msg;
             msg << "Exported";
             for (size_t i = 0; i < lods[0].size(); ++i) {
                 std::string fileName = stem + "_LOD" + std::to_string(i) + ".glb";
                 auto out = (base.parent_path() / std::u8string(fileName.begin(), fileName.end())).u8string();
                 std::string outPath(out.begin(), out.end());
                 std::string err;
                 std::vector<MeshObjectRef> level;
                 size_t tris = 0;
                 for (size_t k = 0; k < objects.size(); ++k) {
                     const Mesh& l = lods[k][std::min(i, lods[k].size() - 1)];
                     level.push_back({objects.size() == 1 ? stem + "_LOD" + std::to_string(i) : objects[k].name, &l});
                     tris += l.triangleCount();
                 }
                 if (!saveGlb(outPath, level, stem + "_LOD" + std::to_string(i), err)) return {false, err};
                 msg << " LOD" << i << " (" << tris << " tris)";
             }
             return {true, msg.str()};
         }});

    {
        // brush_create takes every recipe parameter, so AI tool calls and scripts can design brushes.
        std::vector<ParamSpec> params = {{"name", T::String, "Brush name.", true},
                                         {"description", T::String, "What the brush is for.", false}};
        for (const auto& rp : recipeParams()) {
            T type = rp.kind == RecipeParam::Kind::Float ? T::Number
                     : rp.kind == RecipeParam::Kind::Bool ? T::Bool
                                                          : T::String;
            params.push_back({rp.key, type, rp.help, false});
        }
        params.push_back({"connectedOnly", T::Bool,
                          "Only reach the surface joined to the point under the cursor (the other leg, the next finger "
                          "stay). Default false.",
                          false});
        params.push_back({"viewFalloff", T::Bool,
                          "Measure the falloff on screen: a cylinder through the model along the view. Default false.",
                          false});
        add({"brush_create",
             "Create (or update, by name) a custom sculpting brush from a recipe and make it the active brush. "
             "Omitted parameters use neutral defaults.",
             params, [](Document& doc, const Args& a) -> CommandResult {
                 Json j = Json::object();
                 for (const auto& [key, value] : a) {
                     if (auto* d = std::get_if<double>(&value)) j.set(key, *d);
                     else if (auto* b = std::get_if<bool>(&value)) j.set(key, *b);
                     else j.set(key, std::get<std::string>(value));
                 }
                 doc.activeBrush = doc.addBrush(BrushRecipe::fromJson(j));
                 return {true, "Brush '" + doc.brushes[doc.activeBrush].name + "' ready"};
             }});
    }

    add({"brush_list", "List the custom brushes in the library (brush_library also shows their files).", {},
         [](Document& doc, const Args&) -> CommandResult {
             std::string out;
             for (size_t i = 0; i < doc.brushes.size(); ++i)
                 out += (i ? "\n" : "") + std::to_string(i) + ": " + doc.brushes[i].name + " [" +
                        brushSourceName(doc.brushes[i].origin.source) + "] - " + doc.brushes[i].description;
             return {true, out};
         }});

    add({"brush_save", "Save a custom brush as a shareable .sfbrush file.",
         {{"path", T::String, "Output .sfbrush path.", true},
          {"name", T::String, "Brush name (defaults to the active brush).", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             std::string name = argString(a, "name");
             const BrushRecipe* r = nullptr;
             for (const auto& b : doc.brushes)
                 if (b.name == name) r = &b;
             if (!r && name.empty() && doc.activeBrush >= 0) r = &doc.brushes[doc.activeBrush];
             if (!r) return {false, "No such brush: " + name};
             std::string err;
             if (!r->save(argString(a, "path"), err)) return {false, err};
             return {true, "Saved brush '" + r->name + "'"};
         }});

    add({"brush_load", "Load a .sfbrush file for this session and make it active (brush_install keeps it in your library).",
         {{"path", T::String, ".sfbrush path.", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             BrushRecipe r;
             std::string err;
             if (!BrushRecipe::load(argString(a, "path"), r, err)) return {false, err};
             doc.activeBrush = doc.addBrush(std::move(r));
             return {true, "Loaded brush '" + doc.brushes[doc.activeBrush].name + "'"};
         }});
    registerBrushLibraryCommands(*this); // brush_install, brush_code, brush_delete, ... (BrushLibrary.cpp)
    registerBrushCommands(*this); // brush_set_alpha (BrushAlpha.cpp)
    registerVdmCommands(*this);   // vdm_set, vdm_capture, vdm_stamp, ... (Vdm.cpp)
    registerKelvinletCommands(*this); // kelvinlet_settings (Kelvinlet.cpp)
    registerCaptureCommands(*this); // capture_alpha, capture_vdm: alpha / VDM from the viewport (ViewCapture.cpp)
    registerRetopoCommands(*this); // qremesh, project_all (RetopoCommands.cpp)
    registerQRemeshGuideCommands(*this); // qremesh_guide, ... (QRemeshGuides.cpp)
    registerZSphereCommands(*this);      // zsphere_new, ... (ZSpheres.cpp)
    registerDeformCommands(*this);       // deform_polish, deform_relax, deform_smooth, deform_balloon (Deform.cpp)

    add({"stroke",
         "Apply a brush along a path on the active Part, like dragging the cursor in the viewport (one undo "
         "step). Each point is projected onto the surface along the view direction, so points drawn in the "
         "camera's plane give a screen-space stroke. Used for scripted detailing and brush preview renders.",
         {{"points", T::String,
           "World-space path: \"x,y,z x,y,z ...\" (or separated by '|'). One point stamps a single dab.", true},
          {"brush", T::String,
           "Custom brush (library name) or built-in brush (Standard, Clay, Crease, Inflate, ...). Default: the "
           "active brush.",
           false},
          {"view", T::String, "Direction the path is projected along, \"x,y,z\" (default 0,0,-1: front view).", false},
          {"radius", T::Number, "Brush radius in world units (default 4% of the mesh size).", false},
          {"strength", T::Number, "Strength 0-1 (default 0.5).", false},
          {"pressure", T::Number, "Constant pen pressure 0-1 (default 1).", false},
          {"invert", T::Bool, "Invert the brush (carve instead of build, like holding Alt).", false},
          {"curve", T::Bool, "Smooth curve through the points (default true); false = straight segments.", false},
          {"spacing", T::Number, "Dab spacing as a fraction of the radius (default: the brush's own).", false},
          {"symmetry", T::String, "Mirror axes, e.g. \"x\" or \"xz\" (default none).", false},
          {"seed", T::Number, "Random seed for brush jitter (default 1).", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             PathStroke st;
             std::string err;
             if (!parsePathPoints(argString(a, "points"), st.points, err)) return {false, err};
             const std::string view = argString(a, "view");
             if (!view.empty() && !parseVec3(view, st.view)) return {false, "Bad view '" + view + "' (expected x,y,z)"};
             st.radius = float(argNumber(a, "radius", 0.0));
             st.strength = float(argNumber(a, "strength", 0.5));
             st.pressure = float(argNumber(a, "pressure", 1.0));
             st.invert = argBool(a, "invert", false);
             st.curve = argBool(a, "curve", true);
             st.spacing = float(argNumber(a, "spacing", -1.0));
             st.seed = uint32_t(std::max(argNumber(a, "seed", 1.0), 0.0));
             if (!symmetryArg(a, doc.mesh, st.symmetry, err)) return {false, err};

             // Resolve the brush: a library recipe (exact name, then any case), then a built-in type.
             const std::string name = argString(a, "brush");
             auto lower = [](std::string s) {
                 std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                 return s;
             };
             const BrushRecipe* recipe = nullptr;
             BrushType type = BrushType::Standard;
             if (name.empty()) {
                 if (doc.activeBrush >= 0 && doc.activeBrush < int(doc.brushes.size())) recipe = &doc.brushes[doc.activeBrush];
             } else {
                 for (const auto& b : doc.brushes)
                     if (b.name == name) recipe = &b;
                 for (const auto& b : doc.brushes)
                     if (!recipe && lower(b.name) == lower(name)) recipe = &b;
                 if (!recipe) {
                     if (!brushFromName(name, type)) return {false, "No such brush: " + name}; // stored name or label
                 }
             }
             const int dabs = strokePath(doc, recipe, type, st, err);
             if (dabs < 0) return {false, err};
             if (dabs == 0) return {false, "The path never crosses the surface (check the points and view)"};
             return {true, "Stroked " + std::to_string(dabs) + " dabs with " + (recipe ? recipe->name : brushLabel(type))};
         }});

    add({"splat_info", "Report the loaded Gaussian splats.", {},
         [](Document& doc, const Args&) -> CommandResult {
             if (doc.splats.empty()) return {true, "No splats loaded"};
             const AABB b = doc.splats.bounds(0.98f);
             std::ostringstream s;
             s << doc.splats.size() << " splats, SH degree " << doc.splats.shDegree << ", main body ("
               << b.min.x << ", " << b.min.y << ", " << b.min.z << ") - (" << b.max.x << ", " << b.max.y << ", " << b.max.z << ")";
             return {true, s.str()};
         }});

    add({"splat_save",
         "Save the loaded Gaussian splats: .ply (standard 3DGS layout, e.g. for SuperSplat/Postshot), .splat (compact web viewers), .glb "
         "(glTF KHR_gaussian_splatting) or .usda/.usdz (OpenUSD ParticleField3DGaussianSplat).",
         {{"path", T::String, "Output path (UTF-8).", true}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.splats.empty()) return {false, "No splats loaded"};
             const std::string path = argString(a, "path");
             const std::string ext = lowerExtension(path);
             std::string err;
             bool ok = ext == ".ply"     ? saveSplatPly(path, doc.splats, err)
                       : ext == ".splat" ? saveDotSplat(path, doc.splats, err)
                       : ext == ".glb" ? saveSplatGlb(path, doc.splats, err)
                       : (ext == ".usda" || ext == ".usd" || ext == ".usdz")
                           ? saveUsd(path, nullptr, &doc.splats, doc.assetName(), err)
                           : (err = "Use a .ply, .splat, .glb, .usda or .usdz file name", false);
             if (!ok) return {false, err};
             return {true, "Saved " + std::to_string(doc.splats.size()) + " splats to " + path};
         }});

    add({"splat_clean", "Remove faint splats and outlying floaters from a capture.",
         {{"min_opacity", T::Number, "Remove splats fainter than this (0-1). Default 0.05.", false},
          {"keep", T::Number, "Fraction of the capture to keep per axis, trimming outliers (0.5-1). Default 0.98.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.splats.empty()) return {false, "No splats loaded"};
             const size_t removed = cleanSplats(doc.splats, float(std::clamp(argNumber(a, "min_opacity", 0.05), 0.0, 1.0)),
                                                float(std::clamp(argNumber(a, "keep", 0.98), 0.5, 1.0)));
             doc.splatsChanged();
             return {true, "Removed " + std::to_string(removed) + " splats, " + std::to_string(doc.splats.size()) + " left"};
         }});

    add({"splat_transform",
         "Turn and place the splats (captures are often upside down or off-centre). Rotations are in degrees "
         "about the capture's centre, applied X then Y then Z.",
         {{"rotate_x", T::Number, "Degrees about X (180 flips an upside-down capture).", false},
          {"rotate_y", T::Number, "Degrees about Y.", false},
          {"rotate_z", T::Number, "Degrees about Z.", false},
          {"fit", T::Bool, "Centre at the origin and scale to the size of a new sphere.", false},
          {"reset", T::Bool, "Undo all placement first.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.splats.empty()) return {false, "No splats loaded"};
             if (argBool(a, "reset", false)) doc.splats.transform = glm::mat4(1.0f);
             rotateSplats(doc.splats, glm::vec3(float(argNumber(a, "rotate_x", 0)), float(argNumber(a, "rotate_y", 0)),
                                                float(argNumber(a, "rotate_z", 0))));
             if (argBool(a, "fit", false)) fitSplats(doc.splats);
             doc.splatsChanged();
             return {true, "Splats placed"};
         }});

    // splat_to_mesh (Make sculptable) and face_guides: core/Sculptable.cpp.

    add({"mesh_to_splats",
         "Turn the visible Parts into Gaussian splats (flat oriented surfels with baked clay shading), replacing any "
         "loaded splats - for splat viewers, web scenes and engines that render 3DGS.",
         {{"count", T::Number, "Number of splats (1000-5000000). Default 250000.", false},
          {"shading", T::Bool, "Bake soft lighting and cavity darkening into the colour. Default true.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             Mesh merged; // no copy for a single SubTool
             const Mesh& visible = doc.subtoolCount() == 1 ? doc.mesh : (merged = mergedVisibleMesh(doc));
             if (visible.triangleCount() == 0) return {false, "The mesh is empty"};
             MeshToSplatsOptions o;
             o.count = size_t(std::clamp(argNumber(a, "count", 250000), 1000.0, 5000000.0));
             o.shading = argBool(a, "shading", true);
             doc.splats = meshToSplats(visible, o);
             doc.splats.name = doc.assetName();
             doc.splatsChanged();
             return {true, "Made " + std::to_string(doc.splats.size()) + " splats from the sculpt"};
         }});

    add({"splat_clear", "Remove the loaded Gaussian splats.", {},
         [](Document& doc, const Args&) -> CommandResult {
             doc.splats.clear();
             doc.splatsChanged();
             return {true, "Splats cleared"};
         }});

    add({"undo", "Undo the last action.", {},
         [](Document& doc, const Args&) -> CommandResult {
             if (!doc.undo.undo(doc.mesh, doc.bvh)) return {false, "Nothing to undo"};
             if (UndoRecord* r = doc.undo.lastRedo(); r && r->subtool) doc.applySubtoolStep(*r->subtool);
             doc.modified = true;
             return {true, "Undone"};
         }});

    add({"redo", "Redo the last undone action.", {},
         [](Document& doc, const Args&) -> CommandResult {
             if (!doc.undo.redo(doc.mesh, doc.bvh)) return {false, "Nothing to redo"};
             if (UndoRecord* r = doc.undo.lastUndo(); r && r->subtool) doc.applySubtoolStep(*r->subtool);
             doc.modified = true;
             return {true, "Redone"};
         }});

    add({"mesh_info", "Report vertex/triangle counts and bounding box of the mesh.", {},
         [](Document& doc, const Args&) -> CommandResult {
             AABB b = doc.mesh.bounds();
             std::ostringstream s;
             s << doc.mesh.vertexCount() << " vertices, " << doc.mesh.triangleCount() << " triangles";
             if (b.valid())
                 s << ", bounds (" << b.min.x << ", " << b.min.y << ", " << b.min.z << ") - (" << b.max.x << ", "
                   << b.max.y << ", " << b.max.z << ")";
             return {true, s.str()};
         }});

    registerMaskingCommands(*this);
    registerCutCommands(*this); // cut (core/Cut.h)
    registerPolygroupCommands(*this);
    registerSelectionSetCommands(*this); // selection_set_* (core/SelectionSets.h)
    registerSubToolCommands(*this);
    registerSubdivCommands(*this); // subdiv_* (core/MultiresCommands.cpp)
    registerMicroCommands(*this);  // micro_* (core/MicroCommands.cpp)
    registerDyntopoCommands(*this); // dyntopo (core/DyntopoCommands.cpp)
    registerSculptableCommands(*this); // splat_to_mesh, face_guides (core/Sculptable.h)
    registerTransformCommands(*this); // transform (core/MeshTransform.h)
    registerExtractMirrorCommands(*this); // extract, mirror_and_weld, mirror (core/ExtractMirror.h)
    registerPerformanceCommands(*this); // perf_info, set_multithreading (core/Benchmark.cpp)
    registerHardwareProfileCommands(*this); // performance_profile, calibrate, set_performance_override (core/HardwareProfile.cpp)
    registerTimelineCommands(*this); // timeline_info / seek / select / retune / save / load (core/Timeline.h)
    registerPaintCommands(*this); // core/Polypaint.cpp
    registerGozCommands(*this); // goz_import/export/send/receive, export_stl (io/GozCommands.cpp)
    registerBlenderCommands(*this); // blender_send/receive (io/BlenderCommands.cpp)
    registerMayaCommands(*this); // maya_send/receive (io/MayaCommands.cpp)
    registerBakeCommands(*this); // bake (core/Bake.cpp)
    registerFaceCommands(*this); // face_shapes_generate, face_weights, ... (core/FaceCommands.cpp)
    registerStarterMeshCommands(*this); // new_starter_mesh (core/StarterMeshes.h)
    registerPackCommands(*this);    // pack_install, pack_list, pack_remove, ... (core/Pack.h)
    registerMacroCommands(*this);   // macro_run, macro_list, macro_check (core/Macro.h)
    registerProjectCommands(*this); // keep last (core/Project.cpp)
}

void CommandRegistry::remove(const std::string& name) {
    commands_.erase(std::remove_if(commands_.begin(), commands_.end(), [&](const Command& c) { return c.name == name; }),
                    commands_.end());
}

void CommandRegistry::add(Command cmd) {
    // The commands that follow the brushes' symmetry take radial and local symmetry too (#63).
    static const char* const kSymmetric[] = {"stroke", "vdm_stamp", "mask_rect", "mask_lasso", "hide_rect",
                                             "hide_lasso", "insert_primitive", "insert_mesh", "cut"};
    for (const char* name : kSymmetric)
        if (cmd.name == name)
            for (ParamSpec& p : radialSymmetryParams()) cmd.params.push_back(std::move(p));
    commands_.push_back(std::move(cmd));
}

const Command* CommandRegistry::find(const std::string& name) const {
    for (const auto& c : commands_)
        if (c.name == name) return &c;
    return nullptr;
}

CommandResult CommandRegistry::execute(Document& doc, const std::string& name, const Args& args) const {
    const Command* cmd = find(name);
    if (!cmd) return {false, "Unknown command: " + name};
    for (const auto& p : cmd->params)
        if (p.required && !args.contains(p.name)) return {false, "Missing required argument: " + p.name};
    doc.notice.clear();
    // Extract's preview lasts until Accept or until anything else changes the scene (as in ZBrush); saving or
    // exporting never writes it.
    if (doc.extractPreview.id && name.rfind("extract", 0) != 0)
        doc.dropExtractPreview(!(name.rfind("save", 0) == 0 || name.rfind("export", 0) == 0));
    CommandResult r = cmd->run(doc, args);
    if (const std::string notice = doc.takeNotice(); !notice.empty() && r.ok) r.message += ". " + notice;
    return r;
}

std::string CommandRegistry::toolSchemaJson() const {
    std::ostringstream s;
    s << "[";
    for (size_t i = 0; i < commands_.size(); ++i) {
        const auto& c = commands_[i];
        s << (i ? "," : "") << "{\"name\":\"" << jsonEscape(c.name) << "\",\"description\":\""
          << jsonEscape(c.description) << "\",\"input_schema\":{\"type\":\"object\",\"properties\":{";
        std::vector<std::string> required;
        for (size_t j = 0; j < c.params.size(); ++j) {
            const auto& p = c.params[j];
            const char* type = p.type == ParamSpec::Type::Bool ? "boolean"
                               : p.type == ParamSpec::Type::Number ? "number"
                                                                   : "string";
            s << (j ? "," : "") << "\"" << jsonEscape(p.name) << "\":{\"type\":\"" << type
              << "\",\"description\":\"" << jsonEscape(p.description) << "\"}";
            if (p.required) required.push_back(p.name);
        }
        s << "},\"required\":[";
        for (size_t j = 0; j < required.size(); ++j) s << (j ? "," : "") << "\"" << jsonEscape(required[j]) << "\"";
        s << "]}}";
    }
    s << "]";
    return s.str();
}

} // namespace sf
