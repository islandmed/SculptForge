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
#include <utility>
#include <vector>

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
    Invert,     // like holding Alt: brushes subtract (Lower), Alt+drag on the background pans
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
    // How far the pen turns, pans and zooms the view per pixel, against the mouse's 1 (0.1 - 2). A tablet maps
    // its whole area onto the screen, so a short pen movement crosses many pixels and the view seemed to spin
    // (GonzaloHernandez3D, Huion on Windows): turn it down to taste.
    float viewSpeed = 1.0f;
    // Classic pen timing (Preferences > Tablet > Troubleshooting details): the pen as in 0.11.15 and before, should
    // the new timing misbehave with someone's tablet: the stroke moves at a sample's motion without waiting for its
    // pressure (App::flushPenMotion), and no lost sample is held (PenPressureTracker::holdLostZero).
    bool classicTiming = false;
};

Json toJson(const TabletSettings& t);
void fromJson(const Json& j, TabletSettings& t);

// Guards the start of a pen stroke against pressure spikes: the pen touches down
// before (or after) its first pressure sample, and the value left over from the
// previous stroke (or the 1.0 assumed before any sample) may still be high. The
// first dab waits until a fresh, non-zero sample arrives; the app draws anyway at
// the first motion for pens that have never reported pressure.
//
// A pen without a pressure axis (Windows Ink switched off in the Wacom driver, some old tablets) may still send
// pressure 0 while touching: after kSilentMotions touching motions without any real pressure the pen counts as
// pressure-less and draws at full pressure (noPressure) instead of never starting the stroke.
//
// A sample that lost its value keeps the last pressure (sample() returns false): never a NaN or infinity, and with
// holdLostZero (Linux) not a 0 or below while the tip touches after it already pressed. SDL's X11 pens report 0 for a
// motion whose valuators leave pressure out (an unchanged value, some drivers), and its Wayland pens 0 for a frame
// that also carries a hover distance; either made a stroke's pressure drop to nothing and back between samples.
class PenPressureTracker {
public:
    static constexpr float kContact = 0.002f; // below this the tip is not really pressing
    static constexpr int kSilentMotions = 8;  // touching motions without pressure before the pen counts as without

    bool holdLostZero = false;

    void touchDown() {
        sinceDown_ = false;
        down_ = true;
    }
    void touchUp() {
        sinceDown_ = false;
        down_ = false;
    }
    bool sample(float pressure); // false: a lost sample, the last pressure stays
    void motion(); // the pen moved (counts touching motions that brought no pressure)
    void reset() { // another pen or tablet: forget what this one did
        const bool hold = holdLostZero;
        *this = PenPressureTracker();
        holdLostZero = hold;
    }
    float raw() const { return raw_; }
    float pressure() const { return noPressure() ? 1.0f : raw_; } // what brushes get, before the curve
    bool everSampled() const { return everSampled_; } // false: maybe a pen without pressure
    bool hasPressure() const { return pressed_; }     // the pen has reported a real pressure
    bool noPressure() const { return !pressed_ && silentMotions_ >= kSilentMotions; }
    bool readyForFirstDab() const { return (sinceDown_ && raw_ > kContact) || noPressure(); }

private:
    float raw_ = 1.0f;
    bool sinceDown_ = false;
    bool everSampled_ = false;
    bool down_ = false;
    bool pressed_ = false;
    int silentMotions_ = 0;
};

// What Preferences > Tablet > Troubleshooting shows and "Copy details" puts in a bug report: the pen's events as
// the app saw them (counts since launch), the last pressure and the last stroke's range.
struct PenDiagnostics {
    uint64_t proximity = 0, down = 0, up = 0, motion = 0, pressure = 0, otherAxes = 0, buttons = 0;
    uint64_t lostSamples = 0;     // pressure samples that lost their value (PenPressureTracker::sample)
    uint64_t strayMouse = 0;      // the system's own mouse events while the pen was in range (ignored)
    uint64_t penTouchEvents = 0;  // touch events SDL made from the pen (ignored)
    float lastPressure = -1.0f;   // -1: no pressure sample yet
    float strokeMin = -1.0f, strokeMax = -1.0f; // the last stroke's raw pressure range (-1: none yet)
    bool axesAfterMotion = false; // seen: a pen's pressure arrives after its motion (SDL's order on every platform)

