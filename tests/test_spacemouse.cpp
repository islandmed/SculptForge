// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SpaceMouse navigation (#6): which devices count, the dead zone, speed,
// per-axis inversion, dominant axis, turntable lock, frame-rate independence
// and the settings' JSON round trip.
#include "TestHarness.h"

#include "core/SpaceMouse.h"
#include "render/Camera.h"

#include <cmath>

using namespace sf;

namespace {
bool near(float a, float b, float eps = 1e-5f) { return std::abs(a - b) < eps; }
std::array<float, kSmAxes> axes(float tx, float ty, float tz, float rx, float ry, float rz) { return {tx, ty, tz, rx, ry, rz}; }
} // namespace

TEST(spacemouse_device_ids) {
    CHECK(isSpaceMouseDevice(0x256F, 0xC635));  // SpaceMouse Compact
    CHECK(isSpaceMouseDevice(0x256F, 0xC633));  // Enterprise
    CHECK(isSpaceMouseDevice(0x256F, 0xC652));  // Universal Receiver
    CHECK(isSpaceMouseDevice(0x256F, 0xC6FF));  // a newer model
    CHECK(!isSpaceMouseDevice(0x256F, 0xC650)); // CadMouse
    CHECK(!isSpaceMouseDevice(0x256F, 0xC665)); // Numpad Pro
    CHECK(isSpaceMouseDevice(0x046D, 0xC626));  // SpaceNavigator
    CHECK(isSpaceMouseDevice(0x046D, 0xC62B));  // SpaceMouse Pro
    CHECK(!isSpaceMouseDevice(0x046D, 0xC52B)); // a Logitech Unifying receiver
    CHECK(!isSpaceMouseDevice(0x046D, 0xC215, "Logitech Extreme 3D")); // a flight stick
    CHECK(isSpaceMouseDevice(0x046D, 0xC6AA, "3Dconnexion SpaceNavigator")); // by name
    CHECK(!isSpaceMouseDevice(0x045E, 0x028E, "SpaceMouse")); // another vendor: not by a bare model name
    // #104: no vendor id (Bluetooth on some systems), or a name that says 3Dconnexion, whatever the id.
    CHECK(isSpaceMouseDevice(0x0000, 0x0000, "SpaceMouse Wireless BT"));
    CHECK(isSpaceMouseDevice(0x1234, 0x0001, "3Dconnexion SpaceMouse Pro"));
    CHECK(!isSpaceMouseDevice(0x0000, 0x0000, "Wireless Controller"));
    CHECK(!isSpaceMouseDevice(0x1234, 0x0001, "3Dconnexion CadMouse"));
    CHECK(!isSpaceMouseDevice(0x0000, 0x0000, "3Dconnexion Keyboard Pro"));
}

TEST(spacemouse_dead_zone) {
    CHECK(near(spaceMouseDeadZone(0.0f, 0.1f), 0.0f));
    CHECK(near(spaceMouseDeadZone(0.09f, 0.1f), 0.0f));
    CHECK(near(spaceMouseDeadZone(-0.1f, 0.1f), 0.0f));
    CHECK(near(spaceMouseDeadZone(1.0f, 0.1f), 1.0f)); // the full travel is still reachable
    CHECK(near(spaceMouseDeadZone(-1.0f, 0.1f), -1.0f));
    CHECK(near(spaceMouseDeadZone(0.55f, 0.1f), 0.5f)); // rescaled, no jump at the edge
    CHECK(spaceMouseDeadZone(0.1001f, 0.1f) < 0.001f);
    CHECK(near(spaceMouseDeadZone(2.0f, 0.1f), 1.0f)); // clamped
    CHECK(near(spaceMouseDeadZone(NAN, 0.1f), 0.0f));
    CHECK(near(spaceMouseDeadZone(0.3f, 0.0f), 0.3f));

    // A cap resting slightly off centre doesn't move the view.
    SpaceMouseSettings s;
    CHECK(!spaceMouseMotion(axes(0.05f, -0.06f, 0.04f, 0.07f, -0.02f, 0.05f), 1.0f / 60.0f, s).any());
}

