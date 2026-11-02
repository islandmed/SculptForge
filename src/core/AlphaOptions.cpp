// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AlphaOptions.h"

#include <algorithm>
#include <cmath>

namespace sf {

void AlphaOptions::clamp() {
    angle = std::clamp(angle, -180.0f, 180.0f);
    jitter = std::clamp(jitter, 0.0f, 180.0f);
    tileX = std::clamp(tileX, 1, 8);
    tileY = std::clamp(tileY, 1, 8);
}

void AlphaRake::reset(glm::vec2 at) {
    last_ = at;
    angle_ = 0.0f;
    known_ = false;
}

bool AlphaRake::update(glm::vec2 at) {
    const glm::vec2 d = at - last_;
    if (glm::dot(d, d) >= kRakeStep * kRakeStep) {
        // The alpha's top, (-sin a, cos a) with y up, along the motion (d.x, -d.y): a = atan2(-d.x, -d.y).
        angle_ = std::atan2(-d.x, -d.y);
        last_ = at;
        known_ = true;
    }
    return known_;
}

float alphaDabAngle(const AlphaOptions& o, const AlphaRake& rake, uint32_t& seed) {
    float a = glm::radians(o.angle);
    if (o.followStroke) a += rake.angle();
    if (o.jitter > 0.0f) {
        if (seed == 0) seed = 1;
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        const float r = float(seed & 0xffff) / 65535.0f * 2.0f - 1.0f;
        a += glm::radians(o.jitter) * r;
    }
    return a;
}

} // namespace sf
