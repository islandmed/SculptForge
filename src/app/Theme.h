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
// Options (X, Rgb, Pressure > Strength, Mask, Menus...): off is a dark chip with a readable label (not dim, so it
// doesn't look disabled); on is a raised grey with a bright label and a thin accent underline.
constexpr ImVec4 toggleOff = rgb(0x29292e);
constexpr ImVec4 toggleOffHover = rgb(0x36363c);
constexpr ImVec4 toggleOn = rgb(0x4c4c53);
constexpr ImVec4 toggleOnHover = rgb(0x58585f);
// The unselected segments of a mode or choice group (Draw | Move | Scale | Rotate, Zadd | Zsub): a step above the
// shelf, so the group reads as one control.
constexpr ImVec4 trough = rgb(0x2c2c31);
constexpr ImVec4 selection = rgb(0x3a3a41);  // selected list rows: a lighter neutral row, with a 2 px accent bar on the left
constexpr ImVec4 selectionActive = rgb(0x45454c);
constexpr ImVec4 sliderFill = rgb(0xffffff, 0.09f); // the filled part of a SliderInside
constexpr ImVec4 sliderBar = rgb(0x85858c);  // the thin bar along its bottom (the accent only while dragging)
constexpr ImVec4 checkMark = rgb(0xe6e6e8);  // check marks and plain slider grabs: neutral, the accent is for modes
constexpr ImVec4 checkBorder = rgb(0x55555c); // the outline of an unchecked box, so it stays visible on panelBg
constexpr ImVec4 sectionRule = rgb(0x3a3a3f, 0.63f); // the hairline under a section header in a palette
constexpr ImVec4 text = rgb(0xd8d8d8);
constexpr ImVec4 textBright = rgb(0xf2f2f2); // labels of options that are on
constexpr ImVec4 textOff = rgb(0xb4b4b9);   // labels of options that are off: 6.9:1 on toggleOff
constexpr ImVec4 textDim = rgb(0x8c8c90);    // secondary text and disabled labels
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
