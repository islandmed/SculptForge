// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface looks (app/LookSchemes.h): the scheme table, reading an Omarchy theme's colours, and the SculptForge
// layout's tiled geometry (app/ShellGeometry.h: gaps between every region, dividers in the gaps beside the trays).
#include "TestHarness.h"

#include "app/LookSchemes.h"
#include "app/ShellGeometry.h"

#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace sf;

namespace {

bool near(float a, float b) { return std::abs(a - b) < 0.01f; }

bool overlaps(const CanvasRect& a, const CanvasRect& b) {
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.x + b.w - 0.01f && b.x < a.x + a.w - 0.01f && a.y < b.y + b.h - 0.01f &&
           b.y < a.y + a.h - 0.01f;
}

} // namespace

TEST(looks_keys_round_trip) {
    for (int i = 0; i < int(looks::LookId::Count); ++i) CHECK(looks::lookFromKey(looks::lookKey(looks::LookId(i))) == looks::LookId(i));
    CHECK(looks::lookFromKey("nonsense") == looks::LookId::Classic);
    CHECK(std::string(looks::lookName(looks::LookId::ForgeTerminal)) == "Forge Terminal");
}

TEST(looks_schemes_complete_and_readable) {
    int count = 0;
    const looks::SchemeInfo* list = looks::schemes(count);
    CHECK(count == 8);
    CHECK(std::string(list[0].key) == "neutral"); // the default comes first (0.10.9)
    std::set<std::string> keys;
    auto lum = [](unsigned c) { return 0.2126f * float((c >> 16) & 0xff) + 0.7152f * float((c >> 8) & 0xff) + 0.0722f * float(c & 0xff); };
    for (int i = 0; i < count; ++i) {
        keys.insert(list[i].key);
        CHECK(looks::findScheme(list[i].key) == &list[i]);
        CHECK(list[i].license && *list[i].license);
        const looks::Palette& p = list[i].palette;
        // Darkest to brightest, and text well above the panels.
        CHECK(lum(p.crust) <= lum(p.mantle) + 0.5f);
        CHECK(lum(p.mantle) <= lum(p.base) + 0.5f);
        CHECK(lum(p.base) < lum(p.surface0));
        CHECK(lum(p.text) - lum(p.base) > 120.0f);
        CHECK(lum(p.accent) - lum(p.base) > 70.0f);
    }
    CHECK(keys.size() == size_t(count));
    CHECK(looks::findScheme("solarized") == nullptr);
}

TEST(looks_parse_hex) {
    unsigned v = 0;
    CHECK(looks::parseHex("#7aa2f7", v) && v == 0x7aa2f7);
    CHECK(looks::parseHex("1A1B26", v) && v == 0x1a1b26);
    CHECK(looks::parseHex("#1a1b26ff", v) && v == 0x1a1b26);
    CHECK(!looks::parseHex("#12345", v));
    CHECK(!looks::parseHex("#zzzzzz", v));
}

TEST(looks_omarchy_colors_toml) {
    // An Omarchy theme's colors.toml (Tokyo Night), as Omarchy writes it.
    const std::string colors = "mode = \"dark\"\n\naccent = \"#7aa2f7\"\nselection = \"#292e42\"\nmuted = \"#414868\"\n\n"
                               "background = \"#1a1b26\"\ndark_background = \"#13141c\"\ndarker_background = \"#0e0e14\"\n"
                               "lighter_background = \"#24283b\"\n\nforeground = \"#a9b1d6\"\ndark_foreground = \"#565f89\"\n"
                               "bright_foreground = \"#c0caf5\"\n\nred = \"#f7768e\"\ngreen = \"#9ece6a\"\nyellow = \"#e0af68\"\n"
                               "magenta = \"#ad8ee6\"\n";
    looks::Palette p;
    CHECK(looks::paletteFromOmarchy(colors, "", p));
    CHECK(p.base == 0x1a1b26);
    CHECK(p.mantle == 0x13141c);
    CHECK(p.crust == 0x0e0e14);
    CHECK(p.surface0 == 0x24283b);
    CHECK(p.surface1 == 0x292e42);
    CHECK(p.accent == 0x7aa2f7);
    CHECK(p.text == 0xa9b1d6);
    CHECK(p.bright == 0xc0caf5);
    CHECK(p.red == 0xf7768e);
    CHECK(!p.light);
}

