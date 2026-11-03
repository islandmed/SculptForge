// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Graphics tablets: pen pressure through the global curve, the eraser end,
// the pen's side buttons, and Preferences > Tablet (device, live pressure,
// draw-test area, curve editor, mappings). SDL3 reports pens the same way on
// Windows (Windows Ink), macOS, Wayland and X11 and also turns the tip into
// left mouse button events (SDL_PEN_MOUSEID), which the canvas and the UI use
// like a mouse. The settings live in UiPrefs::tablet (Store Config keeps them);
// the math is in core/Tablet.h. User guide: docs/tablet.md.
#include "app/App.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace sf {

namespace {

ImVec4 col(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

constexpr const char* kButtonNames[kPenButtons] = {"Lower button", "Upper button", "Third button"};

} // namespace

// The pen tip works the panels and the canvas as a left mouse button (SDL_PEN_MOUSEID). SDL 3.2 makes those mouse
// events itself, but keeps one "a pen is touching" flag for all pens: on Windows, a pen SDL drops while it touches
// (WM_POINTERCAPTURECHANGED, e.g. when a drag captures the mouse) leaves the flag set, and from then on no pen gets
// a mouse event, while its own events still come. The pen sculpted once, then could neither pick a second brush nor
// sculpt again (#95, Huion Kamvas, display tablets, Wacom with Windows Ink on or off). So SDL's pen mouse events are
// off (SDL_HINT_PEN_MOUSE_EVENTS) and ignored should a system SDL send them anyway, its clicks for the side buttons
// included (those depend on the SDL version; Preferences > Tablet maps the buttons, see penButton). penMouseEvents
// makes the tip's events from the pen's own, in SDL's order, and lets go of the button whenever the pen goes.
bool App::dropPenMouseEvent(const SDL_Event& e) const {
    if (penMouseSending_) return false;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: return e.motion.which == SDL_PEN_MOUSEID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: return e.button.which == SDL_PEN_MOUSEID;
    case SDL_EVENT_MOUSE_WHEEL: return e.wheel.which == SDL_PEN_MOUSEID;
    default: return false;
    }
}

void App::sendPenMouse(SDL_EventType type, float x, float y, SDL_WindowID window) {
    SDL_Event m{};
    m.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        m.motion.windowID = window;
        m.motion.which = SDL_PEN_MOUSEID;
        m.motion.state = penMouseDown_ ? SDL_BUTTON_LMASK : 0;
        m.motion.x = x;
        m.motion.y = y;
        m.motion.xrel = x - penMouseX_;
        m.motion.yrel = y - penMouseY_;
    } else {
        m.button.windowID = window;
        m.button.which = SDL_PEN_MOUSEID;
        m.button.button = SDL_BUTTON_LEFT;
        m.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        m.button.clicks = 1;
        m.button.x = x;
        m.button.y = y;
    }
    penMouseX_ = x;
    penMouseY_ = y;
    penMouseSending_ = true;
    dispatchEvent(m);
    penMouseSending_ = false;
}

// before: called ahead of the pen event (a pen leaving, or coming back without having left: let go first);
// otherwise after it, as SDL orders them (the pen's touch, then the mouse's button).
void App::penMouseEvents(const SDL_Event& e, bool before) {
    if (before) {
        if ((e.type == SDL_EVENT_PEN_PROXIMITY_OUT || e.type == SDL_EVENT_PEN_PROXIMITY_IN) && penMouseDown_) {
            penMouseDown_ = false;
            sendPenMouse(SDL_EVENT_MOUSE_BUTTON_UP, penMouseX_, penMouseY_, e.pproximity.windowID);
        }
        return;
    }
    switch (e.type) {
    case SDL_EVENT_PEN_DOWN:
        if (penMouseDown_) break; // another pen already touches
        sendPenMouse(SDL_EVENT_MOUSE_MOTION, e.ptouch.x, e.ptouch.y, e.ptouch.windowID);
        penMouseDown_ = true;
        penMouseOwner_ = e.ptouch.which;
        sendPenMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, e.ptouch.x, e.ptouch.y, e.ptouch.windowID);
        break;
    case SDL_EVENT_PEN_UP:
        if (!penMouseDown_ || e.ptouch.which != penMouseOwner_) break;
        penMouseDown_ = false;
        sendPenMouse(SDL_EVENT_MOUSE_BUTTON_UP, e.ptouch.x, e.ptouch.y, e.ptouch.windowID);
        break;
    case SDL_EVENT_PEN_MOTION:
        if (penMouseDown_ && e.pmotion.which != penMouseOwner_) break;
        sendPenMouse(SDL_EVENT_MOUSE_MOTION, e.pmotion.x, e.pmotion.y, e.pmotion.windowID);
        if (!penMouseDown_) {
            // A hovering pen presses nothing. Should the UI still hold a left button (a driver's press whose
            // release never came), let go of it: the pen's next tap is a click again.
            ImGuiIO& io = ImGui::GetIO();
            if (io.MouseDown[ImGuiMouseButton_Left] && !(SDL_GetGlobalMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK))
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        }
        break;
    default:
        break;
    }
}

