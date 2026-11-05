// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/QuickRemesh.h"

#include <algorithm>
#include <glm/geometric.hpp>
#include <utility>
#include <vector>

namespace sf {

StrokeStretch strokeStretch(const Mesh& mesh, std::span<const uint32_t> moved, std::span<const glm::vec3> before) {
    StrokeStretch s;
    const size_t n = std::min(moved.size(), before.size());
    if (n == 0) return s;
    // The old position of a moved vertex, by binary search (vertices the stroke didn't move are where they were).
    std::vector<std::pair<uint32_t, uint32_t>> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = {moved[i], uint32_t(i)};
    std::sort(order.begin(), order.end());
    const auto find = [&](uint32_t v) -> const glm::vec3* {
        const auto it = std::lower_bound(order.begin(), order.end(), std::make_pair(v, 0u));
        return it != order.end() && it->first == v ? &before[it->second] : nullptr;
    };
    const size_t vertices = mesh.positions.size();
    for (const auto& [v, i] : order) {
        if (v >= vertices) continue;
        for (const uint32_t w : mesh.neighbors(v)) {
            if (w >= vertices) continue;
            const glm::vec3* wBefore = find(w);
            if (wBefore && w < v) continue; // both moved: counted once, from the lower index
            const float old = glm::distance(before[i], wBefore ? *wBefore : mesh.positions[w]);
            if (old <= 1e-12f) continue;
            const float ratio = glm::distance(mesh.positions[v], mesh.positions[w]) / old;
            ++s.edges;
            s.worst = std::max(s.worst, ratio);
            if (ratio > kStretchRatio) ++s.stretched;
        }
    }
    return s;
}

} // namespace sf
