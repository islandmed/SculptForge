// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ZBrush-style shell's layout math (docs/ui-design.md §2): where the canvas,
// shelves and trays go for the standard layout, hidden shelves, narrow and
// wide windows, and the left-handed mirror; plus the layout JSON round trip.
#include "TestHarness.h"

#include "app/ShellGeometry.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace sf;

namespace {

bool near(float a, float b) { return std::abs(a - b) < 0.01f; }

Json shelfJson(const std::vector<std::string>& ids) {
    Json a = Json::array();
    for (const auto& id : ids) a.push(id);
    return a;
}

bool rectIs(const CanvasRect& r, float x, float y, float w, float h) {
    return near(r.x, x) && near(r.y, y) && near(r.w, w) && near(r.h, h);
}

} // namespace

TEST(shell_standard_layout_1600x960) {
    const ShellGeometry g = computeShell(standardLayout(), 1600, 960, 1.0f);
    CHECK(!g.narrow);
    CHECK(rectIs(g.canvas, 96, 110, 1144, 772));             // §2.2 (medium shelf tiles)
    CHECK(rectIs(g.rightTray, 1300, 46, 300, 914));
    CHECK(rectIs(g.rightDivider, 1292, 46, 8, 914));
    CHECK(rectIs(g.rightShelf, 1240, 110, 52, 772));
    CHECK(rectIs(g.leftShelf, 8, 110, 88, 772));
    CHECK(rectIs(g.topShelf, 8, 46, 1284, 64));
    CHECK(rectIs(g.bottomShelf, 8, 882, 1284, 78));
    CHECK(near(g.leftTray.w, 0));
}

TEST(shell_bottom_shelf_tile_sizes) {
    UiLayout l = standardLayout();
    l.shelfTiles = 0; // small: the canvas gets the height back
    CHECK(rectIs(computeShell(l, 1600, 960, 1.0f).bottomShelf, 8, 900, 1284, 60));
    CHECK(near(computeShell(l, 1600, 960, 1.0f).canvas.h, 790));
    l.shelfTiles = 2;
    CHECK(near(computeShell(l, 1600, 960, 1.0f).bottomShelf.h, 96));
    UiLayout back;
    std::string err;
    CHECK(fromJson(toJson(l), back, err) && back.shelfTiles == 2);
}

TEST(shell_standard_layout_1920x1080) {
    const ShellGeometry g = computeShell(standardLayout(), 1920, 1080, 1.0f);
    CHECK(rectIs(g.canvas, 96, 110, 1444, 892)); // §2.3: the extra width goes to the canvas
    CHECK(near(g.rightTray.w, 320));
    CHECK(near(g.rightDivider.x, 1592));
}

TEST(shell_scales_with_the_ui) {
    const ShellGeometry g = computeShell(standardLayout(), 3200, 1920, 2.0f);
    CHECK(rectIs(g.canvas, 192, 220, 2288, 1544));
}

TEST(shell_hidden_shelves_console_and_trays) {
    UiLayout l = standardLayout();
    l.shelvesVisible = false; // Tab
    ShellGeometry g = computeShell(l, 1600, 960, 1.0f);
    CHECK(rectIs(g.canvas, 0, 46, 1600, 914));

    l = standardLayout();
    l.consoleOpen = true;
    l.consoleHeight = 160;
    g = computeShell(l, 1600, 960, 1.0f);
    CHECK(near(g.canvas.y + g.canvas.h, 722)); // §2.2: the console pushes the canvas bottom up
    CHECK(rectIs(g.console, 8, 722, 1284, 160));
    l.consoleHeight = 9000;
    CHECK(near(computeShell(l, 1600, 960, 1.0f).console.h, 400)); // clamped

    l = standardLayout();
    l.leftTrayOpen = true;
    l.rightTrayOpen = false;
    l.paletteBarVisible = false;
    g = computeShell(l, 1600, 960, 1.0f);
    CHECK(rectIs(g.leftTray, 0, 24, 300, 936));
    CHECK(near(g.canvas.x, 300 + 8 + 88));
    CHECK(near(g.canvas.x + g.canvas.w, 1600 - 8 - 52));
    CHECK(near(g.canvas.y, 24 + 64));
}

TEST(shell_narrow_and_left_handed) {
    ShellGeometry g = computeShell(standardLayout(), 1200, 800, 1.0f);
    CHECK(g.narrow);
    CHECK(near(g.rightTray.w, 0)); // collapses to its divider
    CHECK(near(g.leftShelf.w, 72));

    UiLayout l = standardLayout();
    l.leftHanded = true;
    g = computeShell(l, 1600, 960, 1.0f);
    CHECK(rectIs(g.rightTray, 0, 46, 300, 914)); // the tray swaps sides
    CHECK(near(g.rightShelf.x, 308));
    CHECK(rectIs(g.canvas, 360, 110, 1144, 772));
}

