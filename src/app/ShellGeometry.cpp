// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ShellGeometry.h"

#include <algorithm>

namespace sf {

using namespace shell;

float trayWidth(float stored, float logicalWindowWidth) {
    if (stored == 300.0f && logicalWindowWidth >= kWideWidth) return 320.0f;
    return std::clamp(stored, kTrayMinW, kTrayMaxW);
}

ShellGeometry computeShell(const UiLayout& l, float W, float H, float s, float topInset) {
    ShellGeometry g;
    g.scale = s;
    const float logicalW = s > 0.0f ? W / s : W;
    g.narrow = logicalW < kNarrowWidth;
    g.shelves = l.shelvesVisible;

    const float inset = std::clamp(topInset, 0.0f, H * 0.25f);
    g.notchBand = {0, 0, W, inset};
    g.title = {0, inset, W, kTitleH * s};
    const float barH = l.paletteBarVisible ? kPaletteBarH * s : 0.0f;
    g.paletteBar = {0, inset + g.title.h, W, barH};
    const float top = inset + g.title.h + barH;

    float left = 0.0f, right = W; // the span between the dividers
    if (g.shelves) {
        const float lw = l.leftTrayOpen ? trayWidth(l.leftTrayWidth, logicalW) * s : 0.0f;
        const float rw = l.rightTrayOpen && !g.narrow ? trayWidth(l.rightTrayWidth, logicalW) * s : 0.0f;
        const float dw = kDividerW * s;
        g.leftTray = {0, top, lw, H - top};
        g.leftDivider = {lw, top, dw, H - top};
        g.rightTray = {W - rw, top, rw, H - top};
        g.rightDivider = {W - rw - dw, top, dw, H - top};
        left = lw + dw;
        right = W - rw - dw;
    } else {
        g.leftTray = {0, top, 0, H - top};
        g.rightTray = {W, top, 0, H - top};
        g.leftDivider = {0, top, 0, H - top};
        g.rightDivider = {W, top, 0, H - top};
    }
    const float span = std::max(0.0f, right - left);

    const float topH = g.shelves ? kTopShelfH * s : 0.0f;
    const float bottomH = g.shelves ? bottomShelfSize(l.shelfTiles).shelfH * s : 0.0f;
    g.topShelf = {left, top, span, topH};
    g.bottomShelf = {left, H - bottomH, span, bottomH};
    const float consoleH = l.consoleOpen ? std::clamp(l.consoleHeight, kConsoleMinH, kConsoleMaxH) * s : 0.0f;
    g.console = {left, g.bottomShelf.y - consoleH, span, consoleH};

    const float y0 = top + topH, y1 = std::max(y0, g.console.y);
    const float leftW = g.shelves ? (g.narrow ? kLeftShelfNarrowW : kLeftShelfW) * s : 0.0f;
    const float rightW = g.shelves ? kRightShelfW * s : 0.0f;
    g.leftShelf = {left, y0, leftW, y1 - y0};
    g.rightShelf = {right - rightW, y0, rightW, y1 - y0};
    g.canvas = {left + leftW, y0, std::max(1.0f, span - leftW - rightW), std::max(1.0f, y1 - y0)};

    if (l.leftHanded) { // Nomad's mirrored layout: everything swaps sides
        for (CanvasRect* r : {&g.topShelf, &g.leftShelf, &g.rightShelf, &g.bottomShelf, &g.console, &g.leftDivider,
                              &g.rightDivider, &g.leftTray, &g.rightTray, &g.canvas})
            r->x = W - r->x - r->w;
    }
    return g;
}

} // namespace sf
