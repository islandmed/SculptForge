// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Shift and Ctrl brushes (Discord #ideas, Cytrick; core/ModifierBrush.h): Shift+click a brush tile on the
// bottom shelf, or a brush in the picker, and holding Shift sculpts with it instead of Smooth; Ctrl+click a shelf
// brush and holding Ctrl sculpts with it instead of masking. The main brush stays. Shift+click Smooth, picking a
// mask brush with Ctrl held (the brush picker or the left shelf's tile), Brush > Reset Shift / Ctrl brush or
// `set_modifier_brush shift=default` go back. Kept in the preferences (ui.json).
#include "app/App.h"

#include <IconsLucide.h>

namespace sf {

ModifierSlot App::heldSlot() const {
    const SDL_Keymod k = keyMods(); // the keyboard (not a pen button set to Smooth, nor the stroke popup's latches)
    return heldModifierSlot(uiPrefs_.modifierBrushes, (k & SDL_KMOD_SHIFT) != 0, (k & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0,
                            (k & SDL_KMOD_ALT) != 0, paintMode_ || guidesMode_);
}

void App::setModifierBrush(ModifierSlot slot, int type) {
    status_ = sf::setModifierBrush(uiPrefs_.modifierBrushes, slot, type);
}

bool App::modifierBrushClick(int type) {
    const SDL_Keymod k = SDL_GetModState();
    const bool shift = (k & SDL_KMOD_SHIFT) != 0, ctrl = (k & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if (type < 0 || shift == ctrl || (k & SDL_KMOD_ALT)) return false;
    setModifierBrush(shift ? ModifierSlot::Shift : ModifierSlot::Ctrl, type);
    return true;
}

float& App::heldIntensity() {
    const bool stroking = drag_ == Drag::Stroke;
    const BrushType b = stroking ? strokeBrush_ : activeBrush();
    if (b == BrushType::Smooth) return smoothStrength_; // Smooth keeps its own Intensity, picked or held
    switch (stroking ? strokeSlot_ : heldSlot()) {
    case ModifierSlot::Shift: return uiPrefs_.modifierBrushes.shiftIntensity;
    case ModifierSlot::Ctrl: return uiPrefs_.modifierBrushes.ctrlIntensity;
    case ModifierSlot::None: break;
    }
    return intensityValue(false); // (in Paint mode the Colour intensity, #164)
}

std::string App::heldIntensityName() {
    const bool stroking = drag_ == Drag::Stroke;
    const BrushType b = stroking ? strokeBrush_ : activeBrush();
    if (b == BrushType::Smooth) return "Smooth Intensity";
    const ModifierSlot s = stroking ? strokeSlot_ : heldSlot();
    if (s == ModifierSlot::None) return "Intensity";
    return std::string(brushLabel(b)) + " Intensity (" + modifierSlotName(s) + ")";
}

float App::strokeStrength() const {
    switch (strokeSlot_) {
    case ModifierSlot::Shift: return uiPrefs_.modifierBrushes.shiftIntensity;
    case ModifierSlot::Ctrl: return uiPrefs_.modifierBrushes.ctrlIntensity;
    case ModifierSlot::None: break;
    }
    return strength_;
}

void App::registerModifierBrushCommands() {
    commands_.add({"set_modifier_brush",
                   "The brushes holding Shift and Ctrl use (Shift+click / Ctrl+click a brush on the bottom shelf does "
                   "the same). 'default' goes back: Shift smooths, Ctrl masks. Omitted slots stay as they are.",
                   {{"shift", ParamSpec::Type::String, "The Shift brush (trim, flatten, clay...) or default (Smooth).", false},
                    {"ctrl", ParamSpec::Type::String, "The Ctrl brush or default (the mask).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       std::string status;
                       for (ModifierSlot s : {ModifierSlot::Shift, ModifierSlot::Ctrl}) {
                           const char* key = s == ModifierSlot::Shift ? "shift" : "ctrl";
                           if (a.find(key) == a.end()) continue;
                           int type = -1;
                           if (!modifierBrushFromName(argString(a, key, ""), s, type))
                               return {false, std::string("no brush named '") + argString(a, key, "") + "'"};
                           status += (status.empty() ? "" : ", ") + sf::setModifierBrush(uiPrefs_.modifierBrushes, s, type);
                       }
                       if (status.empty())
                           status = "Shift brush: " + modifierBrushLabel(uiPrefs_.modifierBrushes, ModifierSlot::Shift) +
                                    ", Ctrl brush: " + modifierBrushLabel(uiPrefs_.modifierBrushes, ModifierSlot::Ctrl);
                       status_ = status;
                       return {true, status};
                   }});
    for (ModifierSlot s : {ModifierSlot::Shift, ModifierSlot::Ctrl}) {
        const bool shift = s == ModifierSlot::Shift;
        Action a;
        a.id = shift ? "brush.shift_reset" : "brush.ctrl_reset";
        a.label = shift ? "Reset Shift brush (Smooth)" : "Reset Ctrl brush (Mask)";
        a.icon = ICON_LC_ROTATE_CCW;
        a.palette = "Brush";
        a.description = shift ? "Holding Shift smooths again. Shift+click a brush on the bottom shelf (or in the brush "
                                "picker) to make it the Shift brush; Shift+click Smooth also goes back."
                              : "Holding Ctrl masks again. Ctrl+click a brush on the bottom shelf to make it the Ctrl "
                                "brush; picking a mask brush with Ctrl held also goes back.";
        a.run = [this, s] { setModifierBrush(s, -1); };
        a.enabled = [this, s] { return uiPrefs_.modifierBrushes.bound(s); };
        actions_.add(std::move(a));
    }
}

} // namespace sf
