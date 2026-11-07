// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Library (comma key): a thumbnail strip over the canvas for projects, models,
// brushes, alphas, materials, images, splats, QuickSaves and recent files.
//
// As in category tabs along the top, back / forward through the tabs
// and folders visited, a "Built-in" and a "User" folder at each tab's root,
// double-click to enter a folder or load an item, drag to scrub the strip
// sideways, 1-4 rows of thumbnails, top or bottom of the canvas.
#pragma once

#include "app/AssetLibrary.h"
#include "app/UiLayout.h"

#include <imgui.h>

#include <mutex>
#include <string>
#include <vector>

namespace sf {

class App;

class LibraryBrowser {
public:
    void draw(App& app, const CanvasRect& canvas);
    bool open() const { return open_; }
    void setOpen(bool open) { open_ = open; }
    void toggle() { open_ = !open_; }
    // Shows `tab` (its root) and opens the Library.
    void show(LibraryTab tab);
    // Shows `folder` (absolute; "" = the root) in `tab` and opens the Library, e.g. the starter meshes.
    void showFolder(LibraryTab tab, const std::string& folder);
    // Preferences > Library (the shell's Preferences palette calls this).
    void drawPreferences(App& app);
    // Height the strip takes over the canvas with the layout's rows and tile size (logical px x scale).
    static float height(const UiLayout& layout, float scale);

    // A file dropped at (x, y) (window coordinates): an image dropped on the Material tab loads as a Lit Sphere (#124).
    // False when the Library doesn't take it (the canvas does what it always does).
    bool takeDrop(App& app, float x, float y, const std::string& path);
    ImVec2 shownCentre() const { return ImVec2((shownMin_.x + shownMax_.x) * 0.5f, (shownMin_.y + shownMax_.y) * 0.5f); }

    // Thread-safe: the folder dialog's callback ("Open folder..." adds a library root).
    void folderChosen(const std::string& path);

    bool showAtStartup = true; // Preferences > Library: opens on the Project tab (interactive sessions only)

private:
    struct Place {
        LibraryTab tab = LibraryTab::Project;
        std::string folder; // "" = the tab's root, else an absolute folder
        bool operator==(const Place& o) const { return tab == o.tab && folder == o.folder; }
    };
    void navigate(App& app, Place to);
    std::vector<AssetEntry> entries(App& app);
    void rootEntries(App& app, std::vector<AssetEntry>& out);
    void activate(App& app, const AssetEntry& e, bool append);
    void contextMenu(App& app, const AssetEntry& e);
    ImTextureID thumbnail(App& app, const AssetEntry& e);
    void drawHeader(App& app, float width);
    void drawBreadcrumb(App& app);
    std::string folderLabel(const App& app, const std::string& folder) const;

    bool open_ = false;
    ImVec2 shownMin_{0, 0}, shownMax_{0, 0}; // the strip's window as last drawn (empty: not shown)
    Place place_;
    std::vector<Place> back_, forward_;
    char filter_[64] = "";
    std::string selected_; // path or name of the selected tile
    bool syncedLayout_ = false;
    // Scrubbing (left-drag sideways) and dragging a tile out onto the canvas.
    bool pressing_ = false, scrubbed_ = false;
    float pressX_ = 0.0f, pressY_ = 0.0f, pressScroll_ = 0.0f;
    int pressTile_ = -1;
    bool tileDrag_ = false;
    AssetEntry dragEntry_;
    float scrollTo_ = -1.0f; // reset scroll after navigating
    // Dragging the strip's canvas edge: the tile size it started from and where.
    bool resizing_ = false;
    float resizeY_ = 0.0f, resizeTile_ = 0.0f;
    void drawResizeHandle(App& app, ImVec2 pos, ImVec2 size);
    std::string imageMenu_;  // image tile double-clicked: its action menu
    // Folders added with "Open folder..." (shown at every tab's root).
    std::vector<std::string> extraRoots_;
    std::mutex folderMutex_;
    std::string chosenFolder_; // from the folder dialog thread
};

} // namespace sf
