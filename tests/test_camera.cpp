// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The orbit camera: turntable and free rotation, snapping to axis views.
#include "TestHarness.h"

#include "render/Camera.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

using namespace sf;

namespace {

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

glm::vec3 up(const Camera& c) { return c.orientation() * glm::vec3(0, 1, 0); }

} // namespace

TEST(camera_turntable_stays_upright) {
    Camera c;
    c.orbit(0.0f, 1000.0f); // far past the top
    CHECK(c.pitch >= -glm::radians(89.01f));
    CHECK(up(c).y > 0.0f);
    CHECK(c.roll == 0.0f);
}

TEST(camera_free_rotation_goes_over_the_top) {
    Camera c;
    const float quarter = glm::half_pi<float>() / 0.008f; // pixels for 90 degrees
    c.orbitFree(0.0f, 2.0f * quarter);                     // tilt 180 degrees: upside down, looking from behind
    CHECK(up(c).y < -0.99f);
    CHECK(near(c.forward(), glm::vec3(0, 0, 1), 1e-3f));
    // Sideways drags turn about the screen's vertical axis, whatever the tilt.
    Camera d;
    d.orbitFree(0.0f, quarter * 0.5f);
    const glm::vec3 screenUp = up(d);
    d.orbitFree(quarter, 0.0f);
    CHECK(near(up(d), screenUp, 1e-3f));
}

TEST(camera_orientation_round_trips_through_angles) {
    Camera c;
    const glm::quat q = glm::normalize(glm::quat(0.3f, -0.5f, 0.7f, 0.2f));
    c.setOrientation(q);
    const glm::quat back = c.orientation();
    CHECK(std::abs(std::abs(glm::dot(back, q)) - 1.0f) < 1e-4f);
}

TEST(camera_snaps_to_axis_views) {
    Camera c;
    c.setOrientation(glm::angleAxis(glm::radians(37.0f), glm::vec3(0, 1, 0)) *
                     glm::angleAxis(glm::radians(-20.0f), glm::vec3(1, 0, 0)));
    c.setOrientation(Camera::snapped(c.orientation()));
    CHECK(near(c.forward(), glm::vec3(0, 0, -1)));
    CHECK(near(up(c), glm::vec3(0, 1, 0)));
    // Near the top view: looks straight down, with an axis as screen up.
    c.setOrientation(glm::angleAxis(glm::radians(-80.0f), glm::vec3(1, 0, 0)) *
                     glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 0, 1)));
    c.setOrientation(Camera::snapped(c.orientation()));
    CHECK(near(c.forward(), glm::vec3(0, -1, 0)));
    const glm::vec3 u = glm::abs(up(c));
    CHECK(std::max({u.x, u.y, u.z}) > 0.9999f);
}

TEST(camera_turntable_after_free_rotation_stands_upright) {
    Camera c;
    c.orbitFree(300.0f, 120.0f);
    c.orbitFree(-50.0f, 400.0f);
    const glm::vec3 f = c.forward();
    c.orbit(0.0f, 0.0f);
    CHECK(c.roll == 0.0f);
    CHECK(up(c).y > 0.0f);
    // The view direction is kept (clamped short of straight up / down).
    if (std::abs(f.y) < 0.99f) CHECK(near(c.forward(), f, 1e-3f));
}