TEST(looks_omarchy_alacritty_fallback) {
    // An older theme with only alacritty.toml: the rest is derived from its background and foreground.
    const std::string alacritty = "[colors.primary]\nbackground = \"#faf4ed\"\nforeground = \"#575279\"\n\n"
                                  "[colors.normal]\nblack = '#f2e9e1'\nred = \"#b4637a\"\nblue = \"#56949f\"\n";
    looks::Palette p;
    CHECK(looks::paletteFromOmarchy("", alacritty, p));
    CHECK(p.base == 0xfaf4ed);
    CHECK(p.text == 0x575279);
    CHECK(p.accent == 0x56949f);
    CHECK(p.red == 0xb4637a);
    CHECK(p.light); // a light theme: its "darker" backgrounds get lighter, not darker
    looks::Palette none;
    CHECK(!looks::paletteFromOmarchy("accent = \"#ffffff\"\n", "", none));
}

TEST(looks_tiled_geometry_has_gaps) {
    const float gap = 6.0f;
    const ShellGeometry g = computeShell(standardLayout(1), 1600, 960, 1.0f, 0.0f, sculptForgeMetrics(gap));
    // The status bar holds the menus: no palette bar; the tool shelf is one row.
    CHECK(near(g.paletteBar.h, 0));
    CHECK(near(g.title.y, gap));
    CHECK(near(g.title.h, 30)); // the bars and shelves grow by the gap: what is inside keeps its size
    CHECK(near(g.topShelf.h, 36));
    CHECK(near(g.leftShelf.w, 88));
    CHECK(near(g.bottomShelf.h, 78));
    const std::vector<const CanvasRect*> tiles = {&g.title, &g.topShelf, &g.leftShelf, &g.rightShelf, &g.bottomShelf, &g.rightTray, &g.canvas};
    for (size_t i = 0; i < tiles.size(); ++i) {
        const CanvasRect& r = *tiles[i];
        CHECK(r.x >= gap - 0.01f && r.y >= gap - 0.01f && r.x + r.w <= 1600 - gap + 0.01f && r.y + r.h <= 960 - gap + 0.01f);
        for (size_t k = i + 1; k < tiles.size(); ++k) CHECK(!overlaps(r, *tiles[k]));
    }
    // Exactly one gap between neighbours.
    CHECK(near(g.canvas.x - (g.leftShelf.x + g.leftShelf.w), gap));
    CHECK(near(g.rightShelf.x - (g.canvas.x + g.canvas.w), gap));
    CHECK(near(g.canvas.y - (g.topShelf.y + g.topShelf.h), gap));
    CHECK(near(g.bottomShelf.y - (g.canvas.y + g.canvas.h), gap));
    CHECK(near(g.rightTray.x - (g.rightShelf.x + g.rightShelf.w), gap));
    // The right divider is that gap, the full height of the shelves.
    CHECK(near(g.rightDivider.x, g.rightShelf.x + g.rightShelf.w));
    CHECK(near(g.rightDivider.w, gap));
    CHECK(near(g.rightDivider.y, g.topShelf.y));
}

TEST(looks_tiled_geometry_left_handed) {
    UiLayout l = standardLayout(1);
    l.leftHanded = true;
    const ShellGeometry g = computeShell(l, 1600, 960, 1.0f, 0.0f, sculptForgeMetrics(6.0f));
    CHECK(near(g.rightTray.x, 6)); // the tray moved to the left edge
    CHECK(g.canvas.x > g.rightTray.x + g.rightTray.w);
    CHECK(!overlaps(g.canvas, g.rightTray));
    CHECK(near(g.rightDivider.x + g.rightDivider.w, g.rightShelf.x)); // after the mirror, still the gap beside the tray
}

TEST(looks_standard_layouts) {
    const UiLayout zb = standardLayout(0), sf = standardLayout(1);
    CHECK(zb.topShelf == standardLayout().topShelf); // the ZBrush-style layout is the old standard one
    CHECK(sf.topShelf != zb.topShelf);
    for (const std::string& id : sf.topShelf) CHECK(id != "row"); // one row
    CHECK(sf.bottomShelf == zb.bottomShelf);
}

TEST(looks_stored_layout_without_shelves) {
    // A SculptForge-layout ui.json that stored no shelves gets the SculptForge layout's shelves, not the ZBrush-style ones.
    Json j = Json::object();
    j.set("version", 3.0);
    j.set("rightTrayOpen", false);
    std::string err;
    UiLayout sf, zb;
    CHECK(fromJson(j, sf, err, 1));
    CHECK(sf.topShelf == standardLayout(1).topShelf);
    CHECK(!sf.rightTrayOpen);
    CHECK(fromJson(j, zb, err, 0));
    CHECK(zb.topShelf == standardLayout(0).topShelf);
    CHECK(fromJson(j, zb, err)); // the ZBrush-style layout's when the layout isn't named
    CHECK(zb.topShelf == standardLayout(0).topShelf);
}
