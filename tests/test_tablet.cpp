// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tablet settings: the global pressure curve, pen button / eraser mappings
// and their JSON round trip, and the stroke-start pressure guard.
#include "TestHarness.h"

#include "core/Tablet.h"

#include <cmath>
#include <string>

using namespace sf;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
} // namespace

TEST(pressure_curve_linear_is_identity) {
    const PressureCurve c;
    CHECK(c.isLinear());
    for (float p : {0.0f, 0.1f, 0.5f, 0.77f, 1.0f}) CHECK(near(c.apply(p), p));
    CHECK(near(c.apply(-1.0f), 0.0f)); // clamped
    CHECK(near(c.apply(2.0f), 1.0f));
    CHECK(near(c.apply(NAN), 0.0f));
}

TEST(pressure_curve_soft_and_firm) {
    const PressureCurve soft = PressureCurve::soft(), firm = PressureCurve::firm();
    CHECK(!soft.isLinear());
    CHECK(!firm.isLinear());
    // Same ends, different middle: soft gives more for a light touch, firm needs a harder press.
    for (const PressureCurve& c : {soft, firm}) {
        CHECK(near(c.apply(0.0f), 0.0f));
        CHECK(near(c.apply(1.0f), 1.0f));
    }
    CHECK(soft.apply(0.3f) > 0.3f);
    CHECK(firm.apply(0.3f) < 0.3f);
    CHECK(near(firm.apply(0.5f), std::pow(0.5f, firm.gamma)));
    // Monotonic.
    float last = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float v = firm.apply(float(i) / 20.0f);
        CHECK(v >= last);
        last = v;
    }
}

TEST(pressure_curve_min_max_threshold) {
    PressureCurve c;
    c.minOut = 0.2f;
    c.maxOut = 0.8f;
    CHECK(near(c.apply(0.0f), 0.2f));
    CHECK(near(c.apply(1.0f), 0.8f));
    CHECK(near(c.apply(0.5f), 0.5f));
    c = PressureCurve();
    c.threshold = 0.1f;
    c.fullAt = 0.6f;
    CHECK(near(c.apply(0.05f), 0.0f)); // below the threshold: the lightest touch
    CHECK(near(c.apply(0.35f), 0.5f)); // halfway between threshold and full
    CHECK(near(c.apply(0.7f), 1.0f));  // a heavy hand reaches full pressure sooner
    c.fullAt = c.threshold;            // degenerate range must not divide by zero
    CHECK(std::isfinite(c.apply(0.1f)));
    c = PressureCurve();
    c.gamma = 100.0f; // out of range: clamped to 4
    CHECK(near(c.apply(0.5f), std::pow(0.5f, 4.0f)));
}

TEST(tablet_settings_json_round_trip) {
    TabletSettings t;
    CHECK(t.eraser == PenAction::Invert);
    CHECK(t.buttons[0] == PenAction::RightClick);
    CHECK(t.buttons[1] == PenAction::Invert);
    t.curve = PressureCurve::firm();
    t.curve.minOut = 0.1f;
    t.eraser = PenAction::EraseMask;
    t.buttons = {PenAction::RotateView, PenAction::Smooth, PenAction::None};
    TabletSettings back;
    fromJson(toJson(t), back);
    CHECK(near(back.curve.gamma, t.curve.gamma));
    CHECK(near(back.curve.minOut, 0.1f));
    CHECK(back.eraser == PenAction::EraseMask);
    CHECK(back.buttons[0] == PenAction::RotateView);
    CHECK(back.buttons[1] == PenAction::Smooth);
    CHECK(back.buttons[2] == PenAction::None);

    // Unknown or unsuitable values keep the defaults.
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"eraser":"pan","buttons":["erase_mask","bogus"],"pressureCurve":{"gamma":-3}})", j, err));
    TabletSettings d;
    fromJson(j, d);
    CHECK(d.eraser == PenAction::Invert);
    CHECK(d.buttons[0] == PenAction::RightClick);
    CHECK(d.buttons[1] == PenAction::Invert);
    CHECK(near(d.curve.gamma, 0.25f));
    CHECK(std::string(penActionLabel(PenAction::RightClick)) == "Right-click");
}

