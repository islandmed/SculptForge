// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/IndexBits.h"
#include "core/PaintLayer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sf {

struct PaintTexture;
struct MultiresStack;
struct SubdivTopology;
struct ImplicitAdjacency;
struct MicroMesh;
struct SelectionSetList;

// The vertices or triangles around a vertex (Mesh::neighbors, Mesh::vertexTriangles), in ascending order: a view
// of the mesh's adjacency lists or, where the mesh's adjacency is implicit, the list itself. Use it like a span
// (but don't keep a pointer into it past its lifetime).
class IndexRing {
public:
    static constexpr uint32_t kCapacity = 64;
    IndexRing() = default;
    IndexRing(const uint32_t* b, const uint32_t* e) : ptr_(b), size_(uint32_t(e - b)) {}
    const uint32_t* data() const { return ptr_ ? ptr_ : buf_; }
    const uint32_t* begin() const { return data(); }
    const uint32_t* end() const { return data() + size_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    uint32_t operator[](size_t i) const { return data()[i]; }
    uint32_t front() const { return data()[0]; }
    uint32_t back() const { return data()[size_ - 1]; }

private:
    friend class Mesh;
    const uint32_t* ptr_ = nullptr; // null: the indices are in buf_
    uint32_t size_ = 0;
    uint32_t buf_[kCapacity]; // (not initialised: only the first size_ are used)
};

struct AABB {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    void expand(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void expand(const AABB& b) {
        min = glm::min(min, b.min);
        max = glm::max(max, b.max);
    }
    bool valid() const { return min.x <= max.x; }
    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extent() const { return max - min; }
};

// A sculpt layer: per-vertex offsets blended in with a weight. Layers are
// exported as glTF morph targets (blend shapes) for facial/corrective animation.
struct SculptLayer {
    std::string name;
    std::vector<glm::vec3> delta;
    float weight = 1.0f;
};

// Triangle mesh with vertex adjacency, suitable for sculpting.
//
// Optional attributes (see docs/architecture-attributes.md) are empty until
// a feature needs them, so plain meshes pay nothing. When present their size
// always matches the vertex / triangle / corner count; rebuildTopology()
// drops any that don't.
// One sphere of a ZSphere armature (core/ZSpheres.h): the root is index 0 with parent -1; every other sphere
// grows from `parent`.
struct ZSphere {
    glm::vec3 center{0.0f};
    float radius = 1.0f;
    int parent = -1;
};

class Mesh {
public:
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::uvec3> triangles;

    // Per vertex: mask (0 = free, 1 = fully masked) and polypaint (linear RGB, 0..1).
    std::vector<float> mask;
    std::vector<glm::vec3> colors;
    // Per triangle: polygroup id (empty = everything in group 0) and hidden flag (empty = all visible).
    std::vector<uint32_t> polygroups;
    std::vector<uint8_t> hidden;
    // Per triangle corner (3 per triangle, in triangle order): texture coordinates.
    std::vector<glm::vec2> uvs;
    // Per triangle corner (empty = none): UV editing flags (core/Uv.h), kUvSeam on the edge from this corner to
    // the next (a marked seam, kept on both sides of the edge) and kUvPin (the corner's UV stays put on Unwrap).
    std::vector<uint8_t> uvFlags;
    // Per triangle: the triangle it forms a quad with (kNoQuad = a plain triangle; empty = no quads). Imported
    // quads are split into two triangles that keep each other here, so the wireframe and the exporters can
    // show and write them as quads again. Pairs are symmetric and share one edge, wound oppositely;
    // sanitizeAttributes() unpairs anything else, so topology changes can never leave a stale pair.
    std::vector<uint32_t> quadPartners;
    static constexpr uint32_t kNoQuad = ~0u;

    bool hasMask() const { return !mask.empty(); }
    bool hasColors() const { return !colors.empty(); }
    bool hasPolygroups() const { return !polygroups.empty(); }
    bool hasHidden() const { return !hidden.empty(); }
    bool hasUvs() const { return !uvs.empty(); }
    bool hasUvFlags() const { return !uvFlags.empty(); }
    bool hasQuads() const { return !quadPartners.empty(); }

