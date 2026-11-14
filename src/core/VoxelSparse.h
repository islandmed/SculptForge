// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The voxel remesh's grid steps above resolution 512 (core/VoxelSparse.cpp), shared helpers with core/Voxel.cpp.
#pragma once

#include "core/Voxel.h"

namespace sf {

// Closest point to `p` on triangle abc.
glm::vec3 closestOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

// The surface voxelRemesh builds from `mesh` (closed, or capped) at `resolution`: the same shell, gap closing, outside
// flood, narrow-band distance (`band` cells), sign and surface nets as the dense grid (Voxel.cpp), on a grid stored
// only in 8^3 bricks near the surface, so memory follows the surface area instead of the volume. `checkClosed`: an
// open mesh whose solid stays a thin shell gives an empty result and `error`. `step` (0..1) returning false cancels;
// a grid larger than the free memory gives an empty result and `error` says how much it needs (`allowSwap`: unless it
// is within RAM + the most swap, MemoryTier::NeedsSwap, and the person agreed).
Mesh sparseVoxelSurface(const Mesh& mesh, int resolution, int padding, float band, bool checkClosed, std::string* error,
                        const std::function<bool(float)>& step, bool allowSwap = false);

} // namespace sf