TEST(pen_pressure_tracker_guards_stroke_start) {
    PenPressureTracker t;
    t.touchDown();
    CHECK(!t.readyForFirstDab()); // the first touch after start: 1.0 is only assumed
    CHECK(!t.everSampled());      // (the app draws at the first motion if no sample ever comes)
    CHECK(near(t.raw(), 1.0f));
    t.sample(0.9f); // end of the previous stroke
    t.touchUp();
    t.touchDown();
    CHECK(!t.readyForFirstDab()); // 0.9 is stale: wait for this stroke's own sample
    t.sample(0.0f);
    CHECK(!t.readyForFirstDab()); // touching but not pressing yet
    t.sample(0.15f);
    CHECK(t.readyForFirstDab());
    CHECK(near(t.raw(), 0.15f));
    t.sample(5.0f);
    CHECK(near(t.raw(), 1.0f));
}

TEST(pen_cursor_mouse_mode_moves_relatively) {
    // Pen mode: the cursor is where the pen is.
    PenCursor c;
    float x = 100.0f, y = 50.0f;
    c.map(x, y, 1000);
    CHECK(near(x, 100.0f) && near(y, 50.0f));
    // Mouse mode (#54): the cursor starts where it is (at the mouse), and moves as far as the pen does.
    c.mouseMode = true;
    c.cursorAt(400.0f, 300.0f);
    c.proximity();
    x = 100.0f, y = 50.0f;
    c.map(x, y, 2000);
    CHECK(near(x, 400.0f) && near(y, 300.0f));
    x = 130.0f, y = 40.0f;
    c.map(x, y, 3000);
    CHECK(near(x, 430.0f) && near(y, 290.0f));
    // Lifted out of range and put down elsewhere on the tablet: the cursor stays, then moves on from there.
    c.proximity();
    c.proximity();
    x = 900.0f, y = 700.0f;
    c.map(x, y, 4000);
    CHECK(near(x, 430.0f) && near(y, 290.0f));
    x = 910.0f, y = 705.0f;
    c.map(x, y, 5000);
    CHECK(near(x, 440.0f) && near(y, 295.0f));
    // A driver that never says the pen left: a pause longer than kLiftedNs counts as lifting it.
    x = 10.0f, y = 10.0f;
    c.map(x, y, 5000 + PenCursor::kLiftedNs + 1);
    CHECK(near(x, 440.0f) && near(y, 295.0f));
    // The same position again (SDL's mouse event for the pen, right after the pen's own) maps the same.
    x = 10.0f, y = 10.0f;
    c.map(x, y, 5000 + PenCursor::kLiftedNs + 2);
    CHECK(near(x, 440.0f) && near(y, 295.0f));
    // The mouse moves the cursor in between: the pen carries on from there.
    c.cursorAt(20.0f, 30.0f);
    x = 15.0f, y = 10.0f;
    c.map(x, y, 5000 + PenCursor::kLiftedNs + 3);
    CHECK(near(x, 20.0f) && near(y, 30.0f));
    // Positions that aren't numbers are left alone and don't move the cursor.
    x = NAN, y = 5.0f;
    c.map(x, y, 5000 + PenCursor::kLiftedNs + 4);
    CHECK(std::isnan(x));
    x = 25.0f, y = 10.0f;
    c.map(x, y, 5000 + PenCursor::kLiftedNs + 5);
    CHECK(near(x, 30.0f) && near(y, 30.0f));
    // Back to pen mode: the pen's own position again.
    c.mouseMode = false;
    x = 25.0f, y = 10.0f;
    c.map(x, y, 5000 + PenCursor::kLiftedNs + 6);
    CHECK(near(x, 25.0f) && near(y, 10.0f));
}

TEST(pen_cursor_mouse_mode_is_relative_with_an_absolute_driver) {
    // #72, drag-d's sequence (0.10.8: "no difference between default and mouse mode"): the tablet driver in pen
    // (absolute) mode reports the pen's place on the tablet. Mouse mode must still move the cursor by the pen's
    // movement only: hover, lift, put down elsewhere, hover again.
    PenCursor c;
    c.mouseMode = true;
    c.cursorAt(400.0f, 300.0f); // where the mouse left the cursor
    c.proximity();
    float x = 100.0f, y = 100.0f; // the pen comes into range at the tablet's top left
    c.map(x, y, 1000);
    CHECK(near(x, 400.0f) && near(y, 300.0f)); // no jump to the pen's place
    for (int i = 1; i <= 10; ++i) { // it moves 100 px right and 50 px down
        x = 100.0f + 10.0f * float(i), y = 100.0f + 5.0f * float(i);
        c.map(x, y, 1000 + uint64_t(i) * 8000000);
    }
    CHECK(near(x, 500.0f) && near(y, 350.0f));
    // Lifted away and put down at the tablet's bottom right: the cursor stays, then moves on by the pen's motion.
    c.proximity();
    x = 1800.0f, y = 1000.0f;
    c.map(x, y, 200000000);
    CHECK(near(x, 500.0f) && near(y, 350.0f));
    x = 1780.0f, y = 990.0f;
    c.map(x, y, 208000000);
    CHECK(near(x, 480.0f) && near(y, 340.0f));
    // Lifted without the driver saying so (silent for longer than kLiftedNs), put down elsewhere: no jump either.
    x = 300.0f, y = 200.0f;
    c.map(x, y, 208000000 + PenCursor::kLiftedNs + 1);
    CHECK(near(x, 480.0f) && near(y, 340.0f));
    // Pen mode: the pen's own place.
    c.mouseMode = false;
    x = 50.0f, y = 60.0f;
    c.map(x, y, 900000000);
    CHECK(near(x, 50.0f) && near(y, 60.0f));
}

