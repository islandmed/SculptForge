// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Close Holes (#103): caps the open boundaries of a Part with new polygon patches, so an extracted shell, a cut
// scan or an imported open mesh becomes closed without remeshing. Each boundary loop is triangulated (the best
// ear first, in the loop's best-fit plane; a fan around its centre when the loop folds over itself in that
// plane), then the patch is refined towards the boundary's edge length (long edges split, edges flipped to
// better-shaped triangles) and its inside vertices faired so the patch continues the curvature around it (a
// sphere's hole closes round, not flat). The rest of the Part is not touched: its vertices, triangles and
// attributes keep their order and values. Patches get a face group of their own.
#pragma once

#include "core/Mesh.h"

#include <cstddef>
#include <string>

namespace sf {

class CommandRegistry;

struct CloseHolesOptions {
    size_t maxEdges = 0;  // only holes with at most this many boundary edges (0 = every hole)
    bool refine = true;   // split and fair the patch to the density and shape around it (off: the plain triangulation)
    bool newGroup = true; // the patches get a new face group
};

struct CloseHolesStats {
    size_t holes = 0;     // boundary loops found
    size_t filled = 0;    // loops capped
    size_t skipped = 0;   // loops over maxEdges, or through a vertex shared by two loops in a way that can't be capped
    size_t triangles = 0; // triangles added
};

// The boundary loops of `mesh`: each a closed list of vertices v0 v1 ... with the mesh's edges vi -> vi+1 open
// (no triangle on their other side). Edges used by more than two triangles are left out.
std::vector<std::vector<uint32_t>> boundaryLoops(const Mesh& mesh);

// Caps the holes of `mesh` in place. Returns false with `error` when the mesh can't be changed (empty).
bool closeHoles(Mesh& mesh, const CloseHolesOptions& options, CloseHolesStats& stats, std::string& error);

// Registers "close_holes".
void registerCloseHolesCommands(CommandRegistry& registry);

} // namespace sf
