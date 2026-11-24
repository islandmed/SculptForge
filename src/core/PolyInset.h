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
// mode every polygon is inset by itself. Border vertices get one new vertex per fan of selected faces around them
// (a vertex where two pieces touch at a corner gets two), so the result is as manifold as the input: the inner
// faces keep their triangle indices, face groups and UVs (re-derived inside each original triangle), the rim quads
// take the face group of the face they came from, and the edge they replace keeps its crease and UV seam. The
// thickness is clamped per piece so that no inner face is turned inside out.
#pragma once

#include "core/Mesh.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

struct InsetOptions {
    float thickness = 0.1f;   // how far the border moves inward, in scene units (negative: outward, within the faces around)
    float depth = 0.0f;       // how far the inner faces move along their normal (negative: into the surface)
    bool individual = false;  // every polygon by itself instead of every connected piece as one shape
    bool rimGroup = false;    // the rim quads join one new face group instead of their faces' groups
};

struct InsetResult {
    bool ok = false;
    std::string error;                  // when !ok: what was wrong with the selection, in plain words
    Mesh mesh;                          // the result (topology and normals built)
    std::vector<uint32_t> selection;    // the inner faces: the selected triangles, same indices (quad partners included)
    std::vector<uint32_t> rim;          // the rim triangles (new, appended after the original ones)
    uint32_t regions = 0;               // pieces inset (polygons in individual mode)
    uint32_t newVertices = 0;
    float thickness = 0.0f;             // the largest thickness actually used (smaller than asked when clamped)
    bool clamped = false;               // some piece was too small for the thickness asked and got less
};

// Insets `triangles` of `mesh` (any order, duplicates and the other half of a selected quad are fine). Needs the
// mesh's topology (as every Part has) for nothing: it works from the triangles alone.
InsetResult insetFaces(const Mesh& mesh, std::span<const uint32_t> triangles, const InsetOptions& options);

// The thickness at which the smallest selected piece would collapse (its inner faces meet): a UI's useful slider
// range. 0 when nothing can be inset.
float insetMaxThickness(const Mesh& mesh, std::span<const uint32_t> triangles, bool individual);

} // namespace sf
