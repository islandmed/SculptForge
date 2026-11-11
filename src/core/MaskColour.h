// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preferences > Interface > Mask colour in ui.json ("maskColor": [r, g, b], display values 0-1). No key means the
// default look (masked areas darkened); a colour that isn't three numbers is ignored. Header-only, so it can be tested.
#pragma once

#include "core/Json.h"

#include <glm/glm.hpp>

#include <algorithm>

namespace sf {

inline Json maskColorToJson(const glm::vec3& c) {
    Json a = Json::array();
    for (int i = 0; i < 3; ++i) a.push(double(c[i]));
    return a;
}

// True and `out` set (clamped) when `j` is a colour; false leaves `out` alone.
inline bool maskColorFromJson(const Json& j, glm::vec3& out) {
    if (j.size() != 3) return false;
    for (int i = 0; i < 3; ++i) out[i] = std::clamp(float(j[size_t(i)].asNumber(out[i])), 0.0f, 1.0f);
    return true;
}

} // namespace sf