// Windows Ink and some tablet drivers (Ugee on X11, #7; Huion) send mouse events of their own beside the pen's, often
// at an old position (where the pen last touched). Following those made the brush ring flick between two places,
// the view shake, and the UI see the cursor jump: a click landed somewhere else, a drag on the Move button threw the
// model off the canvas (#13). So while the pen is in range (heard from in the last second) only the pen moves the
// cursor, for the canvas and the UI alike. The left button too while the pen touches (or just touched or lifted):
// a driver's own press beside the pen's, whose release may never come, left the panels holding a button, and the
// pen's next taps were not clicks (#95). A mouse click while the pen only hovers still counts.
bool App::strayMouseMotion(const SDL_Event& e) const {
    const bool motion = e.type == SDL_EVENT_MOUSE_MOTION && e.motion.which != SDL_PEN_MOUSEID;
    const bool left = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                      e.button.which != SDL_PEN_MOUSEID && e.button.button == SDL_BUTTON_LEFT;
    if (!motion && !left) return false;
    if (dragFromPen_ && drag_ != Drag::None) return true;
    if (left) return penInProximity_ && (penTouching_ || clickClockNs() - lastPenTouchNs_ < 100000000ull);
    return penInProximity_ && SDL_GetTicksNS() - lastPenNs_ < 1000000000ull;
}

// ImGui's SDL backend also reads the system cursor every frame while no button is held (the cursor minus the window
// position, on Windows, macOS and X11). With a pen that cursor can be stale or elsewhere (and in Mouse mode it is
// somewhere else by design), so ImGui's mouse would jump between it and the pen each frame: put it back on the pen.
void App::keepImGuiOnPen() {
    ImGuiIO& io = ImGui::GetIO();
    const bool soft = penSoftCursor();
    io.MouseDrawCursor = soft && !viewportCursorWanted(); // over the view the brush ring is the cursor
    if (!soft && (!penInProximity_ || SDL_GetTicksNS() - lastPenNs_ >= 1000000000ull)) return;
    io.AddMousePosEvent(mouseX_, mouseY_);
}

// Mouse mode with the pen in range: the system arrow is wherever the driver puts it (where the pen is on the
// tablet, with the driver in pen mode), not where the pen's relative movement took the cursor. It is hidden and
// the panels draw their arrow at the cursor instead, so the arrow, the brush and the clicks agree (#72). (A pen
// resting still in range sends nothing, so this lasts until it leaves or the mouse moves, not a second.)
bool App::penSoftCursor() const { return uiPrefs_.tablet.mouseMode && penInProximity_; }

// Mouse mode (Preferences > Tablet): the pen reports where it is on the tablet, but the cursor moves by how far it
// moves, like a mouse: lifting it and putting it down elsewhere leaves the cursor where it was (#54). Every pen
// position, the pen's own events and its mouse events, goes through penCursor_ before anything reads it, and
// the mouse moving the cursor in between moves where the pen carries on from. Only the pen's movement counts, not
// the system cursor: with the tablet driver in pen mode that is where the pen is, which made Mouse mode the same as
// Pen mode (#72, 0.10.8); the arrow follows the cursor instead (penSoftCursor).
void App::mapPenPosition(SDL_Event& e) {
    penCursor_.mouseMode = uiPrefs_.tablet.mouseMode;
    const Uint64 now = SDL_GetTicksNS();
    switch (e.type) {
    case SDL_EVENT_PEN_PROXIMITY_IN:
    case SDL_EVENT_PEN_PROXIMITY_OUT: penCursor_.proximity(); break;
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP: penCursor_.map(e.ptouch.x, e.ptouch.y, now); break;
    case SDL_EVENT_PEN_MOTION: penCursor_.map(e.pmotion.x, e.pmotion.y, now); break;
    case SDL_EVENT_PEN_AXIS: penCursor_.map(e.paxis.x, e.paxis.y, now); break;
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP: penCursor_.map(e.pbutton.x, e.pbutton.y, now); break;
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which == SDL_PEN_MOUSEID) penCursor_.map(e.motion.x, e.motion.y, now);
        else penCursor_.cursorAt(e.motion.x, e.motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.which == SDL_PEN_MOUSEID) penCursor_.map(e.button.x, e.button.y, now);
        break;
    default:
        break;
    }
}

