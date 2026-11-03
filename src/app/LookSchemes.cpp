// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/LookSchemes.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace sf::looks {

namespace {

constexpr const char* kLookKeys[] = {"classic", "forge", "phosphor", "blueprint"};
constexpr const char* kLookNames[] = {"Classic", "Forge Terminal", "Phosphor", "Blueprint"};

// crust, mantle, base, surface0, surface1, surface2, overlay, subtext, text, bright, accent, accent2, red, green,
// yellow. Values from each scheme's published palette; crust and mantle are darkened from its background where the
// scheme has fewer background steps.
const SchemeInfo kSchemes[] = {
    // SculptForge's own (0.10.9, the default): neutral greys, so the model's colours read true, and one accent,
    // the logo's orange. accent2 stays grey: one hue in the whole interface.
    {"neutral", "Neutral", "SculptForge (original)", "GPL-3.0-or-later",
     {0x111111, 0x191919, 0x202020, 0x2b2b2b, 0x363636, 0x494949, 0x777777, 0xaaaaaa, 0xd7d7d7, 0xf2f2f2, 0xf0882e,
      0xaaaaaa, 0xe5604a, 0x8cbf6a, 0xe0b050, false}},
    {"tokyo_night", "Tokyo Night", "github.com/enkia/tokyo-night-vscode-theme", "MIT",
     {0x101118, 0x16161e, 0x1a1b26, 0x232535, 0x292e42, 0x414868, 0x6a73a0, 0xa9b1d6, 0xc0caf5, 0xdfe5ff, 0x7aa2f7,
      0xbb9af7, 0xf7768e, 0x9ece6a, 0xe0af68, false}},
    {"catppuccin_mocha", "Catppuccin Mocha", "github.com/catppuccin/catppuccin", "MIT",
     {0x11111b, 0x181825, 0x1e1e2e, 0x313244, 0x45475a, 0x585b70, 0x7f849c, 0xbac2de, 0xcdd6f4, 0xeef1ff, 0xcba6f7,
      0x89b4fa, 0xf38ba8, 0xa6e3a1, 0xf9e2af, false}},
    {"gruvbox_dark", "Gruvbox Dark", "github.com/morhetz/gruvbox", "MIT",
     {0x141617, 0x1d2021, 0x282828, 0x3c3836, 0x504945, 0x665c54, 0x928374, 0xbdae93, 0xebdbb2, 0xfbf1c7, 0xfabd2f,
      0x8ec07c, 0xfb4934, 0xb8bb26, 0xfabd2f, false}},
    {"nord", "Nord", "github.com/nordtheme/nord", "MIT",
     {0x20242c, 0x272c36, 0x2e3440, 0x3b4252, 0x434c5e, 0x4c566a, 0x7b88a1, 0xd8dee9, 0xe5e9f0, 0xf4f6fa, 0x88c0d0,
      0x81a1c1, 0xbf616a, 0xa3be8c, 0xebcb8b, false}},
    {"rose_pine", "Rosé Pine", "github.com/rose-pine/rose-pine-theme", "MIT",
     {0x0f0d16, 0x15131e, 0x191724, 0x1f1d2e, 0x26233a, 0x403d52, 0x6e6a86, 0x908caa, 0xe0def4, 0xf4f2ff, 0xebbcba,
      0x9ccfd8, 0xeb6f92, 0x9ccfd8, 0xf6c177, false}},
    {"kanagawa", "Kanagawa", "github.com/rebelot/kanagawa.nvim", "MIT",
     {0x16161d, 0x1a1a22, 0x1f1f28, 0x2a2a37, 0x363646, 0x54546d, 0x727169, 0xc8c093, 0xdcd7ba, 0xf2eedb, 0x7fb4ca,
      0xe6c384, 0xe46876, 0x98bb6c, 0xe6c384, false}},
    {"everforest", "Everforest", "github.com/sainnhe/everforest", "MIT",
     {0x1e2326, 0x232a2e, 0x2d353b, 0x343f44, 0x3d484d, 0x475258, 0x859289, 0x9da9a0, 0xd3c6aa, 0xede3cb, 0xa7c080,
      0x83c092, 0xe67e80, 0xa7c080, 0xdbbc7f, false}},
};

unsigned mix(unsigned a, unsigned b, float t) {
    auto ch = [&](int shift) {
        const float x = float((a >> shift) & 0xff), y = float((b >> shift) & 0xff);
        return unsigned(x + (y - x) * t + 0.5f) & 0xff;
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

float luminance(unsigned c) {
    return (0.2126f * float((c >> 16) & 0xff) + 0.7152f * float((c >> 8) & 0xff) + 0.0722f * float(c & 0xff)) / 255.0f;
}

std::string trim(std::string s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// "section.key" -> "#rrggbb" for every `key = "#rrggbb"` (or '#rrggbb' or 0xrrggbb) line of a TOML file.
std::map<std::string, unsigned> tomlColours(const std::string& text) {
    std::map<std::string, unsigned> out;
    std::istringstream in(text);
    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (const size_t hash = value.find(" #"); hash != std::string::npos) value = trim(value.substr(0, hash)); // comment
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')) value = value.substr(1, value.size() - 2);
        if (value.rfind("0x", 0) == 0) value = value.substr(2);
        unsigned c = 0;
        if (parseHex(value, c)) out[section.empty() ? key : section + "." + key] = c;
    }
    return out;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

} // namespace

const char* lookKey(LookId look) {
    const int i = int(look);
    return i >= 0 && i < int(LookId::Count) ? kLookKeys[i] : kLookKeys[0];
}

const char* lookName(LookId look) {
    const int i = int(look);
    return i >= 0 && i < int(LookId::Count) ? kLookNames[i] : kLookNames[0];
}

LookId lookFromKey(std::string_view key) {
    for (int i = 0; i < int(LookId::Count); ++i)
        if (key == kLookKeys[i]) return LookId(i);
    if (key == "forge_terminal") return LookId::ForgeTerminal;
    return LookId::Classic;
}

const SchemeInfo* schemes(int& count) {
    count = int(std::size(kSchemes));
    return kSchemes;
}

const SchemeInfo* findScheme(std::string_view key) {
    for (const SchemeInfo& s : kSchemes)
        if (key == s.key) return &s;
    return nullptr;
}

Palette phosphorPalette(bool amber) {
    // A P1 green or P3 amber phosphor on a near-black tube: every step is the phosphor colour at another brightness.
    if (amber)
        return {0x0a0602, 0x0f0a04, 0x140d05, 0x22170a, 0x30210e, 0x4a3314, 0x9a6a2c, 0xd39a48, 0xffb84d, 0xffe0a3,
                0xffc35c, 0xffe0a3, 0xff6a4d, 0xffc35c, 0xffd27a, false};
    return {0x030805, 0x060d08, 0x08120b, 0x0f2216, 0x15311f, 0x1f4a2e, 0x3f9a5c, 0x63d685, 0x7cf7a0, 0xc8ffd8,
            0x5dff8a, 0xc8ffd8, 0xff6b5d, 0x5dff8a, 0xd8ff6b, false};
}

Palette blueprintPalette() {
    // Cyanotype paper: deep Prussian blue, white and cyan line work.
    return {0x061833, 0x0a2142, 0x0c2750, 0x113463, 0x17427a, 0x2a5d9a, 0x6b93c4, 0xa9c4e6, 0xdbe8fa, 0xffffff,
            0x6fe3ff, 0xffffff, 0xff7a85, 0x7dffc4, 0xffe27a, false};
}

bool parseHex(std::string_view text, unsigned& out) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() == 8) text = text.substr(0, 6); // #rrggbbaa: drop the alpha
    if (text.size() != 6) return false;
    unsigned v = 0;
    for (char c : text) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= unsigned(c - '0');
        else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
        else return false;
    }
    out = v;
    return true;
}

