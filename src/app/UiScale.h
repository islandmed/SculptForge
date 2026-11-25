// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI scale (Preferences > Interface > UI scale, #87): what the fonts and the style are built at. Pure functions,
// so sculptforge_tests checks them without a window.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace sf {

// The scale "Automatic" follows: the display's content scale over its pixel density. Pixel density (Retina, a scaled
// Wayland output) is already covered by the framebuffer scale, so only the rest scales the UI. Windows (per-monitor
// DPI v2) reports 1.5 / 1 at 150 %, macOS 2 / 2 on Retina.
inline float automaticUiScale(float displayScale, float pixelDensity) {
    float s = displayScale / (pixelDensity > 0.0f ? pixelDensity : 1.0f);
#ifdef __ANDROID__
    // Fingers, not a mouse: the desktop layout half again as large, so its rows and buttons are about 30 dp tall
    // (Android's display scale alone makes them 20 dp). The tablet layout (M2) sizes its own touch targets.
    s *= 1.5f;
#endif
    return s > 0.0f && std::isfinite(s) ? s : 1.0f;
}

// The scales Preferences offers: 70 % (a 4K or 5K monitor at its native resolution, the system scaling the UI too large;
// Fenix 11-22) to 300 %. Below 70 % the 10.5 px captions fall under 8 px and stop reading.
constexpr float kMinUiScale = 0.7f, kMaxUiScale = 3.0f;

// The scale the UI is built at: the user's choice (0 = Automatic), kept to 70-300 %.
inline float resolveUiScale(float preference, float automatic) {
    return std::clamp(preference > 0.0f ? preference : automatic, kMinUiScale, kMaxUiScale);
}

// The slider's steps: whole 5 % (a dragged 73 lands on 75; a typed value is kept as typed, clamped).
inline float snapUiScale(float scale) { return std::clamp(std::round(scale * 20.0f) / 20.0f, kMinUiScale, kMaxUiScale); }

// What the paddings, scrollbars and slider grabs shrink by below 100 %: text shrinks with the scale, the room around it
// only by its square root (70 % text keeps 84 % padding), so buttons and grabs stay easy to hit with a mouse or pen.
// At and above 100 % everything scales alike, as before.
inline float compactPadScale(float uiScale) { return uiScale >= 1.0f ? uiScale : std::sqrt(std::max(uiScale, 0.01f)); }

// Remembered per monitor (Preferences > Interface > UI scale): the chosen scale for each display the window has been
// on, by the display's name and pixel size (two identical monitors share a name; the size tells a 4K from a 1080p
// one). A display without an entry uses the general choice; an entry of 0 is Automatic on that display.
using DisplayScales = std::vector<std::pair<std::string, float>>;

inline std::string displayScaleKey(const char* name, int pixelW, int pixelH) {
    return std::string(name && *name ? name : "display") + " " + std::to_string(pixelW) + "x" + std::to_string(pixelH);
}

inline float displayScaleFor(const DisplayScales& scales, const std::string& key, float fallback) {
    for (const auto& [k, v] : scales)
        if (k == key) return v;
    return fallback;
}

inline bool hasDisplayScale(const DisplayScales& scales, const std::string& key) {
    for (const auto& [k, v] : scales)
        if (k == key) return true;
    return false;
}

// Sets (or adds) the entry; keeps the list to the last 16 displays so a laptop that met many projectors stays tidy.
inline void setDisplayScale(DisplayScales& scales, const std::string& key, float scale) {
    for (auto& [k, v] : scales)
        if (k == key) {
            v = scale;
            return;
        }
    if (scales.size() >= 16) scales.erase(scales.begin());
    scales.emplace_back(key, scale);
}

// True when the UI must be rebuilt to show `target` (fonts, style and layout all at the new scale); a display
// reporting 1.2499 after 1.25, or a repeated event, rebuilds nothing.
inline bool uiScaleChanged(float current, float target) { return std::abs(current - target) >= 0.005f; }

// How many framebuffer pixels per logical pixel the fonts are rasterised at: the window's pixel density, so text and
// icons are crisp on Retina and scaled Wayland outputs (a 5K display drew a 1x atlas stretched 2x: soft text, icons off,
// fenix 11-08). With the UI scale kept to 3x the 100 % size: 300 % alone already makes a 4096x16384 atlas (in Chinese
// too), a GPU's usual texture limit, so a 2x display gets 2x up to 150 %, 1.5x at 200 % and 1x at 300 %.
inline float fontRasterDensity(float uiScale, float pixelDensity, float maxTotal = 3.0f) {
    if (!(pixelDensity > 1.0f) || !std::isfinite(pixelDensity) || !(uiScale > 0.0f)) return 1.0f;
    return std::clamp(std::min(pixelDensity, 4.0f), 1.0f, std::max(1.0f, maxTotal / uiScale));
}

} // namespace sf
