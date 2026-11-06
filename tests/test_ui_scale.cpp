// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI scale (#87, Windows DPI): what Automatic follows, the user's choice winning, and the shell growing by the
// scale at 100 / 125 / 150 / 175 / 200 %. tests/ui/ui_scale.sfui checks the same live in the app (fonts included).
#include "TestHarness.h"

#include "app/ShellGeometry.h"
#include "app/UiScale.h"

#include <cmath>

using namespace sf;

namespace {
bool near(float a, float b, float eps = 0.01f) { return std::abs(a - b) < eps; }
} // namespace

TEST(ui_scale_automatic_follows_the_display) {
    CHECK(near(automaticUiScale(1.0f, 1.0f), 1.0f));
    CHECK(near(automaticUiScale(1.25f, 1.0f), 1.25f)); // Windows 125 % (per-monitor DPI v2: pixels, density 1)
    CHECK(near(automaticUiScale(1.5f, 1.0f), 1.5f));
    CHECK(near(automaticUiScale(1.75f, 1.0f), 1.75f));
    CHECK(near(automaticUiScale(2.0f, 2.0f), 1.0f)); // macOS Retina: the framebuffer scale covers it
    CHECK(near(automaticUiScale(3.0f, 2.0f), 1.5f));
    CHECK(near(automaticUiScale(0.0f, 1.0f), 1.0f)); // SDL couldn't tell: 100 %
    CHECK(near(automaticUiScale(1.5f, 0.0f), 1.5f));
    CHECK(near(automaticUiScale(NAN, 1.0f), 1.0f));
}

TEST(ui_scale_choice_wins_and_is_clamped) {
    CHECK(near(resolveUiScale(0.0f, 1.5f), 1.5f)); // Automatic
    for (float s : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        CHECK(near(resolveUiScale(s, 1.0f), s));
        CHECK(near(resolveUiScale(s, 2.0f), s)); // a chosen scale ignores the display (#87: Automatic overrode it)
    }
    CHECK(near(resolveUiScale(9.0f, 1.0f), 3.0f));
    CHECK(near(resolveUiScale(0.0f, 0.1f), 0.5f));
    CHECK(uiScaleChanged(1.0f, 1.25f));
    CHECK(!uiScaleChanged(1.25f, 1.2499f)); // jitter from the system rebuilds nothing
}

TEST(ui_scale_matrix_shell_sizes) {
    // A 2400x1350 window (wide enough that no scale makes it narrow): every shell size grows with the scale.
    const ShellGeometry base = computeShell(standardLayout(), 2400, 1350, 1.0f);
    for (float s : {1.0f, 1.25f, 1.5f, 1.75f}) {
        const ShellGeometry g = computeShell(standardLayout(), 2400, 1350, s);
        CHECK(!g.narrow);
        CHECK(near(g.rightShelf.w, base.rightShelf.w * s, 1.0f));
        CHECK(near(g.leftShelf.w, base.leftShelf.w * s, 1.0f));
        CHECK(near(g.title.h, base.title.h * s, 1.0f));
        CHECK(near(g.topShelf.h, base.topShelf.h * s, 1.0f));
        CHECK(g.canvas.w < base.canvas.w + 0.5f && g.canvas.w > 0.0f);
    }
    // 200 % on the same window: 1200 logical px is a narrow window, the shelves still scale.
    const ShellGeometry g = computeShell(standardLayout(), 2400, 1350, 2.0f);
    CHECK(near(g.rightShelf.w, base.rightShelf.w * 2.0f, 1.0f));
    CHECK(near(g.title.h, base.title.h * 2.0f, 1.0f));
}

TEST(ui_scale_circled_plus_is_centred) {
    // #150: the bottom shelf's Add a brush + sat off centre in its circle (a font glyph). Drawn from lines now: at
    // every scale, and wherever the tile lands (fractional item rects), the bars cover whole pixels, have equal arms
    // both ways and share the circle's centre.
    const auto whole = [](float v) { return near(v, std::round(v), 1e-4f); };
    for (float s : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f})
        for (float x : {100.0f, 100.3f, 100.5f, 100.75f})
            for (float y : {40.0f, 40.2f, 40.5f, 40.9f}) {
                const CircledPlus p = circledPlus(x, y + 4.0f * s + 15.0f * s, 15.0f * s, s);
                CHECK(p.stroke >= 1.0f && whole(p.stroke));
                for (float e : {p.barX0, p.barX1, p.barY0, p.barY1, p.colX0, p.colX1, p.colY0, p.colY1}) CHECK(whole(e));
                CHECK(near((p.barX0 + p.barX1) * 0.5f, p.cx, 1e-4f) && near((p.barY0 + p.barY1) * 0.5f, p.cy, 1e-4f));
                CHECK(near((p.colX0 + p.colX1) * 0.5f, p.cx, 1e-4f) && near((p.colY0 + p.colY1) * 0.5f, p.cy, 1e-4f));
                CHECK(near(p.barX1 - p.barX0, p.colY1 - p.colY0, 1e-4f)); // as wide as tall
                CHECK(near(p.barY1 - p.barY0, p.stroke, 1e-4f) && near(p.colX1 - p.colX0, p.stroke, 1e-4f));
                CHECK(std::abs(p.cx - x) <= 0.5f && std::abs(p.cy - (y + 19.0f * s)) <= 0.5f); // moved at most half a px
                CHECK(p.barX1 - p.barX0 < 2.0f * p.r); // inside the circle
            }
}
