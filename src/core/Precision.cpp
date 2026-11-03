// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Precision.h"

#include <algorithm>
#include <cmath>

namespace sf {

float clampDrawSize(float px) {
    return std::isfinite(px) ? std::clamp(px, kMinDrawSizePx, kMaxDrawSizePx) : 60.0f;
}

float drawSizeStep(float px, int direction, bool fine) {
    if (!fine) return clampDrawSize(direction < 0 ? px / 1.1f : px * 1.1f);
    // One pixel, half a pixel at the small end; a size between steps lands on the next whole (or half) pixel.
    const float step = (direction < 0 ? px <= 2.0f : px < 2.0f) ? 0.5f : 1.0f;
    const float snapped = direction < 0 ? std::ceil(px / step - 1e-3f) * step : std::floor(px / step + 1e-3f) * step;
    return clampDrawSize(snapped + float(direction < 0 ? -1 : 1) * step);
}

float drawSizeScrub(float px, float dx) { return clampDrawSize(px * std::exp(dx * 0.004f)); }

float dabSpacingPx(float radiusPx, float spacingFraction) {
    const float r = std::max(radiusPx, kMinDrawSizePx);
    return std::max(std::min(1.0f, r * 0.5f), r * spacingFraction);
}

glm::vec2 PrecisionDrag::follow(glm::vec2 cursor, bool slow) {
    const glm::vec2 d = cursor - cursor_;
    cursor_ = cursor;
    if (slow) {
        point += d * speed;
        return point;
    }
    point += d;
    // Let go: close the gap by up to the pen's own motion, so the brush glides back under the pen.
    const glm::vec2 gap = cursor - point;
    const float len = glm::length(gap);
    if (len > 0.0f) point += gap * (std::min(len, glm::length(d)) / len);
    return point;
}

float PickSmoother::filter(float distance, float jump) {
    const float predicted = last_ + velocity_;
    if (!have_ || !(amount > 0.0f) || !(std::abs(distance - predicted) <= jump)) {
        last_ = distance;
        velocity_ = 0.0f;
        have_ = true;
        return distance;
    }
    const float residual = distance - predicted;
    const float alpha = 1.0f - std::clamp(amount, 0.0f, 0.9f);
    last_ = predicted + alpha * residual;
    velocity_ += 0.2f * alpha * residual;
    return last_;
}

// ---------------------------------------------------------------------------
// PressurePoints
// ---------------------------------------------------------------------------

namespace {
float clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }
constexpr float kMinGap = 0.04f; // between points' x
} // namespace

void PressurePoints::sort() {
    count = std::clamp(count, 2, kMaxPoints);
    for (int i = 0; i < count; ++i) points[size_t(i)] = {clamp01(points[size_t(i)].x), clamp01(points[size_t(i)].y)};
    std::sort(points.begin() + 1, points.begin() + count - 1, [](glm::vec2 a, glm::vec2 b) { return a.x < b.x; });
    points[0].x = 0.0f;
    points[size_t(count - 1)].x = 1.0f;
    for (int i = 1; i < count - 1; ++i) // inner points keep apart from their neighbours
        points[size_t(i)].x = std::clamp(points[size_t(i)].x, points[size_t(i - 1)].x + kMinGap, 1.0f - kMinGap * float(count - 1 - i));
    for (int i = 1; i < count; ++i) // pressing harder never gives less
        points[size_t(i)].y = std::max(points[size_t(i)].y, points[size_t(i - 1)].y);
}

float PressurePoints::apply(float pressure) const {
    const float p = clamp01(pressure);
    const int n = std::clamp(count, 2, kMaxPoints);
    int k = 0;
    while (k < n - 2 && p > points[size_t(k + 1)].x) ++k;
    // Fritsch-Carlson monotone cubic Hermite: tangents from the neighbouring slopes, limited so nothing overshoots.
    auto slope = [&](int i) {
        const glm::vec2 a = points[size_t(i)], b = points[size_t(i + 1)];
        return (b.y - a.y) / std::max(b.x - a.x, 1e-4f);
    };
    auto tangent = [&](int i) {
        if (i == 0) return slope(0);
        if (i == n - 1) return slope(n - 2);
        const float s0 = slope(i - 1), s1 = slope(i);
        if (s0 * s1 <= 0.0f) return 0.0f;
        return 2.0f / (1.0f / s0 + 1.0f / s1); // harmonic mean: monotone
    };
    const glm::vec2 a = points[size_t(k)], b = points[size_t(k + 1)];
    const float h = std::max(b.x - a.x, 1e-4f);
    const float t = std::clamp((p - a.x) / h, 0.0f, 1.0f);
    const float m0 = tangent(k) * h, m1 = tangent(k + 1) * h;
    const float t2 = t * t, t3 = t2 * t;
    const float y = (2 * t3 - 3 * t2 + 1) * a.y + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * b.y + (t3 - t2) * m1;
    return std::clamp(y, std::min(a.y, b.y), std::max(a.y, b.y));
}

