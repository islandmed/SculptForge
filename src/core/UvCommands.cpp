// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// uv_* commands: the UV Editor's operations (app/PanelUvEditor.cpp) for scripts, the CLI, Python and the AI
// assistant. Each is one undo step holding the old UVs and seams (UvUndo) and, when a painted texture is moved
// onto the new layout (keep_texture), the texture too (core/TextureTransfer.h). Operations that work on "the
// selection" use the UV Editor's (Document::uvSelection, set with uv_select) and the whole Part when nothing is
// selected.
#include "core/Commands.h"
#include "core/Texture.h"
#include "core/TextureTransfer.h"
#include "core/Undo.h"
#include "core/Uv.h"
#include "core/UvEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

using T = ParamSpec::Type;

const char* kNoUvs = "The Part has no UVs: unwrap it first (UV Editor > Unwrap, or uv_unwrap)";

int resolutionArg(const Document& doc, const Args& a) {
    if (a.contains("resolution")) return std::clamp(int(argNumber(a, "resolution", kDefaultTextureSize)), 64, 16384);
    return doc.mesh.texture ? std::max(doc.mesh.texture->width, doc.mesh.texture->height) : kDefaultTextureSize;
}

std::string fmt(const char* f, double x) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, x);
    return buf;
}

std::string plural(size_t n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); }

// The islands to work on: the selection's, or every island when nothing is selected.
std::vector<uint8_t> targetIslands(const Document& doc, const UvTopology& topo) {
    return uvSelectedIslands(doc.mesh, topo, doc.uvSelection, true);
}

// The corners to move: the selection's, or every corner.
std::vector<uint32_t> targetCorners(const Document& doc, const UvTopology& topo) {
    if (doc.uvSelection.valid(doc.mesh) && !doc.uvSelection.empty()) return uvSelectedCorners(doc.mesh, topo, doc.uvSelection);
    std::vector<uint32_t> all(doc.mesh.uvs.size());
    for (uint32_t c = 0; c < all.size(); ++c) all[c] = c;
    return all;
}

UvPackOptions packOptions(const Document& doc, const Args& a) {
    UvPackOptions o;
    o.resolution = resolutionArg(doc, a);
    o.marginPx = float(std::clamp(argNumber(a, "margin", 8.0), 0.0, 256.0));
    o.rotate = argBool(a, "rotate", true);
    o.tiles = std::clamp(int(argNumber(a, "tiles", 1)), 1, 100);
    o.equalDensity = argBool(a, "equal_density", true);
    o.density = float(std::max(argNumber(a, "density", 0.0), 0.0));
    o.firstTile = std::clamp(int(argNumber(a, "first_tile", 1001)), 1001, 1100);
    return o;
}

std::string packNote(const UvPackResult& p) {
    std::string s = plural(p.islands, "island") + " packed";
    if (p.tilesUsed > 1) s += " into " + std::to_string(p.tilesUsed) + " UDIM tiles";
    s += ", " + fmt("%.0f", double(p.coverage) * 100.0) + "% of the space used, " + fmt("%.1f", double(p.density)) +
         " texels per unit";
    return s;
}

} // namespace

UvEditStep::UvEditStep(Document& doc, std::string label, bool keepTexture) : doc_(doc), texture_(keepTexture && hasTexture(doc.mesh)) {
    if (texture_) record_ = captureTexture(doc.mesh, label);
    else record_.label = std::move(label);
    record_.uv = UvUndo::capture(doc.mesh);
}

