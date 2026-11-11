// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Clay dab on a form that curves away under it (#196): the middle of the dab was above the flat plane and stayed
// untouched, the dab lifted a ring, and the ring's edge was a crease, one per dab along a stroke.

#include "TestHarness.h"

#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/SculptKernel.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace sf;

namespace {

constexpr Symmetry kNone{false, false, false};

// One Clay dab (radius `radius`, Intensity 0.5) at the top of the form: the mean lift of the vertices in ten rings
// around the cursor, each 0.1 of the radius wide (in the plane's x and z), starting at the cursor.
std::vector<float> dabLift(Document d, float topY, float radius) {
    const std::vector<glm::vec3> before = d.mesh.positions;
    d.sculptor.beginStroke(d.mesh, "Clay");
    DabParams p;
    p.center = glm::vec3(0.0f, topY, 0.0f);
    p.viewDir = glm::vec3(0, -1, 0);
    p.radius = radius;
    p.strength = 0.5f;
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Clay, p, kNone);
    d.undo.push(d.sculptor.endStroke());
    std::vector<float> sum(10, 0.0f), n(10, 0.0f);
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i].y < topY - 0.5f * radius) continue; // (the sphere's far side)
        const size_t ring = size_t(std::hypot(before[i].x, before[i].z) / (0.1f * radius));
        if (ring >= sum.size()) continue;
        sum[ring] += d.mesh.positions[i].y - before[i].y;
        n[ring] += 1.0f;
    }
    for (size_t i = 0; i < sum.size(); ++i) sum[i] = n[i] > 0.0f ? sum[i] / n[i] : 0.0f;
    return sum;
}

// A stroke of overlapping Clay dabs (0.15 of the radius apart) over the top of the form, each dab's lift measured on the
// surface the dabs before it left: the mean move of the vertices in ten rings around its cursor (0.1 of the radius
// wide, by distance), as an invariant of every dab: no bare middle, and no wall (a ring's edge) in the profile.
struct StrokeCheck {
    int bareMiddles = 0, walls = 0, dabs = 0;
    float worstWall = 0.0f; // (the biggest step outward, as a share of that dab's top)
};
StrokeCheck checkStroke(Document d, float radius) {
    StrokeCheck out;
    d.sculptor.beginStroke(d.mesh, "Clay");
    const float arc = 0.15f * radius; // (the dabs' spacing along the surface)
    for (int i = -5; i <= 5; ++i) {
        const float a = float(i) * arc;
        DabParams p;
        const glm::vec3 up(std::sin(a), std::cos(a), 0.0f);
        p.center = up;
        if (RayHit hit; d.bvh.raycast(d.mesh, up * 3.0f, -up, hit)) { // (picked on the surface the dabs before it left)
            p.center = hit.position;
            p.pickNormal = glm::normalize(hit.normal);
        }
        p.viewDir = -up;
        p.radius = radius;
        p.strength = 0.5f;
        p.strokeDelta = glm::vec3(std::cos(a), -std::sin(a), 0.0f) * arc;
        const std::vector<glm::vec3> before = d.mesh.positions;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Clay, p, kNone);
        std::vector<float> sum(10, 0.0f), n(10, 0.0f);
        for (size_t v = 0; v < before.size(); ++v) {
            const size_t ring = size_t(glm::distance(before[v], p.center) / (0.1f * radius));
            if (ring >= sum.size()) continue;
            sum[ring] += glm::length(d.mesh.positions[v] - before[v]);
            n[ring] += 1.0f;
        }
        float top = 0.0f;
        for (size_t r = 0; r < sum.size(); ++r) sum[r] = n[r] > 0.0f ? sum[r] / n[r] : 0.0f, top = std::max(top, sum[r]);
        ++out.dabs;
        for (size_t r = 1; r < 7; ++r)
            if (!(sum[r] > 0.0f)) ++out.bareMiddles;
        float wall = 0.0f;
        for (size_t r = 0; r + 1 < sum.size(); ++r) wall = std::max(wall, (sum[r + 1] - sum[r]) / std::max(top, 1e-9f));
        out.worstWall = std::max(out.worstWall, wall);
        if (wall >= 0.35f) ++out.walls;
    }
    d.undo.push(d.sculptor.endStroke());
    return out;
}

} // namespace

TEST(clay_stroke_invariant_no_ring_walls_across_overlapping_dabs) {
    // A dense unit sphere under a brush half its size: every dab of the stroke lifts a mound, not a ring.
    const StrokeCheck c = checkStroke(makeDoc(makeIcosphere(7, 1.0f)), 0.5f);
    std::printf("  %d dabs: %d with a wall, %d bare middles, worst step %.0f%% of a dab's top\n", c.dabs, c.walls, c.bareMiddles,
                100.0 * double(c.worstWall));
    CHECK(c.dabs == 11);
    CHECK(c.walls == 0);
    CHECK(c.bareMiddles == 0);
}

TEST(clay_dab_has_no_wall_on_a_curved_form) {
    const float radius = 0.5f;
    // A unit sphere under a brush half its size: the middle of the dab stands above the mean of the area by more than
    // the plane's standoff. The middle stayed untouched (the plane was below it) and the dab lifted a ring with a
    // steep inner wall, where the plane met the surface: a crease per dab along a stroke.
    const std::vector<float> sphere = dabLift(makeDoc(makeIcosphere(6, 1.0f)), 1.0f, radius);
    float top = 0.0f;
    for (float v : sphere) top = std::max(top, v);
    CHECK(top > 0.0f);
    for (size_t i = 1; i < 7; ++i) CHECK(sphere[i] > 0.0f); // (no bare middle)
    for (size_t i = 0; i + 1 < sphere.size(); ++i) CHECK(sphere[i + 1] - sphere[i] < 0.35f * top); // (no wall)
    // On a plane the dab is the disc it always was.
    const std::vector<float> flat = dabLift(makeDoc(makePlane(96, 4.0f)), 0.0f, radius);
    for (size_t i = 0; i < 6; ++i) CHECK(flat[i] > 0.0f);
    CHECK(flat[0] > flat[5]);
    // The lift at the middle is no more than the plane's standoff times the pull (r 0.05 s, 0.5 here).
    CHECK(sphere[0] <= flat[0] * 1.5f);
}

TEST(clay_plane_is_unchanged_on_a_flat_surface) {
    PlaneKernel k;
    k.type = BrushType::Clay;
    k.center = glm::vec3(0.0f);
    k.areaCenter = glm::vec3(0.0f);
    k.areaNormal = glm::vec3(0, 1, 0);
    k.r = 0.5f;
    k.s = 0.5f;
    float scale = 0.0f;
    const glm::vec3 plane = k.clayPlane(scale);
    CHECK(scale == 1.0f);
    CHECK(std::abs(plane.y - 0.5f * 0.05f * 0.5f) < 1e-7f);
    // ... and where the cursor stands above it, the plane reaches the cursor and the pull shrinks by as much.
    k.center = glm::vec3(0.0f, 0.1f, 0.0f);
    const glm::vec3 raised = k.clayPlane(scale);
    CHECK(std::abs(raised.y - 0.1f) < 1e-7f);
    CHECK(std::abs(scale - (0.5f * 0.05f * 0.5f) / 0.1f) < 1e-6f);
    // The middle of the dab is lifted by what it is on a flat surface, from either plane.
    const float flatLift = k.offsetWith(glm::vec3(0.0f, -0.0f, 0.0f), 1.0f).y;
    CHECK(std::abs(flatLift - 0.1f * scale) < 1e-6f);
}
