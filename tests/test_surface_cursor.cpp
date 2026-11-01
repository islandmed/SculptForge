// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brush cursor on the surface (core/SurfaceCursor.h, #25).
#include "TestHarness.h"

#include "core/BVH.h"
#include "core/Primitives.h"
#include "core/SurfaceCursor.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace sf;

TEST(surface_cursor_ring_lies_on_sphere) {
    Mesh m = makeIcosphere(5, 1.0f);
    BVH bvh;
    bvh.build(m);
    RayHit hit;
    CHECK(bvh.raycast(m, glm::vec3(0.2f, 0.3f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f), hit));
    // The steady normal is the sphere's (radial) there, not just the hit triangle's.
    const glm::vec3 n = surfaceCursorNormal(m, bvh, hit, 0.3f);
    CHECK(glm::dot(n, glm::normalize(hit.position)) > 0.999f);
    std::vector<SurfaceRingPoint> ring;
    surfaceCursorRing(m, bvh, hit.position, n, 0.3f, 48, ring);
    CHECK(ring.size() == 48);
    for (const SurfaceRingPoint& p : ring) {
        CHECK(p.onSurface);
        // On the (faceted) sphere, not in the flat plane touching it.
        CHECK(std::abs(glm::length(p.position) - 1.0f) < 0.01f);
        // About the brush radius from the centre (pressed down onto the curve, a little further).
        const float d = glm::length(p.position - hit.position);
        CHECK(d > 0.299f && d < 0.31f);
        CHECK(glm::dot(p.normal, glm::normalize(p.position)) > 0.99f);
    }
}

TEST(surface_cursor_ring_flat_off_edge) {
    // Near the edge of a plane: points past the edge stay in the ring's plane, marked off the surface.
    Mesh m = makePlane(16, 2.0f);
    BVH bvh;
    bvh.build(m);
    const AABB b = m.bounds();
    glm::vec3 up(0.0f);
    const glm::vec3 ext = b.max - b.min;
    up[ext.x < ext.y && ext.x < ext.z ? 0 : ext.y < ext.z ? 1 : 2] = 1.0f; // the plane's flat axis
    const glm::vec3 c = (b.min + b.max) * 0.5f + ext * 0.45f * (glm::vec3(1.0f) - up);
    RayHit hit;
    CHECK(bvh.raycast(m, c + up * 3.0f, -up, hit));
    std::vector<SurfaceRingPoint> ring;
    surfaceCursorRing(m, bvh, hit.position, interpolatedNormal(m, hit), 0.5f, 32, ring);
    int on = 0, off = 0;
    for (const SurfaceRingPoint& p : ring) (p.onSurface ? on : off)++;
    CHECK(on > 0 && off > 0);
}

TEST(surface_cursor_cost_is_rays_not_mesh_passes) {
    // A cursor update is ~90 ray casts: on a 5M-triangle sphere it stays well under a millisecond (it walks the
    // BVH, never the mesh). Generous bound so slow CI machines and debug builds pass.
    Mesh m = makeIcosphere(9, 1.0f);
    BVH bvh;
    bvh.build(m);
    std::vector<SurfaceRingPoint> ring, inner;
    const int n = 200;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        RayHit hit;
        CHECK(bvh.raycast(m, glm::vec3(-0.5f + float(i) / float(n), 0.2f, 5.0f), glm::vec3(0, 0, -1), hit));
        const glm::vec3 nn = surfaceCursorNormal(m, bvh, hit, 0.3f);
        surfaceCursorRing(m, bvh, hit.position, nn, 0.3f, 48, ring);
        surfaceCursorRing(m, bvh, hit.position, nn, 0.15f, 32, inner);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
    std::printf("  surface cursor on %zu triangles: %.3f ms per update\n", m.triangles.size(), ms);
    CHECK(ms < 5.0);
}
