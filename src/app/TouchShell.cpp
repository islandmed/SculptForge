// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The touch layout (app/TouchShell.h): the Sculpt-room overlay for tablets and finger mode on the canvas. Its events
// come first, right after palm rejection (App::event): fingers and the mouse on the rail's raw controls, fingers on
// the canvas to core/FingerMode.h, SDL's mouse events of those fingers kept from ImGui and the canvas. Everything
// else (the dock, the context strip, the top bar) is ImGui buttons that SDL's finger mouse events press as usual.
// Off (Preferences > Interface > Touch layout), none of this runs and Touch.cpp handles touches as before.
#include "app/App.h"

#include "app/ForgeTokens.h"
#include "app/LibraryBrowser.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

// Forge Night (app/ForgeTokens.h, shared with the desktop layout), Inter.
using namespace tokens;
constexpr float kTarget = kHitDp;  // dp: every target at least this
constexpr float kHoldKey = kHoldDp; // dp: the rail's hold keys and the dock's tiles
constexpr double kTapSeconds = 0.35;
constexpr double kLongPress = 0.6;
constexpr uint64_t kMousePointer = ~uint64_t(0) - 1; // the desktop mouse on the raw controls (a testbed)
constexpr float kMinSize = 2.0f, kMaxSize = 800.0f;  // brush size, pixels (as the S slider)

double now() { return double(SDL_GetTicksNS()) * 1e-9; }

// A token with its own opacity times `a` (the chrome's fade).
ImU32 rgba(ImU32 col, float a) {
    const float alpha = float((col >> IM_COL32_A_SHIFT) & 0xff) * std::clamp(a, 0.0f, 1.0f);
    return (col & ~IM_COL32_A_MASK) | (ImU32(alpha + 0.5f) << IM_COL32_A_SHIFT);
}

ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

void centred(ImDrawList* dl, ImFont* font, float size, ImVec2 a, ImVec2 b, ImU32 col, const char* text) {
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(std::round((a.x + b.x - ts.x) * 0.5f), std::round((a.y + b.y - ts.y) * 0.5f)), col, text);
}

float sizeFraction(float px) { return std::log(std::clamp(px, kMinSize, kMaxSize) / kMinSize) / std::log(kMaxSize / kMinSize); }
float sizeFromFraction(float f) { return kMinSize * std::pow(kMaxSize / kMinSize, std::clamp(f, 0.0f, 1.0f)); }

const char* const kKeyNames[TouchShell::KeyCount] = {"Invert", "Smooth", "Mask", "Lock"};

// The raw control under `p`, as last drawn.
bool rawAt(const TouchShell& ts, bool sliders, ImVec2 p, TouchShell::Grab& g) {
    for (int k = 0; k < TouchShell::KeyCount; ++k)
        if (ts.keyRect[k].contains(p)) {
            g.control = TouchShell::Control::Key;
            g.index = k;
            return true;
        }
    if (sliders) {
        for (int i = 0; i < 2; ++i)
            if (ts.slider[i].contains(p)) {
                g.control = TouchShell::Control::Slider;
                g.index = i;
                return true;
            }
    } else if (ts.pad.contains(p)) {
        g.control = TouchShell::Control::Pad;
        return true;
    }
    return false;
}

template <class T> bool has(const std::vector<T>& v, const T& x) { return std::find(v.begin(), v.end(), x) != v.end(); }
template <class T> void drop(std::vector<T>& v, const T& x) { v.erase(std::remove(v.begin(), v.end(), x), v.end()); }

} // namespace

bool App::touchLayoutOn() const {
    if (hideUi_) return false;
    if (uiPrefs_.touchLayout == 1) return true;
    if (uiPrefs_.touchLayout == 2) return false;
#ifdef __ANDROID__
    return true;
#else
    return false;
#endif
}

SDL_Keymod App::touchShellMods() const {
    if (!touchLayoutOn()) return 0;
    auto on = [this](TouchShell::Key k) {
        const TouchShell::KeyState& s = touchShell_.keys[k];
        return s.latched || s.held || s.forStroke;
    };
    // The classic meanings, as the pen buttons' (inputMods): Alt inverts, Shift smooths, Ctrl masks.
    SDL_Keymod mods = 0;
    if (on(TouchShell::Invert)) mods |= SDL_KMOD_LALT;
    if (on(TouchShell::Smooth)) mods |= SDL_KMOD_LSHIFT;
    if (on(TouchShell::Mask)) mods |= SDL_KMOD_LCTRL;
    return mods;
}

