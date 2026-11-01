// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Multiresolution subdivision levels, ZBrush style (Tool > Geometry: Divide, Lower Res / Higher Res, SDiv,
// Del Lower / Del Higher, Reconstruct Subdiv, Smt).
//
// A SubTool's mesh may carry a stack of levels (Mesh::multires). Level 0 is the base cage; each higher level
// is the one below subdivided (Catmull-Clark when the mesh has quads, so quads stay quads; Loop on pure
// triangle meshes; or flat when Smt was off) plus a detail displacement per vertex, stored in the local frame
// (tangent, bitangent, normal) of the subdivided surface:
//
//     P[k] = S(P[k-1]) + Frame(S(P[k-1])) * D[k]
//
// so reshaping a low level carries the high-level detail along. The Mesh itself always holds the current
// level, so every brush, command, exporter and the renderer work on it unchanged. The stack keeps a
// reference copy of every level; edits made to the current level are reconciled lazily (subdivCommit), when
// the level changes, the project is saved or a level is added or deleted:
//  - shape: the change is restricted down to every lower level (each coarse vertex moves by a blend of the
//    change at its own and its neighbouring fine vertices), the details of the levels in between are
//    re-fitted so the current level stays exactly as sculpted, and higher levels are re-evaluated from their
//    unchanged details on the way up;
//  - mask, polypaint and paint layers (per vertex), polygroups and hidden faces (per triangle) and UVs (per
//    corner) are copied down to the parent elements and interpolated / inherited up, only where they changed,
//    so detail painted at a high level survives an edit somewhere else at a low level;
//  - sculpt layers are their own displacement channels, subdivided with the same stencils; their per-level
//    detail is stored in the same local frame as the shape's, so a sculpt layer's fine offsets turn and bend
//    with a low level that is rotated (Gizmo 3D) or reshaped (project format 5; older files are converted);
//  - the painted texture lives in UV space and is shared by every level.
// Stepping between levels that weren't edited copies the stored level back, so up / down round trips are
// bit-for-bit.
//
// The stack is shared copy-on-write (std::shared_ptr): copying a Mesh for an undo snapshot or an export is
// cheap, and changing one level only copies that level. Stepping levels is one undo step (UndoRecord::
// subdiv), and so are Divide, Del Lower, Del Higher and Reconstruct: each keeps the previous stack, which
// shares every level the operation didn't touch.
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
class BVH;
struct Document;

// How level k is built from level k-1. Vertex, edge and face points of the coarse level map to fine vertices
// through vertexChild / edgeChild / faceChild, so a reconstructed level keeps the vertex order it was found in.
//
// A level made by Divide (or loaded / checked to be one) is `generated`: its fine vertices are the coarse
// vertices, then one per coarse edge, then one per polygon (Catmull-Clark), and its fine triangles and quad
// pairs are the ones generateFaces() makes, polygon by polygon. Such a level stores neither the child maps,
// nor its triangles (in SubdivLevel::data), nor their parents and corner codes: all of them follow from the
// coarse polygons, which saves about 30 bytes per triangle at the top level.
struct SubdivTopology {
    bool catmullClark = false; // quads (and triangles) -> quads; false = Loop, triangles -> 4 triangles
    bool smooth = true;        // false: flat subdivision (Smt off), the shape is not smoothed
    bool generated = false;
    uint32_t coarseVertices = 0, fineVertices = 0;
    size_t fineTriangles = 0;
    bool quads = false; // some coarse triangles were paired into quads
    // Coarse polygons (triangles and quads), corners in winding order (CSR).
    std::vector<uint32_t> polyOffsets, polyVerts;
    std::vector<uint32_t> polyEdges;  // per polygon side i (corner i -> i + 1): its edge
    std::vector<glm::uvec2> polyTris; // per polygon: its coarse triangles (y = kNone for a triangle)
    // Per quad: which edge e of its first triangle t is the diagonal, and the slot of the second triangle's far
    // corner (e | far << 2); the quad's corners are t[e+1], t[e+2], t[e], far.
    std::vector<uint8_t> polyDiag;
    // Coarse edges (a < b) and the polygons on each side; y = kNone on boundaries and non-manifold edges.
    std::vector<glm::uvec2> edges, edgeFaces;
    std::vector<uint32_t> vertEdgeOffsets, vertEdges; // coarse vertex -> incident edges (CSR)
    // Not generated only: the fine vertex of each coarse vertex / edge / polygon, and per fine triangle the
    // polygon and the coarse triangle it lies in, and per fine corner where it sits on that polygon (0-3 =
    // corner i, 4-7 = midpoint of side i, 8 = centre), for UVs.
    std::vector<uint32_t> vertexChild, edgeChild, faceChild;
    std::vector<uint32_t> fineParentPoly, fineParentTri;
    std::vector<uint8_t> fineCode;
    bool nonManifold = false; // some coarse edge has more than two polygons, or a polygon repeats a corner
    uint32_t maxValence = 0;  // most edges at one coarse vertex
    static constexpr uint32_t kNone = ~0u;

