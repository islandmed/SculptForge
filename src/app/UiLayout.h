// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The layout of the classic shell (shelves, trays, Library, console): the
// shared contract between the shell, Library and config code, and what Save
// Layout writes to ui.json.
#pragma once

#include "core/Json.h"

#include <map>
#include <string>
#include <vector>

namespace sf {

// The part of the window the 3D view uses (logical pixels, top-left origin).
struct CanvasRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

constexpr float kLibraryTileMin = 48.0f, kLibraryTileMax = 128.0f;

struct UiLayout {
    int version = 5; // 2: the top shelf has Dynamic; 3: the right shelf has Undo / Redo; 4: the left shelf has the stroke
                     // popup; 5: the right shelf has Turntable, Ortho, Floor and Flat (older stored layouts get them on load)
    bool shelvesVisible = true, paletteBarVisible = true, leftHanded = false;
    bool leftTrayOpen = false, rightTrayOpen = true;
    bool rightTrayPeek = false; // not saved: on a narrow window the right tray shows only after it was asked for (a tap)
    float leftTrayWidth = 300, rightTrayWidth = 300; // logical px
    std::vector<std::string> leftTray, rightTray;     // palette ids, top to bottom
    std::vector<std::string> openSubpalettes;         // "tool.part", ...
    // My Panel (Palettes.cpp): what the member dragged in, top to bottom: "action:<action id>" or "section:<section id>".
    std::vector<std::string> myPanel;
    // Item ids: action ids, "widget.*", "sep", "row" (top shelf line break).
    std::vector<std::string> topShelf, leftShelf, rightShelf, bottomShelf;
    bool consoleOpen = false;
    float consoleHeight = 160;
    bool libraryOpen = false;
    int libraryRows = 1;
    // Library thumbnail size, logical px (kLibraryTileMin-Max): drag the strip's edge toward the canvas to change it.
    // 64 since the strip went compact (was 96, @factoberplay: the strip took too much height).
    float libraryTile = 64;
    std::string libraryTab = "project";
    bool libraryBottom = false;
    // Custom brush shelf slots ("brush.recipe.<name>") -> the brush's stable id (BrushRecipe::id): the slot finds its brush
    // by it after a rename or a move, and the name is only a hint. Learned when a slot's brush is first seen.
    std::map<std::string, std::string> brushIds;
    int shelfTiles = 1; // bottom shelf brush tiles: 0 small, 1 medium, 2 large (shell::bottomShelfSize)
    bool customize = false; // runtime only, never stored
};

// One My Panel entry: a button (an action) or a whole section of a palette. Empty id = not an entry.
constexpr size_t kMyPanelMax = 64;
struct MyPanelEntry {
    bool section = false;
    std::string id;
    bool valid() const { return !id.empty(); }
};
MyPanelEntry myPanelEntry(const std::string& entry);

// A shelf with nothing on it but separators and line breaks: hidden outside Customize Shelves, so the canvas takes
// its space, and back while customizing so items can be dropped onto it again (#139).
bool shelfEmpty(const std::vector<std::string>& items);

UiLayout standardLayout(); // the built-in default
UiLayout standardLayout(int shellLayout); // 0 the Classic layout (the same), 1 the SculptForge layout
Json toJson(const UiLayout& layout);
// Unknown ids are dropped, not fatal; false only for unreadable JSON. What the file leaves out (a shelf it never
// stored) comes from standardLayout(shellLayout): the SculptForge layout's shelves for a SculptForge-layout file.
bool fromJson(const Json& j, UiLayout& layout, std::string& error, int shellLayout = 0);

// ui.json keeps two layouts: "layout", the one in use (written by itself a moment after every layout edit and at
// quit), and "layoutSaved", the one the last Save Layout kept (what Revert to Saved Layout returns to). A file
// from before the autosave has only "layout": it is both. "layoutSaved": null is a file where Save Layout never ran.
struct UiLayoutPair {
    Json current; // null: the file has no layout
    Json saved;   // null: no Save Layout yet
};
UiLayoutPair layoutsFromUiJson(const Json& ui);
void layoutsToUiJson(Json& ui, const Json& current, const Json& saved);
// The layout as autosaved: what a session opens by itself (console, Library, Customize Shelves) stays shut.
Json toAutosaveJson(UiLayout layout);

// A panel in a window of its own (docs/design/detach-panels.md): "palette.<id>", "guide" or "controls", and where
// its window was (desktop coordinates, logical px; placed false: the window system chose, e.g. Wayland). Kept in
// <user data>/panels.json, never in ui.json: one desk's monitor coordinates don't travel with a shared interface.
struct PanelPlacement {
    std::string id;
    int x = 0, y = 0, w = 320, h = 480;
    bool placed = false;
};
Json panelPlacementsToJson(const std::vector<PanelPlacement>& panels);
// Ids too long or repeated are dropped, at most 32 panels, sizes clamped to 160..16384.
std::vector<PanelPlacement> panelPlacementsFromJson(const Json& j);
// Whether a display (usable bounds, desktop coordinates) still shows the top of a window placed at x, y: the same
// rule as the main window's (#105), so a panel on a monitor that has gone opens beside the main window instead.
struct DisplayRect {
    int x = 0, y = 0, w = 0, h = 0;
};
bool panelOnDisplays(const PanelPlacement& p, const std::vector<DisplayRect>& displays);

} // namespace sf
