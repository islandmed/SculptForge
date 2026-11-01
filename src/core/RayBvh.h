// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ray BVH for baking (core/Bake.h, core/MeshMaps.h): a binned-SAH tree over a triangle list with a nearest-hit
// query (the hit nearest a target distance along the ray, for cage rays) and an any-hit query (occlusion).
// Queries are const and keep their state on the stack, so any number of threads can cast at once. Unlike the
// brush BVH (core/BVH.h) it is built once and never refitted.
#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sf {

struct RayHitInfo {
    float t = 0, u = 0, v = 0;
    uint32_t tri = 0;
};

class RayBvh {
public:
    void build(const std::vector<glm::vec3>& pos, const std::vector<glm::uvec3>& tris) {
        const size_t n = tris.size();
        nodes_.clear();
        order_.resize(n);
        std::vector<glm::vec3> centroid(n);
        std::vector<Box> box(n);
        for (size_t i = 0; i < n; ++i) {
            order_[i] = uint32_t(i);
            const glm::vec3 &a = pos[tris[i].x], &b = pos[tris[i].y], &c = pos[tris[i].z];
            box[i].lo = glm::min(a, glm::min(b, c));
            box[i].hi = glm::max(a, glm::max(b, c));
            centroid[i] = (box[i].lo + box[i].hi) * 0.5f;
        }
        nodes_.reserve(n ? 2 * n / kLeaf + 16 : 1);
        nodes_.push_back({});
        if (n == 0) {
            nodes_[0].lo = nodes_[0].hi = glm::vec3(0.0f);
            return;
        }
        struct Job {
            uint32_t node, first, count;
        };
        std::vector<Job> stack = {{0, 0, uint32_t(n)}};
        while (!stack.empty()) {
            const Job j = stack.back();
            stack.pop_back();
            Box nb, cb;
            for (uint32_t i = j.first; i < j.first + j.count; ++i) {
                nb.grow(box[order_[i]]);
                cb.grow(centroid[order_[i]]);
            }
            nodes_[j.node].lo = nb.lo;
            nodes_[j.node].hi = nb.hi;
            uint32_t mid = 0;
            if (j.count > kLeaf && split(j.first, j.count, nb, cb, box, centroid, mid)) {
                const uint32_t left = uint32_t(nodes_.size());
                nodes_.push_back({});
                nodes_.push_back({});
                nodes_[j.node].index = left;
                nodes_[j.node].count = 0;
                stack.push_back({left, j.first, mid - j.first});
                stack.push_back({left + 1, mid, j.first + j.count - mid});
            } else {
                nodes_[j.node].index = j.first;
                nodes_[j.node].count = j.count;
            }
        }
        tris_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const glm::uvec3& t = tris[order_[i]];
            tris_[i] = {pos[t.x], pos[t.y] - pos[t.x], pos[t.z] - pos[t.x]};
        }
    }

    // The hit with t in [tmin, tmax] nearest to `target`.
    bool nearest(const glm::vec3& o, const glm::vec3& d, float tmin, float tmax, float target, RayHitInfo& hit) const {
        if (tris_.empty()) return false;
        const glm::vec3 inv = invDir(d);
        float best = std::numeric_limits<float>::max();
        bool found = false;
        uint32_t stack[64];
        int sp = 0;
        float tn, tf;
        if (!slab(nodes_[0], o, inv, tmin, tmax, tn, tf)) return false;
        stack[sp++] = 0;
        while (sp) {
            const Node& n = nodes_[stack[--sp]];
            if (!slab(n, o, inv, tmin, tmax, tn, tf) || bound(tn, tf, target) >= best) continue;
            if (n.count) {
                for (uint32_t i = n.index; i < n.index + n.count; ++i) {
                    float t, u, v;
                    if (!intersect(tris_[i], o, d, t, u, v) || t < tmin || t > tmax) continue;
                    const float e = std::abs(t - target);
                    if (e < best) {
                        best = e;
                        hit = {t, u, v, order_[i]};
                        found = true;
                    }
                }
                continue;
            }
            float an, af, bn, bf;
            const bool ha = slab(nodes_[n.index], o, inv, tmin, tmax, an, af);
            const bool hb = slab(nodes_[n.index + 1], o, inv, tmin, tmax, bn, bf);
            const float la = ha ? bound(an, af, target) : best, lb = hb ? bound(bn, bf, target) : best;
            // Push the less promising child first so the better one is visited next.
            if (la < best && lb < best) {
                if (sp + 2 > 64) continue;
                if (la <= lb) {
                    stack[sp++] = n.index + 1;
                    stack[sp++] = n.index;
                } else {
                    stack[sp++] = n.index;
                    stack[sp++] = n.index + 1;
                }
            } else if (la < best && sp < 64) {
                stack[sp++] = n.index;
            } else if (lb < best && sp < 64) {
                stack[sp++] = n.index + 1;
            }
        }
        return found;
    }

    // Any hit with t in [tmin, tmax].
    bool occluded(const glm::vec3& o, const glm::vec3& d, float tmin, float tmax) const {
        if (tris_.empty()) return false;
        const glm::vec3 inv = invDir(d);
        uint32_t stack[64];
        int sp = 0;
        stack[sp++] = 0;
        float tn, tf;
        while (sp) {
            const Node& n = nodes_[stack[--sp]];
            if (!slab(n, o, inv, tmin, tmax, tn, tf)) continue;
            if (n.count) {
                for (uint32_t i = n.index; i < n.index + n.count; ++i) {
                    float t, u, v;
                    if (intersect(tris_[i], o, d, t, u, v) && t >= tmin && t <= tmax) return true;
                }
                continue;
            }
            if (sp + 2 <= 64) {
                stack[sp++] = n.index + 1;
                stack[sp++] = n.index;
            }
        }
        return false;
    }