bool App::touchShellCanvasAt(ImVec2 p) const {
    if (!canvasRect().contains(p.x, p.y)) return false;
    for (const TouchShell::Rect& r : touchShell_.chrome)
        if (r.contains(p)) return false;
    if (ui::FloatingWindowAt(p) || pickerOpen()) return false;
    return !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void App::touchShellLetGo() {
    TouchShell& ts = touchShell_;
    if (!fingerMode_.idle()) touchShellFinger(fingerMode_.cancel());
    ts.grabs.clear();
    ts.canvasFingers.clear();
    ts.uiFingers.clear();
    for (TouchShell::KeyState& k : ts.keys) k.held = k.forStroke = false;
    ts.penAdjust = ts.penAdjusting = false;
}

bool App::touchShellEvent(const SDL_Event& e) {
    TouchShell& ts = touchShell_;
    if (!touchLayoutOn()) return false;
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) { // (backgrounded with a thumb on a key: its lift never comes here)
        touchShellLetGo();
        return false;
    }
    const double t = now();
    if (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.which != SDL_TOUCH_MOUSEID && e.motion.which != SDL_PEN_MOUSEID)
        ts.pointerOver = true;
    else if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE || e.type == SDL_EVENT_FINGER_DOWN)
        ts.pointerOver = false;
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        const SDL_TouchFingerEvent& f = e.tfinger;
        if (f.touchID == SDL_MOUSE_TOUCHID || f.touchID == SDL_PEN_TOUCHID) return false;
        const SDL_TouchDeviceType type = f.touchID == kUiTestTrackpad     ? SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE
                                         : f.touchID == kUiTestTouchScreen ? SDL_TOUCH_DEVICE_DIRECT
                                                                           : SDL_GetTouchDeviceType(f.touchID);
        if (type == SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE || type == SDL_TOUCH_DEVICE_INDIRECT_RELATIVE)
            return false; // a trackpad: as before (Touch.cpp)
        int w = 0, h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        const ImVec2 p(f.x * float(w), f.y * float(h));
        const glm::vec2 at(p.x, p.y);
        const uint64_t id = f.fingerID;
        const auto grab = std::find_if(ts.grabs.begin(), ts.grabs.end(), [id](const TouchShell::Grab& g) { return g.pointer == id; });
        if (e.type == SDL_EVENT_FINGER_DOWN) {
            if (touchShellGrab(id, p)) return true; // (the rail takes touches even beside the pen)
            if (touchShellCanvasAt(p)) {
                ts.canvasFingers.push_back(id);
                touchShellFinger(fingerMode_.down(id, at, t, palmGuard()));
            } else {
                ts.uiFingers.push_back(id); // ImGui has it through SDL's mouse events
            }
            return true;
        }
        if (e.type == SDL_EVENT_FINGER_MOTION) {
            if (grab != ts.grabs.end()) touchShellDrag(*grab, p);
            else if (has(ts.canvasFingers, id)) touchShellFinger(fingerMode_.move(id, at, t));
            return true;
        }
        if (grab != ts.grabs.end()) {
            if (e.type == SDL_EVENT_FINGER_CANCELED) grab->moved = true; // (a cancelled touch is never a tap)
            touchShellRelease(*grab);
            ts.grabs.erase(grab);
        } else if (has(ts.canvasFingers, id)) {
            if (e.type == SDL_EVENT_FINGER_CANCELED) {
                touchShellFinger(fingerMode_.cancel());
                ts.canvasFingers.clear();
            } else {
                touchShellFinger(fingerMode_.up(id, at, t, palmGuard()));
                drop(ts.canvasFingers, id);
            }
        }
        drop(ts.uiFingers, id);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION: {
        const bool motion = e.type == SDL_EVENT_MOUSE_MOTION;
        const SDL_MouseID which = motion ? e.motion.which : e.button.which;
        if (which == SDL_PEN_MOUSEID) return false;
        const ImVec2 p = motion ? ImVec2(e.motion.x, e.motion.y) : ImVec2(e.button.x, e.button.y);
        if (which == SDL_TOUCH_MOUSEID) {
            // SDL's mouse events for a finger (sent before the finger's own): a finger on the rail or the canvas
            // is ours from its finger events; ImGui and the canvas never see its mouse.
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                TouchShell::Grab g;
                ts.swallowTouchMouse = rawAt(ts, uiPrefs_.touchRailSliders, p, g) || touchShellCanvasAt(p);
                return ts.swallowTouchMouse;
            }
            if (motion) return ts.swallowTouchMouse;
            const bool swallowed = ts.swallowTouchMouse;
            ts.swallowTouchMouse = false;
            return swallowed;
        }
        // The mouse (a desktop trying the layout): its left button works the raw controls too.
        const auto grab = std::find_if(ts.grabs.begin(), ts.grabs.end(),
                                       [](const TouchShell::Grab& g) { return g.pointer == kMousePointer; });
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            return e.button.button == SDL_BUTTON_LEFT && grab == ts.grabs.end() && touchShellGrab(kMousePointer, p);
        if (grab == ts.grabs.end()) return false;
        if (motion) {
            touchShellDrag(*grab, p);
        } else if (e.button.button == SDL_BUTTON_LEFT) {
            touchShellRelease(*grab);
            ts.grabs.erase(grab);
        }
        return true;
    }
    default:
        return false;
    }
}

bool App::touchShellGrab(uint64_t pointer, ImVec2 p) {
    TouchShell& ts = touchShell_;
    TouchShell::Grab g;
    if (!rawAt(ts, uiPrefs_.touchRailSliders, p, g)) return false;
    g.pointer = pointer;
    g.start = g.last = p;
    g.at = now();
    if (g.control == TouchShell::Control::Key) {
        TouchShell::KeyState& k = ts.keys[g.index];
        k.held = true;
        k.pointer = pointer;
        k.downAt = g.at;
        k.strokeWhileHeld = drag_ == Drag::Stroke;
    }
    ts.grabs.push_back(g);
    touchShellDrag(ts.grabs.back(), p); // (a slider jumps to where it was pressed)
    return true;
}

void App::touchShellDrag(TouchShell::Grab& g, ImVec2 p) {
    TouchShell& ts = touchShell_;
    const float slop = 8.0f * uiScale_;
    if (!g.moved && std::hypot(p.x - g.start.x, p.y - g.start.y) > slop) g.moved = true;
    if (g.control == TouchShell::Control::Pad && g.moved) {
        // Up and down: size (one pad height = 4x, on a log scale), sideways: strength (one pad width = 0 to 1).
        const float w = std::max(ts.pad.b.x - ts.pad.a.x, 1.0f), h = std::max(ts.pad.b.y - ts.pad.a.y, 1.0f);
        radiusPx_ = std::clamp(radiusPx_ * std::exp2(-2.0f * (p.y - g.last.y) / h), kMinSize, kMaxSize);
        float& strength = intensityValue(false);
        strength = std::clamp(strength + (p.x - g.last.x) / w, 0.0f, 1.0f);
    } else if (g.control == TouchShell::Control::Slider) {
        const TouchShell::Rect& r = ts.slider[g.index];
        const float f = std::clamp((r.b.y - p.y) / std::max(r.b.y - r.a.y, 1.0f), 0.0f, 1.0f);
        if (g.index == 0) radiusPx_ = sizeFromFraction(f);
        else intensityValue(false) = f;
    }
    g.last = p;
}