    size_t polygonCount() const { return polyOffsets.empty() ? 0 : polyOffsets.size() - 1; }
    // The coarse triangle corner (tri * 3 + slot) of polygon p's corner i, for UVs.
    uint32_t polyCorner(size_t p, uint32_t i) const {
        const glm::uvec2 t = polyTris[p];
        if (t.y == kNone) return t.x * 3 + i;
        const uint32_t e = polyDiag[p] & 3u;
        return i == 3 ? t.y * 3 + (polyDiag[p] >> 2) : t.x * 3 + (e + 1 + i) % 3;
    }
    // The quad pairing of the coarse level's triangles this level was built with (Mesh::kNoQuad = triangle;
    // empty without quads).
    std::vector<uint32_t> pairing() const;
    uint32_t vertexChildOf(uint32_t v) const { return generated ? v : vertexChild[v]; }
    uint32_t edgeChildOf(uint32_t e) const { return generated ? coarseVertices + e : edgeChild[e]; }
    uint32_t faceChildOf(uint32_t p) const { return generated ? coarseVertices + uint32_t(edges.size()) + p : faceChild[p]; }
    // The first fine triangle of polygon p (generated levels: 2 per corner for Catmull-Clark, 4 for Loop).
    size_t firstFineTriangle(size_t p) const { return catmullClark ? size_t(polyOffsets[p]) * 2 : p * 4; }
    size_t bytes() const;
};

// True when a mesh with these triangles can work out its adjacency from `tp` (Mesh::implicitAdjacency): a
// generated level of a manifold mesh without very high valences, whose triangles are the generated ones.
bool subdivImplicitAdjacency(const SubdivTopology& tp, const std::vector<glm::uvec3>& triangles);

// The fine triangles of a generated level, in order, and (Catmull-Clark) their quad pairs (each corner of a
// polygon becomes one quad: triangles 2i and 2i + 1).
void generateFaces(const SubdivTopology& tp, std::vector<glm::uvec3>& triangles, std::vector<uint32_t>* quadPartners);

// One level of the stack. `data` is the level as a mesh without topology (positions, every attribute, sculpt
// and paint layers; no texture; the triangles and quad pairs only when the level isn't generated, see
// SubdivTopology). For levels above the one last edited `stale` is set and data->positions / base / layer
// deltas are out of date until the level is evaluated again.
struct SubdivLevel {
    std::shared_ptr<Mesh> data;
    std::shared_ptr<const SubdivTopology> topo; // null for level 0
    // Local-frame displacement (levels >= 1). Null on a level that isn't stale: its positions are what counts
    // and its detail is fitted to them when it is needed (when a lower level changes), so the stack doesn't
    // keep both. Null on a stale level: no detail (the level is the smooth subdivision of the one below).
    std::shared_ptr<std::vector<glm::vec3>> detail;
    // Per sculpt layer: its offsets minus the subdivided offsets of the level below, in the local frame of the
    // subdivided shape (as `detail`), so they turn with the surface.
    std::vector<std::shared_ptr<std::vector<glm::vec3>>> layerDetail;
    bool stale = false;
    size_t triangleCount() const { return topo && topo->generated ? topo->fineTriangles : data->triangleCount(); }
};

// Working meshes (with topology and normals) and BVHs of the levels most recently left, so stepping back to
// one is a move instead of a rebuild. Keyed by the level's data, so any change to the level misses. Only the
// active SubTool keeps one: Document::selectSubtool frees the cache of the SubTool it leaves (subdivReleaseCache).
struct SubdivLevelCache;

