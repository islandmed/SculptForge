// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The CPU side of the renderer's mesh drawing, kept apart from GL so tests can check it:
//
// - The compact vertex the GPU keeps (12 bytes, was 28): the position on a 21-bit grid over the mesh's bounds
//   (with a margin, so sculpting rarely leaves it; one grid for the whole mesh, so a vertex copied into two
//   clusters lands on exactly the same point and seams can't crack), an octahedral normal (2 x 11 bits) and the
//   curvature the shading uses for cavities (10 bits, square-root companded). The shader decodes it the same way (Renderer.cpp).
// - Culling per cluster (core/UploadOrder.h): outside the view, or every triangle facing away. The second only
//   holds for a closed mesh seen from outside (closedOriented, and nothing cut by the near plane): its back
//   faces are then always behind front faces. Open meshes, hidden triangles and views from inside draw them.
#pragma once

#include "core/Mesh.h"
#include "core/UploadOrder.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace sf {

// Positions: origin + q * step, q in [0, 2^21) per axis.
struct PositionGrid {
    glm::vec3 origin{0.0f};
    float step = 1.0f;
    static constexpr uint32_t kMax = (1u << 21) - 1;
    // A grid over `box` with a margin of an eighth of its size on every side.
    static PositionGrid around(const AABB& box);
    bool contains(const glm::vec3& p) const;
};

struct PackedVertex {
    uint32_t pos[2]; // x: bits 0-20, y: 21-41, z: 42-62
    uint32_t normal; // octahedral x, y (11 bits each, signed), curvature (10 bits, signed)
};
static_assert(sizeof(PackedVertex) == 12);

// Packs a vertex; returns false (and clamps) when p is outside the grid.
bool packVertex(const PositionGrid& grid, const glm::vec3& p, const glm::vec3& n, float curvature, PackedVertex& out);
// What the shader decodes (tests).
glm::vec3 unpackPosition(const PositionGrid& grid, const PackedVertex& v);
glm::vec3 unpackNormal(const PackedVertex& v);
float unpackCurvature(const PackedVertex& v);
// Curvature (Renderer.cpp: neighbour average over mean edge length, so within +-1) is stored as
// sign(c) sqrt(|c|): finest near 0, where the cavity shading ramps (to 0.16 and 0.2), and the whole range kept,
// since coarse meshes interpolate it across big triangles.
constexpr float kCurvatureRange = 1.0f;

// Vertex paint colours in 8 bits per channel, gamma 2.2 encoded so dark colours keep their steps (the shader
// decodes with pow 2.2). A table lookup, it is called for every vertex of an upload.
uint32_t packColor(const glm::vec3& linear);
inline uint8_t packMask(float m) { return uint8_t(std::clamp(m, 0.0f, 1.0f) * 255.0f + 0.5f); }

// A cluster's bounding sphere and the cone holding its triangles' face normals (axis, half-angle in radians;
// coneAngle < 0: no useful cone).
struct ClusterBounds {
    glm::vec3 center{0.0f};
    float radius = -1.0f; // < 0: empty
    glm::vec3 axis{0.0f, 0.0f, 1.0f};
    float coneAngle = -1.0f;
    double volume = 0.0; // six times the signed volume its triangles add to a closed mesh's (outward winding: > 0)
};
ClusterBounds clusterBounds(const Mesh& mesh, const UploadOrder& order, uint32_t cluster);

// The motion proxy of cluster c (drawn while the view moves): its triangles with the vertices snapped to one
// per cell of a grid a few edges wide (the vertex nearest the cell's centre), the degenerate ones dropped, as
// indices into the cluster's vertex range like localTriangles. Vertices on seams between clusters stay, so
// the proxies of neighbouring clusters still meet: the proxy of a closed mesh is closed (a simplicial map of
// it). `hidden` triangles are left out. At most maxTriangles (a coarser grid until it fits); false (and
// nothing) when it doesn't fit even then. `bounds` gets the proxy's normal cone (centre and radius as the
// cluster's, whose vertices it uses).
bool clusterProxy(const Mesh& mesh, const UploadOrder& order, uint32_t c, size_t maxTriangles, std::vector<uint16_t>& out,
                  ClusterBounds& bounds);

