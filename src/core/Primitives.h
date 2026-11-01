// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Mesh.h"

namespace sf {

// Returned meshes have topology and normals built.
Mesh makeIcosphere(int subdivisions, float radius = 1.0f);
// Plane and cube are made of quads (Mesh::quadPartners).
Mesh makePlane(int resolution, float size = 2.0f);
// Closed cube from -size/2 to size/2 with `resolution` cells per edge (welded, one polygroup per side).
Mesh makeCube(int resolution, float size = 2.0f);
// Base-mesh primitives (#31), quads where possible (triangle fans only at poles and cap centres), one polygroup
// per part (caps, sides), fitting in a box of `size`, centred on the origin, axis along Y.
Mesh makeCylinder(int segments, float size = 2.0f);
Mesh makeCone(int segments, float size = 2.0f);
Mesh makeTorus(int segments, float size = 2.0f);
Mesh makeCapsule(int segments, float size = 2.0f);
// A cube pushed out onto a sphere: all quads, no poles.
Mesh makeQuadSphere(int resolution, float size = 2.0f);
// Loop subdivision (smooth; boundaries follow the cubic B-spline rule).
// Layers are subdivided with the same stencils; mask/colours/UVs are
// interpolated linearly and polygroups/hidden flags are inherited. Each quad
// (a pair in Mesh::quadPartners) becomes four quads; triangles stay triangles.
Mesh loopSubdivide(const Mesh& mesh);

} // namespace sf