struct MultiresStack {
    std::vector<SubdivLevel> levels; // at least 2 while a mesh has a stack
    int current = 0;
    std::shared_ptr<SubdivLevelCache> cache; // shared by copies of the stack
};

// --- Queries (0 levels = a plain mesh) ---
int subdivLevelCount(const Mesh& mesh);
int subdivCurrentLevel(const Mesh& mesh); // 0-based; 0 without levels
// "SDiv 3/5" (1-based, as ZBrush shows it), or "" without levels.
std::string subdivLevelText(const Mesh& mesh);
// Triangle count of level `level`.
size_t subdivLevelTriangles(const Mesh& mesh, int level);
// True when the stack still describes this mesh (vertex and triangle counts of the current level match);
// Mesh::sanitizeAttributes drops a stack that doesn't, so topology edits can't leave a stale one behind.
bool subdivMatches(const Mesh& mesh);
// Bytes the stack holds (all levels, details and topology), for the memory readout and tests.
size_t subdivBytes(const Mesh& mesh);
// Bytes the level cache holds (cached working meshes and their BVHs; approximate).
size_t subdivCacheBytes(const Mesh& mesh);
// Frees the level cache (the next step to another level rebuilds it): for SubTools that aren't active and
// meshes kept by undo steps.
void subdivReleaseCache(Mesh& mesh);

// --- Tools that rebuild the mesh (voxel remesh, decimate, QRemesh, Subdivide, Delete Hidden, merging and
// splitting SubTools) work on the current level, pending edits included, and leave a plain mesh: the levels
// are dropped and the current level becomes the new base. ---
// The status-line note for dropping `mesh`'s levels: "Subdivision levels removed: SDiv 3 of 5 kept as the new
// base". "" when the mesh has no levels. `name` (a SubTool name, optional) prefixes the level.
std::string subdivDroppedNote(const Mesh& mesh, const std::string& name = {});
// Adds the note for SubTool `index` (named) to doc.notice, for operations that aren't undoable (merging and
// splitting SubTools). Nothing when it has no levels.
void noteSubtoolLevelsDropped(Document& doc, int index);

// --- Operations on the mesh (no undo; see the commands / Document helpers below) ---
// Each returns false with `error` set when it can't run. All of them reconcile pending edits first.
//
// Operations that change the working mesh take an optional BVH: when given it is made valid for the new
// mesh (rebuilt, or taken from the level cache); otherwise the caller rebuilds it.
//
// Adds a level on top (stepping to the highest level first) and makes it current. `smooth` = Smt.
// `maxTriangles` 0 = subdivMaxTriangles().
bool subdivDivide(Mesh& mesh, bool smooth, std::string& error, BVH* bvh = nullptr, size_t maxTriangles = 0);
// The most triangles Divide makes on this computer: what fits in about 60% of its memory at kDivideBytesPerTriangle
// (mesh, levels, BVH, GPU buffers and the peak while dividing), between 50 million and 1 billion (32-bit indices).
constexpr size_t kDivideBytesPerTriangle = 110;
size_t subdivMaxTriangles();
// Makes `level` current, with topology and normals.
bool subdivSetLevel(Mesh& mesh, int level, std::string& error, BVH* bvh = nullptr);
// Deletes the levels below / above the current one (the current level becomes the base / the top).
bool subdivDeleteLower(Mesh& mesh, std::string& error);
bool subdivDeleteHigher(Mesh& mesh, std::string& error);
// At the lowest level: finds the mesh this one was subdivided from (a regular Catmull-Clark subdivision of
// an all-quad mesh, or a Loop subdivision of a triangle mesh), adds it as a new lowest level and steps down
// to it. The current level keeps its exact shape and vertex order.
bool subdivReconstruct(Mesh& mesh, std::string& error, BVH* bvh = nullptr);
// Folds edits of the current level into the stack (see the file comment). Cheap when nothing changed.
// Returns false (and drops the stack) if the mesh no longer has the current level's topology.
bool subdivCommit(Mesh& mesh);
// A full copy of level `level` (pending edits included), e.g. to export the base cage.
Mesh subdivLevelMesh(const Mesh& mesh, int level);

