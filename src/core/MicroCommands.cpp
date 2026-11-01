// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// micro_* commands (core/Micro.h): Tool > Geometry > Micro for scripts, the console, the CLI, MCP and the AI
// assistant.
#include "core/Commands.h"
#include "core/Micro.h"
#include "core/Multires.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace sf {

namespace {

using T = ParamSpec::Type;

std::string mb(size_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f MB", double(bytes) / 1e6);
    return buf;
}

std::string millions(double n) {
    char buf[32];
    if (n >= 1e9) std::snprintf(buf, sizeof buf, "%.2fB", n / 1e9);
    else std::snprintf(buf, sizeof buf, "%.1fM", n / 1e6);
    return buf;
}

std::string summary(const Mesh& m) {
    if (!m.micro) return "no micro level";
    const MicroMesh& mm = *m.micro;
    char buf[256];
    std::snprintf(buf, sizeof buf, "Micro level %d: %s micro triangles on %s base triangles (%ux%u grid each), %s",
                  mm.level, millions(double(mm.microTriangles())).c_str(), millions(double(mm.baseTriangles)).c_str(), mm.n,
                  mm.n, mb(mm.bytes()).c_str());
    return buf;
}

// An undo step holding every grid tile (whole-surface edits such as micro_noise).
std::unique_ptr<MicroUndo> captureAll(const Mesh& m) {
    const MicroMesh& mm = *m.micro;
    auto u = std::make_unique<MicroUndo>();
    u->step = mm.step;
    u->samples = mm.samples;
    u->tris.resize(mm.baseTriangles);
    for (uint32_t t = 0; t < mm.baseTriangles; ++t) u->tris[t] = t;
    u->disp = mm.disp;
    return u;
}

} // namespace

bool microDivideUndoable(Document& doc, int level, std::string& message) {
    std::shared_ptr<MicroMesh> before = doc.mesh.micro;
    if (!microDivide(doc.mesh, level, message)) return false;
    UndoRecord r;
    r.label = "Micro divide";
    r.micro = std::make_unique<MicroUndo>();
    r.micro->swapWhole = true;
    r.micro->whole = std::move(before);
    doc.undo.push(std::move(r));
    message = "Divided to micro: " + summary(doc.mesh);
    return true;
}

bool microDeleteUndoable(Document& doc, std::string& message) {
    if (!doc.mesh.micro) {
        message = "This Part has no micro level";
        return false;
    }
    UndoRecord r;
    r.label = "Delete micro";
    r.micro = std::make_unique<MicroUndo>();
    r.micro->swapWhole = true;
    r.micro->whole = doc.mesh.micro;
    microDelete(doc.mesh);
    doc.undo.push(std::move(r));
    message = "Micro level deleted (Undo brings it back)";
    return true;
}

