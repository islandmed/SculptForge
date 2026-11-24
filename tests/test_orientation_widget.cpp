// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The orientation widget (Draw > Camera > Corner gizmo > Orientation): which face, edge or corner a pointer position
// picks, the view each stands for, the second-click flip and the move's easing. Invariants: a face click lands on
// the same six side views as the numpad; every one of the 26 directions is reachable from the front view; the
// easing never overshoots.
#include "TestHarness.h"

#include "app/OrientationWidget.h"
#include "render/Camera.h"

#include <glm/glm.hpp>

#include <cmath>
#include <string>

using namespace sf;

namespace {
const glm::vec2 kCentre(200.0f, 200.0f);
constexpr float kHalf = 19.0f;
bool about(float a, float b, float eps = 0.01f) { return std::abs(a - b) <= eps; }
} // namespace

TEST(orientation_widget_front_view_picks_the_front_face_in_the_middle) {
    Camera c; // the front view: +Z faces the viewer
    const glm::mat3 rot(c.view());
    const OrientationWidgetPick p = orientationWidgetPick(rot, kCentre, kHalf, kCentre.x, kCentre.y);
    CHECK(p.axes == 1);
    CHECK(p.direction == glm::ivec3(0, 0, 1));
    CHECK(orientationWidgetFaceVisible(rot, 2, false));
    CHECK(!orientationWidgetFaceVisible(rot, 2, true));
    CHECK(!orientationWidgetFaceVisible(rot, 0, false)); // the sides are edge-on from the front
}

TEST(orientation_widget_edges_and_corners_sit_in_the_bands) {
    Camera c;
    const glm::mat3 rot(c.view());
    // The near face spans half * eye / (eye - 1) = 19 * 1.25 pixels per half edge on screen: 23.75 px.
    const float px = kHalf * kOrientationWidgetEye / (kOrientationWidgetEye - 1.0f);
    // Right edge band of the front face: x past 0.62 of the half edge, y in the middle.
    const OrientationWidgetPick edge = orientationWidgetPick(rot, kCentre, kHalf, kCentre.x + 0.9f * px, kCentre.y);
    CHECK(edge.axes == 2);
    CHECK(edge.direction == glm::ivec3(1, 0, 1));
    // Top right corner (screen y grows down, so up is -y).
    const OrientationWidgetPick corner = orientationWidgetPick(rot, kCentre, kHalf, kCentre.x + 0.9f * px, kCentre.y - 0.9f * px);
    CHECK(corner.axes == 3);
    CHECK(corner.direction == glm::ivec3(1, 1, 1));
    // Bottom left corner.
    const OrientationWidgetPick bl = orientationWidgetPick(rot, kCentre, kHalf, kCentre.x - 0.9f * px, kCentre.y + 0.9f * px);
    CHECK(bl.axes == 3);
    CHECK(bl.direction == glm::ivec3(-1, -1, 1));
    // Inside the band on both axes: still the face.
    const OrientationWidgetPick face = orientationWidgetPick(rot, kCentre, kHalf, kCentre.x + 0.5f * px, kCentre.y - 0.5f * px);
    CHECK(face.axes == 1);
    CHECK(face.direction == glm::ivec3(0, 0, 1));
    // Off the cube: nothing (the disc there orbits).
    CHECK(!orientationWidgetPick(rot, kCentre, kHalf, kCentre.x + 1.3f * px, kCentre.y).hit());
    CHECK(!orientationWidgetPick(rot, kCentre, kHalf, kCentre.x + 60.0f, kCentre.y + 60.0f).hit());
}

TEST(orientation_widget_picks_follow_the_view) {
    Camera c;
    c.yaw = glm::radians(90.0f); // the right view: +X faces the viewer
    const glm::mat3 rot(c.view());
    const OrientationWidgetPick p = orientationWidgetPick(rot, kCentre, kHalf, kCentre.x, kCentre.y);
    CHECK(p.axes == 1);
    CHECK(p.direction == glm::ivec3(1, 0, 0));
    CHECK(orientationWidgetFaceVisible(rot, 0, false));
    // From a three-quarter view, three faces show and the corner between them projects nearest the viewer.
    Camera q;
    q.yaw = glm::radians(35.0f);
    q.pitch = glm::radians(-25.0f);
    const glm::mat3 rq(q.view());
    int visible = 0;
    for (int a = 0; a < 3; ++a)
        for (int neg = 0; neg < 2; ++neg) visible += orientationWidgetFaceVisible(rq, a, neg != 0) ? 1 : 0;
    CHECK(visible == 3);
    const auto corners = orientationWidgetCorners(rq, kCentre, kHalf);
    int nearest = 0;
    for (int i = 1; i < 8; ++i)
        if (corners[size_t(i)].depth > corners[size_t(nearest)].depth) nearest = i;
    CHECK(nearest == 7); // +X +Y +Z
    const OrientationWidgetPick p2 = orientationWidgetPick(rq, kCentre, kHalf, corners[7].at.x, corners[7].at.y);
    CHECK(p2.axes == 3);
    CHECK(p2.direction == glm::ivec3(1, 1, 1));
}

