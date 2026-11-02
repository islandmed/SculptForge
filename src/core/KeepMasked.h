// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keep masked (#43): Voxel Remesh, Decimate and QRemesh can leave the masked part of a mesh exactly as it was
// and rebuild only the rest. Decimate does it by never collapsing a masked vertex (core/Decimate.h); the
// remeshers rebuild the surface, then the masked triangles are put back in place of the new surface there and
// the two are joined by a strip of triangles along each edge of the masked area, so the result stays closed.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

// A vertex counts as masked for Keep masked from this mask value on.
inline constexpr float kKeepMaskedThreshold = 0.5f;

// Per triangle: 1 when all three corners are masked (the triangles Keep masked keeps). Empty without a mask.
std::vector<uint8_t> keepMaskedTriangles(const Mesh& mesh);

// `source` without its kept triangles (keep[t] != 0): the open piece a remesher can rebuild on its own.
Mesh unmaskedPart(const Mesh& source, const std::vector<uint8_t>& keep);

// Puts the kept triangles of `source` (keep[t] != 0, positions, mask, paint, groups and UVs unchanged) into
// `remeshed` (a rebuild of the whole mesh or of its unmasked part): the remeshed surface over and next to
// them is cut away and each edge loop of the kept area is bridged to the cut's matching edge loop with a
// strip of triangles. Sculpt layers, levels and paint layers are dropped (the remeshers bake them). Spots where
// the new surface meets itself (voxel remeshing leaves them where parts of a model touch) are fine. A mask edge
// the stitch can't follow is tidied and tried again (#67): triangles where the edge touches itself and masked
// spots smaller than the new polygons are remeshed, narrow gaps between masked areas are kept; `note` (when
// given) then says so, else it is cleared. Fails, saying what to change, when even that can't be joined up;
// `out` is then untouched.
bool stitchKeptTriangles(const Mesh& source, const std::vector<uint8_t>& keep, const Mesh& remeshed, Mesh& out,
                         std::string& error, std::string* note = nullptr);

} // namespace sf
