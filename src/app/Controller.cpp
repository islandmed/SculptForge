// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game controller (and SpaceMouse) buttons as shortcuts. A controller is opened through SDL's gamepad API, so any
// pad SDL has a mapping for works, plugged or unplugged while the app runs. Preferences > Hotkeys binds a button to
// any action the way it binds a key; a pie menu, the Quick Menu and the stroke popup follow the button as they
// follow a key (held: point, let go; tapped: they stay open). Buttons only: no sticks, no rumble, nothing read from
// the network. The ids, names and the table are in core/Controller.h; User guide: docs/guide/shortcuts.md.
#include "app/App.h"
#include "app/Ui.h"
#include "app/I18n.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cfloat>

namespace sf {

namespace {

ImVec4 col(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// A pseudo key code no real key has (SDL's key codes are Unicode, or carry bit 30 or 29): a held pie menu waits for
// this "key" to be let go.
constexpr SDL_Keycode kControllerCodeBase = SDL_Keycode(1u << 28);

const char* padButtonId(SDL_GamepadButton b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "pad.south";
    case SDL_GAMEPAD_BUTTON_EAST: return "pad.east";
    case SDL_GAMEPAD_BUTTON_WEST: return "pad.west";
    case SDL_GAMEPAD_BUTTON_NORTH: return "pad.north";
    case SDL_GAMEPAD_BUTTON_BACK: return "pad.back";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "pad.guide";
    case SDL_GAMEPAD_BUTTON_START: return "pad.start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "pad.leftstick";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "pad.rightstick";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "pad.leftshoulder";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "pad.rightshoulder";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "pad.dpad_up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "pad.dpad_down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "pad.dpad_left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "pad.dpad_right";
    case SDL_GAMEPAD_BUTTON_MISC1: return "pad.misc1";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "pad.rightpaddle1";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "pad.leftpaddle1";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "pad.rightpaddle2";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "pad.leftpaddle2";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "pad.touchpad";
    case SDL_GAMEPAD_BUTTON_MISC2: return "pad.misc2";
    case SDL_GAMEPAD_BUTTON_MISC3: return "pad.misc3";
    case SDL_GAMEPAD_BUTTON_MISC4: return "pad.misc4";
    case SDL_GAMEPAD_BUTTON_MISC5: return "pad.misc5";
    case SDL_GAMEPAD_BUTTON_MISC6: return "pad.misc6";
    default: return nullptr;
    }
}

} // namespace

SDL_Keycode App::controllerCode(const std::string& button) const {
    return kControllerCodeBase + SDL_Keycode(controllerButtonIndex(button));
}

// (Device names, as printed beside the buttons: not translated.)
std::string App::controllerButtonText(const std::string& button) const { return controllerButtonName(button); }

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

void App::controllerScan() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) {
        const SDL_JoystickID id = ids[i];
        if (std::any_of(pads_.begin(), pads_.end(), [&](const Pad& p) { return p.id == id; })) continue;
        // UI tests take only their own virtual controller, not one plugged into the machine running them.
        if (!options_.uiTest.empty() && !SDL_IsJoystickVirtual(id)) continue;
        const char* name = SDL_GetGamepadNameForID(id);
        // A 6-DoF controller is the SpaceMouse's (SpaceMouse.cpp), never a pad.
        if (isSpaceMouseDevice(SDL_GetGamepadVendorForID(id), SDL_GetGamepadProductForID(id), name ? name : "")) continue;
        SDL_Gamepad* g = SDL_OpenGamepad(id);
        if (!g) {
            SDL_Log("Controller: cannot open %s: %s", name ? name : "?", SDL_GetError());
            continue;
        }
        Pad p;
        p.id = id;
        p.pad = g;
        p.name = name && *name ? name : "Controller";
        SDL_Log("Controller: %s (%04x:%04x)", p.name.c_str(), SDL_GetGamepadVendor(g), SDL_GetGamepadProduct(g));
        pads_.push_back(std::move(p));
    }
    SDL_free(ids);
}