TEST(orientation_widget_faces_give_the_numpad_views) {
    // Invariant: a face click lands exactly where the numpad views land (Input.cpp: Front 0/0, Back 180/0, Right 90/0,
    // Left -90/0, Top 0/-90, Bottom 0/90).
    for (int i = 0; i < 6; ++i) {
        glm::ivec3 d(0);
        d[i % 3] = i < 3 ? 1 : -1;
        const OrientationWidgetView v = orientationWidgetView(d);
        const AxisGizmoView a = axisGizmoView(i % 3, i >= 3);
        CHECK(about(v.yaw, a.yaw));
        CHECK(about(v.pitch, a.pitch));
    }
}

TEST(orientation_widget_every_direction_looks_at_the_centre_from_there) {
    // Invariant: for every face, edge and corner, a camera at that yaw / pitch sits along the direction and looks at
    // the target with no roll.
    int count = 0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z) {
                const glm::ivec3 d(x, y, z);
                if (d == glm::ivec3(0)) continue;
                ++count;
                const OrientationWidgetView v = orientationWidgetView(d);
                Camera c;
                c.yaw = glm::radians(v.yaw);
                c.pitch = glm::radians(v.pitch);
                const glm::vec3 eye = glm::normalize(c.position() - c.target);
                CHECK(glm::length(eye - glm::normalize(glm::vec3(d))) < 1e-4f);
                CHECK(glm::dot(c.forward(), -glm::normalize(glm::vec3(d))) > 0.9999f);
                CHECK(std::abs(v.pitch) <= 90.0f);
            }
    CHECK(count == 26);
    // Corner: yaw 45, pitch -35.26 (looking down from above the front right).
    const OrientationWidgetView corner = orientationWidgetView(glm::ivec3(1, 1, 1));
    CHECK(about(corner.yaw, 45.0f));
    CHECK(about(corner.pitch, -35.264f));
    // Edge between the front and the right: yaw 45, level.
    const OrientationWidgetView edge = orientationWidgetView(glm::ivec3(1, 0, 1));
    CHECK(about(edge.yaw, 45.0f));
    CHECK(about(edge.pitch, 0.0f));
}

TEST(orientation_widget_second_click_on_the_facing_face_flips) {
    Camera c; // the front view: forward is -Z
    const glm::vec3 fwd = c.forward();
    CHECK(orientationWidgetClickTarget(glm::ivec3(0, 0, 1), 1, fwd) == glm::ivec3(0, 0, -1)); // Front again: the back
    CHECK(orientationWidgetClickTarget(glm::ivec3(1, 0, 0), 1, fwd) == glm::ivec3(1, 0, 0));  // another face: itself
    CHECK(orientationWidgetClickTarget(glm::ivec3(1, 0, 1), 2, fwd) == glm::ivec3(1, 0, 1));  // edges never flip
    Camera t;
    t.yaw = glm::radians(35.0f); // a little off the front: Front is Front
    CHECK(orientationWidgetClickTarget(glm::ivec3(0, 0, 1), 1, t.forward()) == glm::ivec3(0, 0, 1));
}

TEST(orientation_widget_ease_and_yaw_delta) {
    CHECK(orientationWidgetEase(0.0f) == 0.0f);
    CHECK(orientationWidgetEase(1.0f) == 1.0f);
    CHECK(about(orientationWidgetEase(0.5f), 0.5f));
    float last = 0.0f;
    for (int i = 1; i <= 20; ++i) { // monotonic, never past 1
        const float e = orientationWidgetEase(float(i) / 20.0f);
        CHECK(e >= last && e <= 1.0f);
        last = e;
    }
    CHECK(orientationWidgetEase(1.5f) == 1.0f);
    CHECK(about(orientationWidgetYawDelta(0.0f, 90.0f), 90.0f));
    CHECK(about(orientationWidgetYawDelta(170.0f, -170.0f), 20.0f));   // the short way round
    CHECK(about(orientationWidgetYawDelta(-170.0f, 170.0f), -20.0f));
    CHECK(about(orientationWidgetYawDelta(0.0f, 180.0f), 180.0f));    // a half turn goes the positive way
    CHECK(about(orientationWidgetYawDelta(0.0f, 0.0f), 0.0f));
}

TEST(orientation_widget_side_names) {
    CHECK(std::string(orientationWidgetSideName(2, false)) == "Front");
    CHECK(std::string(orientationWidgetSideName(2, true)) == "Rear");
    CHECK(std::string(orientationWidgetSideName(0, false)) == "Right");
    CHECK(std::string(orientationWidgetSideName(0, true)) == "Left");
    CHECK(std::string(orientationWidgetSideName(1, false)) == "Top");
    CHECK(std::string(orientationWidgetSideName(1, true)) == "Bottom");
}
