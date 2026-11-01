// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Stroke.h"

#include <algorithm>
#include <cmath>

namespace sf {

const char* strokeTypeName(StrokeType type) {
    switch (type) {
    case StrokeType::Dots: return "Dots";
    case StrokeType::DragRect: return "Drag Stamp";
    case StrokeType::DragDot: return "Slide Stamp";
    case StrokeType::Spray: return "Spray";
    case StrokeType::LazyMouse: return "Lazy Stroke";
    default: return "?";
    }
}

void StrokeSpacer::reset(glm::vec2 start) {
    last_ = start;
    residual_ = 0.0f;
}

void StrokeSpacer::moveTo(glm::vec2 to, float spacing, std::vector<glm::vec2>& out) {
    spacing = std::max(spacing, 1e-3f);
    const glm::vec2 from = last_;
    const float len = glm::length(to - from);
    float travelled = spacing - residual_;
    while (travelled <= len) {
        out.push_back(from + (to - from) * (travelled / len));
        travelled += spacing;
    }
    residual_ = len - (travelled - spacing);
    last_ = to;
}

glm::vec2 LazyMouse::follow(glm::vec2 cursor) {
    const glm::vec2 d = cursor - point;
    const float len = glm::length(d);
    if (len > radius) point += d * ((len - radius) / len);
    return point;
}

float Spray::uniform() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return float(state_ & 0xffffffu) / float(0xffffff);
}

SprayDab Spray::next(float radiusPx, float scatter) {
    SprayDab d;
    const float a = uniform() * 6.2831853f;
    const float r = std::sqrt(uniform()) * scatter * radiusPx;
    d.offset = glm::vec2(std::cos(a), std::sin(a)) * r;
    d.scale = 0.5f + 0.5f * uniform();
    d.strength = 0.5f + 0.5f * uniform();
    d.angle = uniform() * 6.2831853f;
    return d;
}

} // namespace sf
