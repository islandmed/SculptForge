// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Helpers shared by core/Uv.cpp, core/UvPack.cpp and core/UvUnwrap.cpp.
#pragma once

#include "core/Uv.h"

#include <glm/glm.hpp>

#include <numeric>
#include <vector>

namespace sf::uvdetail {

inline float cross2(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

// Union-find over [0, n) with path halving.
struct DisjointSets {
    std::vector<uint32_t> parent;
    explicit DisjointSets(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32_t find(uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    }
    void unite(uint32_t a, uint32_t b) {
        a = find(a), b = find(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
    // Compact labels 0..count-1 per element (in order of first appearance); returns the count.
    uint32_t labels(std::vector<uint32_t>& out) {
        out.assign(parent.size(), ~0u);
        std::vector<uint32_t> id(parent.size(), ~0u);
        uint32_t n = 0;
        for (uint32_t i = 0; i < parent.size(); ++i) {
            const uint32_t r = find(i);
            if (id[r] == ~0u) id[r] = n++;
            out[i] = id[r];
        }
        return n;
    }
};

// Convex hull (counter-clockwise, no repeated first point) of `pts`.
std::vector<glm::vec2> convexHull(std::vector<glm::vec2> pts);
// The rotation (radians) that turns `pts` to their smallest-area bounding box, with the long side along u.
float minAreaRectAngle(const std::vector<glm::vec2>& pts);
inline glm::vec2 rotate(glm::vec2 p, float c, float s) { return {c * p.x - s * p.y, s * p.x + c * p.y}; }

// The corners of each island, grouped: corners of island i are [offsets[i], offsets[i + 1]) of `corners`.
void islandCorners(const UvTopology& topo, std::vector<uint32_t>& offsets, std::vector<uint32_t>& corners);

} // namespace sf::uvdetail