void App::handlePenEvent(const SDL_Event& e) {
    lastPenNs_ = SDL_GetTicksNS();
    switch (e.type) {
    case SDL_EVENT_PEN_PROXIMITY_IN:
        // A pen coming into range while a pen drag is still open: it left without saying so (some drivers never
        // do). End that first, with the buttons it held, instead of carrying the drag on from somewhere else.
        if (penInProximity_ || (dragFromPen_ && drag_ != Drag::None)) penLeft();
        penSeen_ = penInProximity_ = true;
        penJump_ = true;
        penId_ = e.pproximity.which;
        if (const TabletHotplug::Note note = tabletHotplug_.penIn(uint32_t(e.pproximity.which)); note != TabletHotplug::Note::None)
            tabletNote(note, {});
        if (penName_.empty()) detectPenName();
        break;
    case SDL_EVENT_PEN_PROXIMITY_OUT:
        penLeft();
        break;
    case SDL_EVENT_PEN_DOWN:
        penSeen_ = penInProximity_ = penTouching_ = true;
        penId_ = e.ptouch.which;
        penEraser_ = e.ptouch.eraser;
        penTracker_.touchDown(); // SDL's left button down follows and starts the stroke
        lastPenTouchNs_ = clickClockNs();
        break;
    case SDL_EVENT_PEN_UP:
        // End the stroke here rather than waiting for SDL's mouse button up: the pen may leave
        // proximity right after, and some drivers then never send it.
        // The same for a tip drag on the background (rotate / pan / zoom); a side button's view drag goes on.
        if (drag_ != Drag::None && dragFromPen_ && !penNavButton_) onMouseUp();
        penTouching_ = penEraser_ = false;
        penTracker_.touchUp();
        lastPenTouchNs_ = clickClockNs();
        break;
    case SDL_EVENT_PEN_MOTION:
        penSeen_ = penInProximity_ = true;
        penId_ = e.pmotion.which;
        penTracker_.motion();
        break;
    case SDL_EVENT_PEN_AXIS:
        if (e.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
            penTracker_.sample(e.paxis.value);
            if (drag_ == Drag::Stroke && firstDabPending_ && penTracker_.readyForFirstDab()) firstDab(lastX_, lastY_);
        } else if (e.paxis.axis == SDL_PEN_AXIS_XTILT || e.paxis.axis == SDL_PEN_AXIS_YTILT) {
            (e.paxis.axis == SDL_PEN_AXIS_XTILT ? penTiltX_ : penTiltY_) = e.paxis.value;
            penTiltSeen_ = penTiltSeen_ || e.paxis.value != 0.0f;
        }
        break;
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP:
        penButton(e.pbutton.button, e.pbutton.down, e.pbutton.x, e.pbutton.y);
        break;
    default:
        break;
    }
}

void App::penButton(int button, bool down, float x, float y) {
    if (button < 1 || button > 31) return;
    const uint32_t bit = 1u << button;
    if (((penButtons_ & bit) != 0) == down) return;
    penButtons_ = down ? penButtons_ | bit : penButtons_ & ~bit;
    penSeen_ = true;
    if (radialPenButton(button, down, x, y)) return; // a radial menu's button (RadialMenu.cpp)
    // Invert and Smooth need nothing here: inputMods() reports them as Alt / Shift while held.
    const PenAction a = penButtonAction(button);
    if (a != PenAction::RotateView && a != PenAction::PanView && a != PenAction::RightClick) return;
    ImGuiIO& io = ImGui::GetIO();
    if (a == PenAction::RightClick) { // context menus and right-click items in the UI
        io.AddMouseSourceEvent(ImGuiMouseSource_Pen);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, down);
    }
    if (down) {
        if (drag_ != Drag::None || io.WantCaptureMouse || !canvasRect().contains(x, y)) return;
        lastInputPen_ = true;
        if (a == PenAction::RightClick) {
            onMouseDown(x, y, SDL_BUTTON_RIGHT); // drag to rotate (Shift: pan), as with the mouse
        } else {
            mouseX_ = x;
            mouseY_ = y;
            drag_ = a == PenAction::RotateView ? Drag::Orbit : Drag::Pan;
            dragFromPen_ = true;
        }
        if (drag_ != Drag::None) penNavButton_ = button;
    } else if (penNavButton_ == button) {
        if (a == PenAction::RightClick) rightClickRelease(); // a tap of the button: the QuickMenu, as the mouse's
        if (drag_ == Drag::Orbit || drag_ == Drag::Pan || drag_ == Drag::Zoom) {
            drag_ = Drag::None;
            dragFromPen_ = false;
        }
        penNavButton_ = 0;
    }
}

