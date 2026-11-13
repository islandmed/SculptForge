// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The classic shell's layout math (docs/ui-design.md §2): where the canvas,
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

TEST(shell_empty_shelves_hide_outside_customize) { // #139
    UiLayout l = standardLayout();
    l.rightShelf = {"sep"}; // separators alone count as empty
    l.leftShelf.clear();
    ShellGeometry g = computeShell(l, 1600, 960, 1.0f);
    CHECK(near(g.rightShelf.w, 0) && near(g.leftShelf.w, 0));
    CHECK(rectIs(g.canvas, 8, 110, 1284, 772)); // the canvas takes both shelves' room
    l.customize = true; // back while customizing, to drop items onto
    g = computeShell(l, 1600, 960, 1.0f);
    CHECK(rectIs(g.rightShelf, 1240, 110, 52, 772));
    CHECK(rectIs(g.canvas, 96, 110, 1144, 772));
    l.customize = false;
    l.topShelf = {"row"};
    l.bottomShelf.clear();
    g = computeShell(l, 1600, 960, 1.0f);
    CHECK(near(g.topShelf.h, 0) && near(g.bottomShelf.h, 0));
    CHECK(rectIs(g.canvas, 8, 46, 1284, 914));
    UiLayout back; // a saved layout with empty shelves loads with them empty
    std::string err;
    CHECK(fromJson(toJson(l), back, err) && back.leftShelf.empty() && back.bottomShelf.empty());
    CHECK(computeShell(l, 1600, 960, 1.0f, 0.0f, sculptForgeMetrics(8.0f)).canvas.w > 1200.0f); // tiles
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

TEST(shell_right_shelf_view_toggles) {
    // The view toggles (foirgairwolf on Discord) sit under Rotate (Turntable, Ortho, Floor) and Paint (Flat); a layout
    // stored before them (version < 5) gets them there, without doubling any it already has.
    for (int shell = 0; shell < 2; ++shell) {
        const UiLayout l = standardLayout(shell);
        const auto rotate = std::find(l.rightShelf.begin(), l.rightShelf.end(), "widget.nav.rotate");
        CHECK(rotate != l.rightShelf.end() && rotate + 3 < l.rightShelf.end() && *(rotate + 1) == "view.turntable" &&
              *(rotate + 2) == "view.ortho" && *(rotate + 3) == "view.floor_grid");
        const auto paint = std::find(l.rightShelf.begin(), l.rightShelf.end(), "view.vertexpaint");
        CHECK(paint != l.rightShelf.end() && paint + 1 != l.rightShelf.end() && *(paint + 1) == "view.flat_shading");
    }
    Json old = toJson(standardLayout());
    old.set("version", 4.0);
    old.set("rightShelf", shelfJson({"edit.undo", "widget.nav.rotate", "view.floor_grid", "sep", "view.vertexpaint"}));
    UiLayout back;
    std::string err;
    CHECK(fromJson(old, back, err));
    const std::vector<std::string> want = {"edit.undo", "widget.nav.rotate", "view.turntable", "view.ortho", "view.floor_grid",
                                           "sep", "view.vertexpaint", "view.flat_shading"};
    CHECK(back.rightShelf == want);
    // Without the neighbours (moved or taken out), nothing is added; a current layout stays as it is.
    old.set("rightShelf", shelfJson({"view.frame"}));
    CHECK(fromJson(old, back, err));
    CHECK(back.rightShelf.size() == 1);
    Json now = toJson(standardLayout());
    now.set("rightShelf", shelfJson({"widget.nav.rotate", "view.vertexpaint"}));
    CHECK(fromJson(now, back, err));
    CHECK(back.rightShelf.size() == 2);
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

// #107: a tiled look's gaps don't make a 1280 px window narrow; the right tray opens as it does in Classic.
TEST(shell_tiled_look_not_narrow_at_1280) {
    const UiLayout l = standardLayout(1);
    const ShellGeometry classic = computeShell(l, 1280, 720, 1.0f, 0.0f, sculptForgeMetrics(0.0f));
    const ShellGeometry tiled = computeShell(l, 1280, 720, 1.0f, 0.0f, sculptForgeMetrics(6.0f));
    CHECK(!classic.narrow && !tiled.narrow);
    CHECK(classic.rightTray.w > 0.0f && tiled.rightTray.w > 0.0f);
    CHECK(tiled.rightTray.x + tiled.rightTray.w <= 1280.0f);
    CHECK(computeShell(l, 1200, 720, 1.0f, 0.0f, sculptForgeMetrics(6.0f)).narrow); // below 1280: narrow, as before
}

// #105: windows go inside the display's usable area, at its scale.
TEST(window_fits_display) {
    // A Legion 5 Pro: 2560x1600, the taskbar 96 px at 200 %, a 62 px title bar.
    const WindowRect usable{0, 0, 2560, 1504};
    const WindowBorders frame{62, 0, 0, 0};
    // The 1600x960 stored by 0.10.10 (no scale): 3200x1920 at 200 %, which doesn't fit: maximised, inside.
    WindowFit f = fitWindow({100, 100, 1600, 960}, true, true, 0.0f, usable, frame, 2.0f);
    CHECK(f.maximize);
    CHECK(f.rect.x >= 0 && f.rect.y >= 62 && f.rect.x + f.rect.w <= 2560 && f.rect.y + f.rect.h <= 1504);
    // At 150 %: 2400x1440 just fits under the title bar, moved up 36 px to clear the taskbar.
    f = fitWindow({100, 100, 1600, 960}, true, true, 0.0f, usable, frame, 1.5f);
    CHECK(!f.maximize && f.rect.w == 2400 && f.rect.h == 1440 && f.rect.x == 100 && f.rect.y == 64);
    // Stored at 150 % and opened at 150 %: as it was.
    f = fitWindow({200, 150, 1800, 1100}, true, true, 1.5f, usable, frame, 1.5f);
    CHECK(!f.maximize && f.rect.x == 200 && f.rect.y == 150 && f.rect.w == 1800 && f.rect.h == 1100);
    // A first start at 125 %: 2000x1200, centred.
    f = fitWindow({0, 0, 1600, 960}, false, false, 0.0f, usable, frame, 1.25f);
    CHECK(!f.maximize && f.rect.w == 2000 && f.rect.h == 1200 && f.rect.x == 280 && f.rect.y == 62 + (1442 - 1200) / 2);
    // Half off the screen to the left and above (a monitor that went, a moved taskbar): pulled back in.
    f = fitWindow({-300, -40, 1600, 960}, true, true, 1.0f, usable, frame, 1.0f);
    CHECK(!f.maximize && f.rect.x == 0 && f.rect.y == 62);
    f = fitWindow({2000, 1000, 1600, 960}, true, true, 1.0f, usable, frame, 1.0f);
    CHECK(f.rect.x == 960 && f.rect.y == 1504 - 960);
    // A second monitor left of the main one.
    f = fitWindow({-1900, 50, 1600, 960}, true, true, 1.0f, {-1920, 0, 1920, 1040}, {30, 0, 0, 0}, 1.0f);
    CHECK(f.rect.x == -1900 && f.rect.y == 50);
}

// Panels in their own windows (docs/design/detach-panels.md): panels.json round trip, bad entries dropped, and a
// stored position used only while a display still shows the window's top.
TEST(panel_placements_json) {
    std::vector<PanelPlacement> in{{"palette.tool", -1700, 120, 340, 700, true}, {"guide", 0, 0, 900, 700, false}};
    std::vector<PanelPlacement> out = panelPlacementsFromJson(panelPlacementsToJson(in));
    CHECK(out.size() == 2);
    CHECK(out[0].id == "palette.tool" && out[0].placed && out[0].x == -1700 && out[0].y == 120 && out[0].w == 340 && out[0].h == 700);
    CHECK(out[1].id == "guide" && !out[1].placed && out[1].w == 900);
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"panels": [{"id": "controls", "w": 5, "h": 99999}, {"id": "controls"}, {"w": 300},
                                      {"id": ""}, {"id": "palette.brush", "x": "left", "y": 3}]})",
                      j, err));
    out = panelPlacementsFromJson(j);
    CHECK(out.size() == 2);
    CHECK(out[0].id == "controls" && out[0].w == 160 && out[0].h == 16384 && !out[0].placed);
    CHECK(out[1].id == "palette.brush" && !out[1].placed);
    CHECK(panelPlacementsFromJson(Json()).empty());
    // The main monitor and one to its left; the left one unplugged.
    const std::vector<DisplayRect> two{{0, 0, 2560, 1400}, {-1920, 0, 1920, 1040}}, one{{0, 0, 2560, 1400}};
    const PanelPlacement left{"palette.tool", -1700, 120, 340, 700, true};
    CHECK(panelOnDisplays(left, two));
    CHECK(!panelOnDisplays(left, one));
    CHECK(!panelOnDisplays({"guide", 0, 0, 900, 700, false}, two)); // the window system places it
}

