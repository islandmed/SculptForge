// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The layout of the ZBrush-style shell (shelves, trays, LightBox, console): the
// shared contract between the shell, LightBox and config code, and what Store
// Config writes to ui.json.
#pragma once

#include "core/Json.h"

#include <string>
#include <vector>

namespace sf {

// The part of the window the 3D view uses (logical pixels, top-left origin).
struct CanvasRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct UiLayout {
    int version = 2; // 2: the top shelf has Dynamic (older stored layouts get it on load)
    bool shelvesVisible = true, paletteBarVisible = true, leftHanded = false;
    bool leftTrayOpen = false, rightTrayOpen = true;
    float leftTrayWidth = 300, rightTrayWidth = 300; // logical px
    std::vector<std::string> leftTray, rightTray;     // palette ids, top to bottom
    std::vector<std::string> openSubpalettes;         // "tool.subtool", ...
    // Item ids: action ids, "widget.*", "sep", "row" (top shelf line break).
    std::vector<std::string> topShelf, leftShelf, rightShelf, bottomShelf;
    bool consoleOpen = false;
    float consoleHeight = 160;
    bool lightboxOpen = false;
    int lightboxRows = 1;
    std::string lightboxTab = "project";
    bool lightboxBottom = false;
    int shelfTiles = 1; // bottom shelf brush tiles: 0 small, 1 medium, 2 large (shell::bottomShelfSize)
    bool customize = false; // runtime only, never stored
};

UiLayout standardLayout(); // the built-in default
Json toJson(const UiLayout& layout);
// Unknown ids are dropped, not fatal; false only for unreadable JSON.
bool fromJson(const Json& j, UiLayout& layout, std::string& error);

} // namespace sf
