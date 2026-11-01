// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh 2 internals (core/QRemesh2.cpp has the outline): the proxy surface (QRemesh2Proxy.cpp), the cross field
// (QRemesh2Field.cpp) and the integer-grid map (QRemesh2Param.cpp). Not a public header.
#pragma once

#include "core/QRemeshInternal.h"
#include "core/SurfaceQuery.h"

#include <glm/glm.hpp>

#include <complex>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

namespace sf::qr2 {

using glm::vec2;
using glm::vec3;
constexpr uint32_t kNone = ~0u;

inline uint64_t edgeKey(uint32_t a, uint32_t b) { return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a; }
inline vec3 safeNormalize(const vec3& v, const vec3& fallback = vec3(0, 0, 1)) {
    const float l = glm::length(v);
    return l > 1e-20f ? v / l : fallback;
}

// Runs fn(i) for i in [0, n) on all hardware threads (coarse jobs: one i is a lot of work).
void parallelJobs(size_t n, const std::function<void(size_t)>& fn);

// --- Proxy surface ----------------------------------------------------------------------------------------------

// The triangle mesh the field and the map are solved on: about three triangle edges per quad edge, even triangles,
// feature lines (open edges, polygroup borders, hard edges) kept as edge chains on their exact place.
struct Proxy {
    std::vector<vec3> P;
    std::vector<glm::uvec3> T;
    std::vector<uint32_t> group;   // per triangle
    std::vector<uint32_t> opp;     // per corner 3t+k (the edge T[k] -> T[k+1]): the twin corner, kNone when open
    std::vector<uint8_t> feature;  // per corner: the edge is a feature line (open edges included)
    std::vector<uint8_t> sharp;    // per corner: ... because it is a hard edge (Detect Edges)
    std::vector<uint8_t> guide;    // per corner: ... because a QRemesh Guide runs along it (empty: none)
    std::vector<float> size;       // per vertex: wanted quad edge length
    std::vector<float> fieldSize;  // per vertex: the (relative) size the cross field turns with; empty = none
    std::vector<vec3> color;       // per vertex polypaint (usePolypaint only)
    std::vector<vec3> fixedDir;    // per triangle: direction the field is fixed to (a guide's tangent), 0 = from its edges

    size_t triangles() const { return T.size(); }
    void buildOpposites(); // opp from T (edges with other than two consistently wound triangles stay open)
};

struct ProxySource {
    const qr::WorkMesh* mesh = nullptr;
    const TriangleBVH* surface = nullptr;   // the source piece
    const SegmentBVH* features = nullptr;   // the source's feature lines (to keep feature vertices on them)
    std::unordered_set<uint64_t> sharp;     // hard-edge source edges (vertex pairs) that must stay
    bool keepGroups = false;
};

// Dense sculpts: the piece cut into spatial blocks, each decimated towards `edge` on its own thread (vertices shared
// between blocks stay), merged. The builder's own pass then finishes the strips along the block borders. The hard
// edges (source.sharp) come back in the result's vertex numbers.
qr::WorkMesh preDecimate(const ProxySource& source, float edge, std::unordered_set<uint64_t>& sharpOut);

class ProxyBuilder {
public:
    struct Impl;
    explicit ProxyBuilder(const ProxySource& source);
    ~ProxyBuilder();
    // Edge collapses down to edges of about `edge` (the dense sculpt to the proxy's density).
    void decimate(float edge);
    // Isotropic remeshing passes (split, collapse, flip, smooth onto the source) towards `edge` times each
    // vertex's relative size (relSize: per current vertex, empty = 1).
    void remesh(float edge, const std::vector<float>& relSize, int iterations);
    // The current mesh, compacted. vertexMap (optional): proxy vertex -> builder vertex.
    Proxy result(std::vector<uint32_t>* vertexMap = nullptr) const;
    size_t vertexCount() const; // builder vertex slots (relSize's length)

private:
    std::unique_ptr<Impl> impl_;
};

// --- Curvature and cross field -------------------------------------------------------------------------------------

// Per triangle: an orthonormal frame (e1 along the first edge, e2 = n x e1) the field angles are measured in.
struct Frames {
    std::vector<vec3> e1, e2, n;
    std::vector<float> area;
    void build(const Proxy& p);
};

// Per triangle curvature tensor (Cohen-Steiner & Morvan, from dihedral angles), smoothed over `radius`: the
// direction of strongest bending (angle in the frame) and the anisotropy |k1 - k2| (1 / length).
struct Curvature {
    std::vector<float> angle, anisotropy, magnitude;
};
Curvature computeCurvature(const Proxy& p, const Frames& f, float radius);

struct FieldSettings {
    float alignWeight = 1.0f;          // how strongly the field follows the curvature directions
    std::vector<float> guideAngle;     // per triangle: a guide's direction to follow softly (NaN: none)
    std::vector<float> guideWeight;
};

struct CrossField {
    std::vector<float> theta;          // per triangle, combed: the u direction's angle in the triangle's frame
    std::vector<int8_t> match;         // per corner: quarter turns from this triangle's cross to the twin's (combed)
    std::vector<int8_t> vertexIndex;   // per vertex: singularity index in quarter turns (0 regular, +1 = valence 3)
    size_t singularities = 0;
};

// Smooth 4-RoSy field: feature edges fixed, curvature directions followed with `alignWeight`. Solved as one sparse
// complex linear system (u = e^{4 i theta}), then normalised; deterministic.
bool solveCrossField(const Proxy& p, const Frames& f, const Curvature& c, const FieldSettings& s, CrossField& out,
                     const std::function<bool()>& cancelled);
// Matching across each edge and the singularity index of each vertex for the current angles (no combing).
void computeMatching(const Proxy& p, const Frames& f, CrossField& field);

// --- Integer-grid map ------------------------------------------------------------------------------------------------

struct GridMap {
    std::vector<vec2> uv;          // per corner 3t+k: the map at that corner, in the triangle's chart
    size_t flipped = 0;            // triangles whose image is turned over
    size_t cutEdges = 0, integerVars = 0;
    double wantQuads = 0.0;        // in: scale the map to this many unit squares before rounding (0: as solved)
};

// Seamless map with the u / v gradients following the field at 1 / size, singularities and feature lines on the
// integer lattice (greedy rounding), so the lattice is a quad mesh. Combs `field` (match is 0 off the cut).
bool solveGridMap(const Proxy& p, const Frames& f, CrossField& field, GridMap& out, const std::function<bool()>& cancelled);

} // namespace sf::qr2