// Revert to Saved Layout and Reset Layout read whatever ui.json holds (#bug-reports: Revert crashed on a layout with its
// shelf items dragged off): a corrupt or odd layout must load into something the shell can lay out, in every built-in
// layout, and the standard one is always a way back.
TEST(layout_from_corrupt_or_odd_json_still_lays_out) {
    const char* const files[] = {
        R"({})",
        R"({"layout": 5})",
        R"({"topShelf": [], "leftShelf": [], "rightShelf": [], "bottomShelf": []})",
        R"({"topShelf": [1, null, {"a": 1}, "no.such.item"], "leftShelf": "widget.color", "rightShelf": [[], "sep", "sep"]})",
        R"({"leftTrayWidth": -50, "rightTrayWidth": 1e9, "consoleHeight": 0, "libraryTile": 99999, "libraryRows": -3})",
        R"({"leftTray": ["palette.nope", 7], "rightTray": [], "openSubpalettes": [null, "tool.part"]})",
    };
    const float sizes[][2] = {{800, 600}, {1440, 900}, {2880, 1800}, {3840, 2160}};
    for (int shell = 0; shell <= 1; ++shell) {
        for (const char* text : files) {
            Json j;
            std::string err;
            if (!Json::parse(text, j, err)) continue; // (a file that is not JSON never gets this far)
            UiLayout layout = standardLayout(shell);
            fromJson(j, layout, err, shell);
            for (const auto& s : sizes) {
                const ShellGeometry g = computeShell(layout, s[0], s[1], 1.0f);
                CHECK(g.canvas.w > 0 && g.canvas.h > 0);
            }
            CHECK(layout.libraryTile >= kLibraryTileMin && layout.libraryTile <= kLibraryTileMax);
            UiLayout again = standardLayout(shell);
            CHECK(fromJson(toJson(layout), again, err, shell)); // what Save Layout writes reads back
        }
        // The Classic layout keeps the colour picker on a shelf (Reset Layout brings it back).
        const UiLayout std_ = standardLayout(shell);
        auto holds = [](const UiLayout& l, const char* id) {
            for (const auto* shelf : {&l.topShelf, &l.leftShelf, &l.rightShelf, &l.bottomShelf})
                if (std::find(shelf->begin(), shelf->end(), id) != shelf->end()) return true;
            return false;
        };
        if (shell == 0) CHECK(holds(std_, "widget.color"));
    }
}