TEST(camera_orthographic_matches_perspective_at_the_target) {
    // Numpad 5: the orthographic view is as tall as the perspective one at the target, rays are parallel, and a
    // pixel covers the same width at any depth.
    Camera c;
    c.distance = 5.0f;
    c.aspect = 1.5f;
    const float w = 900.0f, h = 600.0f;
    const float persp = c.worldPerPixel(c.target, h);
    c.ortho = true;
    CHECK(std::abs(c.worldPerPixel(c.target, h) - persp) < 1e-5f);
    CHECK(std::abs(c.worldPerPixel(c.target + glm::vec3(0, 0, -3), h) - persp) < 1e-5f);
    glm::vec3 o1, d1, o2, d2;
    c.screenRay(10.0f, 20.0f, w, h, o1, d1);
    c.screenRay(800.0f, 500.0f, w, h, o2, d2);
    CHECK(near(d1, d2) && near(d1, c.forward()));
    // The centre ray goes through the target; one pixel to the right lands one pixel's width to the right.
    glm::vec3 oc, dc, or_, dr;
    c.screenRay(w * 0.5f, h * 0.5f, w, h, oc, dc);
    c.screenRay(w * 0.5f + 1.0f, h * 0.5f, w, h, or_, dr);
    const glm::vec3 toTarget = c.target - oc;
    CHECK(glm::length(toTarget - dc * glm::dot(toTarget, dc)) < 1e-4f);
    CHECK(std::abs(glm::length(or_ - oc) - persp) < 1e-4f);
    // Zooming in never clips the model: the depth range reaches behind the eye.
    c.distance = 0.01f;
    const glm::vec4 p = c.projection() * c.view() * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f); // in front of the target...
    CHECK(std::abs(p.z / p.w) <= 1.0f); // ...and behind the eye, still drawn
}

TEST(camera_orbit_lock_turns_about_one_world_axis) {
    // View > Orbit lock X / Z (foirgairwolf on Discord): whatever the drag, the locked axis stays where it is on screen and
    // nothing turns about the other two.
    for (int axisIndex : {0, 2}) {
        glm::vec3 axis(0.0f);
        axis[axisIndex] = 1.0f;
        Camera c;
        c.setOrientation(glm::angleAxis(glm::radians(25.0f), glm::vec3(0, 1, 0)) *
                         glm::angleAxis(glm::radians(-15.0f), glm::vec3(1, 0, 0)));
        const glm::quat start = c.orientation();
        const glm::vec3 axisOnScreen = glm::conjugate(start) * axis;
        c.orbitAbout(axis, 37.0f, -81.0f, 0.2f);
        c.orbitAbout(axis, -12.0f, 140.0f, -0.05f);
        const glm::quat q = c.orientation();
        CHECK(near(glm::conjugate(q) * axis, axisOnScreen, 1e-4f));
        // The turn from the start is a rotation about the axis alone.
        const glm::quat turn = q * glm::conjugate(start);
        const glm::vec3 v(turn.x, turn.y, turn.z);
        CHECK(glm::length(v - axis * glm::dot(v, axis)) < 1e-4f);
        CHECK(glm::length(v) > 0.05f); // and it did turn
    }
}

TEST(camera_orbit_lock_follows_the_drag) {
    // From the front: X lock tips the model with an up / down drag and ignores a sideways one (the same way free
    // orbiting tips it); Z lock turns it like a wheel with the cursor's sweep, as rolling the view does.
    Camera x;
    x.orbitAbout(glm::vec3(1, 0, 0), 200.0f, 0.0f, 0.0f);
    CHECK(near(x.forward(), glm::vec3(0, 0, -1)));
    Camera free;
    free.orbitFree(0.0f, 50.0f);
    x.orbitAbout(glm::vec3(1, 0, 0), 0.0f, 50.0f, 0.0f);
    CHECK(near(x.forward(), free.forward(), 1e-4f));
    Camera z;
    z.orbitAbout(glm::vec3(0, 0, 1), 0.0f, 0.0f, 0.3f);
    Camera roll;
    roll.setOrientation(roll.orientation() * glm::angleAxis(-0.3f, glm::vec3(0, 0, 1)));
    CHECK(near(z.forward(), glm::vec3(0, 0, -1)));
    CHECK(near(up(z), up(roll), 1e-4f));
}

TEST(camera_orbit_lock_does_not_drift) {
    // 1000 small drags (through straight up and down for X, where yaw / pitch / roll is degenerate) end where one
    // turn by their sum does, and the locked axis has not moved on screen.
    for (int axisIndex : {0, 2}) {
        glm::vec3 axis(0.0f);
        axis[axisIndex] = 1.0f;
        Camera c;
        c.setOrientation(glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0)));
        const glm::quat start = c.orientation();
        const glm::vec3 axisOnScreen = glm::conjugate(start) * axis;
        for (int i = 0; i < 1000; ++i) c.orbitAbout(axis, 0.7f, 1.3f, 0.004f);
        const float total = -(1.3f * axisOnScreen.x + 0.7f * axisOnScreen.y) * 0.008f * 1000.0f - 0.004f * axisOnScreen.z * 1000.0f;
        const glm::quat expected = glm::angleAxis(total, axis) * start;
        CHECK(std::abs(std::abs(glm::dot(glm::normalize(c.orientation()), expected)) - 1.0f) < 1e-4f);
        CHECK(near(glm::conjugate(c.orientation()) * axis, axisOnScreen, 1e-3f));
    }
}

