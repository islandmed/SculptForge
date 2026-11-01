// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brush cursor lying on the surface (#25), as in SculptGL: a circle of the brush's world radius around the
// point under the pointer, in the plane of the surface there, each point pressed onto the mesh so the ring follows
// bumps and curves instead of floating flat in screen space.
//
// Cost: a few dozen BVH ray casts (each O(log n)) per cursor update, nothing that walks the whole mesh, so it stays
// cheap at tens of millions of triangles. The app only calls it on frames it draws anyway (pointer moved, stroke,
// view change), so the idle view stays at 0 fps.
#pragma once

#include "core/BVH.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <vector>

namespace sf {

struct SurfaceRingPoint {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f}; // the surface's there (the ring's own where nothing was hit)
    bool onSurface = false; // pressed onto the mesh; false: left in the ring's plane (over a hole or an edge)
};

// The shading normal at a ray hit: the vertex normals blended across the hit triangle (smoother than the
// triangle's own normal on a coarse mesh); the triangle's normal if the mesh has no usable vertex normals.
glm::vec3 interpolatedNormal(const Mesh& mesh, const RayHit& hit);

// A steady normal for the cursor at `hit`: the shading normal there averaged with the normals at `samples` points
// of a ring of half the brush radius pressed onto the surface (an area normal, so the ring doesn't wobble from
// triangle to triangle as the pointer moves over fine detail). Points facing away from the centre's normal (the
// far side of a thin part) are left out.
glm::vec3 surfaceCursorNormal(const Mesh& mesh, const BVH& bvh, const RayHit& hit, float radius, int samples = 8);

// `count` points of the circle of `radius` about `center` in the plane through it with `normal`, each pressed
// onto the mesh along -normal (from up to `radius` above the plane to `radius` below it). Clears `out` first.
void surfaceCursorRing(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, const glm::vec3& normal, float radius,
                       int count, std::vector<SurfaceRingPoint>& out);

} // namespace sf