TEST(spacemouse_directions_and_rates) {
    SpaceMouseSettings s;
    s.deadZone = 0.0f;
    const float dt = 0.05f;
    // Object mode: the model goes where the cap is pushed.
    SpaceMouseMotion m = spaceMouseMotion(axes(1, 0, 0, 0, 0, 0), dt, s);
    CHECK(near(m.panX, kSpaceMousePanRate * dt));
    CHECK(m.panY == 0.0f && m.zoom == 0.0f && m.spin == 0.0f);
    CHECK(spaceMouseMotion(axes(0, 0, 1, 0, 0, 0), dt, s).panY > 0.0f); // pushed down: the model goes down
    CHECK(near(spaceMouseMotion(axes(0, 1, 0, 0, 0, 0), dt, s).zoom, kSpaceMouseZoomRate * dt)); // pulled: closer
    CHECK(near(spaceMouseMotion(axes(0, 0, 0, 1, 0, 0), dt, s).tip, kSpaceMouseTurnRate * dt));
    CHECK(near(spaceMouseMotion(axes(0, 0, 0, 0, 0, 1), dt, s).spin, -kSpaceMouseTurnRate * dt)); // clockwise twist
    CHECK(near(spaceMouseMotion(axes(0, 0, 0, 0, 1, 0), dt, s).roll, -kSpaceMouseTurnRate * dt));

    // Speed scales every axis.
    s.speed = 2.5f;
    m = spaceMouseMotion(axes(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f), dt, s);
    CHECK(near(m.panX, 0.5f * kSpaceMousePanRate * dt * 2.5f));
    CHECK(near(m.zoom, 0.5f * kSpaceMouseZoomRate * dt * 2.5f));
    CHECK(near(m.spin, -0.5f * kSpaceMouseTurnRate * dt * 2.5f));

    // Off: nothing, whatever the cap does. No time: nothing.
    s.enabled = false;
    CHECK(!spaceMouseMotion(axes(1, 1, 1, 1, 1, 1), dt, s).any());
    s.enabled = true;
    CHECK(!spaceMouseMotion(axes(1, 1, 1, 1, 1, 1), 0.0f, s).any());
}

TEST(spacemouse_invert_each_axis) {
    SpaceMouseSettings s;
    s.deadZone = 0.0f;
    const auto all = axes(0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f);
    const SpaceMouseMotion plain = spaceMouseMotion(all, 0.02f, s);
    for (int i = 0; i < kSmAxes; ++i) {
        SpaceMouseSettings inv = s;
        inv.invert[size_t(i)] = true;
        const SpaceMouseMotion m = spaceMouseMotion(all, 0.02f, inv);
        const float got[] = {m.panX, m.zoom, m.panY, m.tip, m.roll, m.spin};
        const float want[] = {plain.panX, plain.zoom, plain.panY, plain.tip, plain.roll, plain.spin};
        for (int k = 0; k < kSmAxes; ++k) CHECK(near(got[k], k == i ? -want[k] : want[k]));
    }
}

TEST(spacemouse_dominant_axis_and_turntable) {
    SpaceMouseSettings s;
    s.dominant = true;
    // Twisting hard while also nudging sideways: only the twist moves the view.
    SpaceMouseMotion m = spaceMouseMotion(axes(0.5f, 0.2f, 0.0f, 0.3f, 0.1f, -0.9f), 0.02f, s);
    CHECK(m.spin > 0.0f);
    CHECK(m.panX == 0.0f && m.zoom == 0.0f && m.tip == 0.0f && m.roll == 0.0f);
    // Inversion counts before the choice, the magnitude decides.
    m = spaceMouseMotion(axes(-0.95f, 0.2f, 0.0f, 0.3f, 0.1f, -0.9f), 0.02f, s);
    CHECK(m.panX < 0.0f && m.spin == 0.0f);

    // Turntable: rolling the cap does nothing, and a roll can't win the dominant choice.
    s.turntable = true;
    m = spaceMouseMotion(axes(0.0f, 0.0f, 0.0f, 0.3f, 0.95f, 0.0f), 0.02f, s);
    CHECK(m.roll == 0.0f && m.tip > 0.0f);
    s.dominant = false;
    CHECK(!spaceMouseMotion(axes(0, 0, 0, 0, 1, 0), 0.02f, s).any());
}

