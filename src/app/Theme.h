// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Colours and style of the shell (docs/ui-design.md §10): dense, charcoal grey,
// with the SculptForge orange kept for the one current tool or mode in each group.
#pragma once

#include <imgui.h>

namespace sf::theme {

constexpr ImVec4 rgb(unsigned hex, float a = 1.0f) {
    return ImVec4(float((hex >> 16) & 0xff) / 255.0f, float((hex >> 8) & 0xff) / 255.0f, float(hex & 0xff) / 255.0f, a);
}

constexpr ImVec4 shellBg = rgb(0x1b1b1d);   // shelves, trays, title strip, around the canvas
constexpr ImVec4 panelBg = rgb(0x222225);   // palette drop-downs, tray contents, pop-ups
constexpr ImVec4 header = rgb(0x2b2b2f);    // palette and sub-palette headers
constexpr ImVec4 headerHover = rgb(0x37373c);
constexpr ImVec4 frame = rgb(0x28282c);     // slider and input frames
constexpr ImVec4 frameHover = rgb(0x323237);
constexpr ImVec4 button = rgb(0x333337);
constexpr ImVec4 buttonHover = rgb(0x414146);
constexpr ImVec4 buttonPressed = rgb(0x4e4e55);
// The accent marks one thing per group: the current tool or mode (Draw, Sculpt, the brush), plus grabs and checks.
constexpr ImVec4 accent = rgb(0xf0882e);
constexpr ImVec4 accentHover = rgb(0xff9a45);
// Options that are on (Zadd, X, Pressure > Strength, Mask...): a raised grey with a thin accent underline.
constexpr ImVec4 toggleOn = rgb(0x4a4a50);
constexpr ImVec4 toggleOnHover = rgb(0x56565c);
constexpr ImVec4 trough = rgb(0x242428);    // the unselected segments of a mode group (Draw | Move | Scale | Rotate)
constexpr ImVec4 selection = rgb(0xf0882e, 0.18f); // selected list rows, with a 2 px accent bar on the left
constexpr ImVec4 sliderFill = rgb(0xffffff, 0.07f); // the filled part of a SliderInside, above its accent bar
constexpr ImVec4 text = rgb(0xd8d8d8);
constexpr ImVec4 textBright = rgb(0xf2f2f2); // labels of options that are on
constexpr ImVec4 textDim = rgb(0x8c8c90);
constexpr ImVec4 textOnAccent = rgb(0xffffff);
constexpr ImVec4 border = rgb(0x111113);    // 1 px between regions
constexpr ImVec4 danger = rgb(0xe5604a);    // errors
constexpr ImVec4 canvasBorder = rgb(0x0c0c0e);
constexpr ImVec4 tile = rgb(0x2a2a2e);      // thumbnail placeholders
constexpr float markWidth = 2.0f;           // the accent underline / side bar, unscaled pixels

inline ImU32 u32(const ImVec4& c, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }

// Sets the colours, rounding and spacing (unscaled; call ImGuiStyle::ScaleAllSizes afterwards).
void applyStyle();

} // namespace sf::theme
