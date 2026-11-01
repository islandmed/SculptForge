// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// subdiv_* commands: ZBrush's Tool > Geometry subdivision levels for scripts, the console and the AI assistant.
#include "core/Commands.h"
#include "core/Multires.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace sf {

namespace {

using T = ParamSpec::Type;

CommandResult result(bool ok, std::string message) { return {ok, std::move(message)}; }

std::string lowerExtension(const std::string& path) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

// The export command that writes files with this extension.
std::string exportCommandFor(const std::string& ext) {
    if (ext == ".obj") return "save_obj";
    if (ext == ".glb" || ext == ".gltf") return "export_glb";
    if (ext == ".usd" || ext == ".usda" || ext == ".usdc" || ext == ".usdz") return "export_usd";
    if (ext == ".fbx") return "export_fbx";
    if (ext == ".ply") return "export_ply";
    if (ext == ".stl") return "export_stl";
    if (ext == ".goz") return "goz_export";
    return {};
}

} // namespace

void registerSubdivCommands(CommandRegistry& reg) {
    reg.add({"subdiv_divide",
             "Divide (ZBrush Tool > Geometry > Divide, Ctrl+D): add a subdivision level above the highest one and step "
             "to it. Quads stay quads (Catmull-Clark); triangle meshes use Loop subdivision. Lower levels are kept, so "
             "you can step down to sculpt big forms and back up to the detail. Each level has 4x the polygons.",
             {{"smooth", T::Bool, "Smt: smooth the new level (default true); false divides flat, keeping hard edges.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::string msg;
                 const bool ok = divideUndoable(doc, argBool(a, "smooth", true), msg);
                 return result(ok, msg);
             }});

    reg.add({"subdiv_level",
             "Step to a subdivision level (ZBrush's SDiv slider). Detail sculpted at higher levels is kept when you "
             "edit a lower one, and edits at a high level show up (softened) at the lower ones.",
             {{"level", T::Number, "Level to step to: 1 = the base cage, up to the number of levels.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (!a.contains("level")) return {false, "Give level= (1 = the base)"};
                 std::string msg;
                 const bool ok = setSubdivLevelUndoable(doc, int(argNumber(a, "level", 1)) - 1, msg);
                 return result(ok, msg);
             }});

    reg.add({"subdiv_lower", "Lower Res (Shift+D): step one subdivision level down.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (!doc.mesh.multires) return {false, "This Part has no subdivision levels (Divide adds them)"};
                 if (subdivCurrentLevel(doc.mesh) == 0) return {false, "Already at the lowest level (SDiv 1)"};
                 std::string msg;
                 const bool ok = setSubdivLevelUndoable(doc, subdivCurrentLevel(doc.mesh) - 1, msg);
                 return result(ok, msg);
             }});

    reg.add({"subdiv_higher", "Higher Res (D): step one subdivision level up.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (!doc.mesh.multires) return {false, "This Part has no subdivision levels (Divide adds them)"};
                 if (subdivCurrentLevel(doc.mesh) + 1 >= subdivLevelCount(doc.mesh))
                     return {false, "Already at the highest level (" + subdivLevelText(doc.mesh) + ")"};
                 std::string msg;
                 const bool ok = setSubdivLevelUndoable(doc, subdivCurrentLevel(doc.mesh) + 1, msg);
                 return result(ok, msg);
             }});

    reg.add({"subdiv_delete_lower",
             "Del Lower: delete the subdivision levels below the current one; the current level becomes the base cage.",
             {}, [](Document& doc, const Args&) -> CommandResult {
                 std::string msg;
                 const bool ok = deleteLowerUndoable(doc, msg);
                 return result(ok, msg);
             }});

    reg.add({"subdiv_delete_higher",
             "Del Higher: delete the subdivision levels above the current one (and the detail sculpted on them).", {},
             [](Document& doc, const Args&) -> CommandResult {
                 std::string msg;
                 const bool ok = deleteHigherUndoable(doc, msg);
                 return result(ok, msg);
             }});

    reg.add({"subdiv_reconstruct",
             "Reconstruct Subdiv: at the lowest level, find the mesh this one was subdivided from (a regular "
             "Catmull-Clark subdivision of quads, or Loop subdivision of triangles, e.g. an imported subdivided "
             "model) and add it as a new lower level. Run it again for more levels.",
             {}, [](Document& doc, const Args&) -> CommandResult {
                 std::string msg;
                 const bool ok = reconstructUndoable(doc, msg);
                 return result(ok, msg);
             }});

    reg.add({"subdiv_info", "List the active Part's subdivision levels and their polygon counts.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 const int n = subdivLevelCount(doc.mesh);
                 if (n == 0) return {true, "No subdivision levels (" + std::to_string(doc.mesh.triangleCount()) + " triangles)"};
                 std::string out = subdivLevelText(doc.mesh) + ":";
                 for (int i = 0; i < n; ++i) {
                     out += "\n  SDiv " + std::to_string(i + 1) + ": " + std::to_string(subdivLevelTriangles(doc.mesh, i)) + " triangles";
                     if (i == subdivCurrentLevel(doc.mesh)) out += " (current)";
                 }
                 return {true, out};
             }});

    reg.add({"subdiv_export",
             "Export the visible Parts at a subdivision level other than the current one, e.g. the base cage for "
             "another app (exports and GoZ otherwise write the current level). Parts without levels are written "
             "as they are. The format follows the extension (.obj, .glb, .usd, .fbx, .ply, .stl, .goz).",
             {{"path", T::String, "File to write.", true},
              {"level", T::Number, "Level to export: 1 = the base cage (default); higher than a Part has = its highest.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 if (path.empty()) return {false, "Give path="};
                 const std::string command = exportCommandFor(lowerExtension(path));
                 if (command.empty()) return {false, "Unknown export format: " + path};
                 const int level = std::max(1, int(argNumber(a, "level", 1))) - 1;
                 // A scratch scene with each visible SubTool at that level, written by the normal exporter.
                 Document scratch;
                 bool first = true;
                 for (int i = 0; i < doc.subtoolCount(); ++i) {
                     if (!doc.subtools[size_t(i)].visible && doc.subtoolCount() > 1) continue;
                     const Mesh& m = doc.subtoolMesh(i);
                     Mesh lm = subdivLevelMesh(m, std::min(level, std::max(subdivLevelCount(m) - 1, 0)));
                     if (first) scratch.setMesh(std::move(lm), doc.subtools[size_t(i)].name);
                     else scratch.addSubtool(std::move(lm), doc.subtools[size_t(i)].name, false);
                     first = false;
                 }
                 if (first) return {false, "Nothing visible to export"};
                 scratch.path = doc.path;
                 CommandResult r = CommandRegistry().execute(scratch, command, {{"path", path}});
                 if (r.ok) r.message += " (SDiv " + std::to_string(level + 1) + ")";
                 return r;
             }});
}

} // namespace sf