bool paletteFromOmarchy(const std::string& colorsToml, const std::string& alacrittyToml, Palette& out) {
    const auto c = tomlColours(colorsToml);
    const auto a = tomlColours(alacrittyToml);
    auto pick = [&](std::initializer_list<const char*> keys, unsigned& v) {
        for (const char* k : keys) {
            if (auto it = c.find(k); it != c.end()) return v = it->second, true;
            if (auto it = a.find(k); it != a.end()) return v = it->second, true;
        }
        return false;
    };
    Palette p;
    if (!pick({"background", "colors.primary.background"}, p.base)) return false;
    if (!pick({"foreground", "colors.primary.foreground"}, p.text)) return false;
    p.light = luminance(p.base) > 0.5f; // (colors.toml's mode = "light" says the same)
    const unsigned toward = p.light ? 0xffffff : 0x000000; // "darker" backgrounds of a light theme get lighter
    if (!pick({"dark_background"}, p.mantle)) p.mantle = mix(p.base, toward, 0.18f);
    if (!pick({"darker_background"}, p.crust)) p.crust = mix(p.base, toward, 0.36f);
    if (!pick({"lighter_background"}, p.surface0)) p.surface0 = mix(p.base, p.text, 0.08f);
    if (!pick({"selection", "colors.selection.background"}, p.surface1)) p.surface1 = mix(p.base, p.text, 0.14f);
    if (!pick({"muted", "colors.bright.black"}, p.surface2)) p.surface2 = mix(p.base, p.text, 0.26f);
    if (!pick({"dark_foreground"}, p.overlay)) p.overlay = mix(p.base, p.text, 0.5f);
    p.subtext = mix(p.text, p.base, 0.2f);
    if (!pick({"bright_foreground", "light_foreground"}, p.bright)) p.bright = mix(p.text, p.light ? 0x000000 : 0xffffff, 0.3f);
    if (!pick({"accent", "colors.normal.blue", "blue"}, p.accent)) p.accent = mix(p.text, p.base, 0.1f);
    if (!pick({"magenta", "colors.normal.magenta"}, p.accent2)) p.accent2 = p.accent;
    if (!pick({"red", "colors.normal.red"}, p.red)) p.red = 0xe5604a;
    if (!pick({"green", "colors.normal.green"}, p.green)) p.green = 0x9ece6a;
    if (!pick({"yellow", "colors.normal.yellow"}, p.yellow)) p.yellow = 0xe0af68;
    // Themes whose accent is their foreground (Kanagawa's) would show the current tool no differently from text.
    if (p.accent == p.text) pick({"blue", "colors.normal.blue", "cyan"}, p.accent);
    out = p;
    return true;
}

