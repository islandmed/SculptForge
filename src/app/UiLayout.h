// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The layout of the classic shell (shelves, trays, Library, console): the
// shared contract between the shell, Library and config code, and what Save
// Layout writes to ui.json.
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
    int version = 5; // 2: the top shelf has Dynamic; 3: the right shelf has Undo / Redo; 4: the left shelf has the stroke
                     // popup; 5: the bottom shelf has the Insert brush (older stored layouts get them on load)
    bool shelvesVisible = true, paletteBarVisible = true, leftHanded = false;
    bool leftTrayOpen = false, rightTrayOpen = true;
    float leftTrayWidth = 300, rightTrayWidth = 300; // logical px
    std::vector<std::string> leftTray, rightTray;     // palette ids, top to bottom
    std::vector<std::string> openSubpalettes;         // "tool.part", ...
    // Item ids: action ids, "widget.*", "sep", "row" (top shelf line break).
    std::vector<std::string> topShelf, leftShelf, rightShelf, bottomShelf;
    bool consoleOpen = false;
    float consoleHeight = 160;
    bool libraryOpen = false;
    int libraryRows = 1;
    std::string libraryTab = "project";
    bool libraryBottom = false;
    int shelfTiles = 1; // bottom shelf brush tiles: 0 small, 1 medium, 2 large (shell::bottomShelfSize)
    bool customize = false; // runtime only, never stored
};

// A shelf with nothing on it but separators and line breaks: hidden outside Customize Shelves, so the canvas takes
// its space, and back while customizing so items can be dropped onto it again (#139).
bool shelfEmpty(const std::vector<std::string>& items);

UiLayout standardLayout(); // the built-in default
UiLayout standardLayout(int shellLayout); // 0 the Classic layout (the same), 1 the SculptForge layout
Json toJson(const UiLayout& layout);
// Unknown ids are dropped, not fatal; false only for unreadable JSON. What the file leaves out (a shelf it never
// stored) comes from standardLayout(shellLayout): the SculptForge layout's shelves for a SculptForge-layout file.
bool fromJson(const Json& j, UiLayout& layout, std::string& error, int shellLayout = 0);

} // namespace sf
