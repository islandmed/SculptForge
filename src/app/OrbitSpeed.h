// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mouse and trackpad orbit speed (Preferences > Input). The pen has its own View speed (Preferences > Tablet).
#pragma once

#include <algorithm>
#include <cmath>

namespace sf {

constexpr float kMouseOrbitSpeedMin = 0.1f, kMouseOrbitSpeedMax = 2.0f;

// UiPrefs::mouseOrbitSpeed: 1 is the orbit as it always was (0.008 radians per pixel).
inline float clampMouseOrbitSpeed(float v) {
    return std::isfinite(v) ? std::clamp(v, kMouseOrbitSpeedMin, kMouseOrbitSpeedMax) : 1.0f;
}

} // namespace sf
