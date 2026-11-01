// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GPU brushes (Engine v2 phase 1, docs/design/engine-v2.md section 5): the interface the brush engine
// (core/Sculpt.cpp, core/MicroSculpt.cpp) hands dabs to when a GPU compute path is present.
//
// "The GPU computes, the CPU commits." A GPU dab returns Stroke Delta Tiles: one integer delta per point of the
// dab, never new positions. The CPU applies them to its own copy (the only copy saved, undone and read by every
// other tool) with integer-only (micro levels) or exactly reproducible (mesh levels: q * unit, unit a power of
// two) arithmetic, records the undo step as it always does, and does everything after the kernel itself (masks,
// layers, stitching, normals, the BVH, the renderer's upload). So undo, redo, save and the timeline behave exactly
// as with CPU brushes, and a stroke replayed from the same deltas gives the same bits on any machine.
//
// The GPU kernels compute what the CPU kernels do, in float, so the two paths agree within rounding: a micro grid
// point within 1 height unit, a mesh vertex within a few units of `unit` (tests/gpu/test_gpu_sculpt.cpp).
//
// Core holds no GPU code: the app installs an implementation (render/GpuSculpt.h, OpenGL 4.3 compute) when the
// context has compute shaders, and nothing at all otherwise (macOS OpenGL 4.1, old drivers, sculptforge-cli, the
// core tests): then every dab runs on the CPU, unchanged. An implementation may also decline any single dab
// (wrong thread, out of GPU memory, a driver error): the brush then runs that dab on the CPU.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

enum class BrushType;

// The brush kernels the GPU path implements (the rest always run on the CPU).
enum class GpuBrush : uint32_t { Standard = 0, Clay = 1, Inflate = 2, Smooth = 3, Flatten = 4, Pinch = 5, Crease = 6, Count };
// The GPU kernel for `type` on a mesh level (false: the CPU runs it). Smooth reads each vertex's neighbours,
// which the GPU doesn't hold for mesh levels yet: it stays on the CPU there (micro levels smooth on the GPU).
bool gpuMeshBrush(BrushType type, GpuBrush& out);
// ... and on a micro level (Move, the grab, is handled separately: MicroGrabJob).
bool gpuMicroBrush(BrushType type, GpuBrush& out);

// The brush's frame on the area plane (alpha orientation, square tips): `up` from the alpha's up direction
// projected on the plane (or a fixed axis when they are parallel), right = up x normal. Shared by both paths.
void brushPlaneFrame(const glm::vec3& areaNormal, const glm::vec3& alphaUp, glm::vec3& up, glm::vec3& right);

// --- Mesh levels: one mirror pass of one dab over the vertices `verts` ---
struct MeshDabJob {
    GpuBrush brush = GpuBrush::Standard;
    const glm::vec3* positions = nullptr; // the mesh's arrays (read at verts[i])
    const glm::vec3* normals = nullptr;
    std::span<const uint32_t> verts;
    glm::vec3 center{0.0f}, viewDir{0.0f, 0.0f, -1.0f}, alphaUp{0.0f, 1.0f, 0.0f};
    float radius = 0.1f, strength = 0.5f; // strength: already times the pressure, 0..1
    float focal = 0.0f;
    bool invert = false;
    float unit = 1e-6f; // world units per integer delta step (meshDeltaUnit)
};
// What the GPU found: the area plane (as Sculptor::dabSingle's, for its lastFrame) and per vertex the delta in
// units of job.unit (x, y, z), before masks and visibility (the CPU commit applies them, as for CPU dabs).
struct MeshDabResult {
    glm::vec3 areaNormal{0.0f, 0.0f, 1.0f}, areaCenter{0.0f};
    float weight = 0.0f; // the falloff summed over the vertices (0: nothing under the brush, nothing moves)
    std::vector<glm::ivec3> delta;
};
// The delta unit for a dab of `radius`: a power of two about radius / 2^24 (every delta the kernels make is under
// 2 radii, so it fits in 26 bits; finer than a float position's last bit on a unit-sized model).
float meshDeltaUnit(float radius);
// The CPU commit of one delta: exactly delta * unit (a float times a power of two, no rounding).
inline glm::vec3 meshDeltaValue(const glm::ivec3& d, float unit) { return glm::vec3(d) * unit; }

