// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ShellGeometry.h"

#include <algorithm>
#include <cmath>

namespace sf {

using namespace shell;

float trayWidth(float stored, float logicalWindowWidth) {
    if (stored == 300.0f && logicalWindowWidth >= kWideWidth) return 320.0f;
    return std::clamp(stored, kTrayMinW, kTrayMaxW);
}

WindowFit fitWindow(WindowRect win, bool stored, bool placed, float storedScale, const WindowRect& usable,
                    const WindowBorders& b, float scale) {
    WindowFit fit;
    const float s = scale > 0.0f ? scale : 1.0f, was = storedScale > 0.0f ? storedScale : 1.0f;
    const int availW = std::max(1, usable.w - b.left - b.right), availH = std::max(1, usable.h - b.top - b.bottom);
    int wantW = win.w, wantH = win.h;
    if (!stored) {
        wantW = int(1600.0f * s + 0.5f);
        wantH = int(960.0f * s + 0.5f);
    } else if (std::abs(s - was) >= 0.01f) {
        wantW = int(float(win.w) * s / was + 0.5f);
        wantH = int(float(win.h) * s / was + 0.5f);
    }
    fit.maximize = wantW > availW || wantH > availH;
    win.w = std::min(wantW, availW);
    win.h = std::min(wantH, availH);
    if (!placed) {
        win.x = usable.x + b.left + (availW - win.w) / 2;
        win.y = usable.y + b.top + (availH - win.h) / 2;
    } else {
        win.x = std::clamp(win.x, usable.x + b.left, usable.x + usable.w - b.right - win.w);
        win.y = std::clamp(win.y, usable.y + b.top, usable.y + usable.h - b.bottom - win.h);
    }
    fit.rect = win;
    return fit;
}

ShellMetrics sculptForgeMetrics(float gap) {
    ShellMetrics m;
    m.titleH = 30;
    m.paletteBarH = 0;
    m.topShelfH = 36;
    m.dividerW = gap > 0.0f ? 0.0f : kDividerW;
    m.gap = gap;
    return m;
}

ShellGeometry computeShell(const UiLayout& l, float W, float H, float s, float topInset, const ShellMetrics& m) {
    if (m.gap > 0.0f) { // tiles: lay out in a window `gap` smaller, then inset every region by half the gap
        const float half = m.gap * 0.5f * s;
        ShellMetrics inner = m;
        inner.gap = 0.0f;
        inner.grow = m.gap;
        inner.windowW = W;
        UiLayout unmirrored = l; // mirrored at the end, dividers included
        unmirrored.leftHanded = false;
        ShellGeometry g = computeShell(unmirrored, W - 2.0f * half, H - 2.0f * half, s, topInset, inner);
        auto shift = [&](CanvasRect& r) {
            r.x += half;
            r.y += half;
        };
        auto inset = [&](CanvasRect& r) {
            shift(r);
            if (r.w > 0.0f) r.x += half, r.w = std::max(1.0f, r.w - 2.0f * half);
            if (r.h > 0.0f) r.y += half, r.h = std::max(1.0f, r.h - 2.0f * half);
        };
        shift(g.notchBand);
        for (CanvasRect* r : {&g.title, &g.paletteBar, &g.topShelf, &g.leftShelf, &g.rightShelf, &g.bottomShelf, &g.console,
                              &g.leftTray, &g.rightTray, &g.canvas})
            inset(*r);
        // The dividers: the gap beside each tray (between the tray and the shelves, or the window edge when closed).
        if (g.shelves) {
            const float top = g.topShelf.y, bottom = g.bottomShelf.y + g.bottomShelf.h;
            const float lx = g.leftTray.w > 0.0f ? g.leftTray.x + g.leftTray.w : 0.0f;
            const float rx = g.rightTray.w > 0.0f ? g.rightTray.x : W;
            const float leftEdge = std::min(g.topShelf.x, g.leftShelf.x), rightEdge = std::max(g.topShelf.x + g.topShelf.w,
                                                                                                 g.rightShelf.x + g.rightShelf.w);
            g.leftDivider = {lx, top, std::max(2.0f * s, leftEdge - lx), bottom - top};
            g.rightDivider = {rightEdge, top, std::max(2.0f * s, rx - rightEdge), bottom - top};
        }
        if (l.leftHanded)
            for (CanvasRect* r : {&g.topShelf, &g.leftShelf, &g.rightShelf, &g.bottomShelf, &g.console, &g.leftDivider,
                                  &g.rightDivider, &g.leftTray, &g.rightTray, &g.canvas})
                r->x = W - r->x - r->w;
        return g;
    }
    ShellGeometry g;
    g.scale = s;
    const float windowW = m.windowW > 0.0f ? m.windowW : W;
    const float logicalW = s > 0.0f ? windowW / s : windowW;
    g.narrow = logicalW < kNarrowWidth;
    g.shelves = l.shelvesVisible;

    const float inset = std::clamp(topInset, 0.0f, H * 0.25f);
    g.notchBand = {0, 0, W, inset};
    g.title = {0, inset, W, (m.titleH + m.grow) * s};
    const float barH = l.paletteBarVisible && m.paletteBarH > 0.0f ? (m.paletteBarH + m.grow) * s : 0.0f;
    g.paletteBar = {0, inset + g.title.h, W, barH};
    const float top = inset + g.title.h + barH;

    float left = 0.0f, right = W; // the span between the dividers
    if (g.shelves) {
        const float lw = l.leftTrayOpen ? trayWidth(l.leftTrayWidth, logicalW) * s : 0.0f;
        const float rw = l.rightTrayOpen && !g.narrow ? trayWidth(l.rightTrayWidth, logicalW) * s : 0.0f;
        const float dw = m.dividerW * s;
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

    const float topH = g.shelves ? (m.topShelfH + m.grow) * s : 0.0f;
    const float bottomH = g.shelves ? (bottomShelfSize(l.shelfTiles).shelfH + m.grow) * s : 0.0f;
    g.topShelf = {left, top, span, topH};
    g.bottomShelf = {left, H - bottomH, span, bottomH};
    const float consoleH = l.consoleOpen ? std::clamp(l.consoleHeight, kConsoleMinH, kConsoleMaxH) * s : 0.0f;
    g.console = {left, g.bottomShelf.y - consoleH, span, consoleH};

    const float y0 = top + topH, y1 = std::max(y0, g.console.y);
    const float leftW = g.shelves ? ((g.narrow ? kLeftShelfNarrowW : kLeftShelfW) + m.grow) * s : 0.0f;
    const float rightW = g.shelves ? (kRightShelfW + m.grow) * s : 0.0f;
    g.leftShelf = {left, y0, leftW, y1 - y0};
    g.rightShelf = {right - rightW, y0, rightW, y1 - y0};
    g.canvas = {left + leftW, y0, std::max(1.0f, span - leftW - rightW), std::max(1.0f, y1 - y0)};

    if (l.leftHanded) { // the mirrored layout: everything swaps sides
        for (CanvasRect* r : {&g.topShelf, &g.leftShelf, &g.rightShelf, &g.bottomShelf, &g.console, &g.leftDivider,
                              &g.rightDivider, &g.leftTray, &g.rightTray, &g.canvas})
            r->x = W - r->x - r->w;
    }
    return g;
}

} // namespace sf
