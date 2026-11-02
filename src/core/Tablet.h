// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Graphics tablet settings (Preferences > Tablet), independent of SDL so they
// can be unit tested: the global pressure curve applied to every pen sample
// before a brush's own pressure curve, what the eraser end and the pen's side
// buttons do, the stroke-start pressure guard and the mouse mode (PenCursor). The app's event handling
// lives in src/app/Tablet.cpp; docs/tablet.md is the user guide.
#pragma once

#include "core/Json.h"

#include <array>
#include <cstdint>
#include <string>

namespace sf {

// Raw pen pressure (0..1) -> brush pressure:
//   t   = (p - threshold) / (fullAt - threshold), clamped to 0..1
//   out = minOut + (maxOut - minOut) * t^gamma
// gamma < 1 is soft (a light touch already gives a lot), > 1 is firm.
struct PressureCurve {
    float gamma = 1.0f;     // 0.25 .. 4
    float minOut = 0.0f;    // output at the lightest touch
    float maxOut = 1.0f;    // output at full pressure
    float threshold = 0.0f; // input at or below this counts as the lightest touch
    float fullAt = 1.0f;    // input at or above this counts as full pressure

    float apply(float pressure) const;
    bool isLinear() const;

    static PressureCurve soft() { return {0.55f}; }
    static PressureCurve linear() { return {}; }
    static PressureCurve firm() { return {1.8f}; }
};

// What the eraser end or a side button does.
enum class PenAction {
    None,
    Invert,     // like holding Alt: brushes subtract (ZBrush Zsub), Alt+drag on the background pans
    Smooth,     // like holding Shift: the Smooth brush
    RotateView, // hold the button and move the pen to orbit
    PanView,    // hold the button and move the pen to pan
    RightClick, // a right mouse button: drag to orbit on the canvas, context menus in the UI
    EraseMask,  // eraser only: the eraser removes mask (like Ctrl+Alt)
};

const char* penActionId(PenAction a);    // "invert", "right_click" ... (stored in ui.json)
const char* penActionLabel(PenAction a); // "Invert (Alt)", "Right-click" ... (Preferences > Tablet)
PenAction penActionFromId(const std::string& id, PenAction fallback);

constexpr int kPenButtons = 3; // SDL pen buttons 1 (lower barrel), 2 (upper barrel), 3 (third, e.g. Pro Pen 3D)

struct TabletSettings {
    PressureCurve curve;
    PenAction eraser = PenAction::Invert;
    // Lower button: right-click (drag to rotate, as with a Wacom's default mapping); upper: Alt (invert);
    // third: pan.
    std::array<PenAction, kPenButtons> buttons{PenAction::RightClick, PenAction::Invert, PenAction::PanView};
    // Mouse mode (#54): the tablet driver is set to move the cursor relatively, like a mouse, so the pen's
    // reported position isn't where the cursor is (PenCursor).
    bool mouseMode = false;
};

Json toJson(const TabletSettings& t);
void fromJson(const Json& j, TabletSettings& t);

// Guards the start of a pen stroke against pressure spikes: the pen touches down
// before (or after) its first pressure sample, and the value left over from the
// previous stroke (or the 1.0 assumed before any sample) may still be high. The
// first dab waits until a fresh, non-zero sample arrives; the app draws anyway at
// the first motion for pens that have never reported pressure.
class PenPressureTracker {
public:
    static constexpr float kContact = 0.002f; // below this the tip is not really pressing

    void touchDown() { sinceDown_ = false; }
    void touchUp() { sinceDown_ = false; }
    void sample(float pressure);
    float raw() const { return raw_; }
    bool everSampled() const { return everSampled_; } // false: maybe a pen without pressure
    bool readyForFirstDab() const { return sinceDown_ && raw_ > kContact; }

private:
    float raw_ = 1.0f;
    bool sinceDown_ = false;
    bool everSampled_ = false;
};

// Where the pen puts the cursor. In pen mode that is where the pen reports it is. In mouse mode (the driver's
// relative mode, TabletSettings::mouseMode) the pen moves the cursor by how far it moves, like a mouse: lifting
// it away and putting it down elsewhere on the tablet leaves the cursor where it was. Windows Ink reports the
// pen's place on the tablet even then, so following it made the cursor jump back (#54). The app passes every pen
// position (the pen's own events and SDL's mouse events for the pen) through map().
class PenCursor {
public:
    // A pen silent this long was lifted away (the driver may not say it left): the next position starts afresh.
    static constexpr uint64_t kLiftedNs = 150000000ull;

    bool mouseMode = false;

    // The pen came into range or left it: in mouse mode its next position moves the cursor from where it is.
    void proximity() { anchored_ = false; }
    // Something else (the mouse) moved the cursor to x, y.
    void cursorAt(float x, float y);
    // Where the system cursor is (window pixels), read just before the next map(). In mouse mode the driver moves
    // the system cursor itself, so that is the cursor: map() returns it for the brush and the panels alike (#72:
    // a cursor of our own, moved by the pen's motion, drifted away from the system's after the pen was lifted and
    // put down elsewhere, and clicks landed at the brush, not the arrow). Without one (a system that can't say),
    // the pen's motion moves the cursor on from where it was.
    void systemCursor(float x, float y);
    // A pen position (window pixels) at time nowNs -> the cursor's position, in place.
    void map(float& x, float& y, uint64_t nowNs);

private:
    float offsetX_ = 0.0f, offsetY_ = 0.0f; // mouse mode: cursor = pen + offset
    float cursorX_ = 0.0f, cursorY_ = 0.0f;
    float systemX_ = 0.0f, systemY_ = 0.0f;
    bool haveCursor_ = false, anchored_ = false, haveSystem_ = false;
    uint64_t lastNs_ = 0;
};

} // namespace sf
