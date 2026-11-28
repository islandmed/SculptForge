// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Voxel toolkit shared by the voxel remesher (merge parts and
// even out topology), splat-to-mesh conversion and future boolean tools:
// rasterise a surface "shell" into a grid, close small gaps, flood-fill the
// outside to get a watertight solid, then extract a smooth surface with
// surface nets.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sf {

struct VoxelGrid {
    glm::ivec3 dims{0};
    glm::vec3 origin{0.0f}; // world position of sample (0,0,0)
    float cell = 1.0f;      // sample spacing
    std::vector<float> values;

    size_t index(int x, int y, int z) const { return (size_t(z) * size_t(dims.y) + size_t(y)) * size_t(dims.x) + size_t(x); }
    size_t size() const { return size_t(dims.x) * size_t(dims.y) * size_t(dims.z); }
    glm::vec3 position(int x, int y, int z) const { return origin + glm::vec3(x, y, z) * cell; }
};

// A zeroed grid covering `box` with `resolution` cells along its longest side
// and `padding` empty cells around it. Returns false if the grid would exceed
// `maxCells`.
bool makeVoxelGrid(const AABB& box, int resolution, int padding, VoxelGrid& grid, size_t maxCells = 400'000'000);

// Converts a shell (cells with value > threshold) into a watertight solid:
// gaps up to `closeRadius` cells are closed, the outside is flood-filled from
// the grid border, and only the largest solid piece is kept (drops floaters)
// unless `keepAllParts`. Returns 1 for inside cells, 0 outside.
std::vector<uint8_t> solidifyShell(const VoxelGrid& shell, float threshold, int closeRadius, bool keepAllParts = false);

// Smooth surface of a solid occupancy grid (blurred, iso 0.5, surface nets),
// followed by `smoothIterations` of Taubin smoothing. Triangles face outwards, in quad pairs.
Mesh meshFromSolid(const VoxelGrid& grid, const std::vector<uint8_t>& solid, int smoothIterations);

// Surface where `field` crosses `iso` (samples above it are inside), surface nets,
// then `smoothIterations` of Taubin smoothing. Triangles face outwards, in quad pairs (Mesh::quadPartners).
Mesh meshFromField(const VoxelGrid& grid, const std::vector<float>& field, float iso, int smoothIterations);

// The clean-up the remesh does after the surface nets (tests): drops the loose pieces of `m` that are under 64 triangles, under 64 voxels
// of volume (at `resolution` across) or have no thickness, unless they are a small shell of `source` itself; keeps everything when no
// bigger piece is left.
void dropVoxelSpecks(Mesh& m, const Mesh& source, int resolution);

// Signed distance in cells from each sample to `mesh` (positive inside, by `solid`; exact within `band` cells of the
// surface, +-band beyond).
std::vector<float> insideDistanceField(const Mesh& mesh, const VoxelGrid& grid, const std::vector<uint8_t>& solid, float band);

// Marks the shell of a triangle mesh in `grid` (value 1 where the surface passes).
void rasterizeMeshShell(const Mesh& mesh, VoxelGrid& grid);

// Voxel Remesh resolutions (voxels across the longest side). Up to kVoxelDenseMax the grid is dense; above it only
// bricks near the surface are stored (VoxelSparse.cpp), so memory goes with the surface area, not the volume.
constexpr int kVoxelResolutionMin = 32, kVoxelDenseMax = 512, kVoxelResolutionMax = 4096;
constexpr int kVoxelResolutionDefault = 200; // (the panel's and the remesh command's)
// A remeshed surface in the app, per triangle (as MemoryGuard counts a mesh: arrays, adjacency, BVH, GPU, undo).
constexpr double kVoxelResultBytesPerTriangle = 150.0;
// Peak memory of the triangle tree transferSmoothColors builds over the old surface, per old triangle (measured: 21M
// triangles took 1.6 GB). Counted in estimateVoxelRemesh only when the paint is interpolated (a painted Part whose
// result is denser than it was; a denser Part keeps the nearest-vertex copy, which needs no tree).
constexpr double kVoxelPaintBvhBytesPerTriangle = 80.0;

// The Resolution slider's travel (0..1): two thirds for 32-512, a sixth for 528-2048 (steps of 16), a sixth for
// 2112-4096 (steps of 64), so the everyday range keeps its precision.
int voxelResolutionFromSlider(float t);
float voxelSliderFromResolution(int resolution);

