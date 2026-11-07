// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 3Dconnexion SpaceMouse navigation (#6), experimental: the device is opened
// through SDL's joystick API, not the 3Dconnexion SDK, so it works where the
// operating system shows it as a HID joystick (Linux without spacenavd holding
// it, macOS, Windows without 3DxWare hiding it). The cap's six axes move the
// camera every frame about the same pivot the mouse orbits around; the buttons
// run actions (Frame and Front view by default). The math and the settings are
// in core/SpaceMouse.h; Preferences > Input > SpaceMouse edits them, and
// they save themselves. User guide: docs/guide/navigation.md.
#include "app/App.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace sf {

namespace {

ImVec4 col(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// What a button can run from Preferences (ui.json may name any action id).
struct ButtonChoice {
    const char* id;
    const char* label;
};
constexpr ButtonChoice kButtonChoices[] = {
    {"", "Nothing"},
    {"view.frame", "Frame"},
    {"view.frame_selected", "Frame Part"},
    {"view.front", "Front view"},
    {"view.back", "Back view"},
    {"view.right", "Right view"},
    {"view.left", "Left view"},
    {"view.top", "Top view"},
    {"view.bottom", "Bottom view"},
    {"view.opposite", "Opposite side"},
    {"view.ortho", "Orthographic"},
    {"edit.undo", "Undo"},
    {"edit.redo", "Redo"},
};

// Short enough for three to a row in the narrowest tray.
const char* const kAxisNames[kSmAxes] = {"Pan X", "Zoom", "Pan Y", "Tilt", "Roll", "Spin"};

} // namespace

void App::spaceMouseScan() {
    if (spaceMouse_) return;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    joysticksSeen_.clear();
    if (!ids) return;
    for (int i = 0; i < count && !spaceMouse_; ++i) {
        // UI tests take only their own virtual SpaceMouse, not one plugged into the machine running them.
        if (!options_.uiTest.empty() && !SDL_IsJoystickVirtual(ids[i])) continue;
        const char* name = SDL_GetJoystickNameForID(ids[i]);
        const uint16_t vendor = SDL_GetJoystickVendorForID(ids[i]), product = SDL_GetJoystickProductForID(ids[i]);
        char seen[256];
        std::snprintf(seen, sizeof seen, "%s (%04x:%04x)", name && *name ? name : "?", vendor, product);
        auto skip = [&](const char* why) {
            joysticksSeen_.push_back(std::string(seen) + ": " + why);
            SDL_Log("SpaceMouse: skipped %s", joysticksSeen_.back().c_str());
        };
        if (!isSpaceMouseDevice(vendor, product, name ? name : "")) {
            skip("not a SpaceMouse");
            continue;
        }
        SDL_Joystick* j = SDL_OpenJoystick(ids[i]);
        if (!j) {
            skip((std::string("cannot open: ") + SDL_GetError()).c_str());
            continue;
        }
        if (SDL_GetNumJoystickAxes(j) < kSmAxes) { // a 3Dconnexion device without a 6-DoF cap
            skip((std::to_string(SDL_GetNumJoystickAxes(j)) + " axes, needs 6").c_str());
            SDL_CloseJoystick(j);
            continue;
        }
        joysticksSeen_.push_back(std::string(seen) + ": in use");
        spaceMouse_ = j;
        spaceMouseId_ = ids[i];
        spaceMouseName_ = name && *name ? name : "SpaceMouse";
        spaceMouseButtonCount_ = std::max(SDL_GetNumJoystickButtons(j), 0);
        spaceMouseAxes_ = {};
        spaceMouseReported_ = false;
        SDL_Log("SpaceMouse: %s (%04x:%04x, %d buttons)", spaceMouseName_.c_str(), SDL_GetJoystickVendor(j),
                SDL_GetJoystickProduct(j), spaceMouseButtonCount_);
    }
    SDL_free(ids);
}

void App::spaceMouseClose() {
    if (spaceMouse_) SDL_CloseJoystick(spaceMouse_);
    spaceMouse_ = nullptr;
    spaceMouseId_ = 0;
    spaceMouseName_.clear();
    spaceMouseButtonCount_ = 0;
    spaceMouseAxes_ = {};
    spaceMouseMoving_ = spaceMousePushed_ = false;
}

namespace {
std::array<float, kSmAxes> readAxes(SDL_Joystick* j) {
    std::array<float, kSmAxes> axes{};
    for (int i = 0; i < kSmAxes; ++i)
        axes[size_t(i)] = std::clamp(float(SDL_GetJoystickAxis(j, i)) / 32767.0f, -1.0f, 1.0f);
    return axes;
}
} // namespace

bool App::spaceMouseDeflected() const {
    if (!spaceMouse_) return false;
    return spaceMouseMotion(readAxes(spaceMouse_), 1.0f / 60.0f, uiPrefs_.spaceMouse).any();
}

void App::spaceMouseEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_JOYSTICK_ADDED:
        spaceMouseScan();
        break;
    case SDL_EVENT_JOYSTICK_REMOVED:
        if (spaceMouse_ && e.jdevice.which == spaceMouseId_) {
            SDL_Log("SpaceMouse: %s unplugged", spaceMouseName_.c_str());
            spaceMouseClose();
            spaceMouseScan(); // another one may still be plugged in
        }
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: {
        if (!spaceMouse_ || e.jbutton.which != spaceMouseId_) break;
        spaceMouseLastButton_ = e.jbutton.button;
        const SpaceMouseSettings& s = uiPrefs_.spaceMouse;
        if (!s.enabled || e.jbutton.button >= s.buttons.size() || spaceMouseHeld()) break;
        const std::string& id = s.buttons[e.jbutton.button];
        if (id.empty()) break;
        const Action* a = actions_.find(id);
        if (a && a->run && (!a->enabled || a->enabled())) a->run();
        break;
    }
    default:
        break; // axis motion is read once a frame (updateSpaceMouse); the event only wakes the loop
    }
}