std::string omarchyThemeDir() {
#if defined(__linux__)
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    std::error_code ec;
    for (const char* rel : {"/.config/omarchy/current/theme", "/.local/state/omarchy/current/theme"}) {
        const std::filesystem::path p = std::filesystem::path(home).concat(rel);
        if (std::filesystem::is_directory(p, ec)) return p.string();
    }
#endif
    return {};
}

bool readOmarchyTheme(Palette& out, std::string& name, std::string& error) {
    const std::string dir = omarchyThemeDir();
    if (dir.empty()) {
        error = "Match my desktop theme: no Omarchy theme found (~/.config/omarchy/current/theme)";
        return false;
    }
    const std::filesystem::path d(dir);
    const std::string colors = readFile(d / "colors.toml"), alacritty = readFile(d / "alacritty.toml");
    if (!paletteFromOmarchy(colors, alacritty, out)) {
        error = "Match my desktop theme: the Omarchy theme has no colours SculptForge can read (colors.toml or alacritty.toml)";
        return false;
    }
    name = trim(readFile(d.parent_path() / "theme.name"));
    if (name.empty()) {
        std::error_code ec;
        const auto target = std::filesystem::read_symlink(d, ec); // older Omarchy: a link to the theme's folder
        name = ec ? std::string("Omarchy") : target.filename().string();
    }
    return true;
}

} // namespace sf::looks