void App::penLeft() {
    // A pen lifted out of range mid-stroke: finish the stroke (one undo step) instead of leaving it open.
    if (radial_.hold == RadialHold::Pen) closeRadialMenu(); // a pen button held a radial menu open
    if (drag_ != Drag::None && dragFromPen_) onMouseUp();
    ImGuiIO& io = ImGui::GetIO();
    if (penTouching_) io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    for (int b = 1; b <= kPenButtons; ++b)
        if ((penButtons_ & (1u << b)) && penButtonAction(b) == PenAction::RightClick)
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    penInProximity_ = penTouching_ = penEraser_ = false;
    penButtons_ = 0;
    penNavButton_ = 0;
    firstDabPending_ = false;
    penTracker_.touchUp();
    penJump_ = true;
    lastInputPen_ = false; // whatever moves the cursor next is a mouse until the pen says otherwise
}

// A tablet plugged in after launch works without a restart (SDL finds its pens as they come into range); this
// forgets what the last one did (its name, pressure, held buttons) and says so in the status bar.
void App::tabletDeviceEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_ADDED) {
        wintab_.open(window_); // Windows: a tablet without Windows Ink plugged in after launch
        const char* name = SDL_GetMouseNameForID(e.mdevice.which);
        const std::string n = name ? name : "";
        tabletNote(tabletHotplug_.mouseAdded(uint32_t(e.mdevice.which), n), n);
    } else if (e.type == SDL_EVENT_MOUSE_REMOVED) {
        tabletNote(tabletHotplug_.mouseRemoved(uint32_t(e.mdevice.which)), {});
    }
}

void App::tabletNote(TabletHotplug::Note note, const std::string& name) {
    if (note == TabletHotplug::Note::None) return;
    if (note == TabletHotplug::Note::Disconnected) {
        if (penInProximity_ || penButtons_ || (dragFromPen_ && drag_ != Drag::None)) penLeft(); // no Alt left held
        penTracker_.reset();
        wintabPen_ = wintabTip_ = false;
        status_ = "Tablet disconnected";
        return;
    }
    penTracker_.reset(); // a new pen: whether it has pressure is its own
    penTiltSeen_ = false;
    if (note == TabletHotplug::Note::NewPen) return;
    if (!name.empty()) penName_ = name;
    else detectPenName();
    status_ = penName_.empty() ? std::string("Tablet connected") : "Tablet connected: " + penName_;
}

const char* App::pressureStatus() const {
    if (!penSeen_) return "Pressure: no pen seen yet";
    if (penTracker_.hasPressure()) return "Pressure: on";
    if (penTracker_.noPressure() || penTracker_.everSampled()) return "Pressure: not detected";
    return "Pressure: touch the tablet to check";
}

void App::drawPressureStatus() {
    const bool ok = penSeen_ && penTracker_.hasPressure();
    ImGui::TextColored(col(ok ? ui::color::accent : ui::color::textDim), "%s", pressureStatus());
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_LC_CIRCLE_HELP "  Troubleshooting##pressure_help")) guide_.open("troubleshooting.md#pen-tablets");
#if defined(_WIN32)
    ui::HelpText("Pressure", nullptr,
                 "No pressure? In Wacom Tablet Properties > Mapping, switch \"Use Windows Ink\" (on for most tablets, "
                 "off for some older drivers), then bring the pen back. Without pressure the pen draws at full "
                 "strength.",
                 "Preferences > Tablet");
#else
    ui::HelpText("Pressure", nullptr,
                 "Whether the pen reports pressure. Without it the pen draws at full strength; check the tablet in "
                 "your desktop's tablet settings.",
                 "Preferences > Tablet");
#endif
}