namespace {

// Where `p` is on a 900 x 600 window, and its depth.
glm::vec3 onScreen(const Camera& c, const glm::vec3& p) {
    const glm::vec4 clip = c.projection() * c.view() * glm::vec4(p, 1.0f);
    return glm::vec3(clip.x / clip.w * 450.0f + 450.0f, 300.0f - clip.y / clip.w * 300.0f, glm::dot(p - c.position(), c.forward()));
}

} // namespace

TEST(camera_orbit_around_the_pointer_keeps_the_point_put) {
    // Preferences > Input > Orbit around the pointer (dm4nd on Discord): a rotation drag that starts over the model
    // turns the view about the surface point under the pointer. Picking it moves the target to its depth without
    // the view jumping; every turn after that (free, turntable, Shift-snapped) leaves the point where it was on screen.
    for (bool ortho : {false, true}) {
        Camera c;
        c.aspect = 1.5f;
        c.ortho = ortho;
        c.target = glm::vec3(0.2f, -0.1f, 0.0f);
        c.distance = 6.0f;
        c.setOrientation(glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0)) *
                         glm::angleAxis(glm::radians(-20.0f), glm::vec3(1, 0, 0)));
        const glm::vec3 pivot(1.1f, 0.6f, 0.9f); // off the view axis: a point at the side of the model
        const glm::vec3 seen = onScreen(c, pivot);
        const glm::mat4 before = c.view();
        const glm::vec3 corner = onScreen(c, glm::vec3(-1.0f, 1.0f, -0.5f));
        c.targetDepthOf(pivot);
        for (int i = 0; i < 4; ++i) CHECK(near(glm::vec3(before[i]), glm::vec3(c.view()[i]), 1e-4f)); // no jump
        CHECK(near(onScreen(c, glm::vec3(-1.0f, 1.0f, -0.5f)), corner, 1e-2f)); // (only the clip planes follow the depth)
        if (!ortho) {
            CHECK(std::abs(c.distance - seen.z) < 1e-4f); // zooming to the middle now works at the point's depth
            CHECK(near(c.target, c.position() + c.forward() * seen.z));
        } else {
            CHECK(c.distance == 6.0f); // (it sets an orthographic view's size)
        }
        Camera raw = c;
        raw.orbitFree(140.0f, -60.0f);
        c.turnAbout(raw.orientation(), pivot);
        CHECK(near(c.orientation() * glm::vec3(0, 0, 1), raw.orientation() * glm::vec3(0, 0, 1), 1e-5f));
        CHECK(near(onScreen(c, pivot), seen, 2e-2f));
        raw.orbit(-90.0f, 35.0f); // turntable
        c.turnAbout(raw.orientation(), pivot);
        CHECK(near(onScreen(c, pivot), seen, 2e-2f));
        c.turnAbout(Camera::snapped(raw.orientation()), pivot); // Shift: the nearest axis view
        CHECK(near(onScreen(c, pivot), seen, 2e-2f));
        CHECK(std::abs(std::abs(glm::dot(glm::normalize(c.orientation()), Camera::snapped(raw.orientation()))) - 1.0f) < 1e-5f);
        // A long drag in small steps doesn't drift.
        for (int i = 0; i < 500; ++i) {
            raw.orbitFree(1.3f, -0.4f);
            c.turnAbout(raw.orientation(), pivot);
        }
        CHECK(near(onScreen(c, pivot), seen, 5e-2f));
    }
}

TEST(camera_orbit_around_the_pointer_ignores_points_behind_the_eye) {
    Camera c;
    const Camera keep = c;
    c.targetDepthOf(c.position() - c.forward()); // behind the eye
    CHECK(near(c.target, keep.target) && c.distance == keep.distance);
}
