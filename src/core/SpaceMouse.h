// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 3Dconnexion SpaceMouse navigation (#6), independent of SDL so it can be unit
// tested: which USB devices are SpaceMice, the settings (Preferences > Input >
// SpaceMouse) and the mapping from the six axes to a camera move for one frame.
// The device is read through SDL's joystick API (no 3Dconnexion SDK); the app
// side is src/app/SpaceMouse.cpp, the user guide docs/guide/navigation.md.
#pragma once

#include "core/Json.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

// The six axes in the order the device reports them (USB HID: X Y Z Rx Ry Rz). With the cable at the back:
// X is a push to the right, Y a pull towards you, Z a push down; Rx tilts the cap towards you, Ry rolls it to the
// left, Rz twists it clockwise (seen from above).
enum SpaceMouseAxis { kSmTx, kSmTy, kSmTz, kSmRx, kSmRy, kSmRz, kSmAxes };

// True for a 3Dconnexion 6-DoF device: vendor 0x256F (3Dconnexion) or one of the older ones sold under Logitech's
// vendor id 0x046D (SpaceNavigator, SpacePilot, SpaceExplorer...). SDL itself has no list of them (its
// controller_list.h only knows gamepads), so this is ours, from the ids spacenavd and Linux's hid-ids.h use.
// The CadMouse and the 3Dconnexion keyboards / numpads share the vendor id but are not SpaceMice.
// `name` is the device's name: a Logitech-id device with an unknown product counts when it calls itself one.
bool isSpaceMouseDevice(uint16_t vendor, uint16_t product, const std::string& name = {});

constexpr int kSpaceMouseButtonSlots = 8; // buttons whose action Preferences lists (an Enterprise has 31)

struct SpaceMouseSettings {
    bool enabled = true;
    float speed = 1.0f;     // 0.1 .. 4
    float deadZone = 0.08f; // fraction of the full deflection ignored around the rest position (0 .. 0.5)
    std::array<bool, kSmAxes> invert{};
    bool turntable = false; // lock rotation to a turntable: no roll, the world's up stays up
    bool dominant = false;  // only the axis pushed furthest moves the view (no accidental diagonal moves)
    // The action each button runs ("" = nothing): the two side buttons default to Frame and Front view.
    std::array<std::string, kSpaceMouseButtonSlots> buttons{"view.frame", "view.front"};
};

Json toJson(const SpaceMouseSettings& s);
void fromJson(const Json& j, SpaceMouseSettings& s);

// One frame's camera move.
struct SpaceMouseMotion {
    // Rotation in radians, in "the model follows the cap" terms: spin > 0 turns the model's front to the right
    // (like dragging right), tip > 0 brings its top towards you (like dragging down), roll > 0 turns it clockwise
    // on screen.
    float spin = 0.0f, tip = 0.0f, roll = 0.0f;
    // Pan in viewport heights: > 0 moves the model right / down on screen (like a pan drag).
    float panX = 0.0f, panY = 0.0f;
    float zoom = 0.0f; // Camera::zoom steps: > 0 moves closer
    bool any() const { return spin != 0.0f || tip != 0.0f || roll != 0.0f || panX != 0.0f || panY != 0.0f || zoom != 0.0f; }
};

// Full deflection, at speed 1, per second.
constexpr float kSpaceMouseTurnRate = 2.0f;  // radians
constexpr float kSpaceMousePanRate = 1.0f;   // viewport heights
constexpr float kSpaceMouseZoomRate = 6.0f;  // Camera::zoom steps (x0.46 of the distance)
constexpr float kSpaceMouseMaxStep = 0.1f;   // seconds: a stalled frame doesn't throw the view

// An axis value (-1 .. 1) after the dead zone: 0 inside it, rescaled so the rest of the travel still reaches 1.
float spaceMouseDeadZone(float value, float deadZone);

// The move for `dt` seconds with the axes held at `axes` (-1 .. 1, the device's raw directions). Rates are per
// second, so the same deflection turns the view as far at 30 fps as at 144 fps. With turntable on, roll is 0.
SpaceMouseMotion spaceMouseMotion(const std::array<float, kSmAxes>& axes, float dt, const SpaceMouseSettings& s);

} // namespace sf
