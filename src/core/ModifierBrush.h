// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Shift and Ctrl brushes (Discord #ideas, Cytrick): holding Shift smooths and holding Ctrl masks by default;
// Shift+click a brush on the bottom shelf (or in the brush picker) makes it the Shift brush, Ctrl+click on the shelf
// the Ctrl brush, and the main brush stays as it was. Shift+click Smooth, or picking a mask brush with Ctrl held,
// goes back. Each slot keeps its own Intensity, as Smooth does. Independent of SDL and ImGui so it can be unit
// tested; the app side (clicks, badges, strokes) is App::setModifierBrush and App::heldSlot.
#pragma once

#include "core/Json.h"
#include "core/Sculpt.h"

#include <string>
#include <string_view>

namespace sf {

enum class ModifierSlot { None, Shift, Ctrl };

struct ModifierBrushes {
    int shift = -1, ctrl = -1;          // a BrushType, or -1: the default (Shift smooths, Ctrl masks)
    float shiftIntensity = 0.5f, ctrlIntensity = 0.5f; // the slot's own Intensity (Smooth keeps Smooth's own)

    int brush(ModifierSlot s) const { return s == ModifierSlot::Shift ? shift : s == ModifierSlot::Ctrl ? ctrl : -1; }
    bool bound(ModifierSlot s) const { return brush(s) >= 0; }
};

const char* modifierSlotName(ModifierSlot s); // "Shift", "Ctrl"; "" for None
// The brush the slot uses, for the status line and the guide: "Smooth" / "Mask" when it is the default.
std::string modifierBrushLabel(const ModifierBrushes& m, ModifierSlot s);

// Sets the slot. Smooth on Shift, or -1, is the default (Shift smooths with the classic alternate smooth, #40).
// Returns the status line ("Shift brush: Trim").
std::string setModifierBrush(ModifierBrushes& m, ModifierSlot s, int type);

// What a script names: a brush ("trim", "Clay Block"), or "default" / "reset" / "mask" (Ctrl) / "smooth" (Shift)
// for the default. -1 = the default; false when the name means nothing.
bool modifierBrushFromName(std::string_view name, ModifierSlot s, int& type);

// The slot a stroke begun with these keys (the keyboard's, after the input style's mapping) uses. Only a rebound
// slot counts: unbound, Shift and Ctrl keep their usual meanings (Smooth with #40, the mask and its click blur).
// Shift+Ctrl stays the selection keys and Ctrl+Alt the unmask; the Paint brush keeps Shift's smooth and Ctrl's mask.
ModifierSlot heldModifierSlot(const ModifierBrushes& m, bool shift, bool ctrl, bool alt, bool paintMode);

// The classic alternate smooth (#40): a Smooth stroke begun with Shift whose Shift is let go mid-stroke switches
// between plain and volume-keeping smooth. (Not for a rebound Shift brush: its strokes aren't Smooth.)
inline bool alternateSmoothStarts(BrushType strokeBrush, bool shiftAtStart, bool switchedAlready, bool maskStroke,
                                  bool shiftNow) {
    return strokeBrush == BrushType::Smooth && shiftAtStart && !switchedAlready && !maskStroke && !shiftNow;
}

// Preferences (ui.json "modifierBrushes"): brushes by name, so a renamed or newer brush list still reads.
Json toJson(const ModifierBrushes& m);
void fromJson(const Json& j, ModifierBrushes& m);

} // namespace sf