// --- Micro levels (core/Micro.h): one mirror pass of one dab over base triangles `tris` ---
struct MicroDabJob {
    GpuBrush brush = GpuBrush::Standard;
    uint32_t n = 1, samples = 3, stride = 1; // segments per base edge, grid points per triangle, the dab's stride
    float step = 1e-6f;                      // world units per height unit
    std::span<const uint32_t> tris;
    // Per triangle of `tris`: its corners and normals in vertex-index order and which corner each is
    // (MicroTriangle: p, nrm, slot).
    const glm::vec3* cornerP = nullptr; // tris.size() * 3
    const glm::vec3* cornerN = nullptr; // tris.size() * 3
    const uint8_t* cornerSlot = nullptr; // tris.size() * 3
    const int16_t* heights = nullptr;   // MicroMesh::disp (read at tri * samples)
    const uint8_t* mask = nullptr;      // MicroMesh::mask, or null
    glm::vec3 center{0.0f}, viewDir{0.0f, 0.0f, -1.0f}, alphaUp{0.0f, 1.0f, 0.0f};
    float radius = 0.1f, strength = 0.5f, focal = 0.0f;
    bool invert = false;
    bool needPlane = true; // the brush reads the area plane (found on the GPU from every second grid point)
};
struct MicroDabResult {
    glm::vec3 areaNormal{0.0f, 0.0f, 1.0f}, areaCenter{0.0f};
    // Per grid point of each triangle (tris.size() * samples, in `tris` order): the change in height units,
    // rounded as the CPU rounds (half away from zero), mask applied.
    std::vector<int16_t> delta;
};
// Move along the normals on a micro level (MicroStroke::grab): the CPU captured each coarse point's weight and
// normal once; each drag gives the coarse changes, which the GPU spreads over every grid point.
struct MicroGrabJob {
    uint32_t n = 1, samples = 3, stride = 1;
    float step = 1e-6f;
    std::span<const uint32_t> tris;
    const float* coarse = nullptr; // tris.size() * coarseSamples: the change at each coarse point (world units)
    const int16_t* heights = nullptr;
    const uint8_t* mask = nullptr;
};

class BrushCompute {
public:
    virtual ~BrushCompute() = default;
    // "GPU (OpenGL 4.3 compute)": for the status bar, the hardware profile and the benchmark.
    virtual std::string name() const = 0;
    // Each returns false when it can't run this dab (the caller then runs it on the CPU); out is then undefined.
    virtual bool meshDab(const MeshDabJob& job, MeshDabResult& out) = 0;
    virtual bool microDab(const MicroDabJob& job, MicroDabResult& out) = 0;
    virtual bool microGrab(const MicroGrabJob& job, std::vector<int16_t>& delta) = 0;
    // Statistics since the last reset (the benchmark): dabs run on the GPU, and where their time went (ms).
    struct Stats {
        uint64_t dabs = 0, points = 0;
        double uploadMs = 0, kernelMs = 0, readbackMs = 0;
    };
    Stats stats;
};

// The installed implementation, or null (CPU brushes). The app sets it on the thread that owns the GPU context;
// implementations refuse dabs from any other thread.
BrushCompute* brushCompute();
void setBrushCompute(BrushCompute* compute);
// Brushes use the installed implementation only while this is on (Preferences > Performance > GPU brushes, the
// SF_GPU_BRUSHES environment variable; tests flip it to compare both paths).
bool gpuBrushesEnabled();
void setGpuBrushesEnabled(bool on);
// The installed implementation when enabled, else null: what a brush asks before each dab.
inline BrushCompute* activeBrushCompute() { return gpuBrushesEnabled() ? brushCompute() : nullptr; }
// Dabs over fewer points than this run on the CPU (a GPU round trip costs more than they do). 16384 by default,
// SF_GPU_MIN_POINTS overrides; tests set 0.
size_t gpuMinPoints();
void setGpuMinPoints(size_t points);
// "GPU (OpenGL 4.3 compute)" or "CPU": where dabs run now.
std::string brushPathName();

// Grid point (i, j) of each index of an n-grid (MicroMesh::index order), packed i | j << 16. Cached per n.
const std::vector<uint32_t>& microGridCoords(uint32_t n);

} // namespace sf
