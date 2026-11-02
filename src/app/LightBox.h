// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// LightBox (comma key): a thumbnail strip over the canvas for projects, models,
// brushes, alphas, materials, images, splats, QuickSaves and recent files.
//
// As in ZBrush: category tabs along the top, back / forward through the tabs
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

class LightBox {
public:
    void draw(App& app, const CanvasRect& canvas);
    bool open() const { return open_; }
    void setOpen(bool open) { open_ = open; }
    void toggle() { open_ = !open_; }
    // Shows `tab` (its root) and opens the LightBox.
    void show(LibraryTab tab);
    // Shows `folder` (absolute; "" = the root) in `tab` and opens the LightBox, e.g. the starter meshes.
    void showFolder(LibraryTab tab, const std::string& folder);
    // Preferences > LightBox (the shell's Preferences palette calls this).
    void drawPreferences(App& app);
    // Height the strip takes over the canvas at `rows` rows (logical px x scale).
    static float height(int rows, float scale);

    // Thread-safe: the folder dialog's callback ("Open folder..." adds a library root).
    void folderChosen(const std::string& path);

    bool showAtStartup = true; // Preferences > LightBox: opens on the Project tab (interactive sessions only)

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
    std::string imageMenu_;  // image tile double-clicked: its action menu
    // Folders added with "Open folder..." (shown at every tab's root).
    std::vector<std::string> extraRoots_;
    std::mutex folderMutex_;
    std::string chosenFolder_; // from the folder dialog thread
};

} // namespace sf
