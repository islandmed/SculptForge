// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ZBrush-style shell's layout math (docs/ui-design.md §2): where the canvas,
// shelves and trays go for the standard layout, hidden shelves, narrow and
// wide windows, and the left-handed mirror; plus the layout JSON round trip.
#include "TestHarness.h"

#include "app/ShellGeometry.h"

#include <cmath>

using namespace sf;

namespace {

bool near(float a, float b) { return std::abs(a - b) < 0.01f; }

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