// My Panel (PanelMyPanel.cpp): what was dragged in is saved with the layout; bad entries, repeats and extras are dropped.
TEST(my_panel_layout_json) {
    CHECK(!toJson(standardLayout()).has("myPanel")); // nothing stored for an empty panel
    UiLayout l = standardLayout();
    l.myPanel = {"action:view.wire", "section:tool.masking"};
    UiLayout back;
    std::string err;
    CHECK(fromJson(toJson(l), back, err));
    CHECK(back.myPanel == l.myPanel);
    Json j;
    CHECK(Json::parse(R"({"myPanel": ["action:a.b", "action:a.b", "bogus", "section:", "action:", "section:tool.part", 7]})", j, err));
    CHECK(fromJson(j, back, err));
    CHECK(back.myPanel.size() == 2 && back.myPanel[0] == "action:a.b" && back.myPanel[1] == "section:tool.part");
    Json many = Json::array();
    for (int i = 0; i < 100; ++i) many.push("action:x." + std::to_string(i));
    Json big = Json::object();
    big.set("myPanel", many);
    CHECK(fromJson(big, back, err));
    CHECK(back.myPanel.size() == kMyPanelMax);
    CHECK(myPanelEntry("section:tool.geometry").section && myPanelEntry("section:tool.geometry").id == "tool.geometry");
    CHECK(!myPanelEntry("tool.geometry").valid());
}

