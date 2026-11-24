// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Navigation bindings (Preferences > Navigation): one place for how the camera is driven. Two parts, both off by
// default so every key style keeps its gestures:
//   - hold keys: three actions in the registry ("Hold to orbit", "Hold to pan", "Hold to zoom", Input.cpp) with no
//     key of their own; bound to any key (Hotkeys, Ctrl+Alt+click or this panel), holding it turns the next press on
//     the canvas, with any button or the pen's tip, into that camera drag (left-handed and tablet users navigate
//     from the hand that rests on the keyboard; the drag goes on to the button's release, as Hold Space does);
//   - drag gestures: a mouse button plus Ctrl / Shift / Alt per move, checked before the key style's own gestures.
// Pure functions here so tests/test_nav_bindings.cpp covers them; App.cpp wires them into onMouseDown.
#pragma once

#include "core/Json.h"

#include <string>
#include <string_view>

namespace sf {

constexpr int kNavMoves = 3; // orbit, pan, zoom
constexpr const char* kNavMoveIds[kNavMoves] = {"orbit", "pan", "zoom"};
constexpr const char* kNavMoveLabels[kNavMoves] = {"Orbit", "Pan", "Zoom"};
constexpr const char* kNavHoldActions[kNavMoves] = {"view.hold_orbit", "view.hold_pan", "view.hold_zoom"};

// A custom drag gesture: a mouse button (SDL's numbering: 1 left, 2 middle, 3 right; 0 = none) with modifiers.
struct NavDrag {
    int button = 0;
    bool ctrl = false, shift = false, alt = false;
    bool set() const { return button >= 1 && button <= 3; }
    // A plain left drag sculpts (and turns the view beside the model): the left button needs a modifier.
    bool usable() const { return set() && (button != 1 || ctrl || shift || alt); }
    bool matches(int b, bool c, bool s, bool a) const { return usable() && button == b && ctrl == c && shift == s && alt == a; }
    bool operator==(const NavDrag& o) const { return button == o.button && ctrl == o.ctrl && shift == o.shift && alt == o.alt; }
};

struct NavBindings {
    NavDrag drag[kNavMoves];
    bool any() const {
        for (const NavDrag& d : drag)
            if (d.set()) return true;
        return false;
    }
};

inline const char* navButtonName(int button) {
    switch (button) {
    case 1: return "Left";
    case 2: return "Middle";
    case 3: return "Right";
    default: return "";
    }
}

// "Ctrl+Middle", "Shift+Alt+Left"; "" when unset. The same spelling as a key chord, with the button as the key.
inline std::string navDragText(const NavDrag& d) {
    if (!d.set()) return {};
    std::string out;
    if (d.ctrl) out += "Ctrl+";
    if (d.shift) out += "Shift+";
    if (d.alt) out += "Alt+";
    return out + navButtonName(d.button);
}

// "ctrl+middle", "Shift + Left", "right": any order of modifiers, any case; false for anything else (and "" / "none"
// gives an unset gesture, true).
inline bool parseNavDrag(std::string_view text, NavDrag& out) {
    NavDrag d;
    std::string word;
    auto lower = [](std::string s) {
        for (char& c : s) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        return s;
    };
    auto take = [&](std::string w) -> bool {
        w = lower(std::move(w));
        if (w.empty()) return true;
        if (w == "ctrl" || w == "control" || w == "cmd") d.ctrl = true;
        else if (w == "shift") d.shift = true;
        else if (w == "alt" || w == "option") d.alt = true;
        else if (w == "left" || w == "lmb") d.button = 1;
        else if (w == "middle" || w == "mmb") d.button = 2;
        else if (w == "right" || w == "rmb") d.button = 3;
        else if (w == "none" || w == "-") d = NavDrag();
        else return false;
        return true;
    };
    for (char c : text) {
        if (c == '+' || c == ' ' || c == '\t') {
            if (!take(word)) return false;
            word.clear();
        } else {
            word += c;
        }
    }
    if (!take(word)) return false;
    if (!d.set()) d = NavDrag(); // modifiers without a button: nothing
    out = d;
    return true;
}

// The custom gesture a press matches (0 orbit, 1 pan, 2 zoom), or -1. The first match wins when two are equal (the
// panel highlights that).
inline int navDragMatch(const NavBindings& nav, int button, bool ctrl, bool shift, bool alt) {
    for (int i = 0; i < kNavMoves; ++i)
        if (nav.drag[i].matches(button, ctrl, shift, alt)) return i;
    return -1;
}

// What the key style already does with this button and these modifiers, for the conflict highlight: "" when nothing
// (or when the gesture is unset or unusable). style: 0 Classic sculpting keys, 1 SculptForge, 2 Blender, 3 Maya.
// The custom gesture wins over it at a press; the panel only says what it takes over.
inline const char* navStyleGesture(int style, const NavDrag& d) {
    if (!d.usable()) return "";
    const bool c = d.ctrl, s = d.shift, a = d.alt;
    if (style == 2) { // Blender: middle and right drag the view (Shift pans, Ctrl zooms); Alt+left is the emulated middle
        if (d.button == 1) {
            if (a && !c && !s) return "Turn the view (Alt+left-drag, Blender's emulated middle button)";
            if (a && s && !c) return "Pan (Shift+Alt+left-drag)";
            if (a && c && !s) return "Zoom beside the model, Erase mask on it (Ctrl+Alt+left-drag)";
            if (c && !s && !a) return "Invert a stroke (Ctrl+drag)";
            if (s && !c && !a) return "Smooth (Shift+drag)";
            if (c && s && !a) return "Hide / show by a shape (Ctrl+Shift+drag)";
            return "";
        }
        if (s && !c) return "Pan (Shift+drag)";
        if (c && !s) return "Zoom (Ctrl+drag)";
        if (!c && !s && !a) return "Turn the view";
        return "";
    }
    if (style == 3) { // Maya: Alt+left / middle / right orbit / pan / zoom
        if (a && !c && !s) {
            if (d.button == 1) return "Turn the view (Alt+left-drag)";
            if (d.button == 2) return "Pan (Alt+middle-drag)";
            return "Zoom (Alt+right-drag)";
        }
        if (d.button == 1) {
            if (c && a && !s) return "Mask (Ctrl+Alt+drag)";
            if (c && !s && !a) return "Invert a stroke (Ctrl+drag)";
            if (s && !c && !a) return "Smooth (Shift+drag)";
            if (c && s && !a) return "Hide / show by a shape (Ctrl+Shift+drag)";
            return "";
        }
    }
    // Classic sculpting keys and SculptForge (and Maya's middle / right without Alt, which keep the classic gestures)
    if (d.button == 1) {
        if (s && !c && !a) return "Smooth (Shift+drag)";
        if (c && !s && !a) return style == 1 ? "Mask (Ctrl+drag; on the background: zoom)" : "Mask (Ctrl+drag; on the background: clear the mask)";
        if (a && !c && !s) return "Invert a stroke (Alt+drag; on the background: pan)";
        if (c && s && !a) return "Hide / show by a shape (Ctrl+Shift+drag)";
        if (c && a && !s) return "Erase mask (Ctrl+Alt+drag)";
        if (s && a && !c) return "Select and frame a Part (Shift+Alt+click)";
        return "";
    }
    if (d.button == 3) {
        if (c && s && !a) return "Brush size, finely (Ctrl+Shift+right-drag)";
        if (c && !a) return "Zoom (Ctrl+right-drag)";
        if (a && !c) return "Pan (Alt+right-drag)";
        if (!a && !c) return s ? "Turn the view, snapping (Shift+right-drag)" : "Turn the view (right-drag)";
        return "";
    }
    if (c) return "Zoom (Ctrl+middle-drag)";
    return "Pan (middle-drag)";
}

inline Json toJson(const NavBindings& nav) {
    Json j = Json::object();
    for (int i = 0; i < kNavMoves; ++i)
        if (nav.drag[i].set()) j.set(kNavMoveIds[i], navDragText(nav.drag[i]));
    return j;
}

inline void fromJson(const Json& j, NavBindings& nav) {
    if (!j.isObject()) return;
    for (int i = 0; i < kNavMoves; ++i) {
        const Json& v = j[kNavMoveIds[i]];
        NavDrag d;
        if (v.isString() && parseNavDrag(v.asString(), d)) nav.drag[i] = d;
    }
}

} // namespace sf
