// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finger mode (core/FingerMode.h): one finger sculpts once it moves past the slop, two or more navigate, two- and
// three-finger taps undo and redo, two fingers held then slid scrub the history, palms are ignored.
#include "TestHarness.h"

#include "core/FingerMode.h"

#include <limits>

using namespace sf;

namespace {
using K = FingerEvent::Kind;

int count(const FingerMode::Events& e, K k) {
    int n = 0;
    for (const FingerEvent& x : e) n += x.kind == k;
    return n;
}
bool has(const FingerMode::Events& e, K k) { return count(e, k) > 0; }
void append(FingerMode::Events& all, const FingerMode::Events& more) { all.insert(all.end(), more.begin(), more.end()); }
} // namespace

TEST(finger_mode_one_finger_sculpts_after_the_slop) {
    FingerMode m;
    CHECK(m.down(1, {100, 100}, 0.0, false).empty());
    CHECK(m.move(1, {105, 104}, 0.05).empty()); // inside the slop: nothing yet
    CHECK(!m.stroking());
    const auto e = m.move(1, {120, 100}, 0.10);
    CHECK(e.size() == 2 && e[0].kind == K::StrokeBegin && e[1].kind == K::StrokeMove);
    CHECK(e[0].at == glm::vec2(100, 100)); // the stroke starts where the finger landed
    CHECK(e[1].at == glm::vec2(120, 100));
    CHECK(m.stroking());
    const auto more = m.move(1, {140, 110}, 0.15);
    CHECK(more.size() == 1 && more[0].kind == K::StrokeMove);
    const auto end = m.up(1, {140, 110}, 0.2, false);
    CHECK(end.size() == 1 && end[0].kind == K::StrokeEnd);
    CHECK(m.idle());
}

TEST(finger_mode_one_finger_tap_does_nothing) {
    FingerMode m;
    m.down(1, {100, 100}, 0.0, false);
    m.move(1, {103, 101}, 0.05);
    CHECK(m.up(1, {103, 101}, 0.1, false).empty()); // no dab, no undo
    CHECK(m.idle());
    // Held a long time without moving: still not a stroke until it moves.
    m.down(2, {50, 50}, 1.0, false);
    CHECK(m.tick(3.0).empty());
    CHECK(has(m.move(2, {80, 50}, 3.1), K::StrokeBegin));
}

TEST(finger_mode_two_finger_tap_undoes_three_redo) {
    FingerMode m;
    m.down(1, {100, 100}, 0.0, false);
    m.down(2, {200, 100}, 0.02, false);
    m.move(1, {102, 101}, 0.05); // a jitter inside the slop
    CHECK(m.up(1, {102, 101}, 0.12, false).empty());
    const auto e = m.up(2, {200, 100}, 0.14, false);
    CHECK(e.size() == 1 && e[0].kind == K::Undo);
    CHECK(m.idle());

    m.down(1, {100, 100}, 1.0, false);
    m.down(2, {150, 100}, 1.01, false);
    m.down(3, {200, 100}, 1.02, false);
    m.up(2, {150, 100}, 1.10, false);
    m.up(3, {200, 100}, 1.11, false);
    const auto r = m.up(1, {100, 100}, 1.12, false);
    CHECK(r.size() == 1 && r[0].kind == K::Redo);

    // Four fingers: nothing.
    for (uint64_t i = 1; i <= 4; ++i) m.down(i, {float(i) * 50, 100}, 2.0, false);
    FingerMode::Events all;
    for (uint64_t i = 1; i <= 4; ++i) append(all, m.up(i, {float(i) * 50, 100}, 2.1, false));
    CHECK(all.empty());
}

TEST(finger_mode_slow_tap_is_not_undo) {
    FingerMode m;
    m.down(1, {100, 100}, 0.0, false);
    m.down(2, {300, 100}, 0.01, false);
    m.up(1, {100, 100}, 0.40, false); // longer than tapSeconds (and shorter than the hold: two fingers lifted first)
    CHECK(m.up(2, {300, 100}, 0.41, false).empty());
}

TEST(finger_mode_pinch_navigates_not_undo) {
    FingerMode m;
    m.down(1, {100, 100}, 0.0, false);
    m.down(2, {200, 100}, 0.01, false);
    CHECK(m.move(2, {205, 100}, 0.03).empty()); // inside the slop: maybe still a tap
    const auto e = m.move(2, {240, 100}, 0.06);   // spread past it: navigation, caught up from where they landed
    CHECK(has(e, K::Navigate));
    CHECK(m.navigating());
    float scale = 1.0f;
    for (const FingerEvent& x : e) scale *= x.step.scale;
    CHECK(std::abs(scale - 1.4f) < 1e-4f); // 100 -> 140 apart
    CHECK(!has(e, K::StrokeBegin));
    const auto pan = m.move(1, {120, 100}, 0.08);
    CHECK(pan.size() == 1 && pan[0].kind == K::Navigate);
    m.up(1, {120, 100}, 0.1, false);
    CHECK(!has(m.up(2, {240, 100}, 0.12, false), K::Undo)); // a quick pinch is not a tap
    CHECK(m.idle());
}