bool PressurePoints::isLinear() const {
    for (int i = 0; i < count; ++i)
        if (std::abs(points[size_t(i)].y - points[size_t(i)].x) > 1e-3f) return false;
    return true;
}

bool PressurePoints::addPoint(float x) {
    if (count >= kMaxPoints) return false;
    x = clamp01(x);
    for (int i = 0; i < count; ++i)
        if (std::abs(points[size_t(i)].x - x) < kMinGap) return false;
    const float y = apply(x);
    points[size_t(count - 1) + 1] = points[size_t(count - 1)]; // the end moves up one
    points[size_t(count - 1)] = {x, y};
    ++count;
    sort();
    return true;
}

bool PressurePoints::removePoint(int i) {
    if (i <= 0 || i >= count - 1) return false;
    for (int j = i; j < count - 1; ++j) points[size_t(j)] = points[size_t(j + 1)];
    --count;
    return true;
}

bool PressurePoints::operator==(const PressurePoints& o) const {
    if (count != o.count) return false;
    for (int i = 0; i < count; ++i)
        if (glm::length(points[size_t(i)] - o.points[size_t(i)]) > 1e-4f) return false;
    return true;
}

Json PressurePoints::toJson() const {
    Json a = Json::array();
    for (int i = 0; i < count; ++i) {
        Json p = Json::array();
        p.push(double(points[size_t(i)].x));
        p.push(double(points[size_t(i)].y));
        a.push(p);
    }
    return a;
}

PressurePoints PressurePoints::fromJson(const Json& j) {
    PressurePoints c;
    if (!j.isArray() || j.size() < 2 || j.size() > size_t(kMaxPoints)) return c;
    c.count = int(j.size());
    for (size_t i = 0; i < j.size(); ++i) {
        const Json& p = j[i];
        if (!p.isArray() || p.size() != 2) return PressurePoints();
        c.points[i] = {float(p[0].asNumber(double(i) / double(j.size() - 1))), float(p[1].asNumber(0.0))};
    }
    c.sort();
    return c;
}

PressurePoints PressurePoints::linear() { return {}; }

PressurePoints PressurePoints::soft() {
    PressurePoints c;
    c.points = {glm::vec2(0.0f, 0.0f), glm::vec2(0.3f, 0.55f), glm::vec2(1.0f, 1.0f)};
    c.count = 3;
    return c;
}

PressurePoints PressurePoints::firm() {
    PressurePoints c;
    c.points = {glm::vec2(0.0f, 0.0f), glm::vec2(0.6f, 0.3f), glm::vec2(1.0f, 1.0f)};
    c.count = 3;
    return c;
}

PressurePoints PressurePoints::fineDetail() {
    PressurePoints c;
    c.points = {glm::vec2(0.0f, 0.0f), glm::vec2(0.45f, 0.1f), glm::vec2(0.8f, 0.4f), glm::vec2(1.0f, 0.75f)};
    c.count = 4;
    return c;
}

const std::array<PressurePoints::Preset, 4>& PressurePoints::presets() {
    static const std::array<Preset, 4> kPresets{{{"Soft", &PressurePoints::soft},
                                                 {"Linear", &PressurePoints::linear},
                                                 {"Firm", &PressurePoints::firm},
                                                 {"Fine detail", &PressurePoints::fineDetail}}};
    return kPresets;
}

const char* PressurePoints::presetName() const {
    for (const Preset& p : presets())
        if (p.make() == *this) return p.name;
    return nullptr;
}

} // namespace sf