TEST(spacemouse_frame_rate_independent) {
    SpaceMouseSettings s;
    const auto held = axes(0.6f, -0.4f, 0.3f, 0.5f, 0.0f, -0.7f);
    // One second held at 30, 60 and 144 fps moves the view the same distance.
    auto oneSecond = [&](int fps) {
        Camera cam;
        cam.distance = 5.0f;
        SpaceMouseMotion total;
        for (int f = 0; f < fps; ++f) {
            const SpaceMouseMotion m = spaceMouseMotion(held, 1.0f / float(fps), s);
            total.spin += m.spin, total.tip += m.tip, total.panX += m.panX, total.panY += m.panY, total.zoom += m.zoom;
            cam.zoom(m.zoom);
            cam.orbit(m.spin / 0.008f, m.tip / 0.008f);
        }
        return std::make_pair(total, cam);
    };
    const auto [a, camA] = oneSecond(30);
    const auto [b, camB] = oneSecond(60);
    const auto [c, camC] = oneSecond(144);
    for (const SpaceMouseMotion* m : {&b, &c}) {
        CHECK(near(m->spin, a.spin, 1e-4f));
        CHECK(near(m->tip, a.tip, 1e-4f));
        CHECK(near(m->panX, a.panX, 1e-4f));
        CHECK(near(m->panY, a.panY, 1e-4f));
        CHECK(near(m->zoom, a.zoom, 1e-3f));
    }
    CHECK(near(camB.distance, camA.distance, 1e-3f) && near(camC.distance, camA.distance, 1e-3f));
    CHECK(near(camB.yaw, camA.yaw, 1e-4f) && near(camC.yaw, camA.yaw, 1e-4f));
    CHECK(near(camC.pitch, camA.pitch, 1e-4f));
    CHECK(camA.distance > 5.0f); // pushed away from you: further
    // A stalled frame (a second-long hitch) moves no further than kSpaceMouseMaxStep would.
    const SpaceMouseMotion hitch = spaceMouseMotion(held, 1.0f, s), capped = spaceMouseMotion(held, kSpaceMouseMaxStep, s);
    CHECK(near(hitch.spin, capped.spin) && near(hitch.zoom, capped.zoom));
}

TEST(spacemouse_settings_json) {
    SpaceMouseSettings s;
    CHECK(s.enabled && s.holdWhileStroking && s.buttons[0] == "view.frame" && s.buttons[1] == "view.front");
    s.enabled = false;
    s.holdWhileStroking = false;
    s.releaseTimeout = false;
    s.speed = 2.25f;
    s.deadZone = 0.2f;
    s.invert[2] = s.invert[5] = true;
    s.turntable = true;
    s.dominant = true;
    s.buttons[0] = "view.top";
    s.buttons[1] = "";
    s.buttons[3] = "edit.undo";
    SpaceMouseSettings r;
    Json parsed;
    std::string err;
    CHECK(Json::parse(toJson(s).dump(), parsed, err));
    fromJson(parsed, r);
    CHECK(!r.releaseTimeout && SpaceMouseSettings().releaseTimeout);
    CHECK(!r.enabled && !r.holdWhileStroking && near(r.speed, 2.25f) && near(r.deadZone, 0.2f) && r.turntable && r.dominant);
    for (int i = 0; i < kSmAxes; ++i) CHECK(r.invert[size_t(i)] == (i == 2 || i == 5));
    CHECK(r.buttons[0] == "view.top" && r.buttons[1].empty() && r.buttons[3] == "edit.undo");

    // Out-of-range values are clamped, missing ones keep their defaults.
    Json j = Json::object();
    j.set("speed", 100.0);
    j.set("deadZone", -1.0);
    SpaceMouseSettings d;
    fromJson(j, d);
    CHECK(near(d.speed, 4.0f) && near(d.deadZone, 0.0f) && d.enabled && d.holdWhileStroking && d.buttons[0] == "view.frame");
    fromJson(Json(), d); // not an object: untouched
    CHECK(near(d.speed, 4.0f));
}

