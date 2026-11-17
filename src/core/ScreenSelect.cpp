// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ScreenSelect.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace sf {

ScreenPolygon::ScreenPolygon(std::vector<glm::vec2> points) : points_(std::move(points)) {
    if (points_.size() < 3) return;
    lo_ = hi_ = points_[0];
    for (const glm::vec2& p : points_) {
        lo_ = glm::min(lo_, p);
        hi_ = glm::max(hi_, p);
    }
    if (!(hi_.x > lo_.x && hi_.y > lo_.y)) return;
    points_.push_back(points_[0]); // closed: the last edge returns to the start
    const size_t edges = points_.size() - 1;
    bands_.resize(std::clamp<size_t>(edges / 2, 1, 512));
    bandHeight_ = (hi_.y - lo_.y) / float(bands_.size());
    auto band = [&](float y) { return std::min(size_t(std::max(0.0f, (y - lo_.y) / bandHeight_)), bands_.size() - 1); };
    for (size_t i = 0; i < edges; ++i) {
        const float y0 = points_[i].y, y1 = points_[i + 1].y;
        for (size_t b = band(std::min(y0, y1)); b <= band(std::max(y0, y1)); ++b) bands_[b].push_back(uint32_t(i));
    }
    valid_ = true;
}

bool ScreenPolygon::contains(glm::vec2 p) const {
    if (!valid_ || p.x < lo_.x || p.x > hi_.x || p.y < lo_.y || p.y > hi_.y) return false;
    const size_t b = std::min(size_t((p.y - lo_.y) / bandHeight_), bands_.size() - 1);
    bool inside = false;
    for (uint32_t i : bands_[b]) {
        const glm::vec2 a = points_[i], c = points_[i + 1];
        if ((a.y > p.y) == (c.y > p.y)) continue; // the edge doesn't straddle the horizontal through p
        const float x = a.x + (p.y - a.y) / (c.y - a.y) * (c.x - a.x);
        if (p.x < x) inside = !inside;
    }
    return inside;
}

DragEllipse dragEllipse(glm::vec2 press, glm::vec2 cursor, bool fromCorner, bool round) {
    glm::vec2 d = cursor - press;
    DragEllipse e;
    if (fromCorner) {
        if (round) { // the square the longer side makes, toward the cursor
            const float side = std::max(std::abs(d.x), std::abs(d.y));
            d = glm::vec2(d.x < 0.0f ? -side : side, d.y < 0.0f ? -side : side);
        }
        e.centre = press + d * 0.5f;
        e.radius = glm::abs(d) * 0.5f;
    } else {
        e.centre = press;
        e.radius = round ? glm::vec2(glm::length(d)) : glm::abs(d);
    }
    return e;
}

std::vector<glm::vec2> ellipsePolygon(const DragEllipse& e, int segments) {
    segments = std::max(segments, 3);
    std::vector<glm::vec2> out;
    out.reserve(size_t(segments));
    for (int i = 0; i < segments; ++i) {
        const float a = float(i) / float(segments) * 6.28318530718f;
        out.push_back(e.centre + glm::vec2(std::cos(a), std::sin(a)) * e.radius);
    }
    return out;
}

bool projectToNdc(const glm::mat4& viewProj, const glm::vec3& p, glm::vec2& ndc) {
    const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
    if (clip.w <= 1e-8f) return false;
    ndc = glm::vec2(clip) / clip.w;
    return true;
}

glm::mat4 frontViewProj(const Mesh& m) {
    const AABB b = m.bounds();
    const glm::vec3 half = glm::max(b.extent() * 0.5f, glm::vec3(1e-6f));
    const float depth = glm::length(b.extent()) + 1.0f;
    const glm::vec3 eye = b.center() + glm::vec3(0, 0, depth);
    return glm::ortho(-half.x, half.x, -half.y, half.y, 0.01f, depth * 2.0f) *
           glm::lookAt(eye, b.center(), glm::vec3(0, 1, 0));
}

bool viewProjArg(const Mesh& mesh, const Args& args, glm::mat4& out, std::string& error) {
    out = frontViewProj(mesh);
    const std::string matrix = argString(args, "view_proj");
    if (matrix.empty()) return true;
    std::istringstream s(matrix);
    for (int i = 0; i < 16; ++i) {
        if (!(s >> out[i / 4][i % 4])) {
            error = "view_proj needs 16 numbers";
            return false;
        }
    }
    return true;
}

bool lassoArg(const Args& args, std::vector<glm::vec2>& out, std::string& error) {
    out.clear();
    std::istringstream s(argString(args, "points"));
    glm::vec2 p;
    while (s >> p.x >> p.y) out.push_back(p);
    if (out.size() < 3 || !s.eof()) {
        error = "points needs at least 3 x y pairs (-1..1, y up)";
        return false;
    }
    return true;
}

} // namespace sf
