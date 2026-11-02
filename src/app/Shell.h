// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pieces of the ZBrush-style shell shared between Shell.cpp, Palettes.cpp and
// the ui:: widgets: the palette table, shelf ids and the button looks.
#pragma once

#include "app/Actions.h"

#include <imgui.h>

#include <string>
#include <string_view>

namespace sf {

class App;

enum ShelfId { kShelfTop = 0, kShelfLeft, kShelfRight, kShelfBottom, kShelfCount };

struct PaletteDef {
    const char* id;    // "tool", also the UiLayout tray entry
    const char* name;  // "Tool", shown in the palette bar (sorted by it)
    const char* icon;  // ICON_LC_*
    const char* scope; // extra name UI tests find its items under ("SubTools/Append"), or nullptr
    void (App::*draw)();
};
const PaletteDef* palettes(int& count); // alphabetical
const PaletteDef* findPalette(std::string_view id);

// The SculptForge layout's menus, grouped by task (File, Edit, Sculpt, Paint, Mesh, View, Render, Help). Each holds
// whole palettes as its sections, so every palette (and so every item) has exactly one menu.
struct TaskMenuDef {
    const char* id;   // "sculpt": the bar button is "Sculpt##menu.sculpt"
    const char* name;
    const char* sections[6]; // palette ids, nullptr-terminated
    const char* titles[6];   // a section's name in this menu, or nullptr for the palette's own
};
const TaskMenuDef* taskMenus(int& count);
int taskMenuOf(std::string_view paletteId); // index into taskMenus, -1 when none

// ui::ActionButton's Tile look: a brush thumbnail with its name (brush actions), or a large icon over a
// small caption (right shelf). Returns true when clicked.
bool drawShelfTile(App& app, const Action& action, ImVec2 size);

// Brush type of a "brush.<name>" action id, or -1.
int brushTypeOfAction(std::string_view id);

} // namespace sf
