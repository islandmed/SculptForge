// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The blend of a Boolean (MeshBoolean.h, BooleanOptions::blend): a soft fillet along the seam where the two operands
// meet, the way distance-field modellers round a union or a cut, on the exact Boolean's mesh.
//
// The result's triangles near the seam are refined (conformal edge bisection, attributes interpolated, face groups
// inherited) until their edges are a fraction of the radius, then every vertex within reach is moved onto the
// surface F(p) = smin(uA(p), uB(p), k) = 0: uA and uB are the distances to the two pieces of surface that meet at the
// seam (the result's own triangles from each operand, so a piece the Boolean removed pulls nothing), smin the polynomial
// smooth minimum, and k the radius, r where the pieces cross at a right angle and fading to nothing where they run
// parallel (a flat join stays flat). Where the other piece is more than r away, F is 0 on the result's own surface, so
// faces away from the seam stay bit-identical. In a Union the fillet adds material into the crease (the vertices move
// outward); in a Subtract or Intersect it rounds the convex rim (they move inward). At an operand's own edges the
// distance is the offset-planes one, so those edges stay sharp through the band. A relaxation then evens out the
// band's triangles on the fillet.
#pragma once

#include "core/Mesh.h"
#include "core/MeshBoolean.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sf {

struct BooleanBlendOptions {
    double radius = 0.0; // scene units; 0 = sharp
    int segments = 6;    // triangles across the fillet (the band's target edge is 2 * radius / segments)
    bool relax = true;   // even out the band's triangles after the projection (off for an unwelded seam: its two copies must agree)
    size_t maxTriangles = 3000000; // the refinement stops before the result grows past this
};

struct BooleanBlendStats {
    size_t collapsed = 0; // seam slivers collapsed before the refinement
    size_t refined = 0;   // triangles the band refinement added
    size_t moved = 0;     // vertices the projection moved
    double maxMove = 0.0; // the farthest any vertex moved (<= radius)
    int passes = 0;       // refinement passes
};

// Blends `result` (the exact Boolean `op` of `a` and `b`, triangles facing out, attributes already set) along `seam`,
// the crossing segments in world space. `side[t]` says which operand result triangle t came from (0 = a, 1 = b) and
// `parent[t]` which triangle of that operand; `side` grows with the refinement. Union moves the vertices out of the
// result, Subtract and Intersect into it.
void blendBooleanSeam(Mesh& result, std::vector<uint8_t>& side, const std::vector<uint32_t>& parent, const Mesh& a,
                      const Mesh& b, BooleanOp op, const std::vector<std::array<glm::vec3, 2>>& seam,
                      const BooleanBlendOptions& options, BooleanBlendStats* stats = nullptr);

// The polynomial smooth minimum of radius k (k <= 0: the plain minimum).
double smoothMin(double a, double b, double k);

} // namespace sf
