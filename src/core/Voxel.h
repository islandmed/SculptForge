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

// Signed distance in cells from each sample to `mesh` (positive inside, by `solid`; exact within `band` cells of the
// surface, +-band beyond).
std::vector<float> insideDistanceField(const Mesh& mesh, const VoxelGrid& grid, const std::vector<uint8_t>& solid, float band);

// Marks the shell of a triangle mesh in `grid` (value 1 where the surface passes).
void rasterizeMeshShell(const Mesh& mesh, VoxelGrid& grid);

// Rebuilds the mesh as an even, watertight surface (`resolution` cells across
// its longest side). Overlapping parts are merged. Sculpt layers are baked. Open borders (a neck, a cut) are capped
// first; a mesh that still isn't solid gives an empty result and `error` says why (never a surface full of holes).
// Mask and colours transfer from the nearest original vertex; face groups,
// hidden flags and UVs reset (all visible, group 0, no UVs); the original's
// quads are dropped and the result is all quads of its own. `progress` (0..1) returning false cancels (empty result).
// Parallel and narrow-band throughout: well under a second at the default resolution; run it off the UI thread.
Mesh voxelRemesh(const Mesh& mesh, int resolution, std::string* error = nullptr,
                 const std::function<bool(float)>& progress = {});

} // namespace sf