private:
    static constexpr uint32_t kLeaf = 4;
    static constexpr int kBins = 16;
    struct Box {
        glm::vec3 lo{std::numeric_limits<float>::max()}, hi{std::numeric_limits<float>::lowest()};
        void grow(const glm::vec3& p) { lo = glm::min(lo, p), hi = glm::max(hi, p); }
        void grow(const Box& b) { lo = glm::min(lo, b.lo), hi = glm::max(hi, b.hi); }
        float area() const {
            if (lo.x > hi.x) return 0.0f;
            const glm::vec3 e = hi - lo;
            return e.x * e.y + e.y * e.z + e.z * e.x;
        }
    };
    struct Node {
        glm::vec3 lo{0.0f};
        uint32_t index = 0; // leaf: first triangle, inner: left child (right = index + 1)
        glm::vec3 hi{0.0f};
        uint32_t count = 0; // > 0 for leaves
    };
    struct Tri {
        glm::vec3 v0, e1, e2;
    };

    bool split(uint32_t first, uint32_t count, const Box& nb, const Box& cb, const std::vector<Box>& box,
               const std::vector<glm::vec3>& centroid, uint32_t& mid) {
        float bestCost = std::numeric_limits<float>::max();
        int bestAxis = -1, bestBin = 0;
        const glm::vec3 ext = cb.hi - cb.lo;
        for (int axis = 0; axis < 3; ++axis) {
            if (ext[axis] <= 0.0f) continue;
            Box bins[kBins];
            uint32_t cnt[kBins] = {};
            const float k = float(kBins) * 0.9999f / ext[axis];
            for (uint32_t i = first; i < first + count; ++i) {
                const int b = std::min(kBins - 1, int((centroid[order_[i]][axis] - cb.lo[axis]) * k));
                bins[b].grow(box[order_[i]]);
                ++cnt[b];
            }
            float rightArea[kBins];
            uint32_t rightCount[kBins];
            Box acc;
            uint32_t c = 0;
            for (int b = kBins - 1; b > 0; --b) {
                acc.grow(bins[b]);
                c += cnt[b];
                rightArea[b] = acc.area();
                rightCount[b] = c;
            }
            acc = Box();
            c = 0;
            for (int b = 0; b < kBins - 1; ++b) {
                acc.grow(bins[b]);
                c += cnt[b];
                if (!c || !rightCount[b + 1]) continue;
                const float cost = acc.area() * float(c) + rightArea[b + 1] * float(rightCount[b + 1]);
                if (cost < bestCost) {
                    bestCost = cost;
                    bestAxis = axis;
                    bestBin = b;
                }
            }
        }
        if (bestAxis < 0) { // all centroids in one spot: split in the middle of the list
            if (count <= 16) return false;
            mid = first + count / 2;
            return true;
        }
        // A leaf costs about one intersection per triangle; stop when splitting doesn't pay.
        if (count <= 8 && bestCost >= nb.area() * float(count)) return false;
        const float k = float(kBins) * 0.9999f / ext[bestAxis];
        auto* b = order_.data() + first;
        auto* e = b + count;
        auto* m = std::partition(b, e, [&](uint32_t t) {
            return std::min(kBins - 1, int((centroid[t][bestAxis] - cb.lo[bestAxis]) * k)) <= bestBin;
        });
        mid = uint32_t(m - order_.data());
        if (mid == first || mid == first + count) mid = first + count / 2;
        return true;
    }

    static glm::vec3 invDir(const glm::vec3& d) {
        auto inv = [](float x) { return std::abs(x) > 1e-20f ? 1.0f / x : (x < 0 ? -1e20f : 1e20f); };
        return {inv(d.x), inv(d.y), inv(d.z)};
    }
    static bool slab(const Node& n, const glm::vec3& o, const glm::vec3& inv, float tmin, float tmax, float& tn, float& tf) {
        const glm::vec3 t0 = (n.lo - o) * inv, t1 = (n.hi - o) * inv;
        const glm::vec3 a = glm::min(t0, t1), b = glm::max(t0, t1);
        tn = std::max(std::max(a.x, a.y), std::max(a.z, tmin));
        tf = std::min(std::min(b.x, b.y), std::min(b.z, tmax));
        return tn <= tf;
    }
    static float bound(float tn, float tf, float target) { return target < tn ? tn - target : target > tf ? target - tf : 0.0f; }
    // Two-sided Moller-Trumbore.
    static bool intersect(const Tri& tr, const glm::vec3& o, const glm::vec3& d, float& t, float& u, float& v) {
        const glm::vec3 p = glm::cross(d, tr.e2);
        const float det = glm::dot(tr.e1, p);
        if (std::abs(det) < 1e-20f) return false;
        const float inv = 1.0f / det;
        const glm::vec3 s = o - tr.v0;
        u = glm::dot(s, p) * inv;
        if (u < 0.0f || u > 1.0f) return false;
        const glm::vec3 q = glm::cross(s, tr.e1);
        v = glm::dot(d, q) * inv;
        if (v < 0.0f || u + v > 1.0f) return false;
        t = glm::dot(tr.e2, q) * inv;
        return true;
    }

    std::vector<Node> nodes_;
    std::vector<uint32_t> order_;
    std::vector<Tri> tris_;
};

} // namespace sf