TEST(tablet_settings_mouse_mode_round_trip) {
    TabletSettings t;
    CHECK(!t.mouseMode);
    t.mouseMode = true;
    TabletSettings back;
    fromJson(toJson(t), back);
    CHECK(back.mouseMode);
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"mouseMode":"yes"})", j, err)); // not a bool: unchanged
    TabletSettings d;
    fromJson(j, d);
    CHECK(!d.mouseMode);
}

// A pen without a pressure axis (Windows Ink off) sends 0 while touching: after a few motions it draws at full
// pressure instead of never starting the stroke; a pen with pressure never does.
TEST(pen_pressure_tracker_no_pressure_axis) {
    PenPressureTracker t;
    t.touchDown();
    t.sample(0.0f);
    for (int i = 0; i + 1 < PenPressureTracker::kSilentMotions; ++i) t.motion();
    CHECK(!t.readyForFirstDab());
    CHECK(!t.noPressure());
    t.motion();
    CHECK(t.noPressure());
    CHECK(t.readyForFirstDab());
    CHECK(near(t.pressure(), 1.0f));
    t.reset(); // another pen: its own
    CHECK(!t.noPressure() && !t.hasPressure());

    PenPressureTracker p;
    p.touchDown();
    p.sample(0.4f);
    for (int i = 0; i < 3 * PenPressureTracker::kSilentMotions; ++i) p.motion();
    p.touchUp();
    p.touchDown();
    p.sample(0.0f);
    for (int i = 0; i < 3 * PenPressureTracker::kSilentMotions; ++i) p.motion();
    CHECK(p.hasPressure());
    CHECK(!p.noPressure()); // a light touch on a pen with pressure stays light
    CHECK(near(p.pressure(), 0.0f));
    PenPressureTracker hover;
    for (int i = 0; i < 3 * PenPressureTracker::kSilentMotions; ++i) hover.motion();
    CHECK(!hover.noPressure()); // hovering never counts
}

TEST(tablet_hotplug_notes) {
    CHECK(looksLikeTablet("Wacom Intuos3 6x8 Pen"));
    CHECK(looksLikeTablet("Intuos3 6x8"));
    CHECK(looksLikeTablet("HUION Kamvas 13"));
    CHECK(!looksLikeTablet("Logitech USB Optical Mouse"));
    CHECK(!looksLikeTablet("HID-compliant mouse"));
    CHECK(!looksLikeTablet("Openwheel")); // "pen" only as a word

    using N = TabletHotplug::Note;
    TabletHotplug h;
    CHECK(h.penIn(7) == N::NewPen); // the tablet plugged in before launch: no note
    CHECK(h.penIn(7) == N::None);
    CHECK(h.mouseRemoved(3) == N::None); // a mouse, not the tablet

    // Named tablet: connected at once, its pen needs no second note, unplugging says so.
    CHECK(h.mouseAdded(10, "Wacom Intuos3 6x8 Pen") == N::Connected);
    CHECK(h.penIn(7) == N::NewPen); // the same id after a replug is a new pen
    CHECK(h.mouseRemoved(10) == N::Disconnected);

    // Generic name (Windows): connected once its pen shows up; unplugging that device is the tablet leaving.
    CHECK(h.mouseAdded(11, "HID-compliant mouse") == N::None);
    CHECK(h.penIn(8) == N::Connected);
    CHECK(h.penIn(9) == N::NewPen); // the eraser end, on systems where it is a pen of its own
    CHECK(h.mouseRemoved(11) == N::Disconnected);
    CHECK(h.mouseRemoved(11) == N::None);
}