void App::touchShellRelease(TouchShell::Grab& g) {
    TouchShell& ts = touchShell_;
    const bool tap = !g.moved && now() - g.at < kTapSeconds;
    if (g.control == TouchShell::Control::Key) {
        TouchShell::KeyState& k = ts.keys[g.index];
        k.held = std::any_of(ts.grabs.begin(), ts.grabs.end(), [&g](const TouchShell::Grab& o) { // (two fingers on it)
            return &o != &g && o.control == TouchShell::Control::Key && o.index == g.index;
        });
        if (drag_ == Drag::Stroke) k.forStroke = true; // "hold = this stroke": until it ends
        if (tap && !k.strokeWhileHeld) k.latched = !k.latched;
    } else if (g.control == TouchShell::Control::Pad && tap) {
        ts.showValues = !ts.showValues;
    }
}

void App::touchShellUndo(int steps) {
    const char* command = steps < 0 ? "undo" : "redo";
    int done = 0;
    for (int i = 0; i < std::abs(steps); ++i) {
        if (steps < 0 ? !doc_.undo.canUndo() : !doc_.undo.canRedo()) break;
        runCommand(command);
        ++done;
    }
    (steps < 0 ? touchFingerUndos_ : touchFingerRedos_) += done;
}

void App::touchShellFinger(const FingerMode::Events& events) {
    TouchShell& ts = touchShell_;
    auto toast = [&](std::string text) {
        ts.toast = std::move(text);
        ts.toastUntil = now() + 1.0;
        redrawIn(1.05);
    };
    for (const FingerEvent& ev : events) {
        switch (ev.kind) {
        case FingerEvent::Kind::StrokeBegin:
            if (drag_ != Drag::None) break; // (the pen or mouse is already dragging: this finger leaves it alone)
            lastInputPen_ = false;
            mouseX_ = ev.at.x;
            mouseY_ = ev.at.y;
            onMouseDown(ev.at.x, ev.at.y, SDL_BUTTON_LEFT);
            ts.fingerStroke = drag_ != Drag::None;
            break;
        case FingerEvent::Kind::StrokeMove:
            if (ts.fingerStroke && drag_ != Drag::None) onMouseMove(ev.at.x, ev.at.y, ev.at.x - mouseX_, ev.at.y - mouseY_, false);
            break;
        case FingerEvent::Kind::StrokeEnd:
            if (ts.fingerStroke && drag_ != Drag::None) onMouseUp();
            ts.fingerStroke = false;
            break;
        case FingerEvent::Kind::Navigate: {
            const TouchShell::KeyState& lock = ts.keys[TouchShell::Lock];
            if (lock.latched || lock.held) break; // Lock: no finger navigation (a resting palm)
            touchDirect_ = true;
            touchApply(ev.step);
            break;
        }
        case FingerEvent::Kind::Undo:
            touchShellUndo(-1);
            toast("Undo");
            break;
        case FingerEvent::Kind::Redo:
            touchShellUndo(1);
            toast("Redo");
            break;
        case FingerEvent::Kind::ScrubBegin:
            toast("History +0");
            break;
        case FingerEvent::Kind::ScrubStep: {
            touchShellUndo(ev.steps);
            redrawIn(0.05); // (the counter over the canvas: drawTouchShell)
            break;
        }
        case FingerEvent::Kind::ScrubEnd:
            break;
        }
    }
    if (fingerMode_.idle()) {
        touchTwisting_ = touchPinching_ = false;
        touchTwistSum_ = touchZoomSum_ = 0.0f;
    }
}

bool App::touchShellPenButton(bool down, float x, float y) {
    TouchShell& ts = touchShell_;
    const bool had = ts.penAdjust;
    ts.penAdjust = down && touchLayoutOn() && !penTouching_ && drag_ == Drag::None; // (not over a finger's stroke)
    ts.penAdjusting = false;
    ts.penStart = ts.penLast = ImVec2(x, y);
    if (!down) redrawIn(0.0);
    return down ? ts.penAdjust : had; // (taken: no view drag or right click from the same button)
}

void App::touchShellPenHover(float x, float y) {
    TouchShell& ts = touchShell_;
    if (!ts.penAdjust) return;
    if (penTouching_ || !touchLayoutOn() || penButtons_ == 0 || drag_ != Drag::None) { // (or out of range with the button held: penLeft) // touching down with the button held: a Smooth stroke, as before
        ts.penAdjust = ts.penAdjusting = false;
        return;
    }
    const float s = uiScale_;
    if (!ts.penAdjusting && std::hypot(x - ts.penStart.x, y - ts.penStart.y) > 8.0f * s) ts.penAdjusting = true;
    if (ts.penAdjusting) {
        // Sideways: size (200 dp doubles it), up and down: strength (200 dp from 0 to 1, up is stronger).
        radiusPx_ = std::clamp(radiusPx_ * std::exp2((x - ts.penLast.x) / (200.0f * s)), kMinSize, kMaxSize);
        float& strength = intensityValue(false);
        strength = std::clamp(strength - (y - ts.penLast.y) / (200.0f * s), 0.0f, 1.0f);
        redrawIn(0.0);
    }
    ts.penLast = ImVec2(x, y);
}

