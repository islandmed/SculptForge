// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI scale (Preferences > Interface > UI scale, #87): what the fonts and the style are built at. Pure functions,
// so sculptforge_tests checks them without a window.
#pragma once

#include <algorithm>
#include <cmath>

namespace sf {

// The scale "Automatic" follows: the display's content scale over its pixel density. Pixel density (Retina, a scaled
// Wayland output) is already covered by the framebuffer scale, so only the rest scales the UI. Windows (per-monitor
// DPI v2) reports 1.5 / 1 at 150 %, macOS 2 / 2 on Retina.
inline float automaticUiScale(float displayScale, float pixelDensity) {
    const float s = displayScale / (pixelDensity > 0.0f ? pixelDensity : 1.0f);
    return s > 0.0f && std::isfinite(s) ? s : 1.0f;
}

// The scale the UI is built at: the user's choice (0 = Automatic), kept to 50-300 %.
inline float resolveUiScale(float preference, float automatic) {
    return std::clamp(preference > 0.0f ? preference : automatic, 0.5f, 3.0f);
}

// True when the UI must be rebuilt to show `target` (fonts, style and layout all at the new scale); a display
// reporting 1.2499 after 1.25, or a repeated event, rebuilds nothing.
inline bool uiScaleChanged(float current, float target) { return std::abs(current - target) >= 0.005f; }

} // namespace sf
