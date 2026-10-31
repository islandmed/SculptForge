// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads the keys out of a keymap exported from Blender (Preferences > Keymap > Export: a .py file of
// keyconfig_data tuples). The file is never run: a strict scanner reads the item tuples as text and keeps only
// the navigation and sculpt entries SculptForge has an action for (view3d.view_axis, paint.brush_select,
// wm.radial_control...). Everything else, and anything that doesn't look exactly like Blender's export, is
// skipped. Used by Preferences > Input / Controls > Import Blender keymap (Input.cpp).
#pragma once

#include "app/Actions.h"

#include <string>
#include <string_view>
#include <vector>

namespace sf {

struct BlenderKeyBinding {
    std::string action; // SculptForge action id: "view.front", "brush.standard", "ui.size_popup"
    Chord chord;
    int priority = 0;   // the keymap it came from: Sculpt 2 > 3D View 1 > Screen / Window 0 (Blender's order)
};

struct BlenderKeymap {
    std::vector<BlenderKeyBinding> bindings; // one chord per action and per key, highest priority first
    int items = 0;   // key map items read (keyboard presses in the keymaps SculptForge looks at)
    int skipped = 0; // of those, the ones with no SculptForge equivalent
};

// False (with `error`) when the text isn't a Blender keymap export at all.
bool parseBlenderKeymap(std::string_view text, BlenderKeymap& out, std::string& error);

} // namespace sf