void App::updateSpaceMouse(float dt) {
    // The first frame of a move counts as one frame, not the time since the last frame drawn (a loop that sleeps
    // while idle wakes on the axis event: the cap has only just left the centre).
    spaceMouseQuiet_ = std::min(spaceMouseQuiet_ + std::max(dt, 0.0f), 1e9f);
    spaceMouseReadNs_ = SDL_GetTicksNS();
    if (!spaceMouseMoving_) dt = std::min(dt, 1.0f / 60.0f);
    spaceMouseMoving_ = spaceMousePushed_ = false;
    if (!spaceMouse_) return;
    spaceMouseAxes_ = readAxes(spaceMouse_);
    if (spaceMouseStirred(spaceMouseAxes_, uiPrefs_.spaceMouse)) spaceMouseQuiet_ = 0.0f;
    for (float v : spaceMouseAxes_) spaceMouseReported_ = spaceMouseReported_ || v != 0.0f;
    const SpaceMouseSettings& s = uiPrefs_.spaceMouse;
    const SpaceMouseMotion m = spaceMouseMotion(spaceMouseAxes_, dt, s);
    spaceMousePushed_ = m.any();
    if (spaceMouseHeld()) return; // (the axes above still show in Preferences)
    if (!m.any()) return;
    spaceMouseMoving_ = true;
    if (m.spin != 0.0f || m.tip != 0.0f || m.roll != 0.0f) {
        if (autoOrtho_) { // an axis view turned: back to perspective, as when orbiting with the mouse
            camera_.ortho = false;
            autoOrtho_ = false;
        }
        // About the camera's target, the pivot every orbit turns around.
        if (s.turntable) {
            camera_.orbit(m.spin / 0.008f, m.tip / 0.008f); // (Camera::orbit takes pixels at 0.008 rad each)
        } else {
            camera_.setOrientation(camera_.orientation() * glm::angleAxis(-m.spin, glm::vec3(0, 1, 0)) *
                                   glm::angleAxis(-m.tip, glm::vec3(1, 0, 0)) * glm::angleAxis(-m.roll, glm::vec3(0, 0, 1)));
        }
        orbitStarted_ = false; // a mouse orbit under way continues from here, not from where it started
    }
    const float h = canvasHeight();
    if (m.panX != 0.0f || m.panY != 0.0f) camera_.pan(m.panX * h, m.panY * h, h);
    if (m.zoom != 0.0f) camera_.zoom(m.zoom);
    // With Hold during strokes off, a stroke held by the pen carries on: its dabs follow the pen on screen, so the
    // next sample sculpts where the pen now points on the turned model. A grab instead stays where it is: a pen
    // held still while the view turns moves nothing (fenix, Discord 11-06: the model flew off).
    regrabAfterViewMove();
}

bool App::spaceMouseHeld() const {
    switch (drag_) {
    case Drag::None:
    case Drag::Orbit: // the mouse's and the cap's navigation add up
    case Drag::Pan:
    case Drag::Zoom:
    case Drag::Roll:
        break;
    case Drag::Stroke:
        // A stamp dragged out (Drag Rectangle, Drag Dot) is sized and turned in the view it began in.
        if (strokeType_ == StrokeType::DragRect || strokeType_ == StrokeType::DragDot) return true;
        [[fallthrough]];
    case Drag::MaskCanvas:
    case Drag::AirStroke:
        return uiPrefs_.spaceMouse.holdWhileStroking;
    default: // a gizmo, a selection, a Forge Sphere, a guide...: anchored in the view the drag began in
        return true;
    }
    return uiPrefs_.spaceMouse.holdWhileStroking && strokeFinishing_;
}

