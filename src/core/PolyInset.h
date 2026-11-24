// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inset faces (Polygon mode): shrinks a selection of faces inward by a thickness, pushes the inner faces along the
// normal by a depth, and fills the gap between the old border and the new one with a rim of quads. Pure: the mesh
// and a selection in, a new mesh and the new selection out; Polygon mode wraps it with preview, apply and undo.
//
// Faces are the mesh's polygons: a triangle, or two triangles paired as a quad (Mesh::quadPartners), whose
// diagonal is never a border. In region mode every edge-connected piece of the selection is inset as one shape
// (its border is where it meets unselected faces, an open edge of the mesh or a non-manifold edge); in individual
// mode every polygon is inset by itself. The border is offset as a whole: border vertices get one new vertex per fan
// of selected faces around them (a vertex where two pieces touch at a corner gets two), vertices whose border edge
// has no length left at that thickness merge into their neighbour (so a deep inset of a gridded side flows the
// vertices beside each corner into the corner instead of folding the rim), and the vertices inside a piece follow
// the border harmonically, so a panel shrinks evenly. The result is as manifold as the input: the inner faces keep
// their face groups and UVs (re-derived inside each original triangle), the rim quads take the face group of the
// face they came from, and the edge they replace keeps its crease and UV seam. The thickness is held a fifth short
// of the point where a piece's inner faces would meet (the collapse), so a held inset is still faces and quads, never
// a fan of slivers; the Polygon mode slider asks for a fraction of that most (InsetOptions::relative).
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

struct PolyOpInput;
struct PolyOpOutput;
class PolyOpRegistry;

struct InsetOptions {
    float thickness = 0.1f;   // how far the border moves inward, in scene units (negative: outward, within the faces around);
                              // with `relative`: a share -1..1 of the most the faces can take (1: a fifth short of collapse)
    bool relative = false;    // thickness as a share of the collapse thickness x the cap (the slider's unit): of the whole
                              // selection (its smallest piece: every piece gets the same rim), or of each polygon's own
                              // with `individual`
    float depth = 0.0f;       // how far the inner faces move along their normal (negative: into the surface)
    bool individual = false;  // every polygon by itself instead of every connected piece as one shape
    bool rimGroup = false;    // the rim quads join one new face group instead of their faces' groups
};

struct InsetResult {
    bool ok = false;
    std::string error;                  // when !ok: what was wrong with the selection, in plain words
    Mesh mesh;                          // the result (topology and normals built)
    std::vector<uint32_t> selection;    // the inner faces, in the result's numbering (the same indices as the input's
                                        // unless a face collapsed into the border and is gone; quad partners included)
    std::vector<uint32_t> rim;          // the rim triangles (new, after the original ones; a quad's two halves adjacent)
    uint32_t regions = 0;               // pieces inset (polygons in individual mode)
    uint32_t newVertices = 0;
    float thickness = 0.0f;             // the largest thickness actually used, in scene units (smaller than asked when clamped)
    float maxThickness = 0.0f;          // with options.relative: the smallest piece's collapse thickness (insetMaxThickness), else 0
    bool clamped = false;               // some piece was too small for the thickness asked and got less (held at the cap)
};

// Insets `triangles` of `mesh` (any order, duplicates and the other half of a selected quad are fine). Needs the
// mesh's topology (as every Part has) for nothing: it works from the triangles alone.
InsetResult insetFaces(const Mesh& mesh, std::span<const uint32_t> triangles, const InsetOptions& options);

// The thickness at which the smallest selected piece collapses (its inner faces meet), in scene units; the slider's
// unit (times insetCollapseCap()). 0 when nothing can be inset.
float insetMaxThickness(const Mesh& mesh, std::span<const uint32_t> triangles, bool individual);
// The fraction of the collapse thickness an inset never passes (0.8: the inner faces keep a fifth of their size).
float insetCollapseCap();

// The Polygon mode op (core/PolyOps.h): `poly_op op=inset thickness=0.25 depth=0 individual=false rim_group=false`,
// thickness as a fraction 0..1 of the most the selected faces can take (so a short slider drag is a visible, sane
// inset on a Part of any density, and the end of the slider is as deep as it goes), depth as a fraction of the Part's
// size (its bounding box diagonal).
bool polyInset(const PolyOpInput& in, PolyOpOutput& out, std::string& error);
void registerPolyInsetOp(PolyOpRegistry& reg);

} // namespace sf
