// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/RadialMenu.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sf {

namespace {

constexpr const char* kFormat = "sculptforge-radial-menu";
constexpr float kTwoPi = 6.28318530717958647692f;

struct ButtonName {
    const char* id;
    const char* label;
};
constexpr ButtonName kButtons[int(RadialButton::Count)] = {
    {"none", "None"},
    {"mouse_middle", "Middle mouse button"},
    {"mouse_back", "Mouse back button"},
    {"mouse_forward", "Mouse forward button"},
    {"pen_lower", "Pen lower button"},
    {"pen_upper", "Pen upper button"},
    {"pen_third", "Pen third button"},
    {"mouse_right", "Right mouse button"},
};

// An id from a file: a string of sensible length the vocabulary knows, or "".
std::string knownId(const Json& j, const std::function<bool(std::string_view)>& known) {
    if (!j.isString()) return {};
    const std::string& s = j.asString();
    if (s.empty() || s.size() > kRadialIdMax || !known || !known(s)) return {};
    return s;
}

} // namespace

const char* radialButtonId(RadialButton b) {
    const int i = int(b);
    return i >= 0 && i < int(RadialButton::Count) ? kButtons[i].id : "none";
}

const char* radialButtonLabel(RadialButton b) {
    const int i = int(b);
    return i >= 0 && i < int(RadialButton::Count) ? kButtons[i].label : "None";
}

RadialButton radialButtonFromId(std::string_view id) {
    for (int i = 0; i < int(RadialButton::Count); ++i)
        if (id == kButtons[i].id) return RadialButton(i);
    return RadialButton::None;
}

int radialSlotAt(float dx, float dy, int count, float deadZone) {
    if (count <= 0 || !std::isfinite(dx) || !std::isfinite(dy)) return -1;
    if (dx * dx + dy * dy <= deadZone * deadZone) return -1;
    // Clockwise from straight up (screen y points down).
    float a = std::atan2(dx, -dy);
    if (a < 0.0f) a += kTwoPi;
    const float wedge = kTwoPi / float(count);
    const int slot = int(std::floor((a + wedge * 0.5f) / wedge));
    return ((slot % count) + count) % count;
}

void radialSlotDirection(int slot, int count, float& x, float& y) {
    const float a = count > 0 ? kTwoPi * float(slot) / float(count) : 0.0f;
    x = std::sin(a);
    y = -std::cos(a);
}

std::string radialCleanText(std::string_view text, size_t maxBytes) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        const auto c = uint8_t(text[i]);
        size_t len = 0;
        if (c < 0x80) len = 1;
        else if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c >= 0xE0 && c <= 0xEF) len = 3;
        else if (c >= 0xF0 && c <= 0xF4) len = 4;
        if (len == 0 || i + len > text.size()) { // a stray continuation byte, a bad lead byte, a cut-off sequence
            ++i;
            continue;
        }
        bool valid = true;
        for (size_t k = 1; k < len; ++k) valid = valid && (uint8_t(text[i + k]) & 0xC0) == 0x80;
        if (valid && len == 3) { // no overlong forms, no UTF-16 surrogates
            const auto c1 = uint8_t(text[i + 1]);
            valid = !(c == 0xE0 && c1 < 0xA0) && !(c == 0xED && c1 >= 0xA0);
        } else if (valid && len == 4) {
            const auto c1 = uint8_t(text[i + 1]);
            valid = !(c == 0xF0 && c1 < 0x90) && !(c == 0xF4 && c1 >= 0x90);
        }
        if (!valid) {
            ++i;
            continue;
        }
        if (len == 1 && (c < 0x20 || c == 0x7F)) { // control characters (newlines, tabs, escapes)
            ++i;
            continue;
        }
        if (out.size() + len > maxBytes) break;
        out.append(text.substr(i, len));
        i += len;
    }
    return out;
}

Json toJson(const RadialMenu& menu) {
    Json j = Json::object();
    j.set("name", menu.name);
    j.set("button", std::string(radialButtonId(menu.button)));
    Json slots = Json::array();
    for (const RadialSlot& s : menu.slots) {
        Json slot = Json::object();
        slot.set("action", s.action);
        if (!s.label.empty()) slot.set("label", s.label);
        if (!s.icon.empty()) slot.set("icon", s.icon);
        slots.push(std::move(slot));
    }
    j.set("slots", std::move(slots));
    return j;
}

Json toJson(const std::vector<RadialMenu>& menus) {
    Json j = Json::array();
    for (const RadialMenu& m : menus) j.push(toJson(m));
    return j;
}

bool radialMenuFromJson(const Json& j, const RadialVocabulary& vocab, RadialMenu& menu, int* dropped) {
    if (!j.isObject()) return false;
    int lost = 0;
    menu = RadialMenu();
    if (j["name"].isString()) menu.name = radialCleanText(j["name"].asString(), kRadialNameMax);
    if (j["button"].isString() && j["button"].asString().size() <= kRadialIdMax)
        menu.button = radialButtonFromId(j["button"].asString());
    const Json& slots = j["slots"];
    if (slots.isArray()) {
        const std::vector<Json>& items = slots.items();
        for (size_t i = 0; i < items.size(); ++i) {
            if (menu.slots.size() == size_t(kRadialSlots)) { // the rest don't fit
                lost += int(std::min<size_t>(items.size() - i, 1u << 20));
                break;
            }
            const Json& item = items[i];
            if (!item.isObject()) {
                ++lost;
                continue;
            }
            RadialSlot slot;
            slot.action = knownId(item["action"], vocab.action);
            if (slot.action.empty()) {
                ++lost;
                continue;
            }
            if (item["label"].isString()) slot.label = radialCleanText(item["label"].asString(), kRadialLabelMax);
            slot.icon = knownId(item["icon"], vocab.icon);
            if (slot.icon.empty() && item["icon"].isString() && !item["icon"].asString().empty()) ++lost;
            menu.slots.push_back(std::move(slot));
        }
    } else if (!slots.isNull()) {
        ++lost;
    }
    if (dropped) *dropped += lost;
    return true;
}

std::vector<RadialMenu> radialMenusFromJson(const Json& j, const RadialVocabulary& vocab, int* dropped) {
    std::vector<RadialMenu> menus;
    if (!j.isArray()) return menus;
    int lost = 0;
    const std::vector<Json>& items = j.items();
    for (size_t i = 0; i < items.size(); ++i) {
        if (menus.size() == size_t(kRadialMenus)) {
            lost += int(std::min<size_t>(items.size() - i, 1u << 20));
            break;
        }
        RadialMenu m;
        if (radialMenuFromJson(items[i], vocab, m, &lost)) menus.push_back(std::move(m));
        else ++lost;
    }
    if (dropped) *dropped += lost;
    return menus;
}

std::string radialMenuFileText(const RadialMenu& menu) {
    Json j = Json::object();
    j.set("format", kFormat);
    j.set("version", 1);
    j.set("menu", toJson(menu));
    return j.dump(true) + "\n";
}

bool parseRadialMenuFile(const std::string& text, const RadialVocabulary& vocab, RadialMenu& menu, std::string& error,
                         int* dropped) {
    if (text.size() > kRadialFileMax) {
        error = "That file is too big for a radial menu";
        return false;
    }
    Json j;
    if (!Json::parse(text, j, error)) return false;
    if (!j.isObject() || !j["format"].isString() || j["format"].asString() != kFormat) {
        error = "Not a SculptForge radial menu file";
        return false;
    }
    if (!radialMenuFromJson(j["menu"], vocab, menu, dropped)) {
        error = "The file has no menu in it";
        return false;
    }
    return true;
}

} // namespace sf
