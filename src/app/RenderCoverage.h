// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// "Is the model drawn?": the check of the render matrix (tests/render_matrix, UiTestRender.cpp) and of the benchmark scenes on the
// real GPU (Bench.cpp, SF_BENCH_RENDER_CHECK=1).
#include "core/Image.h"
#include "render/Camera.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace sf {

struct RenderCoverage {
    double coverage = 0.0;
    int colors = 0;
    int boxW = 0, boxH = 0;
};

// The share of the pixels inside the projected box of `bounds` that differ from the background (the image's corner: the box
// never reaches it), and how many distinct colours (5 bits a channel) they hold.
inline RenderCoverage measureRenderCoverage(const Image& img, const Camera& cam, const AABB& b) {
    RenderCoverage r;
    const glm::mat4 vp = cam.projection() * cam.view();
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < 8; ++k) {
        const glm::vec3 p((k & 1) ? b.max.x : b.min.x, (k & 2) ? b.max.y : b.min.y, (k & 4) ? b.max.z : b.min.z);
        const glm::vec4 h = vp * glm::vec4(p, 1.0f);
        if (h.w <= 0.0f) return r;
        const float x = (h.x / h.w * 0.5f + 0.5f) * float(img.width), y = (1.0f - (h.y / h.w * 0.5f + 0.5f)) * float(img.height);
        x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
    const int ax = std::clamp(int(x0), 0, img.width - 1), bx = std::clamp(int(std::ceil(x1)), 0, img.width),
              ay = std::clamp(int(y0), 0, img.height - 1), by = std::clamp(int(std::ceil(y1)), 0, img.height);
    r.boxW = bx - ax, r.boxH = by - ay;
    if (r.boxW <= 0 || r.boxH <= 0) return r;
    const uint8_t* bg = img.rgba.data();
    std::set<uint32_t> colors;
    size_t differing = 0;
    for (int y = ay; y < by; ++y)
        for (int x = ax; x < bx; ++x) {
            const uint8_t* p = img.rgba.data() + (size_t(y) * size_t(img.width) + size_t(x)) * 4;
            const int d = std::max({std::abs(int(p[0]) - int(bg[0])), std::abs(int(p[1]) - int(bg[1])), std::abs(int(p[2]) - int(bg[2]))});
            if (d <= 6) continue;
            ++differing;
            colors.insert(uint32_t(p[0] >> 3) << 10 | uint32_t(p[1] >> 3) << 5 | uint32_t(p[2] >> 3));
        }
    r.coverage = double(differing) / (double(r.boxW) * double(r.boxH));
    r.colors = int(colors.size());
    return r;
}


// The least share of the box a drawn model covers (a thin wireframe aside, every mode draws a surface).
constexpr double kRenderCoverageFloor = 0.25;

} // namespace sf
