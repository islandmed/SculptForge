// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/IndexBits.h"
#include "core/Mesh.h"

#include <functional>
#include <span>
#include <vector>

namespace sf {

struct RayHit {
    float t = 0.0f;
    uint32_t triangle = 0;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f}; // geometric normal of the hit triangle
};

// Bounding volume hierarchy over a mesh's triangles. Supports ray casts,
// sphere queries for brushes, and cheap incremental refits while sculpting.
class BVH {
public:
    void build(const Mesh& mesh);
    // Nearest hit along the ray; hidden triangles (Mesh::hidden) are skipped.
    bool raycast(const Mesh& mesh, const glm::vec3& origin, const glm::vec3& dir, RayHit& hit) const;
    // Appends every vertex within `radius` of `center` to `out` (cleared first), in increasing index order.
    // Ignores visibility; brushes use sf::brushVertices, which filters hidden ones.
    void queryVertices(const Mesh& mesh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) const;
    // queryVertices for an infinite cylinder: every vertex within `radius` of the line through `point` along
    // `axis` (a brush whose falloff is measured on screen reaches through the model along the view).
    void queryVerticesCylinder(const Mesh& mesh, const glm::vec3& point, const glm::vec3& axis, float radius,
                               std::vector<uint32_t>& out) const;
    // Appends every triangle whose bounds touch the sphere (cleared first; hidden ones included).
    void queryTriangles(const Mesh& mesh, const glm::vec3& center, float radius, std::vector<uint32_t>& out) const;
    // Calls fn(triangles of the leaf) for every leaf whose box touches the sphere (hidden triangles included);
    // no per-triangle test, for callers that only need where the triangles are (texture painting's tiles).
    void forEachLeafInSphere(const glm::vec3& center, float radius,
                             const std::function<void(std::span<const uint32_t>)>& fn) const;
    // Updates bounds after the given vertices moved. A few leaves grow their ancestors' bounds (conservatively,
    // which keeps queries correct); many leaves (a big brush) recompute their ancestors from their children, in
    // parallel.
    void refit(const Mesh& mesh, std::span<const uint32_t> movedVerts);
    // refit() after only vertices the last queryVertices() returned moved (brush dabs): the query already
    // found the leaves holding them, so this skips looking them up.
    void refitQueried(const Mesh& mesh);
    // The leaves holding the vertices the last queryVertices() returned, and refit() given such leaves (from
    // one or more queries, each once: the Move brush keeps them for its whole stroke).
    std::span<const uint32_t> queriedLeaves() const { return queryLeaves_; }
    // The triangles of a leaf (empty for an inner node): every triangle with a vertex a query found is in one of
    // the leaves it listed.
    std::span<const uint32_t> leafTriangles(uint32_t leaf) const {
        const Node& n = nodes_[leaf];
        return n.count > 0 ? std::span<const uint32_t>(triIndices_.data() + n.first, n.count) : std::span<const uint32_t>();
    }
    void refitLeaves(const Mesh& mesh, std::span<const uint32_t> leaves);
    // Recomputes every box (same tree) after most of the mesh moved, e.g. a whole SubTool transformed.
    void refitAll(const Mesh& mesh);

    // Dynamic topology (core/Dyntopo.h): a few triangles at a time added, removed or renumbered without a
    // rebuild. insertTriangle puts triangle t (a new last index, or one removeTriangle freed) in the leaf of
    // triangle `nearby` (or, if that isn't in the tree, the leaf its centroid falls in); refitChanged() then
    // recomputes the boxes of every leaf edited since the last call (growing ancestors, as refit does) and
    // splits leaves that grew too big. Boxes only grow, so after many edits rebuild when degraded() says so.
    void insertTriangle(const Mesh& mesh, uint32_t t, uint32_t nearby);
    void removeTriangle(uint32_t t);
    // Triangle `from` is now numbered `to` (a free index); a freed last index can then be dropped with resizeTriangles.
    void renumberTriangle(uint32_t from, uint32_t to);
    void resizeTriangles(size_t count);
    // Marks the leaf holding t for refitChanged (its triangle moved).
    void touchTriangle(uint32_t t);
    void refitChanged(const Mesh& mesh);
    bool degraded() const;

    size_t nodeCount() const { return nodes_.size(); }
    size_t memoryBytes() const;

private:
    struct Node {
        AABB box;
        uint32_t first = 0; // leaf: offset into triIndices_, inner: index of left child (right = first + 1)
        uint32_t count = 0; // > 0 for leaves
        uint32_t parent = 0;
    };

    AABB leafBox(const Mesh& mesh, const Node& n) const;
    void recomputeLeaf(const Mesh& mesh, Node& n); // its box, and triLeaf_ for its triangles

    std::vector<Node> nodes_;
    std::vector<uint32_t> triIndices_;
    std::vector<uint32_t> triLeaf_;

    // Dynamic edits: per node the room its leaf has in triIndices_ (empty until the first edit), the leaves
    // edited since refitChanged (each once, flagged in leafEdited_), and how many entries of triIndices_ are no
    // longer used (leaves that moved to make room) or were added since the build.
    std::vector<uint32_t> leafCap_, editedLeaves_;
    std::vector<uint8_t> leafEdited_;
    size_t unusedEntries_ = 0, editsSinceBuild_ = 0;
    static constexpr uint32_t kNoLeaf = ~0u;
    void editLeaf(uint32_t leaf);
    void refitAncestors(std::span<const uint32_t> leaves); // refitLeaves for many leaves, in parallel
    void splitLeaf(const Mesh& mesh, uint32_t leaf);

    template <class Shape>
    void queryVerticesIn(const Mesh& mesh, const Shape& shape, std::vector<uint32_t>& out) const;
    mutable IndexBits vertBits_;               // queryVertices: the vertices found so far
    mutable std::vector<uint32_t> queryLeaves_; // the leaves holding them, for refitQueried()
    mutable std::vector<uint32_t> queryTasks_, queryNext_;
    mutable std::vector<std::vector<uint32_t>> queryParts_;
    IndexBits leafBits_; // refit: the leaves to recompute
    std::vector<uint8_t> nodeDirty_; // refitAncestors: the inner nodes to recompute (all 0 between calls)
    std::vector<uint32_t> scratchLeaves_;
    mutable std::vector<uint32_t> stack_;
};

} // namespace sf
