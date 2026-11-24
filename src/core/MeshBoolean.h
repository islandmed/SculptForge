// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Booleans between meshes (Parts): Union, Subtract and Intersect.
//
// The exact path cuts both surfaces along the curves where they cross and keeps the pieces each operation wants,
// read off which side of the other surface each piece lies on. Intersection points are keyed by the edge of one mesh
// and the triangle of the other it crosses, so both sides of every cut share them: the result is watertight when the
// operands are, and away from the cut it is the operands' own triangles, attributes and all. Decisions run on
// orientation tests; a test too close to call (coplanar faces, edges that touch) nudges the second operand by a tiny
// random amount and starts again, while the output keeps the original positions (a coplanar face then comes out as
// one face, its zero-area slivers cleaned away).
//
// The voxel path rasterises both operands into one grid and meshes the combined signed distance: always a closed
// surface, also from open or self-intersecting input, but remeshed (even quads, no UVs or face groups) at a grid
// resolution. booleanMeshes uses it only when the exact path can't run and the caller allows it, and says so.
#pragma once

#include "core/Mesh.h"

#include <string>

namespace sf {

class CommandRegistry;

enum class BooleanOp : uint8_t { Union, Subtract, Intersect };
const char* booleanOpName(BooleanOp op); // "union", "subtract", "intersect"
bool parseBooleanOp(const std::string& name, BooleanOp& out);

struct BooleanOptions {
    // Seams: the result's triangles share the vertices along the cut (one closed surface). Off: each operand's surface
    // keeps its own copy of them (a hard edge in the shading; the surface is open along the seam).
    bool weld = true;
    // When the exact path can't run (an operand isn't closed, or intersects itself), use the voxel path instead of
    // failing.
    bool voxelFallback = true;
    int voxelResolution = 256; // the voxel path's cells across the operands' longest side (32-512)
};

struct BooleanInfo {
    bool voxel = false;     // the voxel path made the result
    std::string exactError; // with `voxel`: why the exact path couldn't run
    size_t cutTriangles = 0; // exact: operand triangles split along the cut
    int attempts = 0;        // exact: runs it took (each after a nudge)
};

// The exact boolean of `a` and `b` (Subtract: a minus b), both in the same space. Each output triangle keeps the face
// group of the triangle it came from (`b`'s groups as they are: offset them first to keep them apart), vertex paint
// and UVs carry over where either has them, mask, hidden faces, layers and levels don't (the result is plain).
// False with `error` when the operands aren't closed, intersect themselves, or the result would be empty.
bool booleanExact(const Mesh& a, const Mesh& b, BooleanOp op, Mesh& out, std::string& error,
                  const BooleanOptions& options = {}, BooleanInfo* info = nullptr);

// The voxel boolean of `a` and `b` at `resolution` (32-512). False with `error` when an operand has holes too big
// to close or the result is empty.
bool booleanVoxel(const Mesh& a, const Mesh& b, BooleanOp op, int resolution, Mesh& out, std::string& error);

// booleanExact, then (with options.voxelFallback, when the exact path couldn't run on this input) booleanVoxel.
bool booleanMeshes(const Mesh& a, const Mesh& b, BooleanOp op, Mesh& out, std::string& error,
                   const BooleanOptions& options = {}, BooleanInfo* info = nullptr);

// Enclosed volume of a closed mesh (positive when its triangles face outwards).
double meshVolume(const Mesh& mesh);

} // namespace sf
