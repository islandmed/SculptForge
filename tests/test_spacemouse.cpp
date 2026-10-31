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
    CHECK(!isSpaceMouseDevice(0x045E, 0x028E, "SpaceMouse")); // another vendor never
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
    CHECK(s.enabled && s.buttons[0] == "view.frame" && s.buttons[1] == "view.front");
    s.enabled = false;
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
    CHECK(!r.enabled && near(r.speed, 2.25f) && near(r.deadZone, 0.2f) && r.turntable && r.dominant);
    for (int i = 0; i < kSmAxes; ++i) CHECK(r.invert[size_t(i)] == (i == 2 || i == 5));
    CHECK(r.buttons[0] == "view.top" && r.buttons[1].empty() && r.buttons[3] == "edit.undo");

    // Out-of-range values are clamped, missing ones keep their defaults.
    Json j = Json::object();
    j.set("speed", 100.0);
    j.set("deadZone", -1.0);
    SpaceMouseSettings d;
    fromJson(j, d);
    CHECK(near(d.speed, 4.0f) && near(d.deadZone, 0.0f) && d.enabled && d.buttons[0] == "view.frame");
    fromJson(Json(), d); // not an object: untouched
    CHECK(near(d.speed, 4.0f));
}
