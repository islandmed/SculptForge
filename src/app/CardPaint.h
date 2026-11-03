// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing helpers for the full-window cards (the support card, What's New): easing, colours of the current look,
// soft glows, the cube mark and text drawn straight to a draw list (also logged so UI tests find it).
#pragma once

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
// The orange of the cube logo: the brand mark, the same in every look.
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

// The cube mark of the logo (assets/branding/sculptforge-icon.svg: Lucide's "box"), `size` wide at `at`.
inline void cubeMark(ImDrawList* dl, ImVec2 at, float size, ImU32 c) {
    const float u = size / 24.0f;
    auto p = [&](float x, float y) { return ImVec2(at.x + x * u, at.y + y * u); };
    const ImVec2 outline[6] = {p(12, 2), p(21, 7), p(21, 17), p(12, 22), p(3, 17), p(3, 7)};
    const float w = 2.2f * u;
    dl->AddPolyline(outline, 6, c, ImDrawFlags_Closed, w);
    const ImVec2 top[3] = {p(3.3f, 7), p(12, 12), p(20.7f, 7)};
    dl->AddPolyline(top, 3, c, 0, w);
    dl->AddLine(p(12, 22), p(12, 12), c, w);
}

// The cube mark on its dark tile with an orange glow, `m` wide at `m0` (the cards' headers).
inline void cubeTile(ImDrawList* dl, ImVec2 m0, float m, float k, float alpha) {
    const ImVec2 m1(m0.x + m, m0.y + m);
    glow(dl, m0, m1, m * 0.25f, m * 0.375f, kBrandOrange, 0.35f * alpha, 8);
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(m0, m1, col(0xffffff, alpha), m * 0.25f);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, m0, ImVec2(m0.x, m1.y), col(0x23232a, 1.0f),
                                                  col(0x0e0e11, 1.0f));
    dl->AddRect(m0, m1, col(kBrandOrange, 0.45f * alpha), m * 0.25f, 0, std::max(1.0f, k));
    cubeMark(dl, ImVec2(m0.x + m * 0.19f, m0.y + m * 0.19f), m * 0.62f, col(kBrandOrange, alpha));
}

} // namespace sf::card
