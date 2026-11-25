// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI scale (#87, Windows DPI): what Automatic follows, the user's choice winning, and the shell growing by the
// scale at 100 / 125 / 150 / 175 / 200 %. tests/ui/ui_scale.sfui checks the same live in the app (fonts included).
#include "TestHarness.h"

#include "app/ShellGeometry.h"
#include "app/UiScale.h"

#include <cmath>
#include <string>

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
    for (float s : {1.0f, 1.1f, 1.15f, 1.25f, 1.35f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f}) {
        CHECK(near(resolveUiScale(s, 1.0f), s));
        CHECK(near(resolveUiScale(s, 2.0f), s)); // a chosen scale ignores the display (#87: Automatic overrode it)
    }
    CHECK(near(resolveUiScale(9.0f, 1.0f), 3.0f));
    CHECK(near(resolveUiScale(0.0f, 0.1f), 0.7f)); // never below 70 % (the captions stop reading)
    for (float s : {0.7f, 0.75f, 0.85f, 0.9f}) CHECK(near(resolveUiScale(s, 2.0f), s)); // below 100 % (Fenix 11-22)
    CHECK(near(resolveUiScale(0.6f, 1.0f), 0.7f));
    CHECK(uiScaleChanged(1.0f, 1.25f));
    CHECK(!uiScaleChanged(1.25f, 1.2499f)); // jitter from the system rebuilds nothing
}

TEST(ui_scale_slider_steps_and_compact_padding) {
    // The slider lands on whole 5 % steps inside 70-300 %.
    CHECK(near(snapUiScale(0.73f), 0.75f));
    CHECK(near(snapUiScale(0.72f), 0.7f));
    CHECK(near(snapUiScale(1.0f), 1.0f));
    CHECK(near(snapUiScale(1.126f), 1.15f));
    CHECK(near(snapUiScale(0.5f), 0.7f));
    CHECK(near(snapUiScale(4.0f), 3.0f));
    // Below 100 % the padding shrinks by the square root of the scale (70 % text keeps 84 % padding), never more than
    // the text; at and above 100 % everything scales alike.
    CHECK(near(compactPadScale(0.7f), std::sqrt(0.7f)));
    CHECK(compactPadScale(0.7f) > 0.7f && compactPadScale(0.7f) < 1.0f);
    CHECK(compactPadScale(0.85f) > 0.85f && compactPadScale(0.85f) < 1.0f);
    CHECK(near(compactPadScale(1.0f), 1.0f));
    CHECK(near(compactPadScale(1.5f), 1.5f));
    CHECK(near(compactPadScale(3.0f), 3.0f));
}

TEST(ui_scale_remembered_per_display) {
    DisplayScales scales;
    const std::string laptop = displayScaleKey("Built-in Retina Display", 3024, 1964);
    const std::string monitor = displayScaleKey("DELL U2720Q", 3840, 2160);
    CHECK(laptop != monitor);
    CHECK(displayScaleKey("DELL U2720Q", 3840, 2160) == monitor); // the same monitor again: the same key
    CHECK(displayScaleKey("DELL U2720Q", 1920, 1080) != monitor); // its 1080p sibling is another one
    CHECK(displayScaleKey(nullptr, 1920, 1080) == displayScaleKey("", 1920, 1080)); // SDL without a name
    CHECK(!hasDisplayScale(scales, laptop));
    CHECK(near(displayScaleFor(scales, laptop, 1.25f), 1.25f)); // no entry: the general choice
    setDisplayScale(scales, monitor, 0.8f);
    CHECK(near(displayScaleFor(scales, monitor, 1.25f), 0.8f));
    CHECK(near(displayScaleFor(scales, laptop, 1.25f), 1.25f)); // the laptop keeps the general choice
    setDisplayScale(scales, laptop, 0.0f); // Automatic on the laptop
    CHECK(hasDisplayScale(scales, laptop));
    CHECK(near(displayScaleFor(scales, laptop, 1.25f), 0.0f));
    CHECK(near(resolveUiScale(displayScaleFor(scales, laptop, 1.25f), 2.0f), 2.0f)); // ...so the display decides there
    setDisplayScale(scales, monitor, 0.7f); // changed: one entry, the new value
    CHECK(scales.size() == 2);
    CHECK(near(displayScaleFor(scales, monitor, 1.25f), 0.7f));
    for (int i = 0; i < 40; ++i) setDisplayScale(scales, displayScaleKey("Projector", 1280 + i, 720), 1.0f);
    CHECK(scales.size() == 16); // the oldest forgotten
    CHECK(!hasDisplayScale(scales, laptop));
}

TEST(ui_scale_fonts_at_the_pixel_density) {
    // A 5K Retina display (density 2) drew a 1x font atlas stretched 2x: soft text and icons (fenix 11-08).
    CHECK(near(fontRasterDensity(1.0f, 1.0f), 1.0f)); // Windows / X11: the UI scale already sizes the atlas
    CHECK(near(fontRasterDensity(2.5f, 1.0f), 1.0f));
    CHECK(near(fontRasterDensity(1.0f, 2.0f), 2.0f)); // macOS Retina at 100 %
    CHECK(near(fontRasterDensity(1.0f, 1.5f), 1.5f)); // a Wayland output at 150 %
    for (float s : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f}) {
        const float d = fontRasterDensity(s, 2.0f);
        CHECK(d >= 1.0f && d <= 2.0f && s * d <= 3.0f + 1e-4f); // no bigger an atlas than 300 % at 1x (4096x16384)
    }
    CHECK(near(fontRasterDensity(1.5f, 2.0f), 2.0f));
    CHECK(near(fontRasterDensity(2.0f, 2.0f), 1.5f));
    CHECK(near(fontRasterDensity(3.0f, 2.0f), 1.0f));
    CHECK(near(fontRasterDensity(1.0f, 0.0f), 1.0f)); // SDL couldn't tell
    CHECK(near(fontRasterDensity(1.0f, NAN), 1.0f));
    CHECK(near(fontRasterDensity(1.0f, 9.0f), 3.0f));
}

TEST(ui_scale_matrix_shell_sizes) {
    // A 2400x1350 window (wide enough that no scale makes it narrow): every shell size grows with the scale.
    const ShellGeometry base = computeShell(standardLayout(), 2400, 1350, 1.0f);
    for (float s : {0.7f, 0.85f, 1.0f, 1.25f, 1.5f, 1.75f}) { // (70 and 85 %: the shelves shrink with the text)
        const ShellGeometry g = computeShell(standardLayout(), 2400, 1350, s);
        CHECK(!g.narrow);
        CHECK(near(g.rightShelf.w, base.rightShelf.w * s, 1.0f));
        CHECK(near(g.leftShelf.w, base.leftShelf.w * s, 1.0f));
        CHECK(near(g.title.h, base.title.h * s, 1.0f));
        CHECK(near(g.topShelf.h, base.topShelf.h * s, 1.0f));
        CHECK(g.canvas.w > 0.0f);
        if (s >= 1.0f) CHECK(g.canvas.w < base.canvas.w + 0.5f); // larger shelves: less canvas
        else CHECK(g.canvas.w > base.canvas.w - 0.5f);            // smaller shelves: more canvas (the point of 70 %)
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
