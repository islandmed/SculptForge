// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dynamic topology (ZBrush's Sculptris Pro, Blender's Dyntopo): with it on, every brush dab first remeshes the
// surface under the brush towards one edge length, so a sphere can be pulled into horns or fingers without
// stretching its triangles, and a blockout needs no remeshing.
//
// Each dab, inside the brush radius only: edges longer than 4/3 of the target are split at their midpoint
// (longest first), edges shorter than 3/5 of it are collapsed (shortest first, where that keeps the surface
// manifold, turns no triangle over, makes no long edge and leaves no vertex with fewer than three neighbours),
// edges are flipped where that evens out the valences, and the vertices that changed are relaxed along the
// surface. Boundaries, polygroup borders and the symmetry plane are kept: their vertices only slide along them.
// Masked and hidden parts are left alone. With symmetry on, every operation is done on its mirror images at the
// same time, so mirrored dabs make mirrored topology.
//
// Quads under the brush become triangles; UVs are dropped (a remeshed surface has none) and come back on undo.
// Meshes with subdivision levels can't be remeshed (as in ZBrush, delete the levels first).
//
// The mesh stays valid after every dab (no dead entries): elements a dab removed are filled from the end of
// the arrays. The BVH and the renderer follow incrementally (BVH::insertTriangle..., Mesh::changedTriangles), and
// the stroke's undo step keeps only the vertex and triangle slots it touched (TopologyUndo).
#pragma once

#include "core/BVH.h"
#include "core/Mesh.h"
#include "core/Symmetry.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sf {

struct DyntopoSettings {
    bool enabled = false;
    // false: the detail follows the brush size (Sculptris Pro: zoom in or shrink the brush for finer triangles);
    // true: one detail size for the whole mesh, relative to its size.
    bool constantDetail = false;
    float detail = 0.5f; // 0 (coarse) .. 1 (fine)
    // How much Smooth (Shift) remeshes while dynamic topology is on (from the discussions): 0 = smooth only, the
    // topology stays; 1 (the default, as before) = it remeshes toward twice the edge length, so smoothing also
    // simplifies. In between it remeshes toward 1 + smoothRemesh times the edge length.
    float smoothRemesh = 1.0f;
};

// The edge-length scale Smooth remeshes with (0 = it doesn't remesh): 1 + smoothRemesh, as above.
inline float dyntopoSmoothScale(const DyntopoSettings& s) {
    return s.smoothRemesh <= 0.005f ? 0.0f : 1.0f + std::min(s.smoothRemesh, 1.0f);
}

// The edge length dabs of world radius `radius` remesh towards, on a mesh whose bounding box diagonal is `diagonal`.
float dyntopoEdgeLength(const DyntopoSettings& s, float radius, float diagonal);

// Why `mesh` can't be remeshed by dynamic topology ("" when it can).
std::string dyntopoBlocker(const Mesh& mesh);
// "Dynamic topology on: Detail 50 (follows the brush size)".
std::string dyntopoSummary(const DyntopoSettings& s);

class CommandRegistry;
// dyntopo: on / off, Detail, constant detail (core/DyntopoCommands.cpp).
void registerDyntopoCommands(CommandRegistry& registry);

// Per-vertex and per-triangle values of a mesh at some slots (arrays the mesh doesn't have stay empty).
struct TopologyVertexValues {
    std::vector<glm::vec3> positions, colors, base, paintBase;
    std::vector<float> mask;
    std::vector<std::vector<glm::vec3>> deltas, paintColors; // per sculpt layer / paint layer
    std::vector<std::vector<float>> paintAlphas;
    size_t bytes() const;
};
struct TopologyTriangleValues {
    std::vector<glm::uvec3> triangles;
    std::vector<uint32_t> polygroups, quadPartners;
    std::vector<uint8_t> hidden;
    size_t bytes() const;
};