    float maskAt(uint32_t v) const { return mask.empty() ? 0.0f : mask[v]; }
    glm::vec3 colorAt(uint32_t v, const glm::vec3& fallback = glm::vec3(1.0f)) const {
        return colors.empty() ? fallback : colors[v];
    }
    uint32_t polygroupAt(uint32_t t) const { return polygroups.empty() ? 0u : polygroups[t]; }
    bool triangleHidden(uint32_t t) const { return !hidden.empty() && hidden[t] != 0; }
    // The triangle `t` is shown and exported as one quad with, or kNoQuad: a stored pair whose halves share a
    // polygroup (a pair split between groups shows and exports as two triangles).
    uint32_t quadPartnerAt(uint32_t t) const {
        if (quadPartners.empty()) return kNoQuad;
        const uint32_t u = quadPartners[t];
        return u < quadPartners.size() && polygroupAt(t) == polygroupAt(u) ? u : kNoQuad;
    }
    // Which edge of `t` (t[e] -> t[(e + 1) % 3]) is the diagonal it shares with triangle `u` (wound the other
    // way in `u`, whose third corner is not on `t`), or -1 when they don't form a quad.
    int quadDiagonal(uint32_t t, uint32_t u) const;
    // Face for exporting `t`: returns 4 with the quad's corners in winding order (t[e+1], t[e+2], t[e], then the
    // partner's far corner, so an (a,b,c) + (a,c,d) split gives (a,b,c,d) back), 3 with the triangle, or 0 when
    // `t` is the second half of a quad written with its partner.
    int exportFace(uint32_t t, glm::uvec4& face) const;
    // Number of quads (pairs counted once by quadPartnerAt).
    size_t quadCount() const;
    // True when every triangle around `v` is hidden (such vertices must not be edited).
    bool vertexHidden(uint32_t v) const;

    // Allocate an attribute (no-op if present); returns it for convenience.
    std::vector<float>& ensureMask(float fill = 0.0f);
    std::vector<glm::vec3>& ensureColors(const glm::vec3& fill = glm::vec3(1.0f));
    std::vector<uint32_t>& ensurePolygroups(uint32_t fill = 0);
    std::vector<uint8_t>& ensureHidden();
    std::vector<uint32_t>& ensureQuadPartners();
    // Records triangles `t` and `u` as one quad (growing quadPartners to the triangle count, so it can be called
    // while triangles are being added).
    void pairQuad(uint32_t t, uint32_t u);
    // Free an attribute again once it holds only its default (e.g. after "clear mask" / "show all").
    void clearMask() { std::vector<float>().swap(mask); }
    void clearColors() { std::vector<glm::vec3>().swap(colors); }
    void clearPolygroups() { std::vector<uint32_t>().swap(polygroups); }
    void clearHidden() { std::vector<uint8_t>().swap(hidden); }
    void clearQuads() { std::vector<uint32_t>().swap(quadPartners); }
    // Drops attributes whose size no longer matches the mesh and unpairs invalid quad pairs (called by
    // rebuildTopology).
    void sanitizeAttributes();

    // Sculpt layers. While any exist, `base` holds the positions with every
    // layer at zero and positions == base + sum(layer.weight * layer.delta).
    std::vector<glm::vec3> base;
    std::vector<SculptLayer> layers;

    int addLayer(std::string name);
    void removeLayer(int index);
    // Re-evaluates every vertex and recomputes normals (BVH must be rebuilt by the caller).
    void setLayerWeight(int index, float weight);
    // The array a stroke on `layer` writes to (-1 = base), and the weight its offsets are scaled by.
    std::vector<glm::vec3>& layerTarget(int layer);
    float layerTargetWeight(int layer) const;
    // Recomputes positions of `verts` from base + layers.
    void evaluate(std::span<const uint32_t> verts);
    void evaluateAll();
    const std::vector<glm::vec3>& basePositions() const { return layers.empty() ? positions : base; }

