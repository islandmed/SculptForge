// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/QuickRemesh.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <span>
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

namespace {
template <class Next> float medianEdge(const Mesh& mesh, size_t count, Next next) {
    const size_t nv = mesh.positions.size(), nt = mesh.triangleCount();
    std::vector<float> lengths;
    lengths.reserve(3 * count);
    for (size_t i = 0; i < count; ++i) {
        const size_t t = next(i);
        if (t >= nt) continue;
        const glm::uvec3 tri = mesh.triangles[t];
        if (tri.x >= nv || tri.y >= nv || tri.z >= nv) continue;
        for (int c = 0; c < 3; ++c) {
            const float l = glm::distance(mesh.positions[tri[c]], mesh.positions[tri[(c + 1) % 3]]);
            if (std::isfinite(l) && l > 0.0f) lengths.push_back(l);
        }
    }
    if (lengths.empty()) return 0.0f;
    std::nth_element(lengths.begin(), lengths.begin() + lengths.size() / 2, lengths.end());
    return lengths[lengths.size() / 2];
}
} // namespace

float typicalEdgeLength(const Mesh& mesh) {
    const size_t nt = mesh.triangleCount();
    if (nt == 0) return 0.0f;
    constexpr size_t kSampleTriangles = 100'000;
    const size_t stride = std::max<size_t>(1, nt / kSampleTriangles);
    return medianEdge(mesh, (nt + stride - 1) / stride, [&](size_t i) { return i * stride; });
}

float typicalEdgeLength(const Mesh& mesh, std::span<const uint32_t> triangles) {
    if (triangles.size() < 64) return typicalEdgeLength(mesh);
    return medianEdge(mesh, triangles.size(), [&](size_t i) { return size_t(triangles[i]); });
}

} // namespace sf