// The undo of a dynamic-topology stroke: the vertex and triangle slots it touched (in either state), the
// element counts of the other state and its values at those slots (those below its counts). Applying swaps
// them with the mesh's, so the record then holds the inverse; the caller rebuilds topology, normals and BVH.
struct TopologyUndo {
    uint32_t vertexCount = 0, triangleCount = 0;
    std::vector<uint32_t> vertexSlots, triangleSlots;
    TopologyVertexValues vertices;
    TopologyTriangleValues triangles;
    std::vector<glm::vec2> uvs; // the whole UVs of the other state, when it had them (dynamic topology drops them)
    // Slots whose positions / colours must be worked out again from layers after applying (only when brush undo
    // entries from before the stroke's first remesh were merged in; normally every value is exact).
    std::vector<uint32_t> reevaluate;
    void swapWith(Mesh& mesh);
    size_t bytes() const;
};

struct DyntopoStats {
    size_t splits = 0, collapses = 0, flips = 0;
    size_t unpaired = 0; // operations whose mirror image wasn't found (an asymmetric mesh, or one edited without symmetry)
};

// Remeshes the surface under brush dabs (see the top of this file). One per Sculptor; a stroke's first remesh
// starts recording, finish() ends the stroke.
class DynamicTopology {
public:
    // Remeshes within `radius` of `center` and of its symmetric copies (`mirrors` as Sculptor::copies makes them:
    // mirror images and radial turns, the identity first) towards edges of `edgeLength`. Returns false when
    // nothing changed.
    bool remesh(Mesh& mesh, BVH& bvh, const glm::vec3& center, float radius, float edgeLength,
                std::span<const SymCopy> mirrors);
    // The vertices the last remesh renumbered (old slot, new slot), for per-vertex stroke state.
    std::span<const std::pair<uint32_t, uint32_t>> movedVertices() const { return moved_; }
    // The vertex slots the last remesh added to the stroke's undo record (their stroke-start state is there).
    std::span<const uint32_t> recordedVertices() const { return recordedNow_; }
    // The stroke changed the mesh's topology (or recorded it).
    bool active() const { return mesh_ != nullptr; }
    // Keeps vertex slot v's stroke-start state in the stroke's undo record (the brush calls it before moving or
    // painting a vertex while the stroke remeshes; no-op when it is recorded already or nothing is remeshed).
    void recordStrokeStart(uint32_t v) {
        if (mesh_ && v < mesh_->vertexCount()) recordVertex(v);
    }
    // Vertex slot v is in the stroke's undo record (its stroke-start state is kept there).
    bool recordedVertex(uint32_t v) const { return v < vertRec_.size() * 64 && ((vertRec_[v >> 6] >> (v & 63)) & 1); }
    // Ends the stroke: packs the mesh's adjacency, rebuilds the BVH if the edits wore it down, and returns the
    // undo record (null when the stroke didn't remesh).
    std::unique_ptr<TopologyUndo> finish(Mesh& mesh, BVH& bvh);
    // finish() for the mesh the stroke remeshed (null when none).
    Mesh* strokeMesh() const { return mesh_; }
    BVH* strokeBvh() const { return bvh_; }

    DyntopoStats stats;   // since the last reset
    double lastMs = 0.0;  // the last remesh's time

private:
    struct Edge {
        float len2;
        uint32_t a, b;
    };

