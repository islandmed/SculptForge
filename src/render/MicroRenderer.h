// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws a mesh's micro-displacement level (core/Micro.h) without ever building its micro triangles:
//
// - GPU data per base triangle, in a spatial (Morton) order of "slots": its corners and the clusters across its
//   edges (texture buffer), its grid of 16-bit displacements and its shading grid (RGBA8: octahedral normal,
//   curvature, mask; computed on the CPU by MicroShader). Grids live in chunks of at most 2^26 points (texture
//   buffer limits), each a whole number of clusters.
// - Clusters of 16-1024 consecutive slots. Every frame each visible cluster gets a level of detail from its
//   projected size (the micro edge it would draw, in pixels), and the levels are lowered until the triangles
//   drawn fit RenderSettings::renderTriangleBudget: the draw cost depends on the screen, not on the micro level.
// - One instanced draw per run of consecutive clusters at one level: a patch of 4^level triangles per base
//   triangle; the vertex shader evaluates the micro surface (Phong-tessellated base + displacement, the same
//   operations as MicroTriangle::evalWeights). A patch edge whose neighbour draws a coarser level snaps its
//   in-between vertices onto the coarser edge (crack-free). The fragment shader interpolates the full-resolution
//   shading grid, so lighting keeps every micro detail even where the geometry is coarser.
// - Brushes mark base triangles dirty; their displacement tiles are re-sent at once, their shading (and that of
//   their neighbours) within a per-frame budget of grid points (the rest follows on the next frames).
// GL 3.3 core: texture buffers, instancing, no tessellation or compute shaders (macOS stops at GL 4.1).
#pragma once

#include "core/Micro.h"
#include "render/Clusters.h"
#include "render/GL.h"

#include <cstdint>
#include <vector>

namespace sf {

class MicroRenderer {
public:
    ~MicroRenderer();
    void release();
    // Uploads what changed since the last call (everything the first time, or after the base or the level
    // changed); clears the micro level's dirty state. Returns false when the GPU ran out of memory.
    bool sync(Mesh& mesh);
    // Draws with `program` bound (the mesh program with MICRO): sets the micro uniforms and issues the draws.
    // `budget`: most triangles to draw; `targetPx`: the micro edge length to aim for on screen. Returns the
    // triangles drawn.
    size_t draw(GLuint program, const CullView& cull, const glm::mat4& proj, const glm::vec4& viewport, size_t budget,
                float targetPx);
    size_t gpuBytes() const;
    bool closed() const { return closed_; }
    // Grid points whose shading still waits for a frame (budget), for tests and the benchmark.
    size_t pendingShading() const { return pendingCount_; }
    // Grid points of shading recomputed per sync at most (0 = no limit).
    size_t shadeBudget = size_t(3) << 20;
    // The last draw: the level used by each visible cluster (-1 = culled), and the target it settled on.
    float lastTargetPx = 0.0f;

private:
    void buildLayout(const Mesh& mesh);
    void uploadBase(const Mesh& mesh);
    void updateClusterBounds(const Mesh& mesh, const std::vector<uint32_t>& clusters);
    void ensurePatches(int level);
    void uploadDispTiles(const Mesh& mesh, std::vector<uint32_t>& slots, bool all);
    void shadePending(const Mesh& mesh, bool all);

    uint64_t serial_ = 0;
    uint32_t baseTriangles_ = 0, baseVertices_ = 0;
    int level_ = 0;
    uint32_t n_ = 1, samples_ = 3;
    float step_ = 0.0f;
    bool closed_ = false;
    std::vector<uint32_t> order_;   // slot -> base triangle
    std::vector<uint32_t> slotOf_;  // base triangle -> slot
    uint32_t clusterShift_ = 8;
    std::vector<ClusterBounds> bounds_;
    std::vector<float> clusterEdge_; // average base edge per cluster
    AABB box_;
    struct Chunk {
        uint32_t firstSlot = 0, slots = 0;
        GLuint disp = 0, dispTex = 0, shade = 0, shadeTex = 0;
    };
    std::vector<Chunk> chunks_;
    uint32_t chunkSlots_ = 0;
    GLuint triData_ = 0, triTex_ = 0, baseData_ = 0, baseTex_ = 0, levelData_ = 0, levelTex_ = 0;
    struct Patch {
        GLuint vao = 0, vbo = 0, ibo = 0;
        GLsizei indices = 0;
        std::vector<GLint> corners; // levels 0-2: the triangles' corners, packed i | j << 16 (uCorners)
    };
    GLuint emptyVao_ = 0;
    static constexpr int kUnrolledLevels = 1;
    std::vector<Patch> patches_;
    size_t bytes_ = 0;
    MicroShader shader_;
    std::vector<uint64_t> pending_; // per base triangle: shading out of date
    size_t pendingCount_ = 0;
    std::vector<uint32_t> scratch_, written_, stageIndex_;
    std::vector<uint8_t> staging_;
    std::vector<uint32_t> levels_;
    std::vector<int8_t> want_;
    struct Uniforms {
        GLuint program = 0;
        GLint slotBase = -1, chunkSlot = -1, level = -1, n = -1, samples = -1, clusterShift = -1, step = -1, unrolled = -1,
              corners = -1;
    } u_;
};

// The micro mesh shader's vertex stage (with the mesh fragment shader under MICRO).
const char* microVertexShaderSource();

} // namespace sf