TEST(finger_mode_second_finger_ends_a_stroke_and_navigates) {
    FingerMode m;
    m.down(1, {100, 100}, 0.0, false);
    m.move(1, {150, 100}, 0.05);
    CHECK(m.stroking());
    const auto e = m.down(2, {300, 300}, 0.1, false);
    CHECK(e.size() == 1 && e[0].kind == K::StrokeEnd);
    CHECK(m.navigating());
    CHECK(has(m.move(2, {320, 300}, 0.12), K::Navigate));
    m.up(1, {150, 100}, 0.2, false);
    CHECK(m.up(2, {320, 300}, 0.21, false).empty()); // no second StrokeEnd, no undo
}

TEST(finger_mode_hold_and_slide_scrubs_history) {
    FingerMode m;
    m.tuning.scrubStepPx = 30.0f;
    m.down(1, {100, 100}, 0.0, false);
    m.down(2, {200, 100}, 0.01, false);
    CHECK(m.tick(0.2).empty());
    const auto b = m.tick(0.5);
    CHECK(b.size() == 1 && b[0].kind == K::ScrubBegin && m.scrubbing());
    // Slide left 2.5 steps: two steps back (undo), not navigation.
    auto e = m.move(1, {25, 100}, 0.6);           // the centre 37.5 px left: one step
    CHECK(e.size() == 1 && e[0].kind == K::ScrubStep && e[0].steps == -1);
    e = m.move(2, {125, 100}, 0.61); // 75 px left: one more
    CHECK(e.size() == 1 && e[0].kind == K::ScrubStep && e[0].steps == -1);
    CHECK(!has(e, K::Navigate));
    CHECK(m.scrubSteps() == -2);
    CHECK(m.move(1, {40, 100}, 0.7).empty()); // still within step -2
    const auto fwd = m.move(2, {260, 100}, 0.8);
    CHECK(fwd.size() == 1 && fwd[0].steps > 0);
    const auto end = m.up(1, {40, 100}, 1.0, false);
    CHECK(end.size() == 1 && end[0].kind == K::ScrubEnd);
    CHECK(m.up(2, {260, 100}, 1.01, false).empty()); // no tap after a hold
    CHECK(m.idle());
}

TEST(finger_mode_third_finger_leaves_a_scrub_alone) {
    FingerMode m;
    m.tuning.scrubStepPx = 30.0f;
    m.down(1, {100, 100}, 0.0, false);
    m.down(2, {200, 100}, 0.0, false);
    CHECK(has(m.tick(0.5), K::ScrubBegin));
    CHECK(m.down(3, {600, 100}, 0.6, false).empty()); // far right: would move the centre two steps
    CHECK(m.move(3, {700, 100}, 0.7).empty());
    CHECK(m.scrubSteps() == 0);
    CHECK(m.up(3, {700, 100}, 0.8, false).empty()); // its lift doesn't end the scrub
    CHECK(m.scrubbing());
    CHECK(has(m.move(1, {40, 100}, 0.9), K::ScrubStep));
}

TEST(finger_mode_hold_starts_from_motion_events_too) {
    FingerMode m;
    m.down(1, {100, 100}, 0.0, false);
    m.down(2, {200, 100}, 0.0, false);
    CHECK(has(m.move(1, {101, 100}, 0.6), K::ScrubBegin)); // a resting jitter after the hold time
}

TEST(finger_mode_moving_fingers_never_scrub) {
    FingerMode m;
    m.down(1, {100, 100}, 0.0, false);
    m.down(2, {200, 100}, 0.0, false);
    m.move(2, {260, 100}, 0.1); // navigation
    CHECK(m.tick(2.0).empty());
    CHECK(m.navigating());
}

TEST(finger_mode_palm_is_ignored) {
    FingerMode m;
    // A palm lands while the pen is near: it never strokes, navigates or taps.
    CHECK(m.down(1, {100, 100}, 0.0, true).empty());
    CHECK(m.move(1, {300, 300}, 0.1).empty());
    CHECK(m.fingers() == 0 && !m.idle());
    CHECK(m.up(1, {300, 300}, 0.2, false).empty());
    CHECK(m.idle());
    // A palm beside a real finger doesn't make it a two-finger gesture.
    m.down(5, {500, 500}, 1.0, true);
    m.down(1, {100, 100}, 1.0, false);
    CHECK(has(m.move(1, {150, 100}, 1.1), K::StrokeBegin));
    m.up(1, {150, 100}, 1.2, false);
    m.up(5, {500, 500}, 1.2, false);
    // A two-finger tap that ends with the pen near: no undo.
    m.down(1, {100, 100}, 2.0, false);
    m.down(2, {200, 100}, 2.0, false);
    m.up(1, {100, 100}, 2.1, false);
    CHECK(m.up(2, {200, 100}, 2.1, true).empty());
}

TEST(finger_mode_cancel_and_bad_input) {
    FingerMode m;
    m.down(1, {100, 100}, 0.0, false);
    m.move(1, {200, 100}, 0.1);
    const auto e = m.cancel();
    CHECK(e.size() == 1 && e[0].kind == K::StrokeEnd);
    CHECK(m.idle());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(m.down(1, {nan, 0}, 0.0, false).empty());
    CHECK(m.fingers() == 0);
    m.down(1, {0, 0}, 0.0, false);
    CHECK(m.move(1, {nan, nan}, 0.1).empty());
    CHECK(m.move(9, {50, 50}, 0.1).empty()); // never landed
    CHECK(m.up(9, {50, 50}, 0.1, false).empty());
    m.down(1, {300, 300}, 0.1, false); // landing twice: the first landing counts
    CHECK(m.fingers() == 1);
}
