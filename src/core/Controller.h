// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game controller and SpaceMouse buttons as shortcuts (Discord: a pop-up menu from a controller button in Clean view).
// Independent of SDL so it can be unit tested: the stable ids a button is saved under, their names, the table that
// says which action each button runs (Preferences > Hotkeys: click the pad button, press the button) and its JSON.
// Buttons only: no sticks, no rumble. The app side is src/app/Controller.cpp, the guide docs/guide/shortcuts.md.
#pragma once

#include "core/Json.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sf {

// A button's id as saved in ui.json: "pad.south" (the gamepad's bottom face button: A, Cross), "pad.leftshoulder",
// "pad.dpad_up", "pad.lefttrigger" (a trigger counts as a button once it is pulled past half way), or
// "spacemouse.3" (the third button of a SpaceMouse, counted from 1). Ids do not depend on the device's labels, so a
// binding survives swapping one controller for another.
constexpr int kControllerSpaceMouseButtons = 32; // a SpaceMouse Enterprise has 31

// True for an id this build knows (a saved file with another one is dropped).
bool controllerButtonKnown(std::string_view id);
// Every gamepad button id, in the order Preferences lists them.
const std::vector<std::string>& controllerPadButtons();
// "Pad A / Cross", "Pad Left bumper", "SpaceMouse button 3": the English name (the app translates it).
std::string controllerButtonName(std::string_view id);
// A number that is the same for the same button and unique across buttons (1.. for pads, 1000+ for SpaceMouse
// buttons), 0 for an unknown id. The app turns it into the pseudo key code a held pie menu waits on.
int controllerButtonIndex(std::string_view id);
bool controllerButtonIsSpaceMouse(std::string_view id);
// "spacemouse.N" for SpaceMouse button N counted from 0, "" past kControllerSpaceMouseButtons.
std::string controllerSpaceMouseId(int zeroBasedButton);

// A trigger pulled past kControllerTriggerOn (of its 0..1 travel) is pressed, let go below kControllerTriggerOff.
constexpr float kControllerTriggerOn = 0.6f;
constexpr float kControllerTriggerOff = 0.35f;
bool controllerTriggerPressed(bool wasPressed, float value);

constexpr size_t kControllerActionIdMax = 96; // bytes in an action id (longer ones are never real ids)

// Which action each button runs. One action per button; an action may have several buttons.
struct ControllerSettings {
    bool enabled = true; // off: controllers are ignored (the SpaceMouse's own settings are separate)
    std::vector<std::pair<std::string, std::string>> buttons; // button id -> action id, in the order they were bound

    // The action on BUTTON, "" for none.
    std::string actionFor(std::string_view button) const;
    // The buttons that run ACTION, in the order they were bound.
    std::vector<std::string> buttonsOf(std::string_view action) const;
    // Binds BUTTON to ACTION and returns the action it ran before ("" for none, ACTION again for no change).
    std::string bind(std::string_view button, std::string_view action);
    // Frees BUTTON (true when it ran something).
    bool unbind(std::string_view button);
    // Frees every button of ACTION (true when there was one).
    bool unbindAction(std::string_view action);
    bool operator==(const ControllerSettings& o) const { return enabled == o.enabled && buttons == o.buttons; }
};

Json toJson(const ControllerSettings& s);
// Reads what is valid (known buttons, plausible action ids, each button once) and ignores the rest; S is left as
// it was for anything that is not an object.
void fromJson(const Json& j, ControllerSettings& s);

} // namespace sf
