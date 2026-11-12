// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two-finger navigation (phoenix, Discord 11-05): the pairing of fingers into a pan, a pinch and a twist, and the
// pinch's zoom steps (core/TouchGesture.h).
#include "TestHarness.h"

#include "core/TouchGesture.h"
#include "render/Camera.h"

#include <cmath>
#include <limits>

using namespace sf;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
} // namespace

TEST(touch_one_finger_is_not_a_gesture) {
    TouchGesture g;
    TwoFingerStep s;
    g.down(1, {100, 100});
    CHECK(g.fingers() == 1 && !g.gesture());
    CHECK(!g.move(1, {150, 120}, s)); // one finger: a stroke, not navigation
    CHECK(!g.move(7, {0, 0}, s));     // an id that never landed
    g.up(1);
    CHECK(g.fingers() == 0 && !g.gesture());
}

TEST(touch_pinch_pan_twist) {
    TouchGesture g;
    TwoFingerStep s;
    g.down(1, {100, 100});
    g.down(2, {200, 100});
    CHECK(g.gesture());
    // Spread: the second finger moves right by 100, the span doubles; the midpoint moves 50 right.
    CHECK(g.move(2, {300, 100}, s));
    CHECK(near(s.scale, 2.0f) && near(s.angle, 0.0f) && near(s.pan.x, 50.0f) && near(s.pan.y, 0.0f));
    // Both fingers slide down by 40 (two events): each half of a 40 px pan, no pinch or twist.
    CHECK(g.move(1, {100, 140}, s));
    CHECK(near(s.pan.y, 20.0f));
    CHECK(g.move(2, {300, 140}, s));
    CHECK(near(s.pan.y, 20.0f) && near(s.pan.x, 0.0f));
    // Twist clockwise (y down) by 90 degrees about the first finger.
    CHECK(g.move(2, {100, 340}, s));
    CHECK(near(s.angle, 3.14159265f / 2.0f) && near(s.scale, 1.0f));
    // Across -pi / pi: wraps to the short way round.
    TouchGesture w;
    w.down(1, {0, 0});
    w.down(2, {-100, 1});
    CHECK(w.move(2, {-100, -1}, s));
    CHECK(std::abs(s.angle) < 0.05f);
}

TEST(touch_third_finger_and_lifting) {
    TouchGesture g;
    TwoFingerStep s;
    g.down(1, {0, 0});
    g.down(2, {100, 0});
    g.down(3, {50, 50});
    CHECK(g.fingers() == 3);
    CHECK(!g.move(3, {60, 60}, s)); // the third finger doesn't steer
    g.up(1);                        // the pair is now 2 and 3
    CHECK(g.gesture());             // still a gesture until every finger lifts
    CHECK(g.move(3, {60, 0}, s));
    g.up(2);
    g.up(3);
    CHECK(!g.gesture() && g.fingers() == 0);
    // Two fingers on one spot, NaN positions: no pinch or twist from nothing.
    g.down(4, {10, 10});
    g.down(5, {10, 10});
    CHECK(g.move(5, {20, 10}, s));
    CHECK(near(s.scale, 1.0f) && near(s.angle, 0.0f));
    CHECK(!g.move(5, {std::numeric_limits<float>::quiet_NaN(), 0.0f}, s));
    g.clear();
    CHECK(g.fingers() == 0 && !g.gesture());
}

TEST(touch_pinch_zoom_steps) {
    CHECK(near(pinchZoomSteps(1.0f), 0.0f));
    CHECK(pinchZoomSteps(2.0f) > 0.0f && pinchZoomSteps(0.5f) < 0.0f);
    CHECK(near(pinchZoomSteps(0.0f), 0.0f) && near(pinchZoomSteps(-1.0f), 0.0f));
    CHECK(near(pinchZoomSteps(std::numeric_limits<float>::infinity()), 0.0f));
    // Spreading the fingers to twice their span halves the camera's distance (the model looks twice as big).
    Camera c;
    c.distance = 10.0f;
    c.zoom(pinchZoomSteps(2.0f));
    CHECK(near(c.distance, 5.0f, 1e-3f));
}

TEST(palm_guard_follows_the_pen) {
    PalmGuard g;
    CHECK(!g.active(0.0)); // no pen yet: fingers count
    g.pen(true, 10.0);
    CHECK(g.active(10.0) && g.active(10.0 + PalmGuard::kSilent - 0.1)); // in range: while it keeps sending
    g.pen(true, 500.0); // (hover, motion, a stroke: every pen event renews it)
    g.pen(false, 501.0);
    CHECK(g.active(501.0) && g.active(501.0 + PalmGuard::kAfterPen - 0.01)); // the hand is still there for a moment
    CHECK(!g.active(501.0 + PalmGuard::kAfterPen + 0.01)); // then fingers count again
    g.pen(true, 600.0);
    CHECK(g.active(600.0 + PalmGuard::kSilent - 0.1));
    // A pen that never says it left stops counting when it falls silent; any pen event renews it.
    CHECK(!g.active(600.0 + PalmGuard::kSilent + 0.1));
    g.pen(true, 700.0);
    CHECK(g.active(701.0));
}