void App::controllerClose(SDL_JoystickID id) {
    const auto it = std::find_if(pads_.begin(), pads_.end(), [&](const Pad& p) { return p.id == id; });
    if (it == pads_.end()) return;
    SDL_Log("Controller: %s unplugged", it->name.c_str());
    SDL_CloseGamepad(it->pad);
    pads_.erase(it);
    // Whatever it held goes up with it: a pie menu waiting for the button would stay open for good.
    if (pads_.empty()) {
        const std::vector<std::string> held = controllerHeld_;
        for (const std::string& b : held)
            if (!controllerButtonIsSpaceMouse(b)) controllerPress(b, false);
    }
}

void App::controllerCloseAll() {
    for (Pad& p : pads_)
        if (p.pad) SDL_CloseGamepad(p.pad);
    pads_.clear();
}

void App::controllerEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        controllerScan();
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        controllerClose(e.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        if (std::none_of(pads_.begin(), pads_.end(), [&](const Pad& p) { return p.id == e.gbutton.which; })) break;
        if (const char* id = padButtonId(SDL_GamepadButton(e.gbutton.button)))
            controllerPress(id, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: { // only the triggers: they count as buttons past half way
        const int axis = e.gaxis.axis;
        if (axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) break;
        const auto it = std::find_if(pads_.begin(), pads_.end(), [&](const Pad& p) { return p.id == e.gaxis.which; });
        if (it == pads_.end()) break;
        const size_t side = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 0 : 1;
        const bool now = controllerTriggerPressed(it->trigger[side], float(e.gaxis.value) / 32767.0f);
        if (now == it->trigger[side]) break;
        it->trigger[side] = now;
        controllerPress(side == 0 ? "pad.lefttrigger" : "pad.righttrigger", now);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

void App::beginControllerCapture(const std::string& actionId) {
    beginShortcutCapture(actionId); // (the key capture's state and anchor; this one waits for a button instead)
    if (capturingShortcut_ != actionId) return;
    captureController_ = true;
    const Action* a = actions_.find(actionId);
    status_ = "Press a controller button for '" + (a ? a->label : actionId) + "' (Esc cancels, Backspace clears)";
}

bool App::controllerPress(const std::string& button, bool down) {
    ControllerSettings& cs = uiPrefs_.controller;
    if (!down) {
        const auto held = std::find(controllerHeld_.begin(), controllerHeld_.end(), button);
        if (held == controllerHeld_.end()) return false;
        controllerHeld_.erase(held);
        const SDL_Keycode code = controllerCode(button);
        if (cs.actionFor(button) == "ui.quickmenu") quickMenuKeyUp(); // a held Quick Menu closes when let go
        radialKeyUp(code);                                           // a held pie menu: run the slot pointed at
        strokePopupKeyUp(code);                                      // the stroke popup: pick the item pointed at
        return true;
    }
    controllerLast_ = button;
    redrawIn(0.0); // (the frame that shows the menu this press opens, or the new binding)
    if (capturingController()) { // Preferences > Hotkeys: this button becomes the action's
        const std::string actionId = capturingShortcut_;
        const Action* a = actions_.find(actionId);
        const std::string label = a ? trAction(a->id, a->label) : actionId;
        const std::string was = cs.actionFor(button);
        capturingShortcut_.clear();
        captureController_ = false;
        if (was == actionId) {
            status_ = controllerButtonText(button) + " already runs " + label;
            return true;
        }
        cs.bind(button, actionId);
        const Action* old = was.empty() ? nullptr : actions_.find(was);
        status_ = controllerButtonText(button) + ": " + label +
                  (was.empty() ? std::string() : " (was " + (old ? std::string(trAction(old->id, old->label)) : was) + ")");
        return true;
    }
    if (!capturingShortcut_.empty() || shortcutConflictOpen()) return false; // a key is being captured
    if (!controllerButtonIsSpaceMouse(button) && !cs.enabled) return false;
    const std::string id = cs.actionFor(button);
    if (id.empty()) return false;
    // Not under a card that wants the input, nor while typing: the button is for the canvas and the menus.
    if (supportCard_ || whatsNew_ || ImGui::GetIO().WantTextInput || ui::TextFieldFocused()) return true;
    const Action* a = actions_.find(id);
    if (!a || !a->run) {
        status_ = "Controller button " + controllerButtonText(button) + ": no action '" + id + "' in this version";
        return true;
    }
    if (a->enabled && !a->enabled()) {
        status_ = std::string(trAction(a->id, a->label)) + " isn't available right now";
        return true;
    }
    const std::function<void()> run = a->run; // (running an action may register or replace actions)
    keyDown_ = controllerCode(button);        // the pie menu, Quick Menu and stroke popup hold open on it, as on a key
    ++interactiveCommand_;
    run();
    --interactiveCommand_;
    keyDown_ = SDLK_UNKNOWN;
    if (std::find(controllerHeld_.begin(), controllerHeld_.end(), button) == controllerHeld_.end())
        controllerHeld_.push_back(button);
    return true;
}

// ---------------------------------------------------------------------------
// Preferences > Input > Controller
// ---------------------------------------------------------------------------

void App::drawControllerSection() {
    ControllerSettings& cs = uiPrefs_.controller;
    const char* path = "Preferences > Input > Controller";
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Controller buttons"));
    if (pads_.empty()) {
        ImGui::TextUnformatted(tr(ICON_LC_GAMEPAD_2 "  No game controller found"));
    } else {
        for (const Pad& p : pads_) {
            ImGui::TextColored(col(ui::color::accent), ICON_LC_GAMEPAD_2);
            ImGui::SameLine();
            ImGui::TextUnformatted(p.name.c_str());
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::textDim));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(tr("Bind a button to any action in Preferences > Hotkeys: click the controller tile beside the "
                              "action, then press the button. A pie menu on a button works in Clean view too."));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ui::HelpText("Controller buttons", nullptr,
                 "A game controller's buttons (and the SpaceMouse's) run any action, as keys do: the action runs when the "
                 "button goes down. A pie menu, the Quick Menu or the stroke popup on a button: hold it, point with the pen "
                 "or mouse and let go to pick, or tap it and click. Buttons only: the sticks do nothing here.",
                 path);
    ui::Checkbox("Use controller buttons##controller.enabled", &cs.enabled);
    ui::HelpText("Use controller buttons", nullptr, "Off: a game controller's buttons are ignored.", path);
    if (!controllerLast_.empty())
        ImGui::TextColored(col(ui::color::textDim), "%s %s", tr("Last button pressed:"), controllerButtonText(controllerLast_).c_str());
    if (cs.buttons.empty()) {
        ImGui::TextColored(col(ui::color::textDim), "%s", tr("No buttons bound."));
        return;
    }
    std::string remove;
    for (const auto& [button, actionId] : cs.buttons) {
        const Action* a = actions_.find(actionId);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(controllerButtonText(button).c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
        const std::string shown = a ? std::string(trAction(a->id, a->label)) : actionId;
        if (!a) ImGui::PushStyleColor(ImGuiCol_Text, col(ui::color::textDim));
        ImGui::TextUnformatted(shown.c_str());
        if (!a) ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
        if (ImGui::SmallButton((std::string(ICON_LC_X) + "##controller.remove." + button).c_str())) remove = button;
        ui::HelpText("Unbind", nullptr, "Frees this button.", path);
    }
    if (!remove.empty()) cs.unbind(remove);
    if (ImGui::SmallButton(tr(ICON_LC_ROTATE_CCW "  Clear controller buttons##controller.clear"))) cs.buttons.clear();
    ui::HelpText("Clear controller buttons", nullptr, "Frees every controller button.", path);
}

} // namespace sf
