// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where every region of the ZBrush-style shell goes for a window size: title
// strip, palette bar, the four shelves around the canvas, the console, the tray
// dividers and trays. Pure arithmetic (no ImGui), so it can be unit tested and
// used by input handling before a frame is drawn. See docs/ui-design.md §2.
#pragma once

#include "app/UiLayout.h"

namespace sf {

struct ShellGeometry {
    CanvasRect notchBand;               // h 0 unless the top of the window is unsafe (camera notch)
    CanvasRect title, paletteBar;
    CanvasRect topShelf, leftShelf, rightShelf, bottomShelf;
    CanvasRect console;                 // h 0 when closed
    CanvasRect leftDivider, rightDivider; // w 0 when the shelves are hidden
    CanvasRect leftTray, rightTray;     // w 0 when closed
    CanvasRect canvas;                  // the 3D view
    bool narrow = false;                // < 1280 logical px: right tray collapsed, smaller tiles
    bool shelves = true;                // shelves and trays shown (Tab hides them)
    float scale = 1.0f;
};

// Logical sizes at scale 1 (docs/ui-design.md §2.1).
namespace shell {
constexpr float kTitleH = 24, kPaletteBarH = 22, kTopShelfH = 64, kLeftShelfW = 88, kLeftShelfNarrowW = 72;
constexpr float kRightShelfW = 52, kDividerW = 8, kTrayMinW = 240, kTrayMaxW = 480;
constexpr float kConsoleMinH = 100, kConsoleMaxH = 400, kNarrowWidth = 1280, kWideWidth = 1920;

// The bottom shelf for a Shelf tiles setting (UiLayout::shelfTiles): its height and its brush tiles' height and
// width range (tiles shrink to the minimum, the same for every size, before the shelf scrolls), logical px.
struct BottomShelfSize {
    float shelfH, tileH, tileMinW, tileMaxW;
};
constexpr BottomShelfSize bottomShelfSize(int tiles) {
    return tiles <= 0 ? BottomShelfSize{60, 54, 44, 58} : tiles == 1 ? BottomShelfSize{78, 72, 44, 78} : BottomShelfSize{96, 90, 44, 96};
}
} // namespace shell

// Effective tray width in logical px: the stored width, except that the standard 300 grows to 320 on
// windows 1920 logical px wide or more.
float trayWidth(float stored, float logicalWindowWidth);

// `topInset` (window px) keeps everything below a camera notch or other unsafe band at the top.
ShellGeometry computeShell(const UiLayout& layout, float width, float height, float scale, float topInset = 0.0f);

} // namespace sf