void App::drawTouchShell() {
    TouchShell& ts = touchShell_;
    if (!touchLayoutOn()) {
        touchShellLetGo();
        ts.chrome.clear();
        return;
    }
    const double t = now();
    touchShellFinger(fingerMode_.tick(t));
    if (fingerMode_.fingers() > 0 || !ts.grabs.empty()) redrawIn(0.05); // (a hold starts the scrub without events)

    const bool stroking = drag_ == Drag::Stroke;
    for (TouchShell::KeyState& k : ts.keys) {
        if (stroking && k.held) k.strokeWhileHeld = true;
        if (!stroking) k.forStroke = false;
    }
    const float target = stroking ? 0.1f : 1.0f;
    if (uiPrefs_.reduceMotion) ts.fade = target;
    else {
        ts.fade += (target - ts.fade) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f);
        if (std::abs(target - ts.fade) > 0.01f) redrawIn(0.016);
        else ts.fade = target;
    }
    const float fade = ts.fade;

    // The dock's brushes: refreshed now and then (the list reads the packs and recipes).
    static std::vector<BrushItem> items;
    static int itemsFrame = -1000;
    if (ImGui::GetFrameCount() - itemsFrame > 120) {
        items = PickerUi::brushItems(*this, false);
        itemsFrame = ImGui::GetFrameCount();
    }
    std::string current;
    for (const BrushItem& it : items)
        if (PickerUi::isActive(*this, it)) {
            current = it.id();
            break;
        }
    if (current != ts.lastBrush) {
        if (!ts.lastBrush.empty()) {
            drop(ts.recent, ts.lastBrush);
            ts.recent.insert(ts.recent.begin(), ts.lastBrush);
            if (ts.recent.size() > 8) ts.recent.resize(8);
        }
        ts.lastBrush = current;
    }
    auto itemFor = [&](const std::string& id) -> const BrushItem* {
        for (const BrushItem& it : items)
            if (it.id() == id) return &it;
        return nullptr;
    };

    const CanvasRect c = canvasRect();
    const float s = uiScale_, m = 16.0f * s, gap = 8.0f * s, key = kHoldKey * s, tgt = kTarget * s;
    const float round = 14.0f * s;
    const bool left = layout_.leftHanded; // the rail on the free hand's side: the left for a right-handed sculptor
    ImFont* bold = fontInterBold_ ? fontInterBold_ : ImGui::GetFont(); // (Inter in every look, mono or not)
    ImFont* small = fontInterSmall_ ? fontInterSmall_ : ImGui::GetFont();
    ImFont* icons = fontIcons_ ? fontIcons_ : ImGui::GetFont();
    const float boldPx = 15.0f * s, smallPx = 12.0f * s, iconPx = 20.0f * s;
    ts.chrome.clear();

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar;
    auto begin = [&](const char* id, ImVec2 pos, ImVec2 size) {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::Begin(id, nullptr, flags);
        ts.chrome.push_back({pos, pos + size});
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, pos + size, rgba(kGlass, fade), round);
        dl->AddRect(pos, pos + size, rgba(IM_COL32_WHITE, 0.06f * fade), round, 0, 1.0f * s);
        return dl;
    };
    auto end = [] {
        ImGui::End();
        ImGui::PopStyleVar(3);
    };
    // An ImGui button drawn the overlay's way: true when tapped.
    auto button = [&](ImDrawList* dl, const char* id, ImVec2 at, ImVec2 size, const char* icon, const char* label, bool on) {
        ImGui::SetCursorScreenPos(at);
        const bool clicked = ImGui::InvisibleButton(id, size);
        const bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
        const ImVec2 b = at + size;
        if (on) dl->AddRectFilled(at, b, rgba(kForge, 0.22f * fade), 10.0f * s);
        else if (active || hovered) dl->AddRectFilled(at, b, rgba(IM_COL32_WHITE, (active ? 0.12f : 0.06f) * fade), 10.0f * s);
        if (on) dl->AddRect(at, b, rgba(kForge, 0.9f * fade), 10.0f * s, 0, 1.5f * s);
        const ImU32 ink = rgba(on ? kForge : kInk, fade);
        if (icon && label) {
            const float iconH = iconPx, labelH = smallPx, total = iconH + 4.0f * s + labelH;
            const float y0 = at.y + (size.y - total) * 0.5f;
            centred(dl, icons, iconPx, ImVec2(at.x, y0), ImVec2(b.x, y0 + iconH), ink, icon);
            centred(dl, small, smallPx, ImVec2(at.x, y0 + iconH + 4.0f * s), ImVec2(b.x, y0 + total), ink, tr(label));
        } else if (icon) {
            centred(dl, icons, iconPx, at, b, ink, icon);
        } else if (label) {
            centred(dl, bold, boldPx, at, b, ink, tr(label));
        }
        return clicked;
    };

    // ---- Top bar: back and the project, the room pill, undo / redo / more ----
    const float barH = tgt + 2.0f * 4.0f * s, y0 = c.y + timelineTopInset_ + m;
    {
        std::string name = doc_.path.empty() ? std::string(tr("Untitled")) : doc_.path;
        if (const size_t slash = name.find_last_of("/\\"); slash != std::string::npos) name = name.substr(slash + 1);
        if (const size_t dot = name.rfind('.'); dot != std::string::npos && dot > 0) name = name.substr(0, dot);
        const bool saved = !doc_.dirty();
        const char* state = saved ? tr("Saved") : tr("Not saved");
        const float nameW = bold->CalcTextSizeA(boldPx, FLT_MAX, 0.0f, name.c_str()).x;
        const float stateW = small->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, state).x + 12.0f * s;
        const ImVec2 pos(left ? c.x + c.w - m - (tgt + nameW + stateW + 3.0f * gap + 8.0f * s) : c.x + m, y0);
        const ImVec2 size(tgt + nameW + stateW + 3.0f * gap + 8.0f * s, barH);
        ImDrawList* dl = begin("##touch_project", pos, size);
        if (button(dl, "##touch_back", pos + ImVec2(4.0f * s, 4.0f * s), ImVec2(tgt, tgt), ICON_LC_CHEVRON_LEFT, nullptr, false))
            libraryBrowser_.show(LibraryTab::Project);
        ui::HelpText("Projects", nullptr, "Your projects in the Library.", "Touch layout");
        float x = pos.x + 4.0f * s + tgt + gap;
        dl->AddText(bold, boldPx, ImVec2(x, pos.y + (barH - boldPx) * 0.5f), rgba(kInk, fade), name.c_str());
        x += nameW + gap;
        const float cy = pos.y + barH * 0.5f;
        dl->AddCircleFilled(ImVec2(x + 4.0f * s, cy), 3.5f * s, rgba(saved ? kOk : kInkDim, fade));
        dl->AddText(small, smallPx, ImVec2(x + 12.0f * s, cy - smallPx * 0.5f), rgba(kInkDim, fade), state);
        end();
    }
    {
        const char* room = tr("Sculpt");
        const float roomW = bold->CalcTextSizeA(boldPx, FLT_MAX, 0.0f, room).x;
        const ImVec2 size(roomW + 72.0f * s, barH);
        const ImVec2 pos(std::round(c.x + (c.w - size.x) * 0.5f), y0);
        ImDrawList* dl = begin("##touch_room", pos, size);
        ImGui::SetCursorScreenPos(pos);
        if (ImGui::InvisibleButton("##touch_room_pill", size)) {
            ts.toast = tr("Paint and UV rooms are coming");
            ts.toastUntil = t + 1.6;
            redrawIn(1.65);
        }
        ui::HelpText("Room", nullptr, "Sculpt. Paint and UV come next, on the same model.", "Touch layout");
        const float cy = pos.y + barH * 0.5f;
        dl->AddCircleFilled(ImVec2(pos.x + 22.0f * s, cy), 9.0f * s, rgba(kRoomSculpt, 0.25f * fade));
        dl->AddCircleFilled(ImVec2(pos.x + 22.0f * s, cy), 4.5f * s, rgba(kRoomSculpt, fade));
        dl->AddText(bold, boldPx, ImVec2(pos.x + 38.0f * s, cy - boldPx * 0.5f), rgba(kInk, fade), room);
        centred(dl, icons, 14.0f * s, ImVec2(pos.x + 38.0f * s + roomW, cy - 8.0f * s), ImVec2(pos.x + size.x - 6.0f * s, cy + 8.0f * s),
                rgba(kInkDim, fade), ICON_LC_CHEVRON_DOWN);
        end();
    }
    {
        const ImVec2 size(3.0f * tgt + 8.0f * s, barH);
        const ImVec2 pos(left ? c.x + m : c.x + c.w - m - size.x, y0);
        ImDrawList* dl = begin("##touch_history", pos, size);
        ImVec2 at = pos + ImVec2(4.0f * s, 4.0f * s);
        ImGui::BeginDisabled(!doc_.undo.canUndo());
        if (button(dl, "##touch_undo", at, ImVec2(tgt, tgt), ICON_LC_UNDO_2, nullptr, false)) runCommand("undo");
        ImGui::EndDisabled();
        ui::HelpText("Undo", nullptr, "Or tap the canvas with two fingers.", "Touch layout");
        at.x += tgt;
        ImGui::BeginDisabled(!doc_.undo.canRedo());
        if (button(dl, "##touch_redo", at, ImVec2(tgt, tgt), ICON_LC_REDO_2, nullptr, false)) runCommand("redo");
        ImGui::EndDisabled();
        ui::HelpText("Redo", nullptr, "Or tap the canvas with three fingers.", "Touch layout");
        at.x += tgt;
        if (button(dl, "##touch_more", at, ImVec2(tgt, tgt), ICON_LC_ELLIPSIS, nullptr, false)) ImGui::OpenPopup("##touch_more_menu");
        if (ImGui::BeginPopup("##touch_more_menu")) {
            // The corner axes are hidden under the layout: the side views live here (later also on the model's long-press card).
            if (ImGui::BeginMenu(tr("Views"))) {
                static const char* const kViews[][2] = {{"view.front", "Front"}, {"view.back", "Back"}, {"view.right", "Right"},
                                                        {"view.left", "Left"},   {"view.top", "Top"},   {"view.bottom", "Bottom"}};
                for (const auto& v : kViews)
                    if (ImGui::MenuItem(tr(v[1])))
                        if (const Action* a = actions_.find(v[0]); a && a->run) a->run();
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(tr("Left-handed layout"), nullptr, layout_.leftHanded)) layout_.leftHanded = !layout_.leftHanded;
            // The testers' A/B (TESTERS.md): the thumb pad against two plain sliders.
            if (ImGui::MenuItem(tr("Rail: two sliders"), nullptr, uiPrefs_.touchRailSliders))
                uiPrefs_.touchRailSliders = !uiPrefs_.touchRailSliders;
            if (ImGui::MenuItem(tr("Desktop panels"), nullptr, ts.desktopPanels)) ts.desktopPanels = !ts.desktopPanels;
            if (ImGui::MenuItem(tr("Turn the touch layout off"))) uiPrefs_.touchLayout = 2;
            ImGui::EndPopup();
        }
        end();
    }

    // ---- The rail: the thumb pad (or the A/B's two sliders) and the hold keys ----
    {
        const float railW = 2.0f * key + 3.0f * gap, padH = railW - 2.0f * gap;
        const float railH = gap + padH + gap + key + gap + key + gap;
        const float top = std::max(y0 + barH + m, c.y + c.h * 0.42f - railH * 0.5f);
        const ImVec2 pos(left ? c.x + c.w - m - railW : c.x + m, top);
        ImDrawList* dl = begin("##touch_rail", pos, ImVec2(railW, railH));
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(ImVec2(railW, railH)); // (the window takes the hover; the raw controls take the presses)
        const ImVec2 pa = pos + ImVec2(gap, gap), pb = pa + ImVec2(railW - 2.0f * gap, padH);
        const float strength = intensityValue(false);
        char sizeText[16], strengthText[16];
        std::snprintf(sizeText, sizeof sizeText, "%.0f", radiusPx_);
        std::snprintf(strengthText, sizeof strengthText, "%.0f%%", strength * 100.0f);
        if (!uiPrefs_.touchRailSliders) {
            ts.pad = {pa, pb};
            ts.slider[0] = ts.slider[1] = {};
            dl->AddRectFilled(pa, pb, rgba(kNight, 0.7f * fade), 10.0f * s);
            for (int i = 1; i < 4; ++i) { // a quiet grid: where the thumb is on both scales
                const float fx = pa.x + (pb.x - pa.x) * float(i) / 4.0f, fy = pa.y + (pb.y - pa.y) * float(i) / 4.0f;
                dl->AddLine(ImVec2(fx, pa.y), ImVec2(fx, pb.y), rgba(IM_COL32_WHITE, 0.05f * fade), 1.0f);
                dl->AddLine(ImVec2(pa.x, fy), ImVec2(pb.x, fy), rgba(IM_COL32_WHITE, 0.05f * fade), 1.0f);
            }
            const float inset = 14.0f * s;
            const ImVec2 knob(pa.x + inset + (pb.x - pa.x - 2.0f * inset) * strength,
                              pb.y - inset - (pb.y - pa.y - 2.0f * inset) * sizeFraction(radiusPx_));
            dl->AddLine(ImVec2(knob.x, pa.y + 4.0f * s), ImVec2(knob.x, pb.y - 4.0f * s), rgba(kForge, 0.25f * fade), 1.0f * s);
            dl->AddLine(ImVec2(pa.x + 4.0f * s, knob.y), ImVec2(pb.x - 4.0f * s, knob.y), rgba(kForge, 0.25f * fade), 1.0f * s);
            const bool grabbed = std::any_of(ts.grabs.begin(), ts.grabs.end(),
                                             [](const TouchShell::Grab& g) { return g.control == TouchShell::Control::Pad; });
            dl->AddCircleFilled(knob, (grabbed ? 13.0f : 11.0f) * s, rgba(kForge, fade), 32);
            dl->AddCircle(knob, (grabbed ? 13.0f : 11.0f) * s, rgba(IM_COL32_WHITE, 0.35f * fade), 32, 1.5f * s);
            dl->AddText(small, smallPx, pa + ImVec2(6.0f * s, 4.0f * s), rgba(kInk, fade), sizeText);
            const float sw = small->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, strengthText).x;
            dl->AddText(small, smallPx, ImVec2(pb.x - 6.0f * s - sw, pb.y - 4.0f * s - smallPx), rgba(kForge, fade), strengthText);
        } else {
            ts.pad = {};
            const float sliderW = (pb.x - pa.x - gap) * 0.5f;
            const float values[2] = {sizeFraction(radiusPx_), strength};
            const char* texts[2] = {sizeText, strengthText};
            const char* names[2] = {"Size", "Strength"};
            for (int i = 0; i < 2; ++i) {
                const ImVec2 a(pa.x + float(i) * (sliderW + gap), pa.y), b(a.x + sliderW, pb.y);
                ts.slider[i] = {a, b};
                dl->AddRectFilled(a, b, rgba(kNight, 0.7f * fade), 10.0f * s);
                const float fillTop = b.y - (b.y - a.y) * values[i];
                dl->AddRectFilled(ImVec2(a.x, fillTop), b, rgba(kForge, 0.55f * fade), 10.0f * s,
                                  values[i] > 0.95f ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersBottom);
                centred(dl, small, smallPx, a + ImVec2(0, 4.0f * s), ImVec2(b.x, a.y + 4.0f * s + smallPx), rgba(kInk, fade), texts[i]);
                centred(dl, small, smallPx * 0.9f, ImVec2(a.x, b.y - 4.0f * s - smallPx), ImVec2(b.x, b.y - 4.0f * s),
                        rgba(kInk, fade), tr(names[i]));
            }
        }
        const char* keyIcons[TouchShell::KeyCount] = {ICON_LC_MINUS, ICON_LC_WAVES, ICON_LC_CONTRAST,
                                                      ts.keys[TouchShell::Lock].latched ? ICON_LC_LOCK : ICON_LC_LOCK_OPEN};
        for (int k = 0; k < TouchShell::KeyCount; ++k) {
            const ImVec2 a(pos.x + gap + float(k % 2) * (key + gap), pb.y + gap + float(k / 2) * (key + gap)), b = a + ImVec2(key, key);
            ts.keyRect[k] = {a, b};
            const TouchShell::KeyState& st = ts.keys[k];
            const bool on = st.latched || st.held || st.forStroke;
            dl->AddRectFilled(a, b, st.held ? rgba(kForge, 0.35f * fade) : rgba(IM_COL32_WHITE, (on ? 0.10f : 0.04f) * fade), 10.0f * s);
            if (on) dl->AddRect(a, b, rgba(kForge, 0.95f * fade), 10.0f * s, 0, 1.5f * s);
            const ImU32 ink = rgba(on ? kForge : kInk, fade);
            const float y = a.y + (key - iconPx - 4.0f * s - smallPx) * 0.5f;
            centred(dl, icons, iconPx, ImVec2(a.x, y), ImVec2(b.x, y + iconPx), ink, keyIcons[k]);
            centred(dl, small, smallPx, ImVec2(a.x, y + iconPx + 4.0f * s), ImVec2(b.x, y + iconPx + 4.0f * s + smallPx), ink,
                    tr(kKeyNames[k]));
        }
        end();
        if (ts.showValues && !uiPrefs_.touchRailSliders) { // the pad tapped: the exact values beside it
            const ImVec2 size(150.0f * s, 2.0f * tgt);
            const ImVec2 at(left ? pos.x - gap - size.x : pos.x + railW + gap, pa.y);
            ImDrawList* vl = begin("##touch_values", at, size);
            char line[48];
            std::snprintf(line, sizeof line, "%s %.0f px", tr("Size"), radiusPx_);
            vl->AddText(bold, boldPx, at + ImVec2(14.0f * s, size.y * 0.25f - boldPx * 0.5f), rgba(kInk, fade), line);
            std::snprintf(line, sizeof line, "%s %.0f%%", tr("Strength"), strength * 100.0f);
            vl->AddText(bold, boldPx, at + ImVec2(14.0f * s, size.y * 0.75f - boldPx * 0.5f), rgba(kForge, fade), line);
            ImGui::SetCursorScreenPos(at);
            if (ImGui::InvisibleButton("##touch_values_close", size)) ts.showValues = false;
            end();
        }
    }

    // ---- The dock: a column down the pen-hand edge (stylus first: the resting pen hand covers the bottom corner,
    // which holds nothing), the brush, 4 pinned, the recent ones, all brushes. The context strip runs along the top
    // beside it, from the brush's name. Mirrored for left-handers. ----
    {
        std::vector<std::string> recent;
        for (const std::string& id : ts.recent)
            if (id != current && !has(uiPrefs_.touchPinned, id) && itemFor(id) && recent.size() < 2) recent.push_back(id);
        const int pinned = std::min(int(uiPrefs_.touchPinned.size()), 4);
        const float dockW = key + 2.0f * gap;
        const float dockH = gap + (key + gap) * float(1 + pinned) + (recent.empty() ? 0.0f : gap + (key + gap) * float(recent.size())) +
                            gap + key + gap;
        const float top = y0 + barH + gap;
        const ImVec2 pos(left ? c.x + m : c.x + c.w - m - dockW, top);
        ImDrawList* dl = begin("##touch_dock", pos, ImVec2(dockW, dockH));
        auto tile = [&](const char* id, ImVec2 at, const BrushItem* item) {
            ImGui::SetCursorScreenPos(at);
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(key, key));
            const bool active = item && PickerUi::isActive(*this, *item);
            const ImVec2 b = at + ImVec2(key, key);
            dl->AddRectFilled(at, b, rgba(IM_COL32_WHITE, (ImGui::IsItemActive() ? 0.12f : 0.04f) * fade), 10.0f * s);
            if (active) dl->AddRect(at, b, rgba(kForge, 0.95f * fade), 10.0f * s, 0, 1.5f * s);
            if (item) {
                const float th = key - 10.0f * s;
                const ImVec2 ta = at + ImVec2(5.0f * s, 5.0f * s);
                if (const ImTextureID tex = PickerUi::thumbnail(*this, *item))
                    dl->AddImageRounded(tex, ta, ta + ImVec2(th, th), ImVec2(0, 0), ImVec2(1, 1), rgba(IM_COL32_WHITE, fade), th * 0.5f);
                else dl->AddCircleFilled(ta + ImVec2(th * 0.5f, th * 0.5f), th * 0.45f, rgba(kForge, 0.6f * fade), 32);
                if (ImGui::IsItemHovered()) ui::HelpText(item->name.c_str(), nullptr, item->description.c_str(), "Dock");
            }
            return clicked;
        };
        const float x = pos.x + gap;
        float y = pos.y + gap;
        const BrushItem* cur = itemFor(current);
        if (tile("##touch_current", ImVec2(x, y), cur)) openPicker(Picker::Brush, ImVec2(-1, -1));
        y += key + gap;
        for (int i = 0; i < pinned; ++i) {
            char id[24];
            std::snprintf(id, sizeof id, "##touch_pin%d", i);
            const BrushItem* item = itemFor(uiPrefs_.touchPinned[size_t(i)]);
            const bool clicked = tile(id, ImVec2(x, y), item);
            // Long-press: this slot takes the current brush (the dock never rearranges itself).
            if (ImGui::IsItemActive() && ImGui::GetIO().MouseDownDuration[0] > kLongPress && !current.empty() &&
                uiPrefs_.touchPinned[size_t(i)] != current) {
                uiPrefs_.touchPinned[size_t(i)] = current;
                ts.toast = tr("Pinned");
                ts.toastUntil = t + 1.0;
                redrawIn(1.05);
            } else if (clicked && item && ImGui::GetIO().MouseDownDurationPrev[0] < kLongPress) {
                PickerUi::select(*this, *item);
            }
            y += key + gap;
        }
        if (!recent.empty()) { // (none yet: no empty slots)
            dl->AddLine(ImVec2(x + 10.0f * s, y + 0.5f * gap - 0.5f), ImVec2(x + key - 10.0f * s, y + 0.5f * gap - 0.5f),
                        rgba(IM_COL32_WHITE, 0.12f * fade), 1.0f * s);
            y += gap;
            for (size_t i = 0; i < recent.size(); ++i) {
                char id[24];
                std::snprintf(id, sizeof id, "##touch_recent%d", int(i));
                const BrushItem* item = itemFor(recent[i]);
                if (tile(id, ImVec2(x, y), item) && item) PickerUi::select(*this, *item);
                y += key + gap;
            }
        }
        y += gap;
        if (button(dl, "##touch_all_brushes", ImVec2(x, y), ImVec2(key, key), ICON_LC_LAYOUT_GRID, nullptr, false))
            openPicker(Picker::Brush, ImVec2(-1, -1));
        ui::HelpText("All brushes", nullptr, "Every brush. Long-press a pinned slot to put the current brush there.", "Dock");
        end();

        // The context strip: the brush's name, then what it uses.
        const char* falloff = focalShift_ < -0.25f ? "Soft" : focalShift_ > 0.25f ? "Hard" : "Medium";
        struct Chip {
            const char* id;
            const char* icon; // (nullptr: the brush's thumbnail)
            const char* label;
            bool on;
        };
        const Chip chips[] = {{"##touch_brush", nullptr, cur ? cur->name.c_str() : "Brush", false},
                              {"##touch_mirror", ICON_LC_FLIP_HORIZONTAL_2, "Mirror X", symmetry_.x},
                              {"##touch_falloff", ICON_LC_SPLINE, falloff, false},
                              {"##touch_alpha", ICON_LC_IMAGE, "Alpha", false},
                              {"##touch_settings", ICON_LC_SLIDERS_HORIZONTAL, "Settings", false}};
        constexpr int kChips = 5;
        float stripW = gap;
        float widths[kChips];
        for (int i = 0; i < kChips; ++i) {
            const char* label = i == 0 ? chips[i].label : tr(chips[i].label);
            widths[i] = std::max(tgt, iconPx + 8.0f * s + bold->CalcTextSizeA(boldPx, FLT_MAX, 0.0f, label).x + 28.0f * s);
            stripW += widths[i] + gap;
        }
        const float stripH = tgt + 2.0f * 4.0f * s;
        const ImVec2 spos(left ? pos.x + dockW + gap : pos.x - gap - stripW, top);
        ImDrawList* sl = begin("##touch_strip", spos, ImVec2(stripW, stripH));
        float cx = spos.x + gap;
        for (int i = 0; i < kChips; ++i) {
            const Chip& chip = chips[i];
            const char* label = i == 0 ? chip.label : tr(chip.label);
            const ImVec2 at(cx, spos.y + 4.0f * s), b = at + ImVec2(widths[i], tgt);
            ImGui::SetCursorScreenPos(at);
            const bool clicked = ImGui::InvisibleButton(chip.id, ImVec2(widths[i], tgt));
            if (chip.on) sl->AddRectFilled(at, b, rgba(kForgeBg, fade), tgt * 0.5f);
            else if (ImGui::IsItemActive()) sl->AddRectFilled(at, b, rgba(IM_COL32_WHITE, 0.10f * fade), tgt * 0.5f);
            if (chip.on) sl->AddRect(at, b, rgba(kForge, 0.9f * fade), tgt * 0.5f, 0, 1.5f * s);
            const ImU32 ink = rgba(chip.on ? kForge : kInk, fade);
            const float ix = at.x + 14.0f * s;
            if (chip.icon) {
                centred(sl, icons, iconPx * 0.85f, ImVec2(ix, at.y), ImVec2(ix + iconPx, b.y), ink, chip.icon);
            } else if (const ImTextureID tex = cur ? PickerUi::thumbnail(*this, *cur) : ImTextureID(0)) {
                const ImVec2 ta(ix - 4.0f * s, at.y + (tgt - iconPx - 8.0f * s) * 0.5f);
                sl->AddImageRounded(tex, ta, ta + ImVec2(iconPx + 8.0f * s, iconPx + 8.0f * s), ImVec2(0, 0), ImVec2(1, 1),
                                    rgba(IM_COL32_WHITE, fade), iconPx);
            }
            sl->AddText(bold, boldPx, ImVec2(ix + iconPx + 8.0f * s, at.y + (tgt - boldPx) * 0.5f), ink, label);
            if (clicked) {
                if (i == 0) openPicker(Picker::Brush, ImVec2(-1, -1));
                else if (i == 1) symmetry_.x = !symmetry_.x;
                else if (i == 2) PickerUi::sizePopup(*this, PickerUi::Slider::FocalShift);
                else if (i == 3) openPicker(Picker::Alpha, ImVec2(-1, -1));
                else if (const Action* a = actions_.find("ui.palette.brush"); a && a->run) a->run();
            }
            cx += widths[i] + gap;
        }
        end();
    }

    // ---- Over the canvas: the size and pressure by the stroke, a moment's toast ----
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (ts.penAdjusting) { // the side button's hover drag: the values at the pen tip
        char text[48];
        std::snprintf(text, sizeof text, "%.0f  \xc2\xb7  %.0f%%", radiusPx_, intensityValue(false) * 100.0f);
        const ImVec2 ts2 = bold->CalcTextSizeA(boldPx, FLT_MAX, 0.0f, text);
        const ImVec2 a(mouseX_ + 28.0f * s, mouseY_ - 40.0f * s), b = a + ts2 + ImVec2(20.0f * s, 12.0f * s);
        fg->AddRectFilled(a, b, rgba(kNight, 0.82f), (b.y - a.y) * 0.5f);
        fg->AddText(bold, boldPx, a + ImVec2(10.0f * s, 6.0f * s), rgba(kForgeSoft, 1.0f), text);
    }
    if (stroking) {
        char text[48];
        if (lastInputPen_) std::snprintf(text, sizeof text, "%.0f  \xc2\xb7  %.2f %s", radiusPx_, strokePressure(), tr("pressure"));
        else std::snprintf(text, sizeof text, "%.0f  \xc2\xb7  %.0f%%", radiusPx_, intensityValue(false) * 100.0f);
        const ImVec2 ts2 = bold->CalcTextSizeA(boldPx, FLT_MAX, 0.0f, text);
        const ImVec2 a(mouseX_ + 28.0f * s, mouseY_ - 40.0f * s), b = a + ts2 + ImVec2(20.0f * s, 12.0f * s);
        fg->AddRectFilled(a, b, rgba(kNight, 0.82f), (b.y - a.y) * 0.5f);
        fg->AddText(bold, boldPx, a + ImVec2(10.0f * s, 6.0f * s), rgba(kForge, 1.0f), text);
    }
    if (fingerMode_.scrubbing()) { // the history scrub's step counter, for as long as the fingers are held
        char text[48];
        std::snprintf(text, sizeof text, "%s %+d", tr("History"), fingerMode_.scrubSteps());
        ts.toast = text;
        ts.toastUntil = t + 0.4;
    }
    if (!ts.toast.empty() && t < ts.toastUntil) {
        const ImVec2 ts2 = bold->CalcTextSizeA(boldPx, FLT_MAX, 0.0f, ts.toast.c_str());
        const ImVec2 a(std::round(c.x + (c.w - ts2.x) * 0.5f - 16.0f * s), y0 + barH + m);
        const ImVec2 b = a + ts2 + ImVec2(32.0f * s, 16.0f * s);
        fg->AddRectFilled(a, b, rgba(kSurface, 0.9f), (b.y - a.y) * 0.5f);
        fg->AddText(bold, boldPx, a + ImVec2(16.0f * s, 8.0f * s), rgba(kInk, 1.0f), ts.toast.c_str());
    }
}

} // namespace sf