// Total area of the mesh's triangles.
double surfaceArea(const Mesh& mesh);
// About what voxelRemesh makes at `resolution`: triangles (from the surface area) and the memory the remesh and its
// result take. `area`: surfaceArea(mesh) when known (the panel keeps it), negative = work it out.
struct VoxelRemeshEstimate {
    size_t triangles = 0;
    size_t bytes = 0;
};
VoxelRemeshEstimate estimateVoxelRemesh(const Mesh& mesh, int resolution, double area = -1.0);

// Tests: above this resolution voxelRemesh uses the brick grid (default kVoxelDenseMax).
void setVoxelSparseAbove(int resolution);
// Tests: whether voxelRemesh sharpens creases finer than its voxels (default on; Voxel.cpp snapCreases, gh#240).
void setVoxelCreaseSnap(bool on);

// Rebuilds the mesh as an even, watertight surface (`resolution` cells across
// its longest side). Overlapping parts are merged. Sculpt layers are baked. Open borders (a neck, a cut) are capped
// first; a mesh that still isn't solid gives an empty result and `error` says why (never a surface full of holes).
// Mask and colours transfer from the nearest original vertex; face groups,
// hidden flags and UVs reset (all visible, group 0, no UVs); the original's
// quads are dropped and the result is all quads of its own. `progress` (0..1) returning false cancels (empty result).
// `keepGroups` (Keep Face Groups): each new quad takes the face group of the original surface nearest its centre
// (transferFaceGroups below), so the groups come through with crisp borders.
// Parallel and narrow-band throughout: well under a second at the default resolution; run it off the UI thread.
// Resolutions 32-4096; a remesh that won't fit in the free memory gives an empty result and `error` says what it needs
// and which Resolution would fit. `allowSwap`: the person agreed to one that is past the free memory but within RAM +
// swap (core/MemoryGuard.h, MemoryTier::NeedsSwap); past that it still refuses. Running out of memory midway
// (bad_alloc) is caught: an empty result, `error` says so, the source is never touched.
// `notes` (optional): what the remesh noticed. creaseResolution > 0 when a good share of the creases are finer than
// two voxels (the crease pass couldn't follow them): the Resolution that makes them two voxels wide, when it fits the
// free memory (0 otherwise, also when the result is fine), with about how many triangles it makes.
struct VoxelRemeshNotes {
    int creaseResolution = 0;
    size_t creaseTriangles = 0;
};
Mesh voxelRemesh(const Mesh& mesh, int resolution, std::string* error = nullptr,
                 const std::function<bool(float)>& progress = {}, bool allowSwap = false, bool keepGroups = false,
                 VoxelRemeshNotes* notes = nullptr);
// The start of the status line for `notes` ("Resolution 512 keeps fine creases (about 1.2M triangles). ", then
// "Remeshed to ..."), or "": first, so the number shows when a long message is cut short.
std::string voxelRemeshNote(const VoxelRemeshNotes& notes);

// Face groups of `source` carried onto a rebuilt surface `dst` (Keep Face Groups): each polygon of `dst` (a quad pair
// from Mesh::quadPartners, else a triangle) takes the group of the source triangle nearest its centre, then isolated
// polygons whose edge neighbours all agree on another group take that one (no salt and pepper along a border; a border
// stays a hard line, one group per polygon). Nothing happens when `source` has no face groups. Safe to call from a
// worker thread (read-only on `source`).
void transferFaceGroups(const Mesh& source, Mesh& dst);

// Mask and vertex paint onto a rebuilt surface (what Voxel Remesh does): the mask from the nearest old vertex; the paint
// interpolated (transferSmoothColors) when the new surface has more triangles than the old one, else from the nearest old
// vertex, which needs no tree (a source that dense is sampled finely enough, and its tree would cost 80 bytes a triangle).
void transferRemeshPaint(const Mesh& source, Mesh& dst);
// Whether transferRemeshPaint interpolates, for a result of `resultTriangles`.
bool remeshInterpolatesPaint(const Mesh& source, size_t resultTriangles);

// Vertex paint onto a rebuilt surface: each vertex of `dst` takes the colour at the closest point of `source`'s surface,
// interpolated across that triangle (the nearest-vertex copy of transferVertexAttributes steps in blocks wherever the new
// surface is denser than the old one, so a paint edge came out jagged). Nothing if `source` has no paint.
void transferSmoothColors(const Mesh& source, Mesh& dst);

// What a remesh does to vertex paint, for the status line: "" when the Part has none, else how many vertices carry it and,
// when the new surface has fewer, that paint finer than its vertices is softened.
std::string vertexPaintNote(const Mesh& source, const Mesh& result);

} // namespace sf
