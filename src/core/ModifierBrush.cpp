// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ModifierBrush.h"

#include <algorithm>
#include <cctype>

namespace sf {

const char* modifierSlotName(ModifierSlot s) {
    return s == ModifierSlot::Shift ? "Shift" : s == ModifierSlot::Ctrl ? "Ctrl" : "";
}

std::string modifierBrushLabel(const ModifierBrushes& m, ModifierSlot s) {
    if (s == ModifierSlot::None) return "";
    const int b = m.brush(s);
    if (b < 0 || b >= int(BrushType::Count)) return s == ModifierSlot::Shift ? "Smooth" : "Mask";
    return brushLabel(BrushType(b));
}

std::string setModifierBrush(ModifierBrushes& m, ModifierSlot s, int type) {
    if (s == ModifierSlot::None) return "";
    if (type < 0 || type >= int(BrushType::Count) || (s == ModifierSlot::Shift && type == int(BrushType::Smooth))) type = -1;
    (s == ModifierSlot::Shift ? m.shift : m.ctrl) = type;
    return std::string(modifierSlotName(s)) + " brush: " + modifierBrushLabel(m, s) + (type < 0 ? " (default)" : "");
}

bool modifierBrushFromName(std::string_view name, ModifierSlot s, int& type) {
    std::string n;
    for (unsigned char c : name) n += char(std::tolower(c));
    if (n == "default" || n == "reset" || n == "none" || n.empty() || (s == ModifierSlot::Ctrl && n == "mask")) {
        type = -1;
        return true;
    }
    BrushType b;
    if (!brushFromName(name, b)) return false;
    type = s == ModifierSlot::Shift && b == BrushType::Smooth ? -1 : int(b);
    return true;
}

ModifierSlot heldModifierSlot(const ModifierBrushes& m, bool shift, bool ctrl, bool alt, bool paintMode) {
    if (paintMode || shift == ctrl) return ModifierSlot::None;
    if (shift) return m.bound(ModifierSlot::Shift) ? ModifierSlot::Shift : ModifierSlot::None;
    return !alt && m.bound(ModifierSlot::Ctrl) ? ModifierSlot::Ctrl : ModifierSlot::None;
}

Json toJson(const ModifierBrushes& m) {
    Json j = Json::object();
    j.set("shift", std::string(m.shift >= 0 ? brushName(BrushType(m.shift)) : "default"));
    j.set("ctrl", std::string(m.ctrl >= 0 ? brushName(BrushType(m.ctrl)) : "default"));
    j.set("shiftIntensity", double(m.shiftIntensity));
    j.set("ctrlIntensity", double(m.ctrlIntensity));
    return j;
}

void fromJson(const Json& j, ModifierBrushes& m) {
    if (!j.isObject()) return;
    for (ModifierSlot s : {ModifierSlot::Shift, ModifierSlot::Ctrl}) {
        const Json& v = j[s == ModifierSlot::Shift ? "shift" : "ctrl"];
        if (int type; v.isString() && modifierBrushFromName(v.asString(), s, type)) setModifierBrush(m, s, type);
    }
    m.shiftIntensity = std::clamp(float(j["shiftIntensity"].asNumber(m.shiftIntensity)), 0.01f, 1.0f);
    m.ctrlIntensity = std::clamp(float(j["ctrlIntensity"].asNumber(m.ctrlIntensity)), 0.01f, 1.0f);
}

} // namespace sf
