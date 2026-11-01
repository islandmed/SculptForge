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