// fenix (Discord 11-06): slowly turning the cap about a horizontal axis first turned the view, then zoomed it away.
// A turn alone only turns (no zoom or pan from Rx or Ry, free or turntable), and the driver's echo counts from the cap's first stir, inside the
// dead zone, so its scrolling is dropped from the start.
TEST(spacemouse_tilt_only_turns_and_stirs_early) {
    SpaceMouseSettings s;
    for (bool turntable : {false, true}) {
        s.turntable = turntable;
        for (float r : {0.1f, 0.3f, -0.6f, 1.0f}) {
            std::array<float, kSmAxes> a{};
            a[kSmRx] = r; // forward / back
            SpaceMouseMotion m = spaceMouseMotion(a, 1.0f / 60.0f, s);
            CHECK(m.tip != 0.0f && m.zoom == 0.0f && m.panX == 0.0f && m.panY == 0.0f && m.spin == 0.0f && m.roll == 0.0f);
            a = {};
            a[kSmRy] = r; // sideways: a roll, none in turntable
            m = spaceMouseMotion(a, 1.0f / 60.0f, s);
            CHECK((m.roll != 0.0f) != turntable && m.zoom == 0.0f && m.panX == 0.0f && m.panY == 0.0f && m.spin == 0.0f &&
                  m.tip == 0.0f);
        }
    }
    s = SpaceMouseSettings(); // dead zone 8%: stirred past 4%
    std::array<float, kSmAxes> a{};
    CHECK(!spaceMouseStirred(a, s));
    a[kSmRx] = 0.03f;
    CHECK(!spaceMouseStirred(a, s)); // a cap resting a little off centre
    a[kSmRx] = -0.05f;
    CHECK(spaceMouseStirred(a, s) && !spaceMouseMotion(a, 1.0f / 60.0f, s).any()); // stirred, not yet moving the view
    s.deadZone = 0.0f;
    a[kSmRx] = 0.005f;
    CHECK(!spaceMouseStirred(a, s));
    a[kSmRx] = 0.02f;
    CHECK(spaceMouseStirred(a, s));
    s.enabled = false; // SpaceMouse off: its driver's scrolling is the user's to keep
    CHECK(!spaceMouseStirred(a, s));
}

// lauterkunst (Discord #bug-reports, Space Explorer 046d:c627, Windows): after letting go of the cap the view kept
// moving at the last speed. A model that never sends its all-zero report leaves SDL showing the last push for ever.
// Each "frame" below reads the axes as the app does (60 per second), through the report watch, into the view's move.
namespace {
struct Replay {
    SpaceMouseSettings s;
    SpaceMouseReportWatch watch;
    float t = 0.0f;
    float spinTotal = 0.0f, panTotal = 0.0f;
    // One frame at 60 Hz with the axes as the device reports them now.
    SpaceMouseMotion frame(const std::array<float, kSmAxes>& raw) {
        const float dt = 1.0f / 60.0f;
        t += dt;
        const SpaceMouseMotion m = spaceMouseMotion(watch.update(raw, dt, s.releaseTimeout), dt, s);
        spinTotal += m.spin;
        panTotal += m.panX;
        return m;
    }
    void frames(const std::array<float, kSmAxes>& raw, float seconds) {
        for (int n = int(seconds * 60.0f + 0.5f); n > 0; --n) frame(raw);
    }
};
} // namespace