// ui.json keeps the layout in use ("layout", autosaved) and the one Save Layout kept ("layoutSaved", what Revert returns to).
TEST(ui_json_layout_and_saved_layout) {
    auto parse = [](const char* text) {
        Json j;
        std::string err;
        Json::parse(text, j, err);
        return j;
    };
    // A file from before the autosave: the one layout is both.
    const UiLayoutPair old = layoutsFromUiJson(parse(R"({"version": 1, "layout": {"leftShelf": ["view.wire"]}, "prefs": {}})"));
    CHECK(old.current.isObject() && old.saved.isObject());
    CHECK(old.current.dump() == old.saved.dump());
    // The new format: two layouts.
    const UiLayoutPair two = layoutsFromUiJson(parse(R"({"layout": {"leftShelf": ["a"]}, "layoutSaved": {"leftShelf": ["b"]}})"));
    CHECK(two.current.isObject() && two.saved.isObject());
    CHECK(two.current.dump() != two.saved.dump());
    // Autosaved before any Save Layout ("layoutSaved": null), or a file with no layout at all.
    const UiLayoutPair none = layoutsFromUiJson(parse(R"({"layout": {"leftShelf": ["a"]}, "layoutSaved": null})"));
    CHECK(none.current.isObject() && none.saved.isNull());
    const UiLayoutPair empty = layoutsFromUiJson(parse(R"({"prefs": {}})"));
    CHECK(empty.current.isNull() && empty.saved.isNull());
    // Corrupt: never a layout, never a crash.
    for (const char* text : {R"({"layout": 5, "layoutSaved": [1]})", R"([])", R"(null)", R"({"layout": "x", "layoutSaved": "y"})"}) {
        const UiLayoutPair bad = layoutsFromUiJson(parse(text));
        CHECK(bad.current.isNull());
        CHECK(bad.saved.isNull());
    }

    // The round trip through the writer: both layouts come back, the runtime-only bits of the one in use are shut.
    for (int shell = 0; shell <= 1; ++shell) {
        UiLayout inUse = standardLayout(shell), kept = standardLayout(shell);
        inUse.leftShelf.push_back("view.wire");
        inUse.consoleOpen = true;
        inUse.libraryOpen = true;
        inUse.customize = true;
        inUse.rightTrayOpen = false;
        Json ui = Json::object();
        layoutsToUiJson(ui, toAutosaveJson(inUse), toJson(kept));
        Json again;
        std::string err;
        CHECK(Json::parse(ui.dump(true), again, err));
        const UiLayoutPair back = layoutsFromUiJson(again);
        UiLayout cur, sav;
        CHECK(fromJson(back.current, cur, err, shell));
        CHECK(fromJson(back.saved, sav, err, shell));
        CHECK(!cur.consoleOpen && !cur.libraryOpen && !cur.customize);
        CHECK(!cur.rightTrayOpen);
        CHECK(cur.leftShelf == inUse.leftShelf);
        CHECK(sav.leftShelf == kept.leftShelf && sav.rightTrayOpen == kept.rightTrayOpen);
        // Never saved: "layoutSaved" is written as null, not left out (left out would read as the old format).
        Json fresh = Json::object();
        layoutsToUiJson(fresh, toAutosaveJson(inUse), Json());
        CHECK(fresh.has("layoutSaved"));
        CHECK(layoutsFromUiJson(fresh).saved.isNull());
    }
}

// A shelf slot's brush id (custom brush slots find their brush by it) survives the layout file; hand-edited junk is dropped.
TEST(shell_layout_keeps_brush_slot_ids) {
    UiLayout l = standardLayout();
    l.bottomShelf.push_back("brush.recipe.Finish - Classic");
    l.brushIds["Finish - Classic"] = "00ff00ff00ff00ff";
    UiLayout back;
    std::string err;
    CHECK(fromJson(toJson(l), back, err));
    CHECK(back.brushIds.size() == 1 && back.brushIds["Finish - Classic"] == "00ff00ff00ff00ff");
    CHECK(std::find(back.bottomShelf.begin(), back.bottomShelf.end(), "brush.recipe.Finish - Classic") != back.bottomShelf.end());
    Json j = toJson(l);
    Json ids = Json::object();
    ids.set("A", 5.0);
    ids.set("B", std::string());
    ids.set("C", std::string(200, 'x'));
    j.set("brushIds", ids);
    CHECK(fromJson(j, back, err) && back.brushIds.empty());
    CHECK(toJson(standardLayout()).has("brushIds") == false);
}
