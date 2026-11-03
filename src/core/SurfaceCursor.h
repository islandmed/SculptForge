// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brush cursor lying on the surface (#25): a true circle of the brush's world radius around the point under
// the pointer, in the tangent plane of the surface there (#88: not pressed onto the mesh point by point, which
// made the ring wobble and break up on bumpy surfaces). The plane's normal is averaged over the brush's area, so
// it turns smoothly with the form and ignores fine detail.
//
// Cost: about thirty BVH ray casts (each O(log n)) per cursor update, nothing that walks the whole mesh, so it stays
// cheap at tens of millions of triangles. The app only calls it on frames it draws anyway (pointer moved, stroke,
// view change), so the idle view stays at 0 fps.
#pragma once

#include "core/BVH.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <vector>

namespace sf {

// The shading normal at a ray hit: the vertex normals blended across the hit triangle (smoother than the
// triangle's own normal on a coarse mesh); the triangle's normal if the mesh has no usable vertex normals.
glm::vec3 interpolatedNormal(const Mesh& mesh, const RayHit& hit);

// A steady normal for the cursor at `hit`: the plane fitted through `samples` surface points on each of two rings
// (half the brush radius and the full radius) around it, fitted twice, so the ring follows the form under the brush
// and doesn't wobble from bump to bump as the pointer moves over fine detail. The shading normal there when too
// few points land (over a hole, at an edge).
glm::vec3 surfaceCursorNormal(const Mesh& mesh, const BVH& bvh, const RayHit& hit, float radius, int samples = 8);

// `count` points of the circle of `radius` about `center` in the plane through it with `normal` (a true circle:
// nothing is pressed onto the mesh). Clears `out` first.
void surfaceCursorCircle(const glm::vec3& center, const glm::vec3& normal, float radius, int count,
                         std::vector<glm::vec3>& out);

} // namespace sf
