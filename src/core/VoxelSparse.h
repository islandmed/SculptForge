// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The voxel remesh's grid steps above resolution 512 (core/VoxelSparse.cpp), shared helpers with core/Voxel.cpp.
#pragma once

#include "core/Voxel.h"

#include <functional>

namespace sf {

// Closest point to `p` on triangle abc.
glm::vec3 closestOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

// Whether voxelRemesh sharpens creases (setVoxelCreaseSnap), and the pass itself (Voxel.cpp snapCreases) on a surface
// made from a grid laid out at `origin`, `cell`, `dims`, with each sample's nearest source triangle (-1: none). Returns
// whether the triangles changed (rebuild the topology).
// `report`: the crease vertices the pass found, how many of them it couldn't bring onto the source (a crease finer
// than the voxels), and those creases' typical width (scene units).
// The least |distance| (in cells) a sample gets: one exactly on the source (the tip line of a crease on a sample plane, Resolution 2048 of
// gh#240) is inside or outside by the sign pass, but +0 reads as outside to the surface nets, which made zero-thickness membranes along it.
constexpr float kOnSurface = 1e-3f;
// The sign pass's loose-sample cleanup (Voxel.cpp and VoxelSparse.cpp): an inside sample within kLooseBand cells of the source with at most
// kLooseInside of its 6 face neighbours inside, that the nearest triangle puts outside the source, is an extra voxel and is turned out.
constexpr float kLooseBand = 3.0f;
constexpr int kLooseInside = 3; // (a wire of one voxel has 2 inside neighbours; what keeps a real fin or wire is the source's own side)

struct CreaseReport {
    size_t creases = 0, fine = 0;
    float fineWidth = 0.0f;
    size_t splitCells = 0;   // (surface-nets cells whose surface is more than one piece: two walls touching)
    size_t sealedOpened = 0; // (samples of a crease the wall closing had sealed that the sign test turned outside again)
};
// Which side of triangle `t` the point `p` is on, by the triangle's own plane at the point on it nearest to `p`: > 0 outside
// (the side its normal points to), < 0 inside.
float sideOfTriangle(const Mesh& mesh, uint32_t t, const glm::vec3& p);
bool voxelCreaseSnap();
bool snapVoxelCreases(Mesh& m, const Mesh& source, const glm::vec3& origin, float cell, const glm::ivec3& dims,
                      const std::function<int32_t(const glm::ivec3&)>& nearestAt, CreaseReport* report = nullptr);

// The surface voxelRemesh builds from `mesh` (closed, or capped) at `resolution`: the same shell, gap closing, outside
// flood, narrow-band distance (`band` cells), sign and surface nets as the dense grid (Voxel.cpp), on a grid stored
// only in 8^3 bricks near the surface, so memory follows the surface area instead of the volume. `checkClosed`: an
// open mesh whose solid stays a thin shell gives an empty result and `error`. `step` (0..1) returning false cancels;
// a grid larger than the free memory gives an empty result and `error` says how much it needs (`allowSwap`: unless it
// is within RAM + the most swap, MemoryTier::NeedsSwap, and the person agreed).
Mesh sparseVoxelSurface(const Mesh& mesh, int resolution, int padding, float band, bool checkClosed, std::string* error,
                        const std::function<bool(float)>& step, bool allowSwap = false, CreaseReport* creases = nullptr);

} // namespace sf
