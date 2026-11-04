// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The brush cursor on the surface (core/SurfaceCursor.h, #25).
#include "TestHarness.h"

#include "core/BVH.h"
#include "core/Primitives.h"
#include "core/SurfaceCursor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace sf;

TEST(surface_cursor_circle_on_sphere) {
    Mesh m = makeIcosphere(5, 1.0f);
    BVH bvh;
    bvh.build(m);
    RayHit hit;
    CHECK(bvh.raycast(m, glm::vec3(0.2f, 0.3f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f), hit));
    // The steady normal is the sphere's (radial) there, not just the hit triangle's.
    const glm::vec3 n = surfaceCursorNormal(m, bvh, hit, 0.3f);
    CHECK(glm::dot(n, glm::normalize(hit.position)) > 0.999f);
    // A true circle (#88): every point exactly the brush radius from the centre, in the tangent plane.
    std::vector<glm::vec3> ring;
    surfaceCursorCircle(hit.position, n, 0.3f, 48, ring);
    CHECK(ring.size() == 48);
    for (const glm::vec3& p : ring) {
        CHECK(std::abs(glm::length(p - hit.position) - 0.3f) < 1e-4f);
        CHECK(std::abs(glm::dot(p - hit.position, n)) < 1e-4f);
    }
}

namespace {

// A plane with fine bumps all over it (a rough, detailed surface), its flat axis Y.
Mesh bumpyPlane() {
    Mesh m = makePlane(160, 2.0f);
    const AABB b = m.bounds();
    const glm::vec3 ext = b.max - b.min;
    const int up = ext.x < ext.y && ext.x < ext.z ? 0 : ext.y < ext.z ? 1 : 2;
    for (glm::vec3& p : m.positions) {
        const int a = (up + 1) % 3, c = (up + 2) % 3;
        p[up] += 0.012f * std::sin(p[a] * 157.0f) * std::cos(p[c] * 131.0f);
    }
    m.recomputeAllNormals();
    return m;
}

float degreesBetween(const glm::vec3& a, const glm::vec3& b) {
    return glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f)));
}

} // namespace

TEST(surface_cursor_steady_on_bumps) {
    // #88: gliding over fine bumps, the ring's plane barely turns, though the surface right under the pointer
    // tilts a lot from one step to the next.
    Mesh m = bumpyPlane();
    BVH bvh;
    bvh.build(m);
    const AABB b = m.bounds();
    const glm::vec3 ext = b.max - b.min;
    glm::vec3 up(0.0f);
    up[ext.x < ext.y && ext.x < ext.z ? 0 : ext.y < ext.z ? 1 : 2] = 1.0f;
    const glm::vec3 side = glm::normalize(glm::vec3(1.0f) - up);
    float worstCursor = 0.0f, worstPoint = 0.0f;
    glm::vec3 prevCursor(0.0f), prevPoint(0.0f);
    for (int i = 0; i < 40; ++i) {
        const glm::vec3 at = (b.min + b.max) * 0.5f + side * (-0.3f + 0.015f * float(i)) + up * 2.0f;
        RayHit hit;
        CHECK(bvh.raycast(m, at, -up, hit));
        const glm::vec3 cursor = surfaceCursorNormal(m, bvh, hit, 0.25f), point = interpolatedNormal(m, hit);
        if (i > 0) {
            worstCursor = std::max(worstCursor, degreesBetween(cursor, prevCursor));
            worstPoint = std::max(worstPoint, degreesBetween(point, prevPoint));
        }
        CHECK(degreesBetween(cursor, up) < 6.0f); // the form's direction, not the bump's
        prevCursor = cursor;
        prevPoint = point;
    }
    std::printf("  bumps: cursor plane turns at most %.2f deg per step, the surface under it %.2f deg\n", worstCursor,
                worstPoint);
    CHECK(worstPoint > 10.0f);
    CHECK(worstCursor < 3.0f);
}

TEST(surface_cursor_cost_is_rays_not_mesh_passes) {
    // A cursor update is 33 ray casts: on a 5M-triangle sphere it stays well under a millisecond (it walks the
    // BVH, never the mesh). Generous bound so slow CI machines and debug builds pass.
    Mesh m = makeIcosphere(9, 1.0f);
    BVH bvh;
    bvh.build(m);
    std::vector<glm::vec3> ring, inner;
    const int n = 200;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        RayHit hit;
        CHECK(bvh.raycast(m, glm::vec3(-0.5f + float(i) / float(n), 0.2f, 5.0f), glm::vec3(0, 0, -1), hit));
        const glm::vec3 nn = surfaceCursorNormal(m, bvh, hit, 0.3f);
        surfaceCursorCircle(hit.position, nn, 0.3f, 48, ring);
        surfaceCursorCircle(hit.position, nn, 0.15f, 32, inner);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
    std::printf("  surface cursor on %zu triangles: %.3f ms per update\n", m.triangles.size(), ms);
    CHECK(ms < timeLimit(5.0));
}
