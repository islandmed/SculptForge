// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TouchGesture.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {
bool finite(glm::vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
} // namespace

void TouchGesture::down(uint64_t id, glm::vec2 p) {
    if (!finite(p)) return;
    for (Finger& f : fingers_)
        if (f.id == id) {
            f.p = p;
            return;
        }
    if (fingers_.size() >= kMaxFingers) return;
    fingers_.push_back({id, p});
    gesture_ = gesture_ || fingers_.size() >= 2;
}

bool TouchGesture::move(uint64_t id, glm::vec2 p, TwoFingerStep& step) {
    if (!finite(p)) return false;
    const auto it = std::find_if(fingers_.begin(), fingers_.end(), [id](const Finger& f) { return f.id == id; });
    if (it == fingers_.end()) return false;
    const size_t index = size_t(it - fingers_.begin());
    if (fingers_.size() < 2 || index > 1) { // (a third finger resting doesn't steer)
        it->p = p;
        return false;
    }
    const glm::vec2 a0 = fingers_[0].p, b0 = fingers_[1].p;
    it->p = p;
    const glm::vec2 a1 = fingers_[0].p, b1 = fingers_[1].p;
    step = TwoFingerStep();
    step.pan = (a1 + b1) * 0.5f - (a0 + b0) * 0.5f;
    const glm::vec2 d0 = b0 - a0, d1 = b1 - a1;
    const float l0 = glm::length(d0), l1 = glm::length(d1);
    constexpr float kMinSpan = 1e-4f; // two fingers on the same spot: no pinch or twist to measure
    if (l0 > kMinSpan && l1 > kMinSpan) {
        step.scale = l1 / l0;
        float a = std::atan2(d1.y, d1.x) - std::atan2(d0.y, d0.x);
        constexpr float kPi = 3.14159265358979f;
        if (a > kPi) a -= 2.0f * kPi;
        if (a < -kPi) a += 2.0f * kPi;
        step.angle = a;
    }
    return true;
}

void TouchGesture::up(uint64_t id) {
    fingers_.erase(std::remove_if(fingers_.begin(), fingers_.end(), [id](const Finger& f) { return f.id == id; }),
                   fingers_.end());
    if (fingers_.empty()) gesture_ = false;
}

void TouchGesture::clear() {
    fingers_.clear();
    gesture_ = false;
}

float pinchZoomSteps(float scale) {
    if (!std::isfinite(scale) || !(scale > 0.0f)) return 0.0f;
    return std::log(scale) / -std::log(0.88f); // Camera::zoom: distance * 0.88^steps = distance / scale
}

} // namespace sf
