// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SpaceMouse.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sf {

namespace {

constexpr uint16_t kVendor3Dconnexion = 0x256F;
constexpr uint16_t kVendorLogitech = 0x046D;

// 3Dconnexion devices sold under Logitech's vendor id (spacenavd's device list, Linux hid-ids.h).
constexpr uint16_t kLogitechSpaceMice[] = {
    0xC603, // SpaceMouse Plus XT
    0xC605, // CADman
    0xC606, // SpaceMouse Classic
    0xC621, // SpaceBall 5000
    0xC623, // SpaceTraveler
    0xC625, // SpacePilot
    0xC626, // SpaceNavigator
    0xC627, // SpaceExplorer
    0xC628, // SpaceNavigator for Notebooks
    0xC629, // SpacePilot Pro
    0xC62B, // SpaceMouse Pro
};

// Under 3Dconnexion's own id every SpaceMouse counts (Wireless C62E / C62F, Pro Wireless C631 / C632, Enterprise
// C633, Compact C635, Module C636, the Universal Receiver C652, and models newer than this list) except these,
// which have no 6-DoF cap. The app also wants six axes before it takes a device.
constexpr uint16_t kNot6Dof[] = {
    0xC650, // CadMouse
    0xC651, // CadMouse Wireless
    0xC654, // CadMouse Pro Wireless
    0xC655, // CadMouse Compact
    0xC664, // Keyboard Pro
    0xC665, // Numpad Pro
};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool isSpaceMouseDevice(uint16_t vendor, uint16_t product, const std::string& name) {
    if (vendor == kVendor3Dconnexion) return std::find(std::begin(kNot6Dof), std::end(kNot6Dof), product) == std::end(kNot6Dof);
    if (vendor == kVendorLogitech) {
        if (std::find(std::begin(kLogitechSpaceMice), std::end(kLogitechSpaceMice), product) != std::end(kLogitechSpaceMice))
            return true;
        const std::string n = lower(name);
        for (const char* word : {"spacemouse", "space mouse", "spacenavigator", "space navigator", "spacepilot",
                                 "spaceexplorer", "spaceball", "spacetraveler"})
            if (n.find(word) != std::string::npos) return true;
    }
    return false;
}

Json toJson(const SpaceMouseSettings& s) {
    Json j = Json::object();
    j.set("enabled", s.enabled);
    j.set("speed", double(s.speed));
    j.set("deadZone", double(s.deadZone));
    Json invert = Json::array();
    for (bool b : s.invert) invert.push(b);
    j.set("invert", invert);
    j.set("turntable", s.turntable);
    j.set("dominant", s.dominant);
    Json buttons = Json::array();
    for (const std::string& b : s.buttons) buttons.push(b);
    j.set("buttons", buttons);
    return j;
}

void fromJson(const Json& j, SpaceMouseSettings& s) {
    if (!j.isObject()) return;
    s.enabled = j["enabled"].asBool(s.enabled);
    const float speed = float(j["speed"].asNumber(s.speed));
    if (std::isfinite(speed)) s.speed = std::clamp(speed, 0.1f, 4.0f);
    const float dead = float(j["deadZone"].asNumber(s.deadZone));
    if (std::isfinite(dead)) s.deadZone = std::clamp(dead, 0.0f, 0.5f);
    if (const Json& inv = j["invert"]; inv.isArray())
        for (size_t i = 0; i < s.invert.size() && i < inv.size(); ++i) s.invert[i] = inv[i].asBool(s.invert[i]);
    s.turntable = j["turntable"].asBool(s.turntable);
    s.dominant = j["dominant"].asBool(s.dominant);
    if (const Json& b = j["buttons"]; b.isArray())
        for (size_t i = 0; i < s.buttons.size() && i < b.size(); ++i)
            if (b[i].isString()) s.buttons[i] = b[i].asString();
}

float spaceMouseDeadZone(float value, float deadZone) {
    if (!std::isfinite(value)) return 0.0f;
    value = std::clamp(value, -1.0f, 1.0f);
    deadZone = std::clamp(deadZone, 0.0f, 0.95f);
    const float a = std::abs(value);
    if (a <= deadZone) return 0.0f;
    return std::copysign((a - deadZone) / (1.0f - deadZone), value);
}

SpaceMouseMotion spaceMouseMotion(const std::array<float, kSmAxes>& axes, float dt, const SpaceMouseSettings& s) {
    SpaceMouseMotion m;
    if (!s.enabled || !(dt > 0.0f)) return m;
    dt = std::min(dt, kSpaceMouseMaxStep);
    std::array<float, kSmAxes> v{};
    for (int i = 0; i < kSmAxes; ++i) v[i] = spaceMouseDeadZone(axes[i], s.deadZone) * (s.invert[i] ? -1.0f : 1.0f);
    if (s.turntable) v[kSmRy] = 0.0f; // (a roll can't win the dominant axis either)
    if (s.dominant) {
        int best = 0;
        for (int i = 1; i < kSmAxes; ++i)
            if (std::abs(v[i]) > std::abs(v[best])) best = i;
        for (int i = 0; i < kSmAxes; ++i)
            if (i != best) v[i] = 0.0f;
    }
    const float k = s.speed * dt;
    // "Object mode": the model moves the way the cap is pushed. X right -> right on screen; Z down -> down on screen;
    // Y towards you -> closer. Rx tilts the cap's top towards you, Rz twists clockwise from above (the model's front
    // goes left), Ry rolls the cap's top to the left (anticlockwise on screen).
    m.panX = v[kSmTx] * kSpaceMousePanRate * k;
    m.panY = v[kSmTz] * kSpaceMousePanRate * k;
    m.zoom = v[kSmTy] * kSpaceMouseZoomRate * k;
    m.tip = v[kSmRx] * kSpaceMouseTurnRate * k;
    m.spin = -v[kSmRz] * kSpaceMouseTurnRate * k;
    m.roll = -v[kSmRy] * kSpaceMouseTurnRate * k;
    return m;
}

} // namespace sf
