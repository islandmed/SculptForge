// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing helpers for the full-window cards (the support card, What's New): easing, colours of the current look,
// soft glows, the brand mark and text drawn straight to a draw list (also logged so UI tests find it).
#pragma once

#include "app/BrandMark.h"
#include "app/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace sf::card {

inline float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
inline float easeOut(float x) { // cubic
    x = 1.0f - clamp01(x);
    return 1.0f - x * x * x;
}
inline float easeInOut(float x) {
    x = clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline ImU32 col(unsigned hex, float alpha) {
    return IM_COL32((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff, int(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f));
}
// A colour of the current look at an opacity.
inline ImU32 tc(const ImVec4& c, float alpha) { return theme::u32(c, std::clamp(alpha, 0.0f, 1.0f)); }
inline ImVec4 mix(const ImVec4& x, const ImVec4& y, float t) {
    return ImVec4(lerp(x.x, y.x, t), lerp(x.y, y.y, t), lerp(x.z, y.z, t), lerp(x.w, y.w, t));
}
// The SculptForge orange: the same in every look.
constexpr unsigned kBrandOrange = 0xf0882e;

// Text drawn straight to the draw list, also logged so UI tests can find it (expect_text).
struct Painter {
    ImDrawList* dl;
    float k; // logical pixels to screen pixels (UI scale x fit x the opening zoom)

    ImVec2 measure(ImFont* f, float px, const char* s, float wrap = 0.0f) const {
        return f->CalcTextSizeA(px * k, FLT_MAX, wrap, s);
    }
    ImVec2 text(ImFont* f, float px, ImVec2 at, ImU32 c, const char* s, float wrap = 0.0f) const {
        dl->AddText(f, px * k, at, c, s, nullptr, wrap);
        ImGui::LogText("%s\n", s);
        return measure(f, px, s, wrap);
    }
    // Small capitals with letter spacing (eyebrows and badges).
    float spaced(ImFont* f, float px, ImVec2 at, ImU32 c, const char* s, float spacing, bool draw = true) const {
        float x = at.x;
        char one[2] = {0, 0};
        for (const char* p = s; *p; ++p) {
            one[0] = *p;
            if (draw) dl->AddText(f, px * k, ImVec2(x, at.y), c, one);
            x += f->CalcTextSizeA(px * k, FLT_MAX, 0.0f, one).x + spacing * k;
        }
        if (draw) ImGui::LogText("%s\n", s);
        return x - at.x - spacing * k;
    }
};

// A soft shadow / glow: rounded rectangles growing outwards, fading.
inline void glow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float spread, const ImVec4& c, float alpha, int layers = 10) {
    for (int i = layers; i >= 1; --i) {
        const float t = float(i) / float(layers), g = spread * t;
        dl->AddRectFilled(ImVec2(a.x - g, a.y - g), ImVec2(b.x + g, b.y + g), tc(c, alpha * (1.0f - t) * (1.0f - t) * 0.5f),
                          rounding + g);
    }
}
inline void glow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float spread, unsigned hex, float alpha, int layers = 10) {
    glow(dl, a, b, rounding, spread, theme::rgb(hex), alpha, layers);
}

// The bust illustration with a soft orange glow, `m` wide at `m0` (the cards' headers).
inline void markTile(ImDrawList* dl, ImVec2 m0, float m, float alpha) {
    const ImVec2 m1(m0.x + m, m0.y + m);
    glow(dl, m0, m1, m * 0.22f, m * 0.375f, kBrandOrange, 0.3f * alpha, 8);
    brand::bust(dl, m0, m, alpha);
}

} // namespace sf::card