std::string UvEditStep::commit() {
    Mesh& m = doc_.mesh;
    if (!record_.uv) return {};
    if (m.uvs == record_.uv->uvs && m.uvFlags == record_.uv->flags) {
        if (texture_) record_.texture->swapWith(m); // (put back the untouched original)
        record_.uv.reset();
        return {};
    }
    std::string note;
    if (texture_ && m.uvs.size() == record_.uv->uvs.size() && m.uvs != record_.uv->uvs) {
        std::vector<glm::vec2> fresh = std::move(m.uvs);
        m.uvs = record_.uv->uvs;
        note = setUvsKeepingTexture(m, std::move(fresh), true);
    }
    m.facesDirty = true;
    record_.uv->commit(m);
    if (texture_) finishTextureUndo(record_, m);
    doc_.undo.push(std::move(record_));
    record_.uv.reset();
    return note;
}

std::string uvInfoText(const Mesh& m, int resolution) {
    if (!m.hasUvs()) {
        const size_t seams = uvSeamCount(m);
        return "No UVs" + (seams ? "; " + plural(seams, "seam edge") + " marked" : std::string());
    }
    const UvTopology topo = buildUvTopology(m);
    size_t pins = 0;
    for (uint8_t f : m.uvFlags) pins += (f & kUvPin) != 0;
    std::vector<int> tiles;
    for (uint32_t i = 0; i < topo.islandCount; ++i) {
        const uint32_t t = topo.islandTriangles[topo.islandOffsets[i]];
        const glm::vec2 c = (m.uvs[t * 3] + m.uvs[t * 3 + 1] + m.uvs[t * 3 + 2]) / 3.0f;
        tiles.push_back(udimTile(c));
    }
    std::sort(tiles.begin(), tiles.end());
    tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
    const UvStretchStats st = uvStretchStats(m);
    std::string s = plural(topo.islandCount, "island") + ", " + plural(uvSeamCount(m), "seam edge") + ", " +
                    plural(pins, "pinned corner") + "; " + fmt("%.1f", double(uvMeshDensity(m, resolution))) +
                    " texels per unit at " + std::to_string(resolution) + "; tiles";
    for (int t : tiles) s += " " + std::to_string(t);
    s += "; stretch " + fmt("%.3f", double(st.mean)) + " mean, " + fmt("%.2f", double(st.flipped) * 100.0) + "% flipped";
    return s;
}

