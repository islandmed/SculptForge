// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The axis gizmo (#43): where its ends sit for a view, which end a click picks, and that each end snaps to the
// side view looking from it.
#include "TestHarness.h"

#include "app/AxisGizmo.h"
#include "render/Camera.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstring>

using namespace sf;

TEST(axis_gizmo_ends_follow_the_view) {
    Camera c; // the front view
    const auto ends = axisGizmoEnds(glm::mat3(c.view()), glm::vec2(100.0f, 100.0f), 30.0f);
    CHECK(ends[0].axis == 0 && !ends[0].negative && ends[0].at.x > 120.0f); // +X to the right
    CHECK(ends[3].axis == 0 && ends[3].negative && ends[3].at.x < 80.0f);   // -X to the left
    CHECK(ends[1].at.y < 80.0f);                                            // +Y up (screen y grows down)
    CHECK(ends[2].depth > 0.9f && ends[5].depth < -0.9f);                   // +Z towards the viewer
}

TEST(axis_gizmo_hit_prefers_the_near_end) {
    Camera c;
    const auto ends = axisGizmoEnds(glm::mat3(c.view()), glm::vec2(100.0f, 100.0f), 30.0f);
    CHECK(axisGizmoHit(ends, 131.0f, 99.0f, 9.0f) == 0); // +X
    CHECK(axisGizmoHit(ends, 70.0f, 101.0f, 9.0f) == 3); // -X
    CHECK(axisGizmoHit(ends, 100.0f, 100.0f, 9.0f) == 2); // +Z and -Z overlap in the middle: the near one
    CHECK(axisGizmoHit(ends, 115.0f, 85.0f, 9.0f) == -1);
}

TEST(axis_gizmo_ends_snap_to_the_side_they_point_at) {
    for (int i = 0; i < 6; ++i) {
        glm::vec3 axis(0.0f);
        axis[i % 3] = i < 3 ? 1.0f : -1.0f;
        const AxisGizmoView v = axisGizmoView(i % 3, i >= 3);
        CHECK(v.action != nullptr && std::strncmp(v.action, "view.", 5) == 0);
        Camera c;
        c.yaw = glm::radians(v.yaw);
        c.pitch = glm::radians(v.pitch);
        // The camera sits out along the clicked end, looking back at the centre, so that end faces the viewer.
        CHECK(glm::length(glm::normalize(c.position() - c.target) - axis) < 1e-3f);
        const auto ends = axisGizmoEnds(glm::mat3(c.view()), glm::vec2(0.0f), 30.0f);
        CHECK(ends[size_t(i)].depth > 0.99f);
    }
}

TEST(axis_gizmo_hit_works_at_every_size) {
    // Draw > Camera > Gizmo size: the ends and the click radius scale together, so each end is still the one hit.
    Camera c;
    c.yaw = glm::radians(30.0f);
    c.pitch = glm::radians(-20.0f);
    CHECK(axisGizmoScale(0) < axisGizmoScale(1) && axisGizmoScale(1) == 1.0f && axisGizmoScale(1) < axisGizmoScale(2));
    for (int size = 0; size < 3; ++size)
        for (float uiScale : {1.0f, 2.0f}) {
            const float unit = uiScale * axisGizmoScale(size);
            const auto ends = axisGizmoEnds(glm::mat3(c.view()), glm::vec2(200.0f, 200.0f), 34.0f * unit);
            for (int i = 0; i < 6; ++i) {
                const glm::vec2 p = ends[size_t(i)].at + glm::vec2(4.0f, -3.0f) * unit;
                const int hit = axisGizmoHit(ends, p.x, p.y, 9.0f * unit);
                CHECK(hit >= 0 && ends[size_t(hit)].axis == ends[size_t(i)].axis &&
                      ends[size_t(hit)].negative == ends[size_t(i)].negative);
            }
            CHECK(axisGizmoHit(ends, 200.0f + 34.0f * unit * 2.0f, 200.0f, 9.0f * unit) == -1);
        }
}

TEST(view_figure_snaps_to_the_nearest_side) {
    // A view a little off the right side snaps to the right; one mostly from above snaps to the top.
    Camera c;
    c.yaw = glm::radians(70.0f);
    c.pitch = glm::radians(-15.0f);
    int facing = axisGizmoFacing(axisGizmoEnds(glm::mat3(c.view()), glm::vec2(0.0f), 30.0f));
    CHECK(std::strcmp(axisGizmoView(facing % 3, facing >= 3).action, "view.right") == 0);
    c.yaw = glm::radians(20.0f);
    c.pitch = glm::radians(-60.0f);
    facing = axisGizmoFacing(axisGizmoEnds(glm::mat3(c.view()), glm::vec2(0.0f), 30.0f));
    CHECK(std::strcmp(axisGizmoView(facing % 3, facing >= 3).action, "view.top") == 0);
}

