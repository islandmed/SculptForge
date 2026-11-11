// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The touch layout (Preferences > Interface > Touch layout; on by default on Android): a Sculpt-room overlay for
// tablets in the Forge Night look. A rail on the free hand's side (a 2D thumb pad: up and down for size, sideways
// for strength; and hold keys Invert, Smooth, Mask, Lock: hold = this stroke, tap = latch), a fixed dock in the
// other bottom corner (the brush, 4 pinned, 2 recent, all brushes) under a context strip (Mirror X, falloff,
// alpha, settings), and a top bar (project and save state, the room pill, undo, redo, more). Every target is at
// least 48 dp. The chrome fades while a stroke runs. Fingers on the canvas follow finger mode (core/FingerMode.h).
// The App side is TouchShell.cpp; this is its state.
#pragma once

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct TouchShell {
    enum Key { Invert, Smooth, Mask, Lock, KeyCount };
    struct KeyState {
        bool latched = false;   // tapped: on until tapped again
        bool held = false;      // a finger (or the mouse) holds it now
        bool forStroke = false; // let go during a stroke: it lasts until that stroke ends
        bool strokeWhileHeld = false; // a stroke ran while it was held: letting go isn't a tap
        uint64_t pointer = 0;
        double downAt = 0.0;
    };
    KeyState keys[KeyCount];

    // The raw controls (hold keys, the thumb pad or the A/B sliders) are driven by finger and mouse events before
    // ImGui sees them, so a finger resting on one never holds ImGui's single mouse (the pen strokes meanwhile).
    // Their rectangles as last drawn, in window coordinates.
    struct Rect {
        ImVec2 a{0, 0}, b{0, 0};
        bool contains(ImVec2 p) const { return p.x >= a.x && p.y >= a.y && p.x < b.x && p.y < b.y; }
    };
    Rect keyRect[KeyCount], pad, slider[2];
    std::vector<Rect> chrome; // every overlay box: a touch there is the overlay's, never the canvas's

    enum class Control { None, Key, Pad, Slider };
    struct Grab {
        Control control = Control::None;
        int index = 0; // the key or the slider
        uint64_t pointer = 0;
        ImVec2 start{0, 0}, last{0, 0};
        double at = 0.0;
        bool moved = false;
    };
    std::vector<Grab> grabs;
    bool fingerStroke = false;      // finger mode's stroke is the current drag (not a pen or mouse drag it found running)
    bool swallowTouchMouse = false; // SDL's mouse events of a finger the overlay or finger mode owns, until it lifts
    std::vector<uint64_t> canvasFingers, uiFingers; // fingers finger mode has / that landed on the UI

    bool pointerOver = false;   // a mouse (not a finger) is over the window: the brush ring shows where it is
    bool desktopPanels = false; // the More menu's "Desktop panels": the desktop UI around the layout (this session)
    // The pen alone (stylus first): its side button held while it hovers, then dragged: sideways size, up and down
    // strength, with the readout at the tip. From the button press; touching down ends it (the button smooths then).
    bool penAdjust = false, penAdjusting = false;
    ImVec2 penStart{0, 0}, penLast{0, 0};
    bool showValues = false; // the thumb pad was tapped: exact Size and Strength
    float fade = 1.0f;       // chrome opacity (0.1 while a stroke runs)
    std::string toast;       // "Undo", "Redo", "History -3"...: a moment over the canvas
    double toastUntil = 0.0;
    std::vector<std::string> recent; // the dock's recent brushes (BrushItem ids), newest first
    std::string lastBrush;
    int pinnedEditing = -1; // a pinned slot long-pressed: it takes the current brush on release
};

} // namespace sf
