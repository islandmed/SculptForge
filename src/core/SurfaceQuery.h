// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Read-only spatial queries on a fixed surface, safe to call from many threads at once (core/BVH.h is the
// sculpting BVH: refittable, but its queries share scratch buffers). Used by QRemesh (snapping the new
// quads onto the sculpt) and ProjectAll (shooting rays at the source Parts), plus the deviation
// (Hausdorff) measure the tests and the status line report.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <vector>

namespace sf {

struct SurfaceHit {
    uint32_t prim = 0;        // triangle (or segment) index
    glm::vec3 position{0.0f};
    glm::vec3 bary{0.0f};     // barycentric weights of the triangle's corners (segments: x = 1 - u, y = u)
    float distance = 0.0f;    // from the query point, or the ray parameter
};

// Bounding volume hierarchy over primitives given by their boxes (median split, 4 per leaf).
class FlatBVH {
public:
    struct Node {
        AABB box;
        uint32_t first = 0; // leaf: first index into order(); inner: left child (right = first + 1)
        uint32_t count = 0; // > 0 for leaves
    };
    void build(const std::vector<AABB>& boxes);
    const std::vector<Node>& nodes() const { return nodes_; }
    const std::vector<uint32_t>& order() const { return order_; }
    bool empty() const { return nodes_.empty(); }

private:
    std::vector<Node> nodes_;
    std::vector<uint32_t> order_;
};

class TriangleBVH {
public:
    // Takes the triangles to query (hidden ones should be left out by the caller).
    void build(std::vector<glm::vec3> positions, std::vector<glm::uvec3> triangles);
    void build(const Mesh& mesh) { build(mesh.positions, mesh.triangles); }
    bool empty() const { return bvh_.empty(); }

    // Closest surface point within `maxDistance` of `p`.
    bool closestPoint(const glm::vec3& p, float maxDistance, SurfaceHit& hit) const;
    // Nearest hit along the ray (either side of a triangle) with 0 <= t <= maxT; `dir` need not be unit
    // length (t is in its units).
    bool raycast(const glm::vec3& origin, const glm::vec3& dir, float maxT, SurfaceHit& hit) const;

    const std::vector<glm::vec3>& positions() const { return positions_; }
    const std::vector<glm::uvec3>& triangles() const { return triangles_; }
    glm::vec3 faceNormal(uint32_t t) const;

private:
    std::vector<glm::vec3> positions_;
    std::vector<glm::uvec3> triangles_;
    FlatBVH bvh_;
};

// Closest points on a set of line segments (mesh boundaries, face group borders).
class SegmentBVH {
public:
    void build(std::vector<glm::vec3> a, std::vector<glm::vec3> b);
    bool empty() const { return bvh_.empty(); }
    bool closestPoint(const glm::vec3& p, float maxDistance, SurfaceHit& hit) const;
    size_t size() const { return a_.size(); }

private:
    std::vector<glm::vec3> a_, b_;
    FlatBVH bvh_;
};

// Closest point to `p` on triangle abc, with its barycentric weights.
glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                 glm::vec3& bary);

// How far the surface of `mesh` strays from `reference`: distances from every vertex of `mesh` (and, for the
// symmetric measure, every vertex of `reference` back to `mesh`) to the other surface.
struct SurfaceDeviation {
    float max = 0.0f;  // Hausdorff distance (symmetric when measured both ways)
    float mean = 0.0f;
    float rms = 0.0f;
};
SurfaceDeviation measureDeviation(const Mesh& mesh, const Mesh& reference, bool symmetric = true);

} // namespace sf