    // Paint layers (core/PaintLayer.h). While any exist, `paintBase` holds the
    // Base layer and `colors` the composited stack; `activePaintLayer` is the
    // layer painting writes into (-1 = Base). Empty = plain polypaint in `colors`.
    std::vector<glm::vec3> paintBase;
    std::vector<PaintLayer> paintLayers;
    int activePaintLayer = -1;

    // Texture painting (core/Texture.h): an RGBA texture on the UVs, shared by copies of the mesh until
    // one of them changes it (mutableTexture copies first). `paintToTexture`: the Paint brush, fills and
    // the paint layers work on the texture instead of the vertices.
    std::shared_ptr<PaintTexture> texture;
    bool paintToTexture = false;

    // Subdivision levels (core/Multires.h): the stack this mesh is one level of, shared copy-on-write by
    // copies of the mesh. Null = a plain mesh. sanitizeAttributes() drops it if the topology no longer fits.
    std::shared_ptr<MultiresStack> multires;

    // Micro-displacement level (core/Micro.h): above this mesh (the base, its highest subdivision level), a grid of
    // scalar displacements per triangle that the brushes and the renderer use instead of the mesh. Shared
    // copy-on-write by copies of the mesh (mutableMicro). Null = none. sanitizeAttributes() drops it if the
    // triangle or vertex count changes.
    std::shared_ptr<MicroMesh> micro;

    // ZSpheres (core/ZSpheres.h): not empty = a ZSphere SubTool, whose triangles are made from this armature (the
    // spheres and their links, or the skin while `zspherePreview`). Copies, undo steps and projects carry it.
    std::vector<ZSphere> zspheres;
    bool zspherePreview = false;

    // Selection Sets (core/SelectionSets.h): named snapshots of the mask, polygroups, vertex paint, hidden faces, UVs
    // and shape, immutable and shared by copies of the mesh (undo steps swap the whole list). They belong to the
    // Part, not to one subdivision level: stepping levels keeps them. Null = none.
    std::shared_ptr<const SelectionSetList> selectionSets;

    size_t vertexCount() const { return positions.size(); }
    size_t triangleCount() const { return triangles.size(); }

    // Must be called after the triangle list or vertex count changes. A mesh that is the current level of its
    // subdivision stack, made by Divide, gets implicit adjacency (implicitAdjacency).
    void rebuildTopology();
    void recomputeAllNormals();
    // Recompute normals of the faces around `verts` and of every vertex they touch. With `curvature` (indexed
    // by vertex), also each of those vertices' curvatureAt, from the same triangles (a brush stroke hands it to
    // the renderer, which then needn't walk the rings again).
    void updateNormals(std::span<const uint32_t> verts, float* curvature = nullptr);
    // The same, given the vertices whose normals may have changed (any superset: the others come out the
    // same): fill(begin, end, set) adds those of jobs [begin, end) of `count` with set.insertLocal(). A brush
    // gives the corners of the BVH leaves its query listed, which is cheaper than every moved vertex's ring.
    void updateNormalsAround(size_t count, size_t grain, const std::function<void(size_t, size_t, IndexBits&)>& fill,
                             float* curvature = nullptr);
    // Marks the vertices fill() lists dirty for the GPU without recomputing their normals (a draft stroke, whose
    // normals wait for its end: Sculptor::draftStrokes).
    void markDirtyAround(size_t count, size_t grain, const std::function<void(size_t, size_t, IndexBits&)>& fill);
    // The renderer's cavity term at `v` (render/Clusters.h): how far the average of the other corners of its
    // triangles sits above the vertex along its normal (> 0 in a crease), over their mean distance. Each
    // neighbour of an inner vertex of a manifold is the corner of two of its triangles, so this is the average
    // over its ring.
    float curvatureAt(uint32_t v) const;

