// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/BridgeCommands.h"

#include "io/BridgeProtocol.h"

#include "core/Parts.h"
#include "io/BridgeIO.h"
#include "io/StlIO.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace sf {

namespace {

std::string fileStem(const std::string& path) {
    auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

std::string meshSummary(const Mesh& m) {
    std::string s = std::to_string(m.triangleCount()) + " triangles";
    if (m.hasFaceGroups()) s += ", face groups";
    if (m.hasUvs()) s += ", UVs";
    if (m.hasMask()) s += ", mask";
    if (m.hasColors()) s += ", vertex paint";
    return s;
}

// The name the other application sees: the Part the mesh came from, else the document name.
std::string sendName(const Document& doc, const Args& a) {
    std::string name = argString(a, "name");
    if (name.empty()) name = doc.bridgeName;
    if (name.empty()) name = doc.assetName();
    return name;
}

} // namespace

std::string& partBridgeName(Document& doc, int i) {
    return i == doc.activePart ? doc.bridgeName : doc.parts[size_t(i)].bridgeName;
}

std::vector<MeshObjectRef> bridgeObjects(Document& doc, std::vector<int>& indices) {
    std::vector<MeshObjectRef> out;
    for (int i = 0; i < doc.partCount(); ++i) {
        if (!doc.parts[size_t(i)].visible || doc.partMesh(i).triangleCount() == 0) continue;
        const std::string& bridge = partBridgeName(doc, i);
        out.push_back({bridge.empty() ? doc.parts[size_t(i)].name : bridge, &doc.partMesh(i)});
        indices.push_back(i);
    }
    return out;
}

// Each object updates the Part with its Bridge name (undoable), or the single Part of a fresh scene, or
// becomes a new Part. The active Part stays selected.
void receiveObjects(Document& doc, std::vector<MeshObject>& objects, const std::string& undoLabel, bool renameFresh) {
    const int original = doc.activePart; // new Parts go to the end, so this index stays valid
    auto findBridge = [&](const std::string& name) {
        for (int i = 0; i < doc.partCount(); ++i)
            if (!name.empty() && partBridgeName(doc, i) == name) return i;
        return -1;
    };
    for (size_t k = 0; k < objects.size(); ++k) {
        MeshObject& obj = objects[k];
        int target = findBridge(obj.name);
        // The first object sent into a fresh scene (one Part, not  or Blender) replaces it.
        const bool fresh = target < 0 && k == 0 && doc.partCount() == 1 && doc.bridgeName.empty();
        if (fresh) target = 0;
        if (target < 0) {
            const int i = doc.addPart(std::move(obj.mesh), obj.name, false);
            partBridgeName(doc, i) = obj.name;
            continue;
        }
        doc.selectPart(target);
        if (doc.mesh.triangleCount()) doc.replaceMeshUndoable(std::move(obj.mesh), undoLabel);
        else doc.replaceActiveMesh(std::move(obj.mesh));
        doc.bridgeName = obj.name;
        if (fresh && (renameFresh || objects.size() > 1)) doc.activePartInfo().name = doc.uniquePartName(obj.name);
    }
    doc.selectPart(original);
    doc.path.clear();
    doc.modified = true;
}

void registerBridgeCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;

