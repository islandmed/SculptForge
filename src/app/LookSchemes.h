// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface looks (Preferences > Interface > Look) and their colour schemes, as plain data: no ImGui, so the
// scheme table and the Omarchy theme reader can be unit tested. Theme.cpp turns a Palette into the shell's colour
// tokens. The colour values of the open-source schemes are credited in THIRD_PARTY_NOTICES.md.
#pragma once

#include <string>
#include <string_view>

namespace sf::looks {

enum class LookId { Classic = 0, ForgeTerminal, Phosphor, Blueprint, Count };

const char* lookKey(LookId look);  // "classic", "forge", "phosphor", "blueprint" (ui.json, the ui_look command)
const char* lookName(LookId look); // "Classic", "Forge Terminal", ...
LookId lookFromKey(std::string_view key); // Classic when unknown

// A colour scheme as twelve-odd 0xRRGGBB values, darkest to brightest, plus the accents. The names follow
// Catppuccin's roles, which most terminal themes map onto.
struct Palette {
    unsigned crust = 0, mantle = 0, base = 0;          // gaps / wallpaper, bars and shelves, panels
    unsigned surface0 = 0, surface1 = 0, surface2 = 0; // headers and buttons, hover and selection, pressed / borders
    unsigned overlay = 0;                              // dim text, rules
    unsigned subtext = 0, text = 0, bright = 0;        // secondary text, text, emphasised text
    unsigned accent = 0, accent2 = 0;                  // the current tool or mode, a second highlight
    unsigned red = 0, green = 0, yellow = 0;
    bool light = false;
};

struct SchemeInfo {
    const char* key;     // "neutral", "tokyo_night"
    const char* name;    // "Tokyo Night"
    const char* source;  // where the colours come from
    const char* license; // its licence (THIRD_PARTY_NOTICES.md)
    Palette palette;
};

// Forge Terminal's schemes, Neutral first (the default since 0.10.9; Tokyo Night was before).
const SchemeInfo* schemes(int& count);
const SchemeInfo* findScheme(std::string_view key); // nullptr when unknown

// Phosphor's two screens and Blueprint's paper.
Palette phosphorPalette(bool amber);
Palette blueprintPalette();

// "#7aa2f7" or "7aa2f7" -> 0x7aa2f7.
bool parseHex(std::string_view text, unsigned& out);

// An Omarchy theme's colours. `colorsToml` is its colors.toml (accent, background, foreground, ... = "#rrggbb");
// `alacrittyToml` its alacritty.toml, used for what colors.toml lacks (older themes have only that). Either may
// be empty. False when neither gives at least a background and a foreground.
bool paletteFromOmarchy(const std::string& colorsToml, const std::string& alacrittyToml, Palette& out);

// The current Omarchy theme folder: ~/.config/omarchy/current/theme or, on newer Omarchy, ~/.local/state/omarchy/
// current/theme; "" when neither exists (or not on Linux). Only ever read.
std::string omarchyThemeDir();
// Reads the current Omarchy theme (never writes anything). `name` gets its name when known.
bool readOmarchyTheme(Palette& out, std::string& name, std::string& error);

} // namespace sf::looks