// Coarse levels below the mesh (docs/design/micro-coarse-lod.md; Clark 1976, Rossignac-Borrel 1993): nested grids
// over the whole mesh, the first cell four times the mean edge, each next level twice as coarse; a cell's point is the
// mean of the vertices in it (its normal their normals' sum). Every cluster gets per level its triangles mapped to
// cells (degenerate and repeated ones dropped) and its error, the farthest any of its vertices is from its cell's
// point. Two clusters at one level share their cells' points: no cracks between them, so the renderer draws every
// cluster at one level (levels side by side would leave gaps along the seams: a scan's clusters are ragged strips).
struct CoarseLevels {
    static constexpr int kMaxLevels = 5;
    struct Lod {
        int levels = 0; // 0: none (drawn in full)
        uint32_t first[kMaxLevels] = {}, count[kMaxLevels] = {}; // in `indices`
        float error[kMaxLevels] = {};                                                    // world units
    };
    struct Level {
        std::vector<uint64_t> keys; // the cells' grid coordinates (packed), sorted; cell i is point base + i
        uint32_t base = 0;
    };
    glm::vec3 origin{0.0f};
    float cell = 0.0f; // level 0's
    std::vector<Level> levels;
    std::vector<glm::vec3> positions, normals; // every level's cell points, one level after the other
    // Per cell the mean of its vertices' colours (linear) and mask, as the full mesh shows them; empty when the mesh
    // has none (colours: vertex paint and the paint layers' composite).
    std::vector<glm::vec3> colors;
    std::vector<float> mask;
    std::vector<uint32_t> indices;             // into positions (the renderer frees them once on the GPU)
    std::vector<Lod> clusters;
    // Membership is the build's: an edit moves the cells' points (the means of their vertices where they are now),
    // never which cell a vertex is in, so the triangles stay as built and the surface stays closed.
    std::vector<uint32_t> vertexCell;              // per mesh vertex: its level-0 cell (into positions)
    std::vector<uint32_t> memberFirst, members;    // level-0 cell k's vertices: members[memberFirst[k - base0] ...)
    std::vector<uint32_t> parent;                  // per cell: its cell one level up (~0u at the top)
    std::vector<uint32_t> childFirst, children;    // per cell: its cells one level down (level 0: none)
    std::vector<uint32_t> weight;                  // per cell: its vertices
    // The cell of point p at a level by its grid position (cell id into positions), or ~0u: none there at the build.
    uint32_t cellOf(const glm::vec3& p, int level) const;
};
CoarseLevels buildCoarseLevels(const Mesh& mesh, const UploadOrder& order);
// After an edit: the points (and normals) of the level-0 cells holding the `dirty` clusters' vertices, from those
// vertices where they are now, and of their cells up the levels. Returns the cells changed.
std::vector<uint32_t> updateCoarseLevels(CoarseLevels& levels, const Mesh& mesh, const UploadOrder& order,
                                         const std::vector<uint32_t>& dirty);
// The cells' colours and mask from the mesh's: of the level-0 cells `cells0` (cell ids into positions; empty: all)
// and their cells up the levels. Where the mesh gained or lost colours or a mask since, every cell's.
void coarseAttributes(CoarseLevels& levels, const Mesh& mesh, std::vector<uint32_t> cells0 = {});
// Whether the cells' colours and mask are there exactly where the mesh has them.
bool coarseAttributesMatch(const CoarseLevels& levels, const Mesh& mesh);

// The measured GPU time per drawn triangle (ms; 0 = none yet), a moving average of whole-frame timer queries, after
// one more frame: `gpuMs` for `triangles`, of which `cpuBeforeDrawMs` the CPU spent before the 3D view's first draw
// (the UI, an action, the upload: the GPU waits through it inside the query). Frames too small to say, and samples
// over four times the estimate (a first draw linking its shaders, a stall) move it by at most that much: one slow
// frame never decides that a mesh's full detail can't be drawn at rest (#151: a paint layer added on a 4M mesh left
// it on its coarse level until a restart, as coarse frames never measure the full mesh again).
double nextGpuMsPerTriangle(double estimate, double gpuMs, double cpuBeforeDrawMs, uint64_t triangles);

// Every directed edge a->b has as many b->a: the mesh is the closed, consistently wound boundary of a volume
// (it may intersect itself). Wound outwards (a positive signed volume: the sum of ClusterBounds::volume), every
// back face then lies behind a front face from outside. (Strictly: where no part of the surface is turned
// inside out, a lobe pushed through the rest; there a back face can show.)
bool closedOriented(const Mesh& mesh);

// One frame's culling: the view's frustum planes, the eye, and whether back-facing clusters may go.
struct CullView {
    glm::vec4 planes[6];
    glm::vec3 eye{0.0f};
    float nearDistance = 0.0f;
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    bool backfaces = false; // cull clusters facing away
    bool ortho = false;     // an orthographic projection: no eye point, so facing away isn't judged from `eye`
    static CullView from(const glm::mat4& view, const glm::mat4& proj);
};
bool insideFrustum(const CullView& v, const ClusterBounds& b);
bool facingAway(const CullView& v, const ClusterBounds& b); // every triangle of the cluster faces away from the eye

} // namespace sf