TEST(spacemouse_no_zero_report_stops_the_view) {
    Replay r;
    r.s.speed = 0.55f;
    r.s.deadZone = 0.15f; // the reporter's settings
    // A push on the cap that comes in as a few changing reports, then nothing: the device never says "zero".
    r.frame(axes(0, 0, 0, 0, 0, 0));
    for (float v : {0.2f, 0.45f, 0.7f, 0.82f}) r.frames(axes(0, 0, 0, 0, 0, v), 0.05f);
    r.frames(axes(0, 0, 0, 0, 0, 0.82f), 0.1f); // held a moment: still moving
    CHECK(r.frame(axes(0, 0, 0, 0, 0, 0.82f)).any());
    // The cap is let go; the last value stays latched. The view stops within the timeout and stays stopped.
    r.frames(axes(0, 0, 0, 0, 0, 0.82f), kSpaceMouseReportTimeout + 0.05f);
    CHECK(!r.frame(axes(0, 0, 0, 0, 0, 0.82f)).any());
    const float stopped = r.spinTotal;
    r.frames(axes(0, 0, 0, 0, 0, 0.82f), 5.0f);
    CHECK(near(r.spinTotal, stopped, 1e-7f));
    // An opposite push is a new report: the view moves the other way at once, not "slower".
    const SpaceMouseMotion back = r.frame(axes(0, 0, 0, 0, 0, -0.6f));
    CHECK(back.spin > 0.0f); // (Rz > 0 turns the model the other way)
    r.frames(axes(0, 0, 0, 0, 0, -0.6f), 3.0f);
    CHECK(!r.frame(axes(0, 0, 0, 0, 0, -0.6f)).any()); // ...and that push latches too, then stops
    // And a real zero report still works the normal way.
    CHECK(!r.frame(axes(0, 0, 0, 0, 0, 0)).any());
}

TEST(spacemouse_steady_hold_keeps_moving) {
    // A hand holds the cap: values jitter by one count (1/350) now and then, or are perfectly identical for a while
    // (dead zone and quantisation). Anything shorter than the timeout must keep the view moving.
    Replay r;
    std::array<float, kSmAxes> a = axes(0.5f, 0, 0, 0, 0, 0);
    r.frame(a);
    for (int i = 0; i < 180; ++i) { // 3 seconds, a one-count change every 0.2 s
        if (i % 12 == 0) a[kSmTx] += (i % 24 == 0 ? 1.0f : -1.0f) / 350.0f;
        CHECK(r.frame(a).panX > 0.0f);
    }
    // Identical for just under the timeout, then a change: no stop in between.
    Replay q;
    const std::array<float, kSmAxes> hold = axes(0.5f, 0, 0, 0, 0, 0);
    for (int i = 0; i < 4; ++i) {
        for (int n = 0; n < int((kSpaceMouseReportTimeout - 0.05f) * 60.0f); ++n) CHECK(q.frame(hold).panX > 0.0f);
        std::array<float, kSmAxes> next = hold;
        next[kSmTx] += float(i + 1) / 350.0f;
        CHECK(q.frame(next).panX > 0.0f);
    }
    // With the timeout setting off a latched cap behaves as before (nothing is guessed).
    Replay off;
    off.s.releaseTimeout = false;
    off.frames(hold, 5.0f);
    CHECK(off.frame(hold).panX > 0.0f);
}

TEST(spacemouse_split_report_ids) {
    // Older models send the move on report 1 and the turn on report 2. SDL merges them into one axis set; the
    // reports arrive at different moments. One report's axes unchanged while the other's keep changing is a live cap.
    Replay r;
    std::array<float, kSmAxes> a = axes(0.6f, 0, 0, 0, 0, 0); // report 1: translation
    r.frame(a);
    for (int i = 0; i < 120; ++i) { // 2 s: only report 2 (rotation) comes in, each 0.1 s
        if (i % 6 == 0) a[kSmRz] = 0.3f + 0.01f * float(i / 6);
        const SpaceMouseMotion m = r.frame(a);
        CHECK(m.panX > 0.0f);
    }
    // Report 1 sends its zero (release of the translation) but report 2 never does: the turn stays latched.
    a[kSmTx] = 0.0f;
    CHECK(r.frame(a).spin != 0.0f);
    r.frames(a, kSpaceMouseReportTimeout + 0.05f);
    CHECK(!r.frame(a).any());
    // Peeking between frames agrees with the frame (the echo filter reads the axes this way).
    CHECK(r.watch.peek(a, true) == axes(0, 0, 0, 0, 0, 0));
    CHECK(r.watch.peek(axes(0, 0, 0, 0, 0, 0.5f), true) == axes(0, 0, 0, 0, 0, 0.5f));
    CHECK(r.watch.latched());
    r.watch.reset();
    CHECK(!r.watch.latched() && r.watch.peek(a, true) == a);
}