// Windows with "Use Windows Ink" off in the tablet driver: no SDL pen events, the driver moves the mouse, and the
// pressure comes from Wintab. While Windows Ink speaks (SDL pen events in the last second) it is the one source.
void App::wintabEvent(const SDL_Event& e) {
    const bool mouse = (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.which != SDL_PEN_MOUSEID) ||
                       ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                        e.button.which != SDL_PEN_MOUSEID);
    if (!mouse || !wintab_.isOpen()) return;
    const Uint64 now = SDL_GetTicksNS();
    if (penInProximity_ && now - lastPenNs_ < 1000000000ull) return; // Windows Ink is on: SDL's pen events count
    WintabPressure::Sample s;
    if (wintab_.poll(s)) {
        wintabNs_ = now;
        if (!wintabPen_) {
            wintabPen_ = penSeen_ = true;
            if (penName_.empty()) penName_ = "Tablet (Wintab)";
        }
        if (s.tip != wintabTip_) {
            wintabTip_ = s.tip;
            if (s.tip) penTracker_.touchDown();
            else penTracker_.touchUp();
        }
        penTracker_.sample(s.pressure);
        if (s.tip) penTracker_.motion();
        penEraser_ = s.tip && s.eraser;
    } else if (wintabPen_ && now - wintabNs_ > 250000000ull) {
        wintabPen_ = wintabTip_ = penEraser_ = false; // the mouse itself: full pressure
        penTracker_.touchUp();
    }
}

float App::strokePressure() const {
    if (!lastInputPen_ && !wintabPen_) return 1.0f; // the mouse always presses fully
    return uiPrefs_.tablet.curve.apply(penTracker_.pressure()); // a pen without pressure: full
}

SDL_Keymod App::inputMods(bool latch) const {
    SDL_Keymod mods = keyMods();
    for (int b = 1; b <= kPenButtons; ++b) {
        if (!(penButtons_ & (1u << b))) continue;
        const PenAction a = penButtonAction(b);
        if (a == PenAction::Invert) mods |= SDL_KMOD_LALT;
        else if (a == PenAction::Smooth) mods |= SDL_KMOD_LSHIFT;
    }
    if (latch) { // the stroke popup's Smooth / Mask / Select (StrokePopup.cpp)
        const StrokeLatchKeys k = strokeLatchKeys(strokeLatch());
        if (k.shift) mods |= SDL_KMOD_LSHIFT;
        if (k.ctrl) mods |= SDL_KMOD_LCTRL;
    }
    return mods;
}

SDL_Keymod App::keyMods() const {
    SDL_Keymod mods = SDL_GetModState();
    if (blenderStyle()) mods = blenderMods(mods); // Blender's Ctrl inverts, its Mask brush masks (Input.cpp)
    if (mayaStyle()) { // Maya: Ctrl inverts (as ZBrush's Alt), Ctrl+Alt masks (as ZBrush's Ctrl)
        const bool ctrl = (mods & SDL_KMOD_CTRL) != 0, alt = (mods & SDL_KMOD_ALT) != 0, shift = (mods & SDL_KMOD_SHIFT) != 0;
        // Ctrl+Alt+Shift erases the mask (#32): ZBrush's Ctrl+Alt, not Ctrl+Shift (visibility).
        if (ctrl && alt && shift) mods = SDL_Keymod((mods & ~SDL_KMOD_SHIFT) | SDL_KMOD_LCTRL | SDL_KMOD_LALT);
        else if (ctrl && alt) mods = SDL_Keymod((mods & ~SDL_KMOD_ALT) | SDL_KMOD_LCTRL);
        else if (ctrl && !shift) mods = SDL_Keymod((mods & ~SDL_KMOD_CTRL) | SDL_KMOD_LALT);
    }
    return mods;
}

bool App::erasing() const { return (lastInputPen_ || wintabPen_) && penEraser_; }

void App::firstDab(float x, float y) {
    if (lastInputPen_ && !penTracker_.readyForFirstDab()) {
        firstDabPending_ = true; // strokeTo / the next pressure sample places it
        return;
    }
    firstDabPending_ = false;
    lastStrokePressure_ = strokePressure();
    if (asyncStroke_) dabQueue_.push({x, y, lastStrokePressure_}); // the solver thread places it (AsyncStroke.cpp)
    else dabAt(x, y, lastStrokePressure_);
}

// SDL has no pen names, so on Linux read the kernel's input device list; elsewhere the pen stays unnamed.
void App::detectPenName() {
#if defined(__linux__)
    std::ifstream f("/proc/bus/input/devices");
    std::string line, fallback;
    auto has = [](std::string s, const char* what) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s.find(what) != std::string::npos;
    };
    while (std::getline(f, line)) {
        if (line.rfind("N: Name=\"", 0) != 0) continue;
        std::string name = line.substr(9);
        if (!name.empty() && name.back() == '"') name.pop_back();
        if (has(name, "pen") || has(name, "stylus")) {
            penName_ = name;
            return;
        }
        if (fallback.empty() && (has(name, "wacom") || has(name, "huion") || has(name, "xp-pen") || has(name, "tablet") ||
                                 has(name, "gaomon") || has(name, "ugee")))
            fallback = name;
    }
    penName_ = fallback;
#endif
}

