// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 3Dconnexion SpaceMouse navigation (#6), experimental: the device is opened
// through SDL's joystick API, not the 3Dconnexion SDK, so it works where the
// operating system shows it as a HID joystick (Linux without spacenavd holding
// it, macOS, Windows without 3DxWare hiding it). The cap's six axes move the
// camera every frame about the same pivot the mouse orbits around; the buttons
// run actions (Frame and Front view by default). The math and the settings are
// in core/SpaceMouse.h; Preferences > Input > SpaceMouse edits them, Store
// Config keeps them. User guide: docs/guide/navigation.md.
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
    if (!ids) return;
    for (int i = 0; i < count && !spaceMouse_; ++i) {
        // UI tests take only their own virtual SpaceMouse, not one plugged into the machine running them.
        if (!options_.uiTest.empty() && !SDL_IsJoystickVirtual(ids[i])) continue;
        const char* name = SDL_GetJoystickNameForID(ids[i]);
        if (!isSpaceMouseDevice(SDL_GetJoystickVendorForID(ids[i]), SDL_GetJoystickProductForID(ids[i]), name ? name : ""))
            continue;
        SDL_Joystick* j = SDL_OpenJoystick(ids[i]);
        if (!j) {
            SDL_Log("SpaceMouse: cannot open %s: %s", name ? name : "?", SDL_GetError());
            continue;
        }
        if (SDL_GetNumJoystickAxes(j) < kSmAxes) { // a 3Dconnexion device without a 6-DoF cap
            SDL_CloseJoystick(j);
            continue;
        }
        spaceMouse_ = j;
        spaceMouseId_ = ids[i];
        spaceMouseName_ = name && *name ? name : "SpaceMouse";
        spaceMouseButtonCount_ = std::max(SDL_GetNumJoystickButtons(j), 0);
        spaceMouseAxes_ = {};
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
    spaceMouseMoving_ = false;
}

bool App::spaceMouseDeflected() const {
    if (!spaceMouse_) return false;
    std::array<float, kSmAxes> axes{};
    for (int i = 0; i < kSmAxes; ++i)
        axes[size_t(i)] = std::clamp(float(SDL_GetJoystickAxis(spaceMouse_, i)) / 32767.0f, -1.0f, 1.0f);
    return spaceMouseMotion(axes, 1.0f / 60.0f, uiPrefs_.spaceMouse).any();
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
        if (!s.enabled || e.jbutton.button >= s.buttons.size()) break;
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
    if (!spaceMouseMoving_) dt = std::min(dt, 1.0f / 60.0f);
    spaceMouseMoving_ = false;
    if (!spaceMouse_) return;
    for (int i = 0; i < kSmAxes; ++i)
        spaceMouseAxes_[size_t(i)] = std::clamp(float(SDL_GetJoystickAxis(spaceMouse_, i)) / 32767.0f, -1.0f, 1.0f);
    const SpaceMouseSettings& s = uiPrefs_.spaceMouse;
    const SpaceMouseMotion m = spaceMouseMotion(spaceMouseAxes_, dt, s);
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
    // A stroke held by the pen carries on: its dabs follow the pen on screen, so the next sample sculpts where the
    // pen now points on the turned model; nothing else about the stroke changes.
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
        ImGui::TextUnformatted(ICON_LC_JOYSTICK "  No SpaceMouse found");
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
        ImGui::TextColored(col(ui::color::textDim), "Last button pressed: %d", spaceMouseLastButton_ + 1);
    if (ImGui::SmallButton(ICON_LC_ROTATE_CCW "  SpaceMouse defaults##spacemouse.defaults")) s = SpaceMouseSettings();
    ui::HelpText("SpaceMouse defaults", nullptr,
                 "On, speed 1, dead zone 8%, nothing inverted, free rotation, all axes at once; left button Frame, right "
                 "button Front view.",
                 path);
}

} // namespace sf