    reg.add({"bridge_import",
             "Open a Bridge file, replacing the scene. Face Groups, UVs, mask and vertex paint are kept; "
             "quads are split into triangle pairs (exported back as quads).",
             {{"path", T::String, "Input Bridge file path (UTF-8).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 Mesh m;
                 BridgeInfo info;
                 std::string err;
                 if (!loadBridge(path, m, err, &info)) return {false, err};
                 const std::string name = info.name.empty() ? fileStem(path) : info.name;
                 doc.setMesh(std::move(m), name);
                 doc.path.clear();
                 doc.bridgeName = name;
                 std::string msg = "Opened Bridge file " + doc.bridgeName + " (" + meshSummary(doc.mesh) + ")";
                 if (!err.empty()) msg += "; " + err;
                 return {true, msg};
             }});

    reg.add({"bridge_export",
             "Export the mesh as a Bridge file with face groups, UVs, mask and vertex paint. With several "
             "visible Parts, each is written next to `path` as <name>_<Part> with the same extension.",
             {{"path", T::String, "Output Bridge file path (UTF-8).", true},
              {"name", T::String, "Part name. Default: the name it came through the Bridge with, else the document name.", false},
              {"quads", T::Bool, "Rebuild quads from triangle pairs made by the Bridge file import. Default true.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 BridgeWriteOptions o;
                 o.quads = argBool(a, "quads", true);
                 std::string err;
                 std::vector<int> indices;
                 const auto objects = bridgeObjects(doc, indices);
                 if (doc.partCount() == 1 || objects.size() == 1) {
                     const int i = objects.size() == 1 ? indices[0] : doc.activePart;
                     const std::string& bridge = partBridgeName(doc, i);
                     const std::string name = argString(a, "name", bridge.empty() ? fileStem(path) : bridge);
                     if (!saveBridge(path, doc.partMesh(i), name, err, o)) return {false, err};
                     return {true, "Exported " + path};
                 }
                 if (objects.empty()) return {false, "Nothing visible to export"};
                 // One file per Part beside `path`: <stem>_<object>.<ext>.
                 const std::filesystem::path base(std::u8string(path.begin(), path.end()));
                 std::vector<std::string> used;
                 for (const auto& obj : objects) {
                     std::string name = bridgeObjectName(obj.name);
                     const std::string wanted = name;
                     for (int k = 2; std::find(used.begin(), used.end(), name) != used.end(); ++k) name = wanted + "_" + std::to_string(k);
                     used.push_back(name);
                     const std::string file = fileStem(path) + "_" + name + bridgefile::kExt;
                     const auto out = (base.parent_path() / std::u8string(file.begin(), file.end())).u8string();
                     if (!saveBridge(std::string(out.begin(), out.end()), *obj.mesh, name, err, o)) return {false, err};
                 }
                 return {true, "Exported " + std::to_string(objects.size()) + " Parts as " + fileStem(path) + "_*" + bridgefile::kExt};
             }});

    reg.add({"bridge_send",
             "Send the visible Parts through the Bridge (Windows/macOS): writes each to the shared bridge folder, "
             "lists them in its object list and notifies or starts the other application. Sending meshes received "
             "through the Bridge updates those Parts.",
             {{"name", T::String, "Part name (single Part only). Default: the name it came through the Bridge with, else the document name.", false},
              {"folder", T::String, "Bridge folder. Default: the shared bridge folder of this system.", false},
              {"launch", T::Bool, "Notify or start the other application. Default true.", false},
              {"app", T::String, "The other application's executable, started if its notifier is missing (optional).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 BridgeSendOptions o;
                 o.root = argString(a, "folder");
                 o.launch = argBool(a, "launch", true);
                 o.appPath = argString(a, "app");
                 std::string msg;
                 if (doc.partCount() == 1) {
                     o.name = sendName(doc, a);
                     const bool ok = bridgeSend(doc.mesh, o, msg);
                     if (ok) doc.bridgeName = bridgeObjectName(o.name);
                     return {ok, msg};
                 }
                 std::vector<int> indices;
                 auto objects = bridgeObjects(doc, indices);
                 if (objects.size() == 1 && a.contains("name")) objects[0].name = argString(a, "name");
                 std::vector<std::string> sent;
                 const bool ok = bridgeSend(objects, o, msg, &sent);
                 // Remember the names, so next send updates these Parts.
                 if (ok)
                     for (size_t k = 0; k < sent.size() && k < indices.size(); ++k) partBridgeName(doc, indices[k]) = sent[k];
                 return {ok, msg};
             }});

    reg.add({"bridge_receive",
             "Receive what was last sent through the Bridge (the objects in its object list). Each object updates the "
             "Part it was sent from (an undoable step on that Part), or becomes a new Part; a single object "
             "replaces a scene of one Part that didn't come through the Bridge.",
             {{"folder", T::String, "Bridge folder. Default: the shared bridge folder of this system.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<MeshObject> objects;
                 std::string msg;
                 if (!bridgeReceive(argString(a, "folder"), objects, msg)) return {false, msg};
                 receiveObjects(doc, objects, "Receive via Bridge", false);
                 return {true, msg};
             }});

    reg.add({"export_stl",
             "Export the visible Parts as one binary STL mesh for 3D printing, in millimetres with Z up (what "
             "slicers expect): 1 unit = 1 m in SculptForge becomes 1000 mm.",
             {{"path", T::String, "Output .stl path (UTF-8).", true},
              {"height_mm", T::Number, "Print height in millimetres: scales the model to this height (overrides scale).", false},
              {"scale", T::Number, "File units per SculptForge unit. Default 1000 (metres to millimetres).", false},
              {"z_up", T::Bool, "Turn Y up into Z up, so the model stands upright on the print bed. Default true.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 std::string err;
                 Mesh merged; // no copy for a single Part
                 const Mesh& mesh = doc.partCount() == 1 ? doc.mesh : (merged = mergedVisibleMesh(doc));
                 StlExportOptions o;
                 o.heightMm = float(argNumber(a, "height_mm", 0.0));
                 o.scale = float(argNumber(a, "scale", 1000.0));
                 o.zUp = argBool(a, "z_up", true);
                 if (!saveStl(path, mesh, err, o)) return {false, err};
                 // In millimetres, the printed size as a slicer shows it: width x depth x height.
                 std::string size;
                 if (o.zUp && (o.heightMm > 0.0f || o.scale == 1000.0f)) {
                     const glm::vec3 e = mesh.bounds().extent();
                     const float s = o.heightMm > 0.0f ? o.heightMm / e.y : o.scale;
                     char text[96];
                     std::snprintf(text, sizeof(text), ", %.1f x %.1f x %.1f mm", e.x * s, e.z * s, e.y * s);
                     size = text;
                 }
                 return {true, "Exported " + path + " (" + std::to_string(mesh.triangleCount()) + " triangles" + size + ")"};
             }});
}

} // namespace sf