void registerMicroCommands(CommandRegistry& reg) {
    reg.add({"micro_divide",
             "Divide to micro level (Tool > Geometry > Micro): store the surface above the highest subdivision level as "
             "a grid of scalar displacements per triangle (2 bytes per point) instead of a mesh, so hundreds of millions "
             "of triangles stay sculptable: 300M micro triangles take about 0.4 GB. Each micro level has 4x the "
             "triangles of the one below. The standard, clay, smooth, inflate, flatten, trim, layer, pinch / crease "
             "and mask brushes work on it; it bakes down to a mesh or to displacement maps.",
             {{"level", T::Number, "Micro levels above the base: 1-7 (each 4x; 4 = 256 micro triangles per triangle).", false},
              {"triangles", T::Number, "Instead of level: about this many micro triangles, in millions (e.g. 300).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 int level = int(argNumber(a, "level", 0));
                 if (a.contains("triangles"))
                     level = microLevelFor(doc.mesh, size_t(std::max(1.0, argNumber(a, "triangles", 100)) * 1e6));
                 if (level <= 0) level = microLevelFor(doc.mesh, size_t(100e6));
                 std::string msg;
                 const bool ok = microDivideUndoable(doc, level, msg);
                 return {ok, msg};
             }});

    reg.add({"micro_delete", "Delete the micro level (the highest subdivision level stays). Undoable.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 std::string msg;
                 const bool ok = microDeleteUndoable(doc, msg);
                 return {ok, msg};
             }});

    reg.add({"micro_info", "The micro level of the active Part: level, micro triangles, memory, displacement range.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (!doc.mesh.micro) return {true, "No micro level (micro_divide adds one above the highest subdivision level)"};
                 const MicroMesh& mm = *doc.mesh.micro;
                 char buf[160];
                 std::snprintf(buf, sizeof buf, "; displacement step %.3g, range +/-%.3g (used %.3g)%s", double(mm.step),
                               double(mm.rangeWorld()), double(mm.rangeMax) * double(mm.step), mm.hasMask() ? "; masked" : "");
                 return {true, summary(doc.mesh) + buf};
             }});

    reg.add({"micro_bake",
             "Bake down: turn the micro level into a regular mesh (for export or tools that need real geometry). By "
             "default a new Part next to this one; replace=true replaces this Part's mesh (undoable). Its UVs, "
             "face groups, hidden faces and the micro mask carry over.",
             {{"level", T::Number, "Micro level to bake (default: the full micro level; lower = 1/4 the triangles each).", false},
              {"replace", T::Bool, "Replace this Part's mesh instead of adding a Part (default false).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (!doc.mesh.micro) return {false, "This Part has no micro level"};
                 const int level = int(argNumber(a, "level", doc.mesh.micro->level));
                 Mesh baked;
                 std::string err;
                 if (!microBakeMesh(doc.mesh, level, baked, err)) return {false, err};
                 const std::string tris = millions(double(baked.triangleCount()));
                 if (argBool(a, "replace", false)) {
                     doc.replaceMeshUndoable(std::move(baked), "Bake micro");
                     return {true, "Baked the micro level into this Part: " + tris + " triangles"};
                 }
                 const std::string name = doc.uniqueSubtoolName(doc.activeSubtoolInfo().name + " baked");
                 doc.addSubtool(std::move(baked), name, false, doc.activeSubtool + 1);
                 return {true, "Baked the micro level into Part \"" + name + "\": " + tris + " triangles"};
             }});

    reg.add({"micro_export_displacement",
             "Export a displacement map of the micro level over the base UVs, for game and film pipelines: scalar "
             "(height along the interpolated normal) or vector (object-space offset), measured from the flat base "
             "triangles. .exr = 32-bit float in world units; .png = 16-bit, mid-grey = 0 (the scale is in the message).",
             {{"path", T::String, "Output file (.exr or .png).", true},
              {"size", T::Number, "Map size in pixels (default 4096).", false},
              {"type", T::String, "scalar (default) or vector.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string path = argString(a, "path");
                 if (path.empty()) return {false, "Give path= (.exr or .png)"};
                 std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
                 std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                 const std::string format = ext == ".png" ? "png" : "exr";
                 const bool vector = argString(a, "type", "scalar") == "vector";
                 float scale = 0.0f;
                 std::string err;
                 if (!microExportDisplacement(doc.mesh, path, int(argNumber(a, "size", 4096)), vector, format, scale, err))
                     return {false, err};
                 char buf[128];
                 std::snprintf(buf, sizeof buf, " (largest offset %.4g world units%s)", double(scale),
                               format == "png" ? ", at white / black" : "");
                 return {true, "Wrote " + std::string(vector ? "vector" : "scalar") + " displacement " + path + buf};
             }});

    reg.add({"micro_noise",
             "Surface noise on the micro level: procedural pore / wrinkle detail over the whole surface (respects the "
             "mask). Undoable.",
             {{"amplitude", T::Number, "Height as a fraction of the model's size (default 0.0015).", false},
              {"scale", T::Number, "Noise features across the model (default 120).", false},
              {"seed", T::Number, "Random seed (default 1).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (!doc.mesh.micro) return {false, "This Part has no micro level"};
                 const float diag = glm::length(doc.mesh.bounds().extent());
                 const float amp = float(argNumber(a, "amplitude", 0.0015)) * diag;
                 const float freq = float(argNumber(a, "scale", 120)) / std::max(diag, 1e-6f);
                 UndoRecord r;
                 r.label = "Micro noise";
                 r.micro = captureAll(doc.mesh);
                 microNoise(doc.mesh, amp, freq, uint32_t(argNumber(a, "seed", 1)));
                 doc.undo.push(std::move(r));
                 return {true, "Added surface noise to " + millions(double(microTriangleCount(doc.mesh))) + " micro triangles"};
             }});
}

} // namespace sf