bool App::grabStroke() const {
    if (drag_ != Drag::Stroke || !strokeSculpt_ || strokeRecipe()) return false;
    return strokeBrush_ == BrushType::Move || strokeBrush_ == BrushType::SnakeHook ||
           strokeBrush_ == BrushType::KelvinSnake || (strokeBrush_ == BrushType::KelvinGrab && kelvinStrokeBrush());
}

void App::regrabAfterViewMove() {
    if (!grabStroke()) return;
    // Where the grabbed point is on screen now: the pen's next motion moves it from there, by as much as the pen moves.
    ImVec2 at;
    if (!gizmoProject(strokeBrush_ == BrushType::KelvinGrab ? kelvinTarget_ : grabLast_, at)) return;
    grabScreenOffset_ = glm::vec2(at.x, at.y) - grabScreen_;
}

// A SpaceMouse driver that doesn't know SculptForge (3DxWare's defaults) also moves the system pointer or scrolls the
// wheel while the cap is pushed: the cursor jumped from the pen to wherever the driver put it, and the wheel zoomed
// toward it (fenix, Discord 11-06). While the cap moves the view, only the pen and touch move the cursor; a mouse's
// buttons still click. It starts inside the dead zone and its last scroll events may trail the cap: a slow tilt
// zoomed the view away (fenix, 11-06), so the echo counts from the cap's first stir, its wheel until kSpaceMouseEchoHold
// after.
bool App::spaceMouseEcho(const SDL_Event& e) const {
    SDL_MouseID which = 0;
    if (e.type == SDL_EVENT_MOUSE_MOTION) which = e.motion.which;
    else if (e.type == SDL_EVENT_MOUSE_WHEEL) which = e.wheel.which;
    else return false;
    if (which == SDL_PEN_MOUSEID || which == SDL_TOUCH_MOUSEID) return false;
    // (The cap as it is now, too: the echo of its first push comes in the same batch of events as the push, before
    // the frame reads the axes.)
    if (!spaceMouse_) return false;
    if (spaceMousePushed_ || spaceMouseStirred(readAxes(spaceMouse_), uiPrefs_.spaceMouse)) return true;
    // Only the scrolling may trail the cap: a mouse picked up as the cap is let go moves the cursor at once.
    if (e.type != SDL_EVENT_MOUSE_WHEEL) return false;
    // (Plus the time since that read: a loop that slept while idle reads the cap again only after this event.)
    const float sinceRead = spaceMouseReadNs_ ? float(double(SDL_GetTicksNS() - spaceMouseReadNs_) * 1e-9) : 0.0f;
    return spaceMouseQuiet_ + sinceRead < kSpaceMouseEchoHold;
}

bool App::sixDofPointerEvent(const SDL_Event& e) {
    SDL_MouseID which = 0;
    if (e.type == SDL_EVENT_MOUSE_MOTION) which = e.motion.which;
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) which = e.button.which;
    else if (e.type == SDL_EVENT_MOUSE_WHEEL) which = e.wheel.which;
    else return false;
    if (which == 0 || which == SDL_PEN_MOUSEID || which == SDL_TOUCH_MOUSEID) return false; // (0: the system's pointer)
    auto it = sixDofMice_.find(which);
    if (it == sixDofMice_.end()) {
        const char* name = SDL_GetMouseNameForID(which);
        // By name only (a mouse has no USB ids in SDL): "3Dconnexion ...", "SpaceMouse ...", never a CadMouse.
        const bool sixDof = name && isSpaceMouseDevice(0, 0, name);
        if (sixDof) SDL_Log("SpaceMouse: pointer events from \"%s\" ignored (a 6-DoF device, not a mouse)", name);
        it = sixDofMice_.emplace(which, sixDof).first;
    }
    return it->second;
}

// ---------------------------------------------------------------------------
// Preferences > Input > SpaceMouse
// ---------------------------------------------------------------------------

void App::drawSpaceMouseSection() {
    SpaceMouseSettings& s = uiPrefs_.spaceMouse;
    const char* path = "Preferences > Input > SpaceMouse";
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::Spacing();
    ImGui::SeparatorText(tr("SpaceMouse (experimental)"));
    if (spaceMouse_) {
        ImGui::TextColored(col(ui::color::accent), ICON_LC_JOYSTICK);
        ImGui::SameLine();
        ImGui::TextUnformatted(spaceMouseName_.c_str());
    } else {
        ImGui::TextUnformatted(tr(ICON_LC_JOYSTICK "  No SpaceMouse found"));
        ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::textDim));
        ImGui::PushTextWrapPos(0.0f);