    // The vertices sharing an edge with `v`, and the triangles around it, in ascending order.
    IndexRing neighbors(uint32_t v) const {
        if (implicit_) return implicitNeighbors(v);
        return {adjList_.data() + adjOffsets_[v], adjList_.data() + (adjEnds_.empty() ? adjOffsets_[v + 1] : adjEnds_[v])};
    }
    IndexRing vertexTriangles(uint32_t v) const {
        if (implicit_) return implicitTriangles(v);
        return {vtList_.data() + vtOffsets_[v], vtList_.data() + (vtEnds_.empty() ? vtOffsets_[v + 1] : vtEnds_[v])};
    }
    // The vertices sharing a polygon edge with `v`: neighbors() less the far ends of the quad diagonals at `v`
    // (each quad is stored as two triangles; its diagonal is no edge of the polygon). Smoothing averages this ring:
    // with the diagonals, a vertex on one end of a quad's diagonal and not the other is pulled along it, which
    // shears a quad mesh into zigzags (#69). Without quads, neighbors() itself.
    IndexRing polygonNeighbors(uint32_t v) const {
        if (quadPartners.empty()) return neighbors(v);
        return quadNeighbors(v);
    }
    // True when the adjacency follows from the subdivision level this mesh is (see rebuildTopology).
    bool implicitAdjacency() const { return bool(implicit_); }

    // Dynamic topology (core/Dyntopo.h): adjacency lists that can be rewritten one vertex at a time, so local
    // remeshing under the brush doesn't rebuild the whole mesh's. Between begin and end each vertex's lists
    // have their own capacity (a list that outgrows it moves to the end of the arrays); end packs them back
    // into the usual form. rebuildTopology() also ends it. Not for meshes with implicit adjacency.
    bool beginDynamicAdjacency();
    void endDynamicAdjacency();
    bool dynamicAdjacency() const { return !adjEnds_.empty(); }
    // Replace vertex v's triangles / ring (ascending). v may be vertexCount() - 1 after addVertexSlot().
    void setVertexTriangles(uint32_t v, std::span<const uint32_t> tris);
    void setNeighbors(uint32_t v, std::span<const uint32_t> ring);
    // Appends empty lists for a new last vertex, or drops the last vertex's (the caller sizes the vertex arrays).
    void addVertexSlot();
    void removeLastVertexSlot();

    AABB bounds() const;
    // Bytes this mesh holds (every array, topology included; not the shared texture or subdivision stack).
    size_t memoryBytes() const;

    // GPU synchronisation state: [dirtyBegin, dirtyEnd) bounds what needs re-uploading,
    // topologyDirty means everything (including indices) does.
    // Vertex attributes (mask, colours) share the vertex range; facesDirty
    // means per-triangle state (hidden, polygroups) needs re-uploading.
    // Within the range each changed vertex has a bit in dirtyBits, and each block of kDirtyBlock vertices with
    // any a mark in dirtyBlocks, so uploads find a dab's vertices without scanning the mesh (core/UploadOrder.h).
    uint32_t dirtyBegin = std::numeric_limits<uint32_t>::max();
    uint32_t dirtyEnd = 0;
    bool topologyDirty = true;
    bool facesDirty = true;
    static constexpr uint32_t kDirtyBlockShift = 11, kDirtyBlock = 1u << kDirtyBlockShift; // 2048 vertices
    std::vector<uint8_t> dirtyBlocks;
    std::vector<uint64_t> dirtyBits;
    // Dynamic topology (core/Dyntopo.h): triangle slots whose corners changed (in place, added, removed or
    // renumbered, so possibly >= triangleCount()) since the last upload. A renderer can patch those instead of
    // a full upload; topologyDirty still means everything. Cleared by clearDirty().
    std::vector<uint32_t> changedTriangles;

