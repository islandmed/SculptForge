// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The axis gizmo (#43): where its ends sit for a view, which end a click picks, and that each end snaps to the
// side view looking from it.
#include "TestHarness.h"

#include "app/AxisGizmo.h"
#include "render/Camera.h"

#include <glm/glm.hpp>

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