    void touchDown() { strokeMin = strokeMax = -1.0f; }
    void sampled(float p, bool touching);
};

// The bug-report text: one "name: value" line per fact (SDL version, video driver, package ...), then the counts.
std::string penReport(const std::vector<std::pair<std::string, std::string>>& facts, const PenDiagnostics& d);

// The pressure a re-pressed stamp uses (Drag Stamp, Slide Stamp: each move undoes the stamp and presses it again):
// the firmest of the drag so far. A pen's last samples before it lifts fall towards 0 (every tablet does, whether
// the pressure comes from Windows Ink, Wintab or Linux), and the stamp re-pressed with them went weak or vanished at
// release (#146); the stamp now ends as the drag showed it.
class StampPressure {
public:
    void reset() { peak_ = 0.0f; }
    float hold(float pressure) {
        if (pressure > peak_) peak_ = pressure;
        return peak_;
    }

private:
    float peak_ = 0.0f;
};

// Tablets plugged in or out while the app runs. SDL has no pen hot-plug events: a new pen is a new SDL_PenID at
// its first proximity, and the tablet itself shows up as a mouse device (SDL_EVENT_MOUSE_ADDED / _REMOVED) with
// the maker's name on Linux and usually on Windows. A mouse device added after launch with a generic name may be
// the tablet too: if a new pen follows it, it was, and unplugging it says so.
class TabletHotplug {
public:
    enum class Note { None, NewPen, Connected, Disconnected };

    Note mouseAdded(uint32_t id, const std::string& name);
    Note mouseRemoved(uint32_t id);
    Note penIn(uint32_t penId);

private:
    std::vector<uint32_t> tabletMice_, pens_;
    uint32_t candidate_ = 0;   // a generic mouse device added after launch (0: none)
    bool announced_ = false;   // "Tablet connected" already said for the device just added
};

// The name of an input device that is a pen tablet or pen display (Wacom, Huion, XP-Pen ...).
bool looksLikeTablet(const std::string& name);

// Where the pen puts the cursor. In pen mode that is where the pen reports it is. In mouse mode
// (TabletSettings::mouseMode) the pen moves the cursor by how far it moves, like a mouse: lifting it away and
// putting it down elsewhere on the tablet leaves the cursor where it was. Tablet drivers report the pen's place on
// the tablet (Windows Ink even with the driver in its own mouse mode), so following it made the cursor jump back
// (#54), and following the system cursor made Mouse mode the same as Pen mode with the driver in pen mode (#72).
// Only the pen's movement counts. The app passes every pen position (the pen's own events and its mouse events)
// through map().
class PenCursor {
public:
    // A pen silent this long was lifted away (the driver may not say it left): the next position starts afresh.
    static constexpr uint64_t kLiftedNs = 150000000ull;

    bool mouseMode = false;

    // The pen came into range or left it: in mouse mode its next position moves the cursor from where it is.
    void proximity() { anchored_ = false; }
    // Something else (the mouse) moved the cursor to x, y.
    void cursorAt(float x, float y);
    // A pen position (window pixels) at time nowNs -> the cursor's position, in place.
    void map(float& x, float& y, uint64_t nowNs);

private:
    float offsetX_ = 0.0f, offsetY_ = 0.0f; // mouse mode: cursor = pen + offset
    float cursorX_ = 0.0f, cursorY_ = 0.0f;
    bool haveCursor_ = false, anchored_ = false;
    uint64_t lastNs_ = 0;
};

} // namespace sf
