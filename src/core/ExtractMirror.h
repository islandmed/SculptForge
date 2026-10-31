// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two ZBrush mesh tools:
//  - Extract (Tool > SubTool > Extract): the masked part of a SubTool becomes a new SubTool, a shell with a
//    thickness (a helmet, a strap, a piece of clothing). Extract shows it as a preview; Accept keeps it as an
//    undoable step. Thick is a fraction of the SubTool's size (negative = inward), E Smt smooths the jagged edge
//    the mask's triangles leave, Double grows the shell both ways; Thick 0 gives a single-sided surface. The rim
//    is quads, except at sharp corners with TCorne and all along it with TBorder.
//  - Mirror And Weld (Tool > Geometry > Modify Topology): the half on the + side of a plane through the origin
//    is kept and mirrored onto the - side, and the seam welded into one watertight mesh. Triangles crossing the
//    plane are cut on it; polygroups, polypaint, mask, UVs and sculpt layers are carried over.
//  - Mirror (Tool > Deformation): flips the SubTool across a plane (normals stay outward).
#pragma once

#include "core/Mesh.h"

#include <string>

namespace sf {

class CommandRegistry;

struct ExtractOptions {
    float thickness = 0.015f; // fraction of the SubTool's largest side; < 0 grows inward, 0 = a single surface
    int smooth = 5;           // E Smt: smoothing passes along the edge of the extracted part (0-50)
    bool doubleSided = false; // Double: the shell grows both ways from the surface by the thickness
    bool triangleCorners = false; // TCorne: the rim keeps triangles (not quads) where the edge turns sharply
    bool triangleBorder = false;  // TBorder: the whole rim is triangles
    float threshold = 0.5f;   // a triangle is extracted when its corners' mean mask reaches this
};

// The shell made from the masked, visible triangles of `mesh`: the outer surface (polygroup 1), the inner one
// (polygroup 2) and the rim joining them (polygroup 3, quads), with the polypaint of the source. A single
// surface (polygroup 1) when the thickness is 0. Empty (with `error`) when nothing is masked.
Mesh extractShell(const Mesh& mesh, const ExtractOptions& options, std::string& error);

// Mirror And Weld across the plane `axis` = 0 (X), 1 (Y) or 2 (Z) through the origin, keeping the + side (or
// the - side when !keepPositive). Empty (with `error`) when nothing is on the kept side.
Mesh mirrorAndWeld(const Mesh& mesh, int axis, bool keepPositive, std::string& error);

// The mesh flipped across the plane `axis` through `pivot` (winding reversed, so it stays outward-facing).
Mesh mirrorFlip(const Mesh& mesh, int axis, float pivot);

// extract, extract_accept, extract_cancel, mirror_and_weld, mirror.
void registerExtractMirrorCommands(CommandRegistry& registry);

} // namespace sf