// --- Project files ---
// Rebuilds the topology of a saved level from what the file keeps: the coarse level's triangles and vertex
// count, the fine vertex count, the scheme, Smt, the quad pairing and the child maps. `fineTriangles` are the
// level's own triangles, or null for a generated level (which stores none). A level whose child maps and
// triangles are the generated ones becomes generated (attachSubdivStack then drops its triangles). Null with
// `error` set when they don't fit the meshes.
std::shared_ptr<const SubdivTopology> rebuildSubdivTopology(const std::vector<glm::uvec3>& coarseTriangles,
                                                            uint32_t coarseVertices, uint32_t fineVertices,
                                                            const std::vector<glm::uvec3>* fineTriangles,
                                                            std::vector<uint32_t> pairing, bool catmullClark,
                                                            bool smooth, std::vector<uint32_t> vertexChild,
                                                            std::vector<uint32_t> edgeChild,
                                                            std::vector<uint32_t> faceChild, std::string& error);
// Gives a loaded mesh (the current level) its stack; false when the stack doesn't fit it.
bool attachSubdivStack(Mesh& mesh, MultiresStack stack, std::string& error);
// Projects before format 5 stored sculpt-layer details in world space: converts a loaded stack's (with its
// topology) to the local frames SubdivLevel::layerDetail uses now. Evaluates stale levels on the way.
void convertWorldLayerDetail(MultiresStack& stack);
// The level data `level` keeps (for writing projects; may be stale, see SubdivLevel), and the level itself.
const Mesh& subdivLevelData(const Mesh& mesh, int level);
const SubdivLevel& subdivLevel(const Mesh& mesh, int level);
// The triangles of level `level` (stored or generated).
void subdivLevelFaces(const Mesh& mesh, int level, std::vector<glm::uvec3>& out);
// True when the mesh holds no edits its stack hasn't folded in yet (its current level's data is the mesh).
bool subdivInSync(const Mesh& mesh);

// --- Moving values between levels (Selection Sets, core/SelectionSets.h) ---
// Each carries `values`, laid out for level `from` (per vertex, per triangle or per triangle corner), to level `to`
// through the levels in between, as the stack passes attributes on: copied down to the parent elements,
// interpolated (vertices, UV corners) or inherited (triangles) up. `smooth` (vec3 per vertex): up through the
// levels' own subdivision stencils instead of linear interpolation (for offsets of the shape). False when the mesh
// has no such levels.
bool subdivMapVertices(const Mesh& mesh, int from, int to, std::vector<float>& values);
bool subdivMapVertices(const Mesh& mesh, int from, int to, std::vector<glm::vec3>& values, glm::vec3 fill, bool smooth);
bool subdivMapTriangles(const Mesh& mesh, int from, int to, std::vector<uint32_t>& values);
bool subdivMapTriangles(const Mesh& mesh, int from, int to, std::vector<uint8_t>& values);
bool subdivMapCorners(const Mesh& mesh, int from, int to, std::vector<glm::vec2>& values);

// --- Undoable document operations (one undo step each). Return a message for the status bar / console. ---
bool divideUndoable(Document& doc, bool smooth, std::string& message);
bool setSubdivLevelUndoable(Document& doc, int level, std::string& message);
bool deleteLowerUndoable(Document& doc, std::string& message);
bool deleteHigherUndoable(Document& doc, std::string& message);
bool reconstructUndoable(Document& doc, std::string& message);

// The undo step of a level operation (UndoRecord::subdiv): the level stack before it (after folding in
// pending edits), so applying swaps stacks and loads the other stack's current level. `plain` is the mesh
// without levels, when one side of the step has none. Cheap: the stacks share every level not changed.
struct SubdivUndo {
    std::shared_ptr<MultiresStack> stack;
    std::shared_ptr<Mesh> plain;
    size_t bytes() const;
};
// Swaps `r` with the mesh's stack (so it then holds the inverse) and rebuilds the BVH.
void applySubdivUndo(SubdivUndo& r, Mesh& mesh, BVH& bvh);

// subdiv_divide, subdiv_level, subdiv_lower, subdiv_higher, subdiv_delete_lower, subdiv_delete_higher,
// subdiv_reconstruct, subdiv_info.
void registerSubdivCommands(CommandRegistry& registry);

} // namespace sf