    // Recording (the stroke's undo), and what changed this remesh (for rings, normals, BVH and renderer).
    void recordVertex(uint32_t v);
    void recordTriangle(uint32_t t);
    void markVertex(uint32_t v);
    void markTriangle(uint32_t t);
    // Elements.
    uint32_t newVertex();
    void interpolateVertex(uint32_t v, uint32_t a, uint32_t b, float t, const glm::vec3& position);
    void killVertex(uint32_t v);
    uint32_t newTriangle(const glm::uvec3& tri, uint32_t like);
    void setTriangle(uint32_t t, const glm::uvec3& tri);
    void killTriangle(uint32_t t);
    void unpairQuad(uint32_t t);
    void vtInsert(uint32_t v, uint32_t t);
    void vtErase(uint32_t v, uint32_t t);
    void vtReplace(uint32_t v, uint32_t from, uint32_t to);
    void moveVertex(uint32_t from, uint32_t to);
    void moveTriangle(uint32_t from, uint32_t to);
    void compact();
    // Queries (from the triangle lists, which are always current).
    int edgeTriangles(uint32_t a, uint32_t b, uint32_t* out) const; // up to 4
    void ring(uint32_t v, std::vector<uint32_t>& out) const;
    bool locked(uint32_t v) const;
    bool planeVertex(uint32_t v) const;
    bool featureEdge(uint32_t a, uint32_t b) const;
    int featureEdges(uint32_t v, uint32_t* other) const;
    bool boundaryVertex(uint32_t v) const;
    float len2(uint32_t a, uint32_t b) const;
    bool inside(uint32_t a, uint32_t b, bool mirrored = false) const;
    // An edge to remesh: under the brush, or under a mirror image of it with no image on the brush's side (an
    // asymmetric mesh: that side is remeshed on its own).
    bool candidate(uint32_t a, uint32_t b) const;
    glm::vec3 faceNormalAt(uint32_t v) const;
    // Symmetry: the mirror images of vertex v (kNone where unknown).
    uint32_t mirrorOf(size_t mirror, uint32_t v) const;
    void setMirror(size_t mirror, uint32_t v, uint32_t w);
    void matchMirrors(const std::vector<uint32_t>& verts);
    void orbit(uint32_t a, uint32_t b, std::vector<std::pair<uint32_t, uint32_t>>& out) const;
    bool orbitSharesTriangle(const std::vector<std::pair<uint32_t, uint32_t>>& edges) const;
    // Operations.
    void splitPass();
    void splitOrbit(uint32_t a, uint32_t b, int depth);
    uint32_t split(uint32_t a, uint32_t b);
    void collapsePass();
    bool collapseTarget(uint32_t a, uint32_t b, uint32_t& keep, uint32_t& drop, glm::vec3& p) const;
    bool canCollapse(uint32_t keep, uint32_t drop, const glm::vec3& p) const;
    void collapse(uint32_t keep, uint32_t drop, const glm::vec3& p);
    void flipPass();
    bool flipGain(uint32_t a, uint32_t b, uint32_t& c, uint32_t& d) const;
    void flip(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    void relaxPass();
    void pushEdgesAround(uint32_t v, bool longer);
    void gatherRegion();
    void queryRegion();

    Mesh* mesh_ = nullptr;
    BVH* bvh_ = nullptr;
    uint32_t startVertices_ = 0, startTriangles_ = 0;
    std::unique_ptr<TopologyUndo> undo_; // stroke-start values of the recorded slots (in the undo's layout)
    std::vector<uint64_t> vertRec_, triRec_;

    // The current remesh.
    glm::vec3 center_{0.0f};
    float radius_ = 0.0f, target_ = 0.0f, splitLen2_ = 0.0f, collapseLen2_ = 0.0f, eps_ = 0.0f;
    std::vector<SymCopy> mirrorSigns_; // the symmetric copies, without the identity
    std::vector<size_t> mirrorInverse_; // per copy, the index (in mirrorSigns_) of its inverse
    // Per mirror: each vertex's image (valid where stamp == mirrorStamp_, so a new remesh forgets them all).
    struct MirrorMap {
        std::vector<uint32_t> to, stamp;
    };
    std::vector<MirrorMap> mirrorMaps_;
    uint32_t mirrorStamp_ = 0;
    std::vector<uint64_t> gridKeys_; // matchMirrors' search grid
    std::vector<uint32_t> gridHead_, gridNext_;
    std::vector<uint32_t> seen_, regionVerts_; // gatherRegion: vertices listed this remesh (seen_ == mirrorStamp_)
    std::vector<uint8_t> deadV_, deadT_;
    std::vector<uint32_t> freeV_, freeT_;
    std::vector<uint32_t> changedV_, changedT_, regionTris_, slivers_;
    std::vector<uint32_t> mirrorTris_; // the triangles under the brush's mirror images (queryRegion)
    std::vector<uint8_t> changedVFlag_;
    std::vector<Edge> heap_;
    std::vector<std::pair<uint32_t, uint32_t>> moved_;
    std::vector<uint32_t> recordedNow_;
    mutable bool orbitIncomplete_ = false; // the last orbit() missed a mirror image
    mutable std::vector<std::pair<uint32_t, uint32_t>> orbitScratch_; // candidate()
    size_t ops_ = 0, maxOps_ = 0;
    mutable std::vector<uint32_t> scratch_, scratch2_, ringA_, ringB_, ringC_, lockScratch_;
};

} // namespace sf
