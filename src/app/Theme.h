// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Colours and style of the shell (docs/ui-design.md §10): dense, charcoal grey,
// with the SculptForge orange kept for the one current tool or mode in each group.
#pragma once

#include <imgui.h>

#include <string>

namespace sf::theme {

constexpr ImVec4 rgb(unsigned hex, float a = 1.0f) {
    return ImVec4(float((hex >> 16) & 0xff) / 255.0f, float((hex >> 8) & 0xff) / 255.0f, float(hex & 0xff) / 255.0f, a);
}

inline ImVec4 shellBg = rgb(0x1c1c1c);   // shelves, trays, title strip, around the canvas
inline ImVec4 panelBg = rgb(0x232323);   // palette drop-downs, tray contents, pop-ups
inline ImVec4 header = rgb(0x2c2c2c);    // palette and sub-palette headers
inline ImVec4 headerHover = rgb(0x393939);
inline ImVec4 frame = rgb(0x292929);     // slider and input frames
inline ImVec4 frameHover = rgb(0x343434);
inline ImVec4 button = rgb(0x343434);
inline ImVec4 buttonHover = rgb(0x434343);
inline ImVec4 buttonPressed = rgb(0x505050);
// The accent marks one thing per group: the current tool or mode (Draw, Sculpt, the brush), plus grabs and checks.
inline ImVec4 accent = rgb(0xf0882e);
inline ImVec4 accentHover = rgb(0xff9a45);
// Options (X, Rgb, Pressure > Strength, Mask, Menus...): off is a dark chip with a readable label (not dim, so it
// doesn't look disabled); on is a raised grey with a bright label and a thin accent underline.
inline ImVec4 toggleOff = rgb(0x2b2b2b);
inline ImVec4 toggleOffHover = rgb(0x383838);
inline ImVec4 toggleOn = rgb(0x4e4e4e);
inline ImVec4 toggleOnHover = rgb(0x5a5a5a);
// The unselected segments of a mode or choice group (Draw | Move | Scale | Rotate, Zadd | Zsub): a step above the
// shelf, so the group reads as one control.
inline ImVec4 trough = rgb(0x2e2e2e);
inline ImVec4 selection = rgb(0x3c3c3c);  // selected list rows: a lighter neutral row, with a 2 px accent bar on the left
inline ImVec4 selectionActive = rgb(0x474747);
inline ImVec4 sliderFill = rgb(0xffffff, 0.09f); // the filled part of a SliderInside
inline ImVec4 sliderBar = rgb(0x878787);  // the thin bar along its bottom (the accent only while dragging)
inline ImVec4 checkMark = rgb(0xe7e7e7);  // check marks and plain slider grabs: neutral, the accent is for modes
inline ImVec4 checkBorder = rgb(0x575757); // the outline of an unchecked box, so it stays visible on panelBg
inline ImVec4 sectionRule = rgb(0x3c3c3c, 0.63f); // the hairline under a section header in a palette
inline ImVec4 text = rgb(0xd8d8d8);
inline ImVec4 textBright = rgb(0xf2f2f2); // labels of options that are on
inline ImVec4 textOff = rgb(0xb6b6b6);   // labels of options that are off: 6.9:1 on toggleOff
inline ImVec4 textDim = rgb(0x8d8d8d);    // secondary text and disabled labels
inline ImVec4 textOnAccent = rgb(0xffffff);
inline ImVec4 textHover = rgb(0xffffff);   // hovered or open labels on a header (not on the accent)
inline ImVec4 border = rgb(0x121212);    // 1 px between regions
inline ImVec4 danger = rgb(0xe5604a);    // errors
inline ImVec4 canvasBorder = rgb(0x0d0d0d);
inline ImVec4 tile = rgb(0x2b2b2b);      // thumbnail placeholders
inline float markWidth = 2.0f;           // the accent underline / side bar, unscaled pixels

// The SculptForge orange of the cube logo: the brand mark only, the same in every look.
constexpr ImVec4 brand = rgb(0xf0882e);
// Tiled looks (Preferences > Interface > Look): the wallpaper showing in the gaps between the tiles, a tile's thin
// border and the focused tile's border.
inline ImVec4 gapBg = rgb(0x0d0d0d);
inline ImVec4 tileBorder = rgb(0x2b2b2b);
inline ImVec4 focusBorder = rgb(0xf0882e);

inline ImU32 u32(const ImVec4& c, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }

// How the current look draws the shell, beyond its colours (set by applyLook).
struct LookStyle {
    int id = 0;               // looks::LookId
    bool tiled = false;       // panels as tiles: gaps, thin borders, the focused one highlighted
    float gap = 0.0f;         // between tiles, unscaled px
    float roundingScale = 1.0f; // multiplies the shell's hand-drawn corner radii (0: square corners)
    bool monoLabels = false;  // captions, headers, readouts and numbers in the monospace font
    bool monoAll = false;     // everything in the monospace font
    enum class Status { Plain, Modules, Terminal, TitleBlock } status = Status::Plain; // the top bar (SculptForge layout)
    enum class Headers { Plain, Bar, Ascii, Grid } headers = Headers::Plain;           // palette and section headers
    bool scanlines = false;   // Phosphor's effects (off by default): scanlines and a glow, on the chrome only
    bool glow = false;
    bool upperHeaders = false; // palette headers in capitals
    int outlines = 1;         // tiled looks' panel borders: 0 off, 1 subtle (the default), 2 on (LookSettings)
    float outlineOpacity = 1.0f; // Outline opacity (LookSettings), 0-1; the hover rings follow both
};
inline LookStyle look;

// The fonts a look may switch to (App fills them in at start-up); nullptr = keep the default.
struct LookFonts {
    ImFont* mono = nullptr;      // JetBrains Mono 15 px: the numbers in sliders and readouts
    ImFont* monoSmall = nullptr;
    ImFont* monoBold = nullptr;
};
inline LookFonts fonts;

// A corner radius in the current look (square looks return 0).
inline float rounding(float r) { return r * look.roundingScale; }

// The font for numbers and technical readouts: the monospace one in looks that use it, else nullptr.
inline ImFont* numberFont() { return look.monoLabels || look.monoAll ? fonts.mono : nullptr; }

// Sets the colours, rounding and spacing (unscaled; call ImGuiStyle::ScaleAllSizes afterwards).
void applyStyle();

// What Preferences > Interface > Look holds (UiPrefs::look).
struct LookSettings {
    int look = 1;                        // looks::LookId: 0 Classic, 1 Forge Terminal (the default), Phosphor, Blueprint
    std::string scheme = "neutral";      // Forge Terminal's colour scheme (looks::schemes); Tokyo Night before 0.10.9
    bool matchOmarchy = false;           // Forge Terminal: the colours of the current Omarchy theme (Linux, read-only)
    bool amber = false;                  // Phosphor: amber instead of green
    bool effects = false;                // Phosphor: scanlines and glow on the chrome (never on the 3D view)
    // The tiled looks' panel borders (0.10.9, user feedback: "the outlines are distracting"): 0 off, 1 subtle (the
    // default: faint, and nothing lights up under the pointer), 2 on (clear borders, a faint step on the panel
    // under the pointer). Never the accent on hover.
    int outlines = 1;
    float outlineOpacity = 1.0f; // #98: how strong the borders are, 0-1, on top of Outlines (100%: as the setting draws them)
};

// Sets the look's colour tokens, LookStyle and ImGui style (unscaled, like applyStyle). `status` gets a message
// when Match my Omarchy theme can't read the theme (the scheme is used instead).
void applyLook(const LookSettings& settings, std::string* status = nullptr);
// The 3D view's gradient background in the current look (display sRGB 0-1): behind the model and at the edges.
void lookViewportGradient(ImVec4& centre, ImVec4& edge);

} // namespace sf::theme