    void markDirty(uint32_t v) {
        dirtyBegin = std::min(dirtyBegin, v);
        dirtyEnd = std::max(dirtyEnd, v + 1);
        const size_t b = v >> kDirtyBlockShift;
        if (b >= dirtyBlocks.size()) growDirtyBlocks(b + 1);
        dirtyBlocks[b] = 1;
        dirtyBits[v >> 6] |= uint64_t(1) << (v & 63);
    }
    void growDirtyBlocks(size_t blocks); // (out of line: rarely needed)
    // Marks the vertices [begin, end).
    void markDirtyRange(uint32_t begin, uint32_t end);
    void clearDirty();
    void markAllDirty() {
        if (positions.empty()) return;
        markDirtyRange(0, static_cast<uint32_t>(positions.size()));
    }
    bool vertexDirty(uint32_t v) const { return (v >> 6) < dirtyBits.size() && (dirtyBits[v >> 6] >> (v & 63)) & 1; }

private:
    void finishTopology();
    bool beginNormalSet(); // updateNormals: room for the set (false: no topology)
    void finishNormals(float* curvature, bool computeNormals = true); // ... then each member's normal (and curvatureAt)
    IndexRing implicitTriangles(uint32_t v) const;
    IndexRing implicitNeighbors(uint32_t v) const;
    IndexRing quadNeighbors(uint32_t v) const;

    // Vertex adjacency (CSR), or, on a mesh that is a level made by Divide (whose triangles are the ones its
    // subdivision topology generates), that topology and two small indexes into it: the triangles and rings
    // around a vertex are worked out from the coarse polygons around it instead of stored, which saves about 24
    // bytes per triangle.
    std::vector<uint32_t> adjOffsets_, adjList_;
    std::vector<uint32_t> vtOffsets_, vtList_;
    std::shared_ptr<const ImplicitAdjacency> implicit_; // (core/Mesh.cpp)
    // Dynamic adjacency (beginDynamicAdjacency): each list starts at its offset and ends at its end, with
    // room up to offset + cap. Empty otherwise.
    std::vector<uint32_t> adjEnds_, adjCaps_, vtEnds_, vtCaps_;
    static void setList(std::vector<uint32_t>& offsets, std::vector<uint32_t>& ends, std::vector<uint32_t>& caps,
                        std::vector<uint32_t>& list, uint32_t v, std::span<const uint32_t> values);

    // updateNormals: the vertices to update, as a set and as a list.
    IndexBits normalBits_;
    std::vector<uint32_t> scratchVerts_;
};

// Shape-preserving Taubin (lambda/mu) smoothing of positions; needs topology.
// Ignores sculpt layers, so use it on freshly built meshes.
void taubinSmooth(Mesh& m, int iterations);

// Merges coincident vertices (UV/normal seams from importers), drops
// degenerate triangles and builds topology and normals. `layers` (optional)
// hold per-vertex deltas indexed like m.positions and become sculpt layers.
// Optional attributes of `m` are carried over (vertex ones from the first
// vertex of each welded group, triangle/corner ones per kept triangle).
Mesh weldImported(const Mesh& m, const std::vector<SculptLayer>& layers = {});

// Carries the quad pairs of `src` over to `dst`, built from some of its triangles: `newIndex[t]` is the index of
// src triangle t in dst (Mesh::kNoQuad = dropped). Pairs that lost a half are dropped; call before
// dst.rebuildTopology(), which checks the rest.
void remapQuads(const Mesh& src, const std::vector<uint32_t>& newIndex, Mesh& dst);
// Appends the quad pairs of `src` (whose triangles were appended to `dst` after `firstTriangle` others).
void appendQuads(Mesh& dst, size_t firstTriangle, const Mesh& src);

// Mesh::colors are linear RGB; files that store display colours (OBJ, PLY) convert.
glm::vec3 srgbToLinear(const glm::vec3& c);
glm::vec3 linearToSrgb(const glm::vec3& c);

// Gives every vertex of `dst` the mask and colour of the nearest vertex of
// `src` (only the attributes `src` has). For rebuilt surfaces such as voxel
// remesh or re-imported scans; needs no topology.
void transferVertexAttributes(const Mesh& src, Mesh& dst);

} // namespace sf