#if defined(_WIN32) || defined(__APPLE__)
        ImGui::TextUnformatted(tr("Plug it in. If 3DxWare is installed it may keep the device to itself: see the "
                               "Navigation page of the guide."));
#else
        ImGui::TextUnformatted(tr("Plug it in. SculptForge reads it as a joystick (/dev/input): you need read access to "
                               "it, and spacenavd may hold it. See the Navigation page of the guide."));
#endif
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    // What SDL sees, so a report says why a device wasn't taken (#104); Copy puts it on the clipboard for an issue.
    if (!spaceMouse_ || !spaceMouseReported_) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::textDim));
        ImGui::PushTextWrapPos(0.0f);
        if (spaceMouse_)
            ImGui::TextUnformatted(tr("Push the cap: if the bars below stay still, another program (3DxWare, spacenavd) "
                                   "holds the SpaceMouse. See the Navigation page of the guide."));
        if (joysticksSeen_.empty() && !spaceMouse_) ImGui::TextUnformatted(tr("No joysticks or game controllers seen."));
        for (const std::string& d : joysticksSeen_) { // (wrapped: a name and its ids can be long)
            ImGui::Bullet();
            ImGui::TextUnformatted(d.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (ImGui::SmallButton(tr(ICON_LC_COPY "  Copy device report##spacemouse.report"))) {
            std::string r = std::string("SculptForge ") + SCULPTFORGE_VERSION + ", " + SDL_GetPlatform() + ", SDL " +
                            std::to_string(SDL_MAJOR_VERSION) + "." + std::to_string(SDL_MINOR_VERSION) + "." +
                            std::to_string(SDL_MICRO_VERSION) + "\nSpaceMouse: " +
                            (spaceMouse_ ? spaceMouseName_ + (spaceMouseReported_ ? " (moving)" : " (no motion yet)")
                                         : std::string("none")) +
                            "\nJoysticks seen:";
            for (const std::string& d : joysticksSeen_) r += "\n- " + d;
            if (joysticksSeen_.empty()) r += " none";
            SDL_SetClipboardText(r.c_str());
        }
        ui::HelpText("Copy device report", nullptr,
                     "Copies what SculptForge sees (each joystick's name, USB ids and why it was or wasn't used) to "
                     "paste into a bug report.",
                     path);
    }
    ui::HelpText("SpaceMouse", nullptr,
                 "Experimental. A 3Dconnexion SpaceMouse moves the view with one hand while the other holds the pen: "
                 "push, pull and twist the cap to pan, zoom and turn the model about the pivot. Read directly as a USB "
                 "device, without 3Dconnexion's driver; tested only with simulated input so far.",
                 path);

    ui::Checkbox("Use SpaceMouse##spacemouse.enabled", &s.enabled);
    ui::HelpText("Use SpaceMouse", nullptr, "Experimental. Off: the SpaceMouse is ignored.", path);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::SliderInside("##spacemouse.speed", &s.speed, 0.1f, 4.0f, "Speed %.2fx", ImGuiSliderFlags_Logarithmic);
    ui::HelpText("SpaceMouse speed", nullptr, "How fast the view moves for the same push on the cap.", path);
    float dead = s.deadZone * 100.0f;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ui::SliderInside("##spacemouse.dead_zone", &dead, 0.0f, 50.0f, "Dead zone %.0f%%"))
        s.deadZone = std::clamp(dead / 100.0f, 0.0f, 0.5f);
    ui::HelpText("Dead zone", nullptr,
                 "How far the cap must move before the view does, so a hand resting on it or a slightly off-centre cap "
                 "doesn't drift the view.",
                 path);
    ui::Checkbox("Lock rotation to turntable##spacemouse.turntable", &s.turntable);
    ui::HelpText("Lock rotation to turntable", nullptr,
                 "The model stays upright: twisting turns it about the world's up axis, tilting looks over or under it, "
                 "and rolling the cap does nothing.",
                 path);
    ui::Checkbox("Hold during strokes##spacemouse.hold", &s.holdWhileStroking);
    ui::HelpText("Hold during strokes", nullptr,
                 "While you sculpt, paint or mask, the SpaceMouse leaves the view alone and its buttons do nothing, so "
                 "a hand resting on the cap never moves the model under the pen. Off: the view turns mid-stroke.",
                 path);
    ui::Checkbox("Dominant axis##spacemouse.dominant", &s.dominant);
    ui::HelpText("Dominant axis", nullptr,
                 "Only the direction you push the most moves the view: a twist doesn't also pan a little.", path);

    // Invert, two rows of three: the moves, then the turns.
    ImGui::TextDisabled("%s", tr("Invert"));
    const float cw = (width - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    for (int i = 0; i < kSmAxes; ++i) {
        if (i % 3) ImGui::SameLine(float(i % 3) * (cw + ImGui::GetStyle().ItemSpacing.x));
        bool inv = s.invert[size_t(i)];
        if (ui::Checkbox((std::string(kAxisNames[i]) + "##spacemouse.invert" + std::to_string(i)).c_str(), &inv))
            s.invert[size_t(i)] = inv;
        ui::HelpText((std::string("Invert ") + kAxisNames[i]).c_str(), nullptr,
                     "Move the view the other way for this push or turn of the cap.", path);
    }

    // The live axes, so a cap that drifts or an axis going the wrong way is easy to see: one bar per axis, in the
    // order above, filled from the centre; grey while inside the dead zone.
    if (spaceMouse_) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", tr("Live"));
        ImGui::SameLine();
        const float x0 = ImGui::GetCursorScreenPos().x, avail = ImGui::GetContentRegionAvail().x;
        const float gap = 4.0f * ImGui::GetFontSize() / 15.0f, bw = (avail - 5.0f * gap) / 6.0f;
        const float bh = std::max(3.0f, ImGui::GetFontSize() * 0.3f);
        const float y = ImGui::GetCursorScreenPos().y + (ImGui::GetFrameHeight() - bh) * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < kSmAxes; ++i) {
            const float a = x0 + float(i) * (bw + gap), mid = a + bw * 0.5f, v = spaceMouseAxes_[size_t(i)];
            dl->AddRectFilled(ImVec2(a, y), ImVec2(a + bw, y + bh), IM_COL32(255, 255, 255, 28), bh * 0.5f);
            dl->AddRectFilled(ImVec2(std::min(mid, mid + v * bw * 0.5f), y), ImVec2(std::max(mid, mid + v * bw * 0.5f), y + bh),
                              std::abs(v) <= s.deadZone ? ui::color::textDim : ui::color::accent, bh * 0.5f);
            dl->AddLine(ImVec2(mid, y - 1.0f), ImVec2(mid, y + bh + 1.0f), ui::color::textDim);
        }
        ImGui::Dummy(ImVec2(avail, ImGui::GetFrameHeight()));
        ui::HelpText("Live axes", nullptr,
                     "What the SpaceMouse reports now: Pan X, Zoom, Pan Y, Tilt, Roll, Spin. Grey: inside the dead zone.",
                     path);
    }

    // Buttons: as many rows as the device has (at least the two side buttons), up to kSpaceMouseButtonSlots.
    const int rows = std::clamp(spaceMouseButtonCount_, 2, kSpaceMouseButtonSlots);
    for (int b = 0; b < rows; ++b) {
        std::string& id = s.buttons[size_t(b)];
        const char* shown = id.empty() ? "Nothing" : id.c_str();
        for (const ButtonChoice& c : kButtonChoices)
            if (id == c.id) shown = c.label;
        const std::string label = b == 0 ? "Left button" : b == 1 ? "Right button" : "Button " + std::to_string(b + 1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine(width * 0.42f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo(("##spacemouse.button" + std::to_string(b)).c_str(), shown)) {
            for (const ButtonChoice& c : kButtonChoices)
                if (ImGui::Selectable((std::string(c.label) + "##spacemouse.button" + std::to_string(b)).c_str(), id == c.id))
                    id = c.id;
            ImGui::EndCombo();
        }
        ui::HelpText(label.c_str(), nullptr,
                     b < 2 ? "What this side button of the SpaceMouse does (the two side buttons: Frame and Front view)."
                           : "What this button of the SpaceMouse does.",
                     path);
    }
    if (spaceMouse_ && spaceMouseLastButton_ >= 0)
        ImGui::TextColored(col(ui::color::textDim), tr("Last button pressed: %d"), spaceMouseLastButton_ + 1);
    if (ImGui::SmallButton(tr(ICON_LC_ROTATE_CCW "  SpaceMouse defaults##spacemouse.defaults")))
        s = SpaceMouseSettings();
    ui::HelpText("SpaceMouse defaults", nullptr,
                 "On, speed 1, dead zone 8%, nothing inverted, free rotation, held during strokes, all axes at once; left "
                 "button Frame, right button Front view.",
                 path);
}

} // namespace sf