TEST(shell_tray_width_and_layout_json) {
    CHECK(near(trayWidth(300, 1600), 300));
    CHECK(near(trayWidth(300, 1920), 320));
    CHECK(near(trayWidth(100, 1600), 240));
    CHECK(near(trayWidth(900, 1600), 480));

    UiLayout l = standardLayout();
    CHECK(l.bottomShelf.size() > 10);
    CHECK(l.rightTray.size() == 1 && l.rightTray[0] == "tool");
    l.leftTray = {"brush", "stroke"};
    l.leftTrayOpen = true;
    l.bottomShelf.push_back("brush.recipe.Dragon Scales");
    UiLayout back;
    std::string err;
    CHECK(fromJson(toJson(l), back, err));
    CHECK(back.leftTray == l.leftTray);
    CHECK(back.leftTrayOpen);
    CHECK(back.bottomShelf == l.bottomShelf);
    CHECK(back.topShelf == l.topShelf);
}

TEST(shell_camera_notch_inset) {
    // A 16-inch MacBook Pro filled edge to edge: 1728x1117 logical, a 37 px band beside the notch.
    const UiLayout l = standardLayout();
    const ShellGeometry plain = computeShell(l, 1728, 1117, 1.0f);
    const ShellGeometry g = computeShell(l, 1728, 1117, 1.0f, 37.0f);
    CHECK(plain.notchBand.h == 0.0f && plain.title.y == 0.0f);
    CHECK(g.notchBand.h == 37.0f && g.title.y == 37.0f);
    CHECK(g.paletteBar.y == 37.0f + g.title.h);
    CHECK(g.canvas.y == plain.canvas.y + 37.0f && g.canvas.h == plain.canvas.h - 37.0f);
    CHECK(g.rightTray.y == plain.rightTray.y + 37.0f);
    CHECK(computeShell(l, 1728, 1117, 1.0f, 5000.0f).notchBand.h <= 1117.0f * 0.25f); // never eats the window
}

TEST(shell_right_shelf_undo_redo) {
    // #43: Undo and Redo top the right shelf; a layout stored before them (version < 3) gets them on load, once.
    const UiLayout l = standardLayout();
    CHECK(l.rightShelf.size() > 3 && l.rightShelf[0] == "edit.undo" && l.rightShelf[1] == "edit.redo");
    Json old = toJson(l);
    old.set("version", 2.0);
    old.set("rightShelf", shelfJson({"view.frame", "view.mask"}));
    UiLayout back;
    std::string err;
    CHECK(fromJson(old, back, err));
    CHECK(back.rightShelf.size() == 5 && back.rightShelf[0] == "edit.undo" && back.rightShelf[1] == "edit.redo" &&
          back.rightShelf[3] == "view.frame");
    // A current layout the user took them out of stays without them.
    Json now = toJson(l);
    now.set("rightShelf", shelfJson({"view.frame"}));
    CHECK(fromJson(now, back, err));
    CHECK(back.rightShelf.size() == 1);
}

TEST(shell_left_shelf_stroke_popup) {
    // The stroke popup's button sits under the Stroke tile; a layout stored before it (version < 4) gets it there.
    const UiLayout l = standardLayout();
    const auto tile = std::find(l.leftShelf.begin(), l.leftShelf.end(), "widget.tile.stroke");
    CHECK(tile != l.leftShelf.end() && tile + 1 != l.leftShelf.end() && *(tile + 1) == "stroke.popup");
    Json old = toJson(l);
    old.set("version", 3.0);
    old.set("leftShelf", shelfJson({"widget.tile.brush", "widget.tile.stroke", "widget.color"}));
    UiLayout back;
    std::string err;
    CHECK(fromJson(old, back, err));
    CHECK(back.leftShelf.size() == 4 && back.leftShelf[2] == "stroke.popup" && back.leftShelf[3] == "widget.color");
    // A current layout the user took it out of stays without it.
    Json now = toJson(l);
    now.set("leftShelf", shelfJson({"widget.tile.stroke"}));
    CHECK(fromJson(now, back, err));
    CHECK(back.leftShelf.size() == 1);
}

// #85 (regression of #41): on macOS a window saved full screen comes back maximised, never straight into full screen
// (its traffic lights couldn't be reached); elsewhere it comes back full screen.
TEST(window_restore_fullscreen) {
    CHECK(!restoresFullscreen(true, true));
    CHECK(!restoresFullscreen(false, true));
    CHECK(restoresFullscreen(true, false));
    CHECK(!restoresFullscreen(false, false));
#if defined(__APPLE__)
    CHECK(!restoresFullscreen(true));
#endif
}
