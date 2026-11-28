// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Controller and SpaceMouse buttons as shortcuts: ids, names and the binding table (Controller.h).
#include "core/Controller.h"

#include <algorithm>

namespace sf {

namespace {

struct PadButton {
    const char* id;
    const char* name;
};

// In the order Preferences lists them. The face buttons name both common layouts (the labels differ, the places do not).
constexpr PadButton kPad[] = {
    {"pad.south", "Pad A / Cross"},
    {"pad.east", "Pad B / Circle"},
    {"pad.west", "Pad X / Square"},
    {"pad.north", "Pad Y / Triangle"},
    {"pad.leftshoulder", "Pad Left bumper"},
    {"pad.rightshoulder", "Pad Right bumper"},
    {"pad.lefttrigger", "Pad Left trigger"},
    {"pad.righttrigger", "Pad Right trigger"},
    {"pad.dpad_up", "Pad D-pad up"},
    {"pad.dpad_down", "Pad D-pad down"},
    {"pad.dpad_left", "Pad D-pad left"},
    {"pad.dpad_right", "Pad D-pad right"},
    {"pad.start", "Pad Start"},
    {"pad.back", "Pad Back"},
    {"pad.guide", "Pad Guide"},
    {"pad.leftstick", "Pad Left stick click"},
    {"pad.rightstick", "Pad Right stick click"},
    {"pad.touchpad", "Pad Touchpad click"},
    {"pad.leftpaddle1", "Pad Left paddle 1"},
    {"pad.rightpaddle1", "Pad Right paddle 1"},
    {"pad.leftpaddle2", "Pad Left paddle 2"},
    {"pad.rightpaddle2", "Pad Right paddle 2"},
    {"pad.misc1", "Pad Extra 1"},
    {"pad.misc2", "Pad Extra 2"},
    {"pad.misc3", "Pad Extra 3"},
    {"pad.misc4", "Pad Extra 4"},
    {"pad.misc5", "Pad Extra 5"},
    {"pad.misc6", "Pad Extra 6"},
};

constexpr std::string_view kSpaceMousePrefix = "spacemouse.";
constexpr int kSpaceMouseIndexBase = 1000;

// The N of "spacemouse.N" (1 .. kControllerSpaceMouseButtons), 0 for any other text. Digits only, no sign or zero pad.
int spaceMouseNumber(std::string_view id) {
    if (id.substr(0, kSpaceMousePrefix.size()) != kSpaceMousePrefix) return 0;
    const std::string_view digits = id.substr(kSpaceMousePrefix.size());
    if (digits.empty() || digits.size() > 2 || digits[0] == '0') return 0;
    int n = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return 0;
        n = n * 10 + (c - '0');
    }
    return n <= kControllerSpaceMouseButtons ? n : 0;
}

} // namespace

bool controllerButtonKnown(std::string_view id) { return controllerButtonIndex(id) != 0; }

const std::vector<std::string>& controllerPadButtons() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> v;
        for (const PadButton& b : kPad) v.push_back(b.id);
        return v;
    }();
    return ids;
}

std::string controllerButtonName(std::string_view id) {
    for (const PadButton& b : kPad)
        if (id == b.id) return b.name;
    if (const int n = spaceMouseNumber(id)) return "SpaceMouse button " + std::to_string(n);
    return std::string(id);
}

int controllerButtonIndex(std::string_view id) {
    int i = 1;
    for (const PadButton& b : kPad) {
        if (id == b.id) return i;
        ++i;
    }
    if (const int n = spaceMouseNumber(id)) return kSpaceMouseIndexBase + n;
    return 0;
}

bool controllerButtonIsSpaceMouse(std::string_view id) { return spaceMouseNumber(id) != 0; }

std::string controllerSpaceMouseId(int zeroBasedButton) {
    if (zeroBasedButton < 0 || zeroBasedButton >= kControllerSpaceMouseButtons) return {};
    return std::string(kSpaceMousePrefix) + std::to_string(zeroBasedButton + 1);
}

bool controllerTriggerPressed(bool wasPressed, float value) {
    return wasPressed ? value > kControllerTriggerOff : value >= kControllerTriggerOn;
}

std::string ControllerSettings::actionFor(std::string_view button) const {
    for (const auto& [b, a] : buttons)
        if (b == button) return a;
    return {};
}

std::vector<std::string> ControllerSettings::buttonsOf(std::string_view action) const {
    std::vector<std::string> out;
    if (action.empty()) return out;
    for (const auto& [b, a] : buttons)
        if (a == action) out.push_back(b);
    return out;
}

std::string ControllerSettings::bind(std::string_view button, std::string_view action) {
    for (auto& [b, a] : buttons)
        if (b == button) {
            std::string was = std::move(a);
            a = std::string(action);
            return was;
        }
    buttons.emplace_back(std::string(button), std::string(action));
    return {};
}

bool ControllerSettings::unbind(std::string_view button) {
    const size_t n = buttons.size();
    std::erase_if(buttons, [&](const auto& p) { return p.first == button; });
    return buttons.size() != n;
}

bool ControllerSettings::unbindAction(std::string_view action) {
    const size_t n = buttons.size();
    std::erase_if(buttons, [&](const auto& p) { return p.second == action; });
    return buttons.size() != n;
}

Json toJson(const ControllerSettings& s) {
    Json j = Json::object();
    j.set("enabled", s.enabled);
    Json b = Json::object();
    for (const auto& [button, action] : s.buttons) b.set(button, action);
    j.set("buttons", b);
    return j;
}

void fromJson(const Json& j, ControllerSettings& s) {
    if (!j.isObject()) return;
    s.enabled = j["enabled"].asBool(s.enabled);
    if (!j.has("buttons")) return;
    s.buttons.clear();
    const Json& b = j["buttons"];
    if (!b.isObject()) return;
    for (const auto& [button, action] : b.members()) {
        if (!controllerButtonKnown(button) || !action.isString()) continue;
        const std::string& id = action.asString();
        if (id.empty() || id.size() > kControllerActionIdMax) continue;
        if (std::any_of(s.buttons.begin(), s.buttons.end(), [&](const auto& p) { return p.first == button; })) continue;
        s.buttons.emplace_back(button, id);
    }
}

} // namespace sf