// ---------------------------------------------------------------------------
// Preferences > Tablet
// ---------------------------------------------------------------------------

void App::drawTabletSection() {
    TabletSettings& t = uiPrefs_.tablet;
    const float s = ImGui::GetFontSize() / 15.0f;
    const float width = ImGui::GetContentRegionAvail().x;
    const char* path = "Preferences > Tablet";

    // --- Device ---
    if (!penSeen_) {
        ImGui::TextUnformatted(ICON_LC_PEN_LINE "  No pen seen yet");
        ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::textDim));
        ImGui::PushTextWrapPos(0.0f);
#if defined(_WIN32)
        ImGui::TextUnformatted(tr("Bring the pen close to the tablet. Wacom: turn on \"Use Windows Ink\" in Wacom Tablet "
                               "Properties > Mapping; Huion / XP-Pen: enable Windows Ink in the driver."));
#else
        ImGui::TextUnformatted(tr("Bring the pen close to the tablet. Pressure works through the system's tablet driver."));
#endif
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    } else {
        ImGui::TextColored(col(ui::color::accent), ICON_LC_PEN_LINE);
        ImGui::SameLine();
        ImGui::TextUnformatted(penName_.empty() ? "Pen detected" : penName_.c_str());
        ui::HelpText("Pen", nullptr, "The pen SculptForge last saw, and whether it is in range or touching.", path);
        std::string info = penTouching_ ? (penEraser_ ? "eraser touching" : "touching") : penInProximity_ ? "in range" : "away";
        const char* sep = " \xc2\xb7 "; // middle dot
#if SDL_VERSION_ATLEAST(3, 4, 0)
        const SDL_PenDeviceType type = SDL_GetPenDeviceType(penId_);
        if (type == SDL_PEN_DEVICE_TYPE_DIRECT) info += std::string(sep) + "pen display";
        else if (type == SDL_PEN_DEVICE_TYPE_INDIRECT) info += std::string(sep) + "tablet";
#endif
        if (const char* driver = SDL_GetCurrentVideoDriver()) info += sep + std::string(driver);
        ImGui::TextColored(col(ui::color::textDim), "%s", info.c_str());
    }

    drawPressureStatus();

    // --- Pen or mouse mode (#54) ---
    ImGui::Checkbox(tr("Mouse mode (relative)##tablet_mouse_mode"), &t.mouseMode);
    ui::HelpText("Mouse mode", nullptr,
                 "Turn on if the tablet driver is set to Mouse mode: the pen moves the cursor by how far it moves, and "
                 "lifting it and putting it down elsewhere leaves the cursor where it was. Off (Pen mode): the cursor "
                 "is where the pen is.",
                 path);

    // --- Live pressure ---
    const float raw = penTracker_.raw(), curved = t.curve.apply(raw);
    char overlay[64];
    std::snprintf(overlay, sizeof overlay, "Pressure %.2f  " ICON_LC_ARROW_RIGHT "  %.2f", raw, curved);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col(ui::color::accent));
    ImGui::ProgressBar(penSeen_ ? curved : 0.0f, ImVec2(-FLT_MIN, 0), penSeen_ ? overlay : "Pressure -");
    ImGui::PopStyleColor();
    ui::HelpText("Pressure meter", nullptr,
                 "The pen's pressure as the tablet reports it, and after the pressure curve below (what brushes get).", path);
    if (penTiltSeen_) {
        ImGui::Text(tr("Tilt  X %+.0f" "\xc2\xb0" "   Y %+.0f" "\xc2\xb0"), penTiltX_, penTiltY_);
        ui::HelpText("Tilt", nullptr, "The pen's tilt, as the tablet reports it (brushes don't use it yet).", path);
    } else if (penSeen_) {
        ImGui::TextColored(col(ui::color::textDim), "Tilt: not reported by this pen");
    }

    // --- Draw-test area: the stroke's width follows the pressure, the line along the bottom plots it ---
    const ImVec2 area(width, 86.0f * s);
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tablet_test", area);
    ui::HelpText("Pressure test", nullptr,
                 "Draw here with the pen: the stroke is as thick as the brush pressure, and the line at the bottom plots "
                 "it along the stroke.",
                 path);
    if (ImGui::IsItemActivated()) tabletTest_.clear();
    if (ImGui::IsItemActive()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const ImVec2 rel(std::clamp(m.x - a0.x, 0.0f, area.x), std::clamp(m.y - a0.y, 0.0f, area.y));
        const float p = strokePressure();
        if (tabletTest_.empty() || std::abs(tabletTest_.back().at.x - rel.x) + std::abs(tabletTest_.back().at.y - rel.y) > 0.5f ||
            std::abs(tabletTest_.back().pressure - p) > 0.01f)
            if (tabletTest_.size() < 4000) tabletTest_.push_back({rel, lastInputPen_ ? raw : 1.0f, p});
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a1(a0.x + area.x, a0.y + area.y);
    dl->AddRectFilled(a0, a1, IM_COL32(0x18, 0x18, 0x1a, 0xff), 3.0f * s);
    dl->AddRect(a0, a1, ImGui::IsItemHovered() ? ui::color::accent : ui::color::border, 3.0f * s);
    if (tabletTest_.empty()) {
        const char* hint = "Draw here to test the pressure";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(a0.x + (area.x - ts.x) * 0.5f, a0.y + (area.y - ts.y) * 0.5f), ui::color::textDim, hint);
    } else {
        dl->PushClipRect(a0, a1, true);
        for (size_t i = 0; i < tabletTest_.size(); ++i) {
            const TabletTestSample& q = tabletTest_[i];
            const ImVec2 p(a0.x + q.at.x, a0.y + q.at.y);
            const float r = 0.75f * s + q.pressure * 9.0f * s;
            if (i > 0) {
                const TabletTestSample& prev = tabletTest_[i - 1];
                dl->AddLine(ImVec2(a0.x + prev.at.x, a0.y + prev.at.y), p, ui::color::accent, 2.0f * r);
            }
            dl->AddCircleFilled(p, r, ui::color::accent, 12);
        }
        // Pressure along the stroke (left to right = first to last sample).
        const float plotH = 26.0f * s, n = float(std::max<size_t>(tabletTest_.size() - 1, 1));
        for (size_t i = 1; i < tabletTest_.size(); ++i) {
            auto at = [&](size_t k) {
                return ImVec2(a0.x + 4.0f * s + (area.x - 8.0f * s) * float(k) / n, a1.y - 3.0f * s - plotH * tabletTest_[k].pressure);
            };
            dl->AddLine(at(i - 1), at(i), IM_COL32(255, 255, 255, 170), 1.5f * s);
        }
        dl->PopClipRect();
    }

    // --- Global pressure curve ---
    ImGui::Spacing();
    ImGui::TextColored(col(ui::color::textDim), "Pressure curve (all brushes)");
    const float graphW = width, graphH = 96.0f * s;
    const ImVec2 g0 = ImGui::GetCursorScreenPos(), g1(g0.x + graphW, g0.y + graphH);
    ImGui::Dummy(ImVec2(graphW, graphH));
    ui::HelpText("Pressure curve", nullptr,
                 "How pen pressure (left to right) becomes brush pressure (bottom to top), before each brush's own curve. "
                 "The dot is the pen right now.",
                 path);
    dl->AddRectFilled(g0, g1, IM_COL32(0x18, 0x18, 0x1a, 0xff), 3.0f * s);
    for (int i = 1; i < 4; ++i) {
        const float fx = g0.x + graphW * float(i) / 4.0f, fy = g0.y + graphH * float(i) / 4.0f;
        dl->AddLine(ImVec2(fx, g0.y), ImVec2(fx, g1.y), IM_COL32(255, 255, 255, 18));
        dl->AddLine(ImVec2(g0.x, fy), ImVec2(g1.x, fy), IM_COL32(255, 255, 255, 18));
    }
    dl->AddLine(ImVec2(g0.x, g1.y), ImVec2(g1.x, g0.y), IM_COL32(255, 255, 255, 40)); // linear reference
    ImVec2 pts[49];
    for (int i = 0; i <= 48; ++i) {
        const float x = float(i) / 48.0f;
        pts[i] = ImVec2(g0.x + x * graphW, g1.y - t.curve.apply(x) * graphH);
    }
    dl->AddPolyline(pts, 49, ui::color::accent, 0, 2.0f * s);
    if (penSeen_) dl->AddCircleFilled(ImVec2(g0.x + raw * graphW, g1.y - curved * graphH), 4.0f * s, IM_COL32(255, 255, 255, 230));
    dl->AddRect(g0, g1, ui::color::border, 3.0f * s);

    // Presets, then the numbers.
    struct Preset {
        const char* label;
        PressureCurve curve;
        const char* help;
    };
    const Preset presets[] = {{"Soft", PressureCurve::soft(), "A light touch already gives a strong stroke."},
                              {"Linear", PressureCurve::linear(), "Brush pressure equals pen pressure."},
                              {"Firm", PressureCurve::firm(), "Press harder for a strong stroke; fine control when light."}};
    const float bw = (width - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        const PressureCurve& c = presets[i].curve;
        const bool on = std::abs(t.curve.gamma - c.gamma) < 1e-3f && t.curve.minOut == c.minOut && t.curve.maxOut == c.maxOut &&
                        t.curve.threshold == c.threshold && t.curve.fullAt == c.fullAt;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col(ui::color::accent));
        if (ImGui::Button((std::string(presets[i].label) + "##tablet_curve").c_str(), ImVec2(bw, 0))) t.curve = c;
        if (on) ImGui::PopStyleColor();
        ui::HelpText((std::string(presets[i].label) + " pressure").c_str(), nullptr, presets[i].help, path);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::SliderInside("##tablet_gamma", &t.curve.gamma, 0.25f, 4.0f, "Curve %.2f", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("Curve", nullptr, "Below 1: soft, a light touch gives a lot. Above 1: firm, press harder.", path);
    const float half = (width - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::SetNextItemWidth(half);
    ui::SliderInside("##tablet_min", &t.curve.minOut, 0.0f, 1.0f, "Min %.2f");
    ui::HelpText("Minimum", nullptr, "Brush pressure at the lightest touch.", path);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    ui::SliderInside("##tablet_max", &t.curve.maxOut, 0.0f, 1.0f, "Max %.2f");
    ui::HelpText("Maximum", nullptr, "Brush pressure when pressing fully.", path);
    ImGui::SetNextItemWidth(half);
    ui::SliderInside("##tablet_threshold", &t.curve.threshold, 0.0f, 0.5f, "Threshold %.2f");
    ui::HelpText("Threshold", nullptr, "Pen pressure below this counts as the lightest touch (ignores a resting hand).",
                 path);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    ui::SliderInside("##tablet_full", &t.curve.fullAt, 0.3f, 1.0f, "Full at %.2f");
    ui::HelpText("Full pressure at", nullptr, "Pen pressure that already counts as pressing fully (for a light hand).",
                 path);
    t.curve.fullAt = std::max(t.curve.fullAt, t.curve.threshold + 0.05f);

    // --- The current brush's own curve (after the one above; Brush Lab shows the same) ---
    ImGui::Spacing();
    drawPressureCurveEditor("prefs_brush_curve", width);

    // --- Pressure > Strength / Size: the same toggles as the top shelf ---
    ImGui::Spacing();
    const float tw = (width - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ui::ActionButton(*this, "draw.pressure_strength", ImVec2(tw, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "draw.pressure_size", ImVec2(tw, 0));

    // --- Eraser and side buttons ---
    ImGui::Spacing();
    auto mapping = [&](const char* label, PenAction& value, std::initializer_list<PenAction> choices, const char* help) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(width * 0.42f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo((std::string("##") + label).c_str(), penActionLabel(value))) {
            for (PenAction a : choices)
                if (ImGui::Selectable((std::string(penActionLabel(a)) + "##" + label).c_str(), a == value)) value = a;
            ImGui::EndCombo();
        }
        ui::HelpText(label, nullptr, help, path);
    };
    mapping("Eraser", t.eraser, {PenAction::Invert, PenAction::Smooth, PenAction::EraseMask, PenAction::None},
            "What the pen's eraser end does. Invert: brushes subtract, like holding Alt (with Ctrl it erases mask). "
            "Erase mask: removes mask wherever it draws.");
    static const char* kButtonHelp[kPenButtons] = {
        "The side button nearer the tip. Right-click: drag to rotate the view (Shift: pan) and open context menus.",
        "The side button further from the tip. Invert (Alt): brushes subtract while it is held.",
        "A third button, on pens that have one (e.g. Wacom Pro Pen 3D)."};
    for (int b = 0; b < kPenButtons; ++b)
        mapping(kButtonNames[b], t.buttons[size_t(b)],
                {PenAction::RightClick, PenAction::Invert, PenAction::Smooth, PenAction::RotateView, PenAction::PanView,
                 PenAction::None},
                kButtonHelp[b]);
    if (ImGui::SmallButton(ICON_LC_ROTATE_CCW "  Tablet defaults")) {
        const PressureCurve keep = t.curve;
        const bool mouseMode = t.mouseMode; // (follows the driver's setting, not a preference)
        t = TabletSettings();
        t.curve = keep;
        t.mouseMode = mouseMode;
    }
    ui::HelpText("Tablet defaults", nullptr, "Eraser: Invert; lower button: Right-click; upper button: Invert (Alt); third "
                                             "button: Pan view. The curve and Mouse mode stay.", path);
    ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::textDim));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(tr("Store Config (Ctrl+Shift+I) keeps these settings."));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

} // namespace sf