void registerUvCommands(CommandRegistry& reg) {
    reg.add({"uv_unwrap",
             "Unwrap the active Part's UVs (UV Editor > Unwrap). Methods: isometric (least stretch: angles and lengths, "
             "along the marked seams; the default), conformal (LSCM: angles, along the seams), polygroups (cut along "
             "the face group borders and the seams), planar (each piece projected flat), unfold (flatten the current "
             "islands again in place, keeping pinned corners), auto (finds its own seams, xatlas). Pieces that are "
             "closed or looped get extra cuts. The result is packed into the 0-1 tile, or across UDIM tiles. With a UV "
             "selection only the selected faces are unwrapped (fitted where they were). A painted texture moves onto "
             "the new UVs (keep_texture). One undo step.",
             {{"method", T::String, "isometric (default), conformal, polygroups, planar, unfold or auto", false},
              {"selected", T::Bool, "Only the UV Editor's selected faces (default true when something is selected)", false},
              {"iterations", T::Number, "Least-stretch refinement steps (default 10; 0 = conformal)", false},
              {"resolution", T::Number, "Texture size the margin is measured in (default: the texture's, or 2048)", false},
              {"margin", T::Number, "Texels between packed islands (default 8)", false},
              {"tiles", T::Number, "UDIM tiles to pack across (default 1)", false},
              {"rotate", T::Bool, "Turn islands to fit better when packing (default true)", false},
              {"density", T::Number, "Pack at exactly this texel density (texels per unit; uses as many tiles as needed)", false},
              {"pack", T::Bool, "Pack the result (default true; false fits each island where its old UVs were)", false},
              {"keep_texture", T::Bool, "Move a painted texture onto the new UVs (default true)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (m.triangleCount() == 0) return {false, "There is no mesh to unwrap"};
                 UvUnwrapOptions o;
                 if (a.contains("method") && !parseUvUnwrapMethod(argString(a, "method"), o.method))
                     return {false, "method must be isometric, conformal, polygroups, planar, unfold or auto"};
                 o.resolution = resolutionArg(doc, a);
                 o.iterations = std::clamp(int(argNumber(a, "iterations", 10)), 0, 100);
                 o.packing = packOptions(doc, a);
                 o.pack = argBool(a, "pack", o.method != UvUnwrapMethod::Unfold);
                 const bool haveSel = doc.uvSelection.valid(m) && !doc.uvSelection.empty();
                 std::vector<uint8_t> faces;
                 if (argBool(a, "selected", haveSel) && haveSel && m.hasUvs()) {
                     const UvTopology topo = buildUvTopology(m);
                     faces.assign(m.triangleCount(), 0);
                     const std::vector<uint32_t> corners = uvSelectedCorners(m, topo, doc.uvSelection);
                     if (doc.uvSelection.mode == UvSelectMode::Face || doc.uvSelection.mode == UvSelectMode::Island) {
                         faces = doc.uvSelection.faces;
                     } else {
                         for (uint32_t c : corners) faces[c / 3] = 1;
                     }
                     o.faces = &faces;
                 }
                 UvUnwrapResult r;
                 std::string err;
                 if (!uvUnwrap(m, o, r, err)) return {false, err};
                 UvEditStep edit(doc, std::string("Unwrap (") + uvUnwrapMethodLabel(o.method) + ")", argBool(a, "keep_texture", true));
                 m.uvs = std::move(r.uvs);
                 const std::string note = edit.commit();
                 std::string msg = std::string("Unwrapped (") + uvUnwrapMethodLabel(o.method) + "): " + plural(r.islands, "island");
                 if (r.extraCuts) msg += ", " + plural(r.extraCuts, "edge") + " cut automatically to flatten closed pieces";
                 if (r.packing.tilesUsed > 1) msg += ", " + std::to_string(r.packing.tilesUsed) + " UDIM tiles";
                 const UvStretchStats st = uvStretchStats(m);
                 msg += "; stretch " + fmt("%.3f", double(st.mean));
                 return {true, msg + note};
             }});

    reg.add({"uv_pack",
             "Pack the active Part's UV islands (the selected ones, or all) into the 0-1 tile or across UDIM tiles: "
             "each island turned to its smallest box, scaled to one texel density (equal_density) and laid out with a "
             "margin, as large as fits. With density, keeps that texel density and uses as many tiles as it needs. "
             "One undo step; a painted texture moves with the islands (keep_texture).",
             {{"margin", T::Number, "Texels between islands (default 8)", false},
              {"resolution", T::Number, "Texture size the margin and density are measured in (default: the texture's, or 2048)", false},
              {"rotate", T::Bool, "Turn islands to fit better (default true)", false},
              {"tiles", T::Number, "UDIM tiles to fill, 1001 onwards (default 1)", false},
              {"first_tile", T::Number, "The UDIM tile to start at (default 1001)", false},
              {"equal_density", T::Bool, "Scale islands to one texel density first (default true; false keeps their sizes relative)", false},
              {"density", T::Number, "Keep exactly this texel density (texels per unit) and spill into more tiles", false},
              {"keep_texture", T::Bool, "Move a painted texture with the islands (default true)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!m.hasUvs()) return {false, kNoUvs};
                 const UvTopology topo = buildUvTopology(m);
                 const std::vector<uint8_t> islands = targetIslands(doc, topo);
                 UvEditStep edit(doc, "Pack UVs", argBool(a, "keep_texture", true));
                 UvPackResult r;
                 std::string err;
                 if (!uvPack(m, topo, islands, packOptions(doc, a), r, err)) return {false, err};
                 const std::string note = edit.commit();
                 return {true, packNote(r) + note};
             }});

    reg.add({"uv_seam",
             "Mark or clear UV seams on the active Part (seams are where Unwrap cuts). op: mark / clear (the UV "
             "Editor's selected edges, or the border of its selected faces), path (mark the shortest edge path from "
             "vertex `from` to vertex `to`; clear=true clears it), polygroups (every face group border), islands (every "
             "current UV island border), angle (edges sharper than `angle` degrees), clear_all. One undo step.",
             {{"op", T::String, "mark, clear, path, polygroups, islands, angle or clear_all", true},
              {"from", T::Number, "path: the vertex to start at", false},
              {"to", T::Number, "path: the vertex to end at", false},
              {"clear", T::Bool, "path: clear the seams along it instead of marking them", false},
              {"angle", T::Number, "angle: the dihedral angle in degrees above which edges become seams (default 60)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (m.triangleCount() == 0) return {false, "There is no mesh"};
                 const std::string op = argString(a, "op");
                 const size_t before = uvSeamCount(m);
                 UvEditStep edit(doc, "UV Seams", false);
                 const std::vector<uint32_t> twin = cornerTwins(m);
                 if (op == "mark" || op == "clear") {
                     const UvSelection& sel = doc.uvSelection;
                     if (!sel.valid(m) || sel.empty()) return {false, "Select edges or faces in the UV Editor first"};
                     std::vector<uint32_t> corners;
                     if (sel.mode == UvSelectMode::Edge) {
                         for (uint32_t c = 0; c < sel.corners.size(); ++c)
                             if (sel.corners[c]) corners.push_back(c);
                     } else if (sel.mode == UvSelectMode::Vertex) {
                         for (uint32_t c = 0; c < sel.corners.size(); ++c)
                             if (sel.corners[c] && sel.corners[cornerNext(c)]) corners.push_back(c);
                     } else {
                         corners = uvFaceSetBorder(m, twin, sel.faces);
                     }
                     uvSetSeams(m, twin, corners, op == "mark");
                 } else if (op == "path") {
                     const double from = argNumber(a, "from", -1), to = argNumber(a, "to", -1);
                     if (from < 0 || to < 0 || from >= double(m.vertexCount()) || to >= double(m.vertexCount()))
                         return {false, "Give the path's `from` and `to` vertices (0-" + std::to_string(m.vertexCount() - 1) + ")"};
                     const std::vector<uint32_t> path = uvEdgePath(m, uint32_t(from), uint32_t(to));
                     if (path.empty()) return {false, "Those vertices aren't connected"};
                     uvSetSeams(m, twin, path, !argBool(a, "clear", false));
                 } else if (op == "polygroups" || op == "face_groups") {
                     uvSeamsFromPolygroups(m, twin);
                 } else if (op == "islands") {
                     if (!m.hasUvs()) return {false, kNoUvs};
                     uvSeamsFromIslands(m, buildUvTopology(m, twin));
                 } else if (op == "angle") {
                     uvSeamsFromAngle(m, twin, float(argNumber(a, "angle", 60.0)));
                 } else if (op == "clear_all") {
                     uvClearSeams(m);
                 } else {
                     return {false, "op must be mark, clear, path, polygroups, islands, angle or clear_all"};
                 }
                 edit.commit();
                 const size_t after = uvSeamCount(m);
                 return {true, plural(after, "seam edge") + " (" + (after >= before ? "+" : "-") +
                                   std::to_string(after >= before ? after - before : before - after) + ")"};
             }});

    reg.add({"uv_select",
             "Set the UV Editor's selection on the active Part (what the other uv_* commands work on). op: all, none, "
             "invert, island (the island of triangle `face`), face (triangle `face`), polygroup (every face of face group "
             "`group`), rect (faces touching u0 v0 u1 v1), seams (the marked seam edges), mode (just switch to `mode`). "
             "`mode` (vertex, edge, face, island) converts the selection; `add` adds to it instead of replacing it.",
             {{"op", T::String, "all, none, invert, island, face, polygroup, rect, seams or mode", true},
              {"mode", T::String, "vertex, edge, face or island", false},
              {"face", T::Number, "island / face: a triangle index", false},
              {"group", T::Number, "polygroup: the face group id", false},
              {"u0", T::Number, "rect: lower u", false},
              {"v0", T::Number, "rect: lower v", false},
              {"u1", T::Number, "rect: upper u", false},
              {"v1", T::Number, "rect: upper v", false},
              {"add", T::Bool, "Add to the selection (default false)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!m.hasUvs()) return {false, kNoUvs};
                 UvSelection& sel = doc.uvSelection;
                 const UvTopology topo = buildUvTopology(m);
                 if (!sel.valid(m)) {
                     const UvSelectMode mode = sel.mode;
                     sel.clear();
                     sel.mode = mode;
                 }
                 if (a.contains("mode")) {
                     UvSelectMode mode;
                     if (!parseUvSelectMode(argString(a, "mode"), mode)) return {false, "mode must be vertex, edge, face or island"};
                     uvConvertSelection(m, topo, sel, mode);
                 }
                 const size_t nt = m.triangleCount();
                 const bool faceMode = sel.mode == UvSelectMode::Face || sel.mode == UvSelectMode::Island;
                 if (faceMode && sel.faces.size() != nt) sel.faces.assign(nt, 0);
                 if (!faceMode && sel.corners.size() != nt * 3) sel.corners.assign(nt * 3, 0);
                 const std::string op = argString(a, "op");
                 const bool add = argBool(a, "add", false);
                 // Faces picked by the op, converted to the mode.
                 auto pickFaces = [&](const std::vector<uint8_t>& faces) {
                     UvSelection pick;
                     pick.mode = UvSelectMode::Face;
                     pick.faces = faces;
                     uvConvertSelection(m, topo, pick, sel.mode);
                     std::vector<uint8_t>& dst = faceMode ? sel.faces : sel.corners;
                     const std::vector<uint8_t>& src = faceMode ? pick.faces : pick.corners;
                     if (!add) std::fill(dst.begin(), dst.end(), uint8_t(0));
                     for (size_t i = 0; i < src.size(); ++i) dst[i] = dst[i] || src[i];
                 };
                 std::vector<uint8_t>& flags = faceMode ? sel.faces : sel.corners;
                 if (op == "all") {
                     std::fill(flags.begin(), flags.end(), uint8_t(1));
                 } else if (op == "none") {
                     std::fill(flags.begin(), flags.end(), uint8_t(0));
                 } else if (op == "invert") {
                     for (uint8_t& f : flags) f = !f;
                 } else if (op == "island" || op == "face") {
                     const double f = argNumber(a, "face", -1);
                     if (f < 0 || f >= double(nt)) return {false, "Give `face`, a triangle index (0-" + std::to_string(nt - 1) + ")"};
                     std::vector<uint8_t> faces(nt, 0);
                     if (op == "island") {
                         const uint32_t isl = topo.island[uint32_t(f)];
                         for (uint32_t k = topo.islandOffsets[isl]; k < topo.islandOffsets[isl + 1]; ++k) faces[topo.islandTriangles[k]] = 1;
                     } else {
                         faces[uint32_t(f)] = 1;
                     }
                     pickFaces(faces);
                 } else if (op == "polygroup") {
                     const uint32_t g = uint32_t(std::max(argNumber(a, "group", 0), 0.0));
                     std::vector<uint8_t> faces(nt, 0);
                     for (uint32_t t = 0; t < nt; ++t) faces[t] = m.polygroupAt(t) == g;
                     pickFaces(faces);
                 } else if (op == "rect") {
                     const glm::vec2 p0(float(argNumber(a, "u0", 0)), float(argNumber(a, "v0", 0)));
                     const glm::vec2 p1(float(argNumber(a, "u1", 1)), float(argNumber(a, "v1", 1)));
                     std::vector<uint32_t> list;
                     uvFacesInRect(m, glm::min(p0, p1), glm::max(p0, p1), true, list);
                     std::vector<uint8_t> faces(nt, 0);
                     for (uint32_t t : list) faces[t] = 1;
                     pickFaces(faces);
                 } else if (op == "seams") {
                     uvConvertSelection(m, topo, sel, UvSelectMode::Edge);
                     sel.corners.assign(nt * 3, 0);
                     if (m.uvFlags.size() == nt * 3)
                         for (uint32_t c = 0; c < nt * 3; ++c) sel.corners[c] = (m.uvFlags[c] & kUvSeam) != 0;
                 } else if (op != "mode") {
                     return {false, "op must be all, none, invert, island, face, polygroup, rect, seams or mode"};
                 }
                 if (sel.mode == UvSelectMode::Island) uvConvertSelection(m, topo, sel, UvSelectMode::Island);
                 const size_t n = sel.count();
                 const char* what = sel.mode == UvSelectMode::Vertex ? "corner" : sel.mode == UvSelectMode::Edge ? "edge" : "face";
                 const std::vector<uint8_t> touched = uvSelectedIslands(m, topo, sel, false);
                 const size_t islands = size_t(std::count(touched.begin(), touched.end(), uint8_t(1)));
                 return {true, std::string("UV selection (") + uvSelectModeId(sel.mode) + "s): " + plural(n, what) + " in " +
                                   plural(islands, "island")};
             }});

    reg.add({"uv_transform",
             "Move, rotate and scale the UV Editor's selection (or every UV of the active Part when nothing is "
             "selected) about its centre. One undo step; a painted texture follows the surface (keep_texture).",
             {{"move_u", T::Number, "Offset in u", false},
              {"move_v", T::Number, "Offset in v", false},
              {"rotate", T::Number, "Degrees, counter-clockwise", false},
              {"scale", T::Number, "Uniform scale (default 1)", false},
              {"scale_u", T::Number, "Scale in u (times scale)", false},
              {"scale_v", T::Number, "Scale in v (times scale)", false},
              {"keep_texture", T::Bool, "Move a painted texture with the UVs, so the model looks the same (default true)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!m.hasUvs()) return {false, kNoUvs};
                 const UvTopology topo = buildUvTopology(m);
                 const std::vector<uint32_t> corners = targetCorners(doc, topo);
                 const float s = float(argNumber(a, "scale", 1.0));
                 const float su = s * float(argNumber(a, "scale_u", 1.0)), sv = s * float(argNumber(a, "scale_v", 1.0));
                 const float ang = glm::radians(float(argNumber(a, "rotate", 0.0)));
                 const glm::mat2 rot(std::cos(ang), std::sin(ang), -std::sin(ang), std::cos(ang));
                 const glm::mat2 mat = rot * glm::mat2(su, 0.0f, 0.0f, sv);
                 UvEditStep edit(doc, "Transform UVs", argBool(a, "keep_texture", true));
                 uvTransform(m.uvs, corners, mat, uvCenter(m.uvs, corners),
                             glm::vec2(float(argNumber(a, "move_u", 0.0)), float(argNumber(a, "move_v", 0.0))));
                 const std::string note = edit.commit();
                 return {true, "Transformed " + plural(corners.size(), "UV corner") + note};
             }});

    reg.add({"uv_edit",
             "UV Editor operations on its selection (the whole Part when nothing is selected, where that makes sense). "
             "op: pin / unpin (selected corners keep their UVs on Unfold), clear_pins, weld (selected vertices, or the "
             "edges between them, become one UV each, removing the seam), split (the selected edges, or the border of "
             "the selected faces, become seams), straighten (selected vertices line up horizontally or vertically), "
             "flip_u / flip_v (mirror the selected islands). One undo step.",
             {{"op", T::String, "pin, unpin, clear_pins, weld, split, straighten, flip_u or flip_v", true},
              {"keep_texture", T::Bool, "Move a painted texture with the UVs (default true)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!m.hasUvs()) return {false, kNoUvs};
                 const std::string op = argString(a, "op");
                 const UvTopology topo = buildUvTopology(m);
                 const bool haveSel = doc.uvSelection.valid(m) && !doc.uvSelection.empty();
                 const std::vector<uint32_t> corners = targetCorners(doc, topo);
                 UvEditStep edit(doc, "UV " + op, argBool(a, "keep_texture", true));
                 std::string msg;
                 if (op == "pin" || op == "unpin") {
                     if (!haveSel) return {false, "Select the UVs to " + op + " first"};
                     uvSetPins(m, corners, op == "pin");
                     msg = (op == "pin" ? "Pinned " : "Unpinned ") + plural(corners.size(), "corner");
                 } else if (op == "clear_pins") {
                     std::vector<uint32_t> all(m.uvs.size());
                     for (uint32_t c = 0; c < all.size(); ++c) all[c] = c;
                     uvSetPins(m, all, false);
                     msg = "Cleared every pin";
                 } else if (op == "weld") {
                     if (!haveSel) return {false, "Select the vertices or edges to weld first"};
                     msg = "Welded " + plural(uvWeld(m, topo, corners), "vertex");
                 } else if (op == "split") {
                     if (!haveSel) return {false, "Select the edges or faces to split off first"};
                     msg = "Split along " + plural(uvSplit(m, topo, doc.uvSelection), "edge");
                 } else if (op == "straighten") {
                     if (!haveSel) return {false, "Select the vertices to straighten first (an edge loop)"};
                     msg = "Straightened " + plural(uvStraighten(m, topo, corners), "UV vertex");
                 } else if (op == "flip_u" || op == "flip_v") {
                     uvFlipIslands(m, topo, targetIslands(doc, topo), op == "flip_u");
                     msg = op == "flip_u" ? "Flipped horizontally" : "Flipped vertically";
                 } else {
                     return {false, "op must be pin, unpin, clear_pins, weld, split, straighten, flip_u or flip_v"};
                 }
                 return {true, msg + edit.commit()};
             }});

    reg.add({"uv_density",
             "Texel density of the active Part's UV islands (the selected ones, or all), in texels per unit at the "
             "texture's resolution. op: set (scale them to `value`), match (scale them all to their mean, or to the "
             "first selected island's with match=first), info. One undo step.",
             {{"op", T::String, "set, match or info", true},
              {"value", T::Number, "set: texels per unit", false},
              {"match", T::String, "match: mean (default) or first", false},
              {"resolution", T::Number, "Texture size (default: the texture's, or 2048)", false},
              {"keep_texture", T::Bool, "Move a painted texture with the UVs (default true)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!m.hasUvs()) return {false, kNoUvs};
                 const int res = resolutionArg(doc, a);
                 const UvTopology topo = buildUvTopology(m);
                 const std::vector<uint8_t> islands = targetIslands(doc, topo);
                 const std::vector<float> d = uvIslandDensity(m, topo, res);
                 double sum = 0.0, area = 0.0;
                 float lo = 1e30f, hi = 0.0f;
                 int first = -1;
                 for (uint32_t i = 0; i < topo.islandCount; ++i) {
                     if (!islands[i] || d[i] <= 0.0f) continue;
                     if (first < 0) first = int(i);
                     const double w = double(topo.islandOffsets[i + 1] - topo.islandOffsets[i]);
                     sum += double(d[i]) * w, area += w;
                     lo = std::min(lo, d[i]), hi = std::max(hi, d[i]);
                 }
                 if (area <= 0.0) return {false, "The islands have no area"};
                 const float mean = float(sum / area);
                 const std::string op = argString(a, "op");
                 if (op == "info")
                     return {true, "Texel density at " + std::to_string(res) + ": " + fmt("%.1f", double(mean)) + " mean, " +
                                       fmt("%.1f", double(lo)) + " - " + fmt("%.1f", double(hi)) + " texels per unit"};
                 float target = 0.0f;
                 if (op == "set") target = float(argNumber(a, "value", 0.0));
                 else if (op == "match") target = argString(a, "match", "mean") == "first" ? d[size_t(first)] : mean;
                 else return {false, "op must be set, match or info"};
                 if (!(target > 0.0f)) return {false, "Give the texel density: value (texels per unit)"};
                 UvEditStep edit(doc, "Texel Density", argBool(a, "keep_texture", true));
                 const size_t n = uvSetDensity(m, topo, islands, target, res);
                 const std::string note = edit.commit();
                 return {true, plural(n, "island") + " at " + fmt("%.1f", double(target)) + " texels per unit" + note};
             }});

    reg.add({"uv_trim",
             "Trim sheets: lay the selected UV islands (or all) into one horizontal strip of the texture. Each island "
             "is turned lengthwise and scaled to the strip's height, and they line up along u from 0 (fit_width "
             "squeezes the row to 0-1; otherwise it keeps its proportions and may run past 1, as trim textures repeat "
             "in u). The texture stays put (keep_texture is off by default: the islands pick up the strip's look).",
             {{"strip", T::Number, "Which strip, 1 = the top one (default 1)", false},
              {"strips", T::String, "The sheet: a count (4, the default), heights from the top (0.5,0.25,0.25), or v ranges (0-0.5;0.5-1)", false},
              {"fit_width", T::Bool, "Fit the row to u 0-1 exactly (default false)", false},
              {"margin", T::Number, "Texels kept free above and below inside the strip (default 4)", false},
              {"resolution", T::Number, "Texture size the margin is measured in", false},
              {"keep_texture", T::Bool, "Move a painted texture with the islands (default false)", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 Mesh& m = doc.mesh;
                 if (!m.hasUvs()) return {false, kNoUvs};
                 std::vector<TrimStrip> strips;
                 std::string err;
                 const std::string spec = a.contains("strips") ? argString(a, "strips", "") : "4";
                 if (!parseTrimStrips(spec.empty() ? std::to_string(int(argNumber(a, "strips", 4))) : spec, strips, err))
                     return {false, err};
                 const int k = int(argNumber(a, "strip", 1));
                 if (k < 1 || k > int(strips.size())) return {false, "strip must be 1-" + std::to_string(strips.size())};
                 const UvTopology topo = buildUvTopology(m);
                 const std::vector<uint8_t> islands = targetIslands(doc, topo);
                 const float margin = float(std::max(argNumber(a, "margin", 4.0), 0.0)) / float(resolutionArg(doc, a));
                 UvEditStep edit(doc, "Trim Sheet", argBool(a, "keep_texture", false));
                 const size_t n = uvTrimIslands(m, topo, islands, strips[size_t(k - 1)], argBool(a, "fit_width", false), margin);
                 if (!n) return {false, "Nothing to lay into the strip (it may be too thin for the margin)"};
                 const std::string note = edit.commit();
                 const TrimStrip& s = strips[size_t(k - 1)];
                 return {true, plural(n, "island") + " in " + s.name + " (v " + fmt("%.3f", double(s.v0)) + " - " +
                                   fmt("%.3f", double(s.v1)) + ")" + note};
             }});

    reg.add({"uv_info",
             "Describe the active Part's UVs: islands, seams, pins, texel density, the UDIM tiles used, stretch.",
             {{"resolution", T::Number, "Texture size for the texel density (default: the texture's, or 2048)", false}},
             [](Document& doc, const Args& a) -> CommandResult { return {true, uvInfoText(doc.mesh, resolutionArg(doc, a))}; }});
}

} // namespace sf