TEST(axis_gizmo_click_on_each_end_snaps_to_its_view_then_the_opposite) {
    // From a three-quarter view a click on each end picks that end's view; from there a second click on the same
    // spot (now that end faces you) picks the opposite view.
    for (int i = 0; i < 6; ++i) {
        Camera c;
        c.yaw = glm::radians(35.0f);
        c.pitch = glm::radians(-25.0f);
        auto ends = axisGizmoEnds(glm::mat3(c.view()), glm::vec2(100.0f, 100.0f), 34.0f);
        const int first = axisGizmoSnapEnd(ends, axisGizmoHit(ends, ends[size_t(i)].at.x, ends[size_t(i)].at.y, 10.0f));
        CHECK(first == i);
        const AxisGizmoView v = axisGizmoView(i % 3, i >= 3);
        c.yaw = glm::radians(v.yaw);
        c.pitch = glm::radians(v.pitch);
        ends = axisGizmoEnds(glm::mat3(c.view()), glm::vec2(100.0f, 100.0f), 34.0f);
        CHECK(axisGizmoAligned(ends) == i);
        CHECK(axisGizmoSnapEnd(ends, axisGizmoHit(ends, ends[size_t(i)].at.x, ends[size_t(i)].at.y, 10.0f)) == (i + 3) % 6);
    }
    CHECK(axisGizmoSnapEnd(axisGizmoEnds(glm::mat3(1.0f), glm::vec2(0.0f), 34.0f), -1) == -1);
}

TEST(axis_gizmo_names_only_exact_side_views) {
    Camera c; // the front view
    CHECK(axisGizmoAligned(axisGizmoEnds(glm::mat3(c.view()), glm::vec2(0.0f), 34.0f)) == 2);
    CHECK(std::strcmp(axisGizmoView(2, false).label, "Front view") == 0);
    CHECK(std::strcmp(axisGizmoView(1, false).label, "Top view") == 0);
    c.yaw = glm::radians(3.0f);
    CHECK(axisGizmoAligned(axisGizmoEnds(glm::mat3(c.view()), glm::vec2(0.0f), 34.0f)) == -1);
}

TEST(axis_gizmo_far_ends_are_smaller_and_fainter) {
    Camera c;
    c.yaw = glm::radians(30.0f);
    c.pitch = glm::radians(-20.0f);
    const auto ends = axisGizmoEnds(glm::mat3(c.view()), glm::vec2(0.0f), 34.0f);
    for (int i = 0; i < 3; ++i) {
        // The near end of each axis is drawn bigger and more opaque than the same kind of end pointing away.
        AxisGizmoEnd near = ends[size_t(i)], far = near;
        near.depth = std::abs(near.depth);
        far.depth = -near.depth;
        if (near.depth > 0.05f) {
            CHECK(axisGizmoEndRadius(near, 1.0f) > axisGizmoEndRadius(far, 1.0f));
            CHECK(axisGizmoEndAlpha(near.depth) > axisGizmoEndAlpha(far.depth));
        }
        // A positive end (filled, lettered) is always bigger than a negative one (a ring) at the same depth.
        AxisGizmoEnd neg = ends[size_t(i)];
        neg.negative = true;
        CHECK(axisGizmoEndRadius(ends[size_t(i)], 1.0f) > axisGizmoEndRadius(neg, 1.0f));
    }
    CHECK(axisGizmoEndAlpha(-1.0f) >= 0.45f); // still visible from behind
}

TEST(axis_gizmo_disc_holds_every_end_at_every_size) {
    // The disc behind the gizmo (a drag there orbits) covers each end's circle, at every size and UI scale, so a
    // press on the gizmo either hits an end or lands on the disc.
    Camera c;
    for (float yaw : {0.0f, 30.0f, 90.0f, 135.0f})
        for (int size = 0; size < 3; ++size)
            for (float uiScale : {1.0f, 1.5f, 2.0f}) {
                c.yaw = glm::radians(yaw);
                c.pitch = glm::radians(-20.0f);
                const float unit = uiScale * axisGizmoScale(size);
                const glm::vec2 centre(300.0f, 300.0f);
                const auto ends = axisGizmoEnds(glm::mat3(c.view()), centre, kAxisGizmoLength * unit);
                for (const AxisGizmoEnd& e : ends)
                    CHECK(glm::length(e.at - centre) + kAxisGizmoHitRadius * unit <= kAxisGizmoDisc * unit + 0.01f);
                CHECK(axisGizmoOnDisc(centre, kAxisGizmoDisc * unit, 300.0f + 44.0f * unit, 300.0f));
                CHECK(!axisGizmoOnDisc(centre, kAxisGizmoDisc * unit, 300.0f + 49.0f * unit, 300.0f));
            }
}
