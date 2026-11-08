// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SurfaceQuery.h"

#include "core/Parallel.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace sf {

namespace {

constexpr uint32_t kLeafSize = 4;

float boxDistance2(const AABB& b, const glm::vec3& p) {
    const glm::vec3 d = glm::max(glm::max(b.min - p, p - b.max), glm::vec3(0.0f));
    return glm::dot(d, d);
}

// Ray / box slab test: entry distance, or a negative value for a miss.
float rayBox(const AABB& b, const glm::vec3& o, const glm::vec3& inv, float maxT) {
    const glm::vec3 t0 = (b.min - o) * inv, t1 = (b.max - o) * inv;
    const glm::vec3 lo = glm::min(t0, t1), hi = glm::max(t0, t1);
    const float enter = std::max(std::max(lo.x, lo.y), std::max(lo.z, 0.0f));
    const float exit = std::min(std::min(hi.x, hi.y), std::min(hi.z, maxT));
    return enter <= exit ? enter : -1.0f;
}

glm::vec3 closestPointOnSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, float& u) {
    const glm::vec3 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    u = len2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return a + ab * u;
}

} // namespace

namespace {

// One node of a median-split build over order[begin, end): its box, and either a leaf or the split (the two halves
// as the next tasks). Shared by the serial top of the tree and the subtrees built in parallel, so both make the
// same tree. `boundsInParallel`: the box pass over a big range on the thread pool (min/max: the same box).
struct BuildTask {
    uint32_t node, begin, end;
};
template <class Push>
void buildNode(std::vector<FlatBVH::Node>& nodes, std::vector<uint32_t>& order, const std::vector<AABB>& boxes,
               const std::vector<glm::vec3>& centers, const BuildTask& t, bool boundsInParallel, Push&& push) {
    AABB box, cbox;
    if (boundsInParallel) {
        constexpr size_t block = 65536;
        const size_t count = t.end - t.begin, blocks = (count + block - 1) / block;
        std::vector<AABB> pb(blocks), pc(blocks);
        poolFor(count, [&](size_t b0, size_t e0) {
            AABB bb, cc;
            for (size_t i = t.begin + b0; i < t.begin + e0; ++i) bb.expand(boxes[order[i]]), cc.expand(centers[order[i]]);
            pb[b0 / block] = bb, pc[b0 / block] = cc;
        }, block, 2);
        for (size_t k = 0; k < blocks; ++k) box.expand(pb[k]), cbox.expand(pc[k]);
    } else {
        for (uint32_t i = t.begin; i < t.end; ++i) {
            box.expand(boxes[order[i]]);
            cbox.expand(centers[order[i]]);
        }
    }
    nodes[t.node].box = box;
    const uint32_t count = t.end - t.begin;
    const glm::vec3 ext = cbox.extent();
    if (count <= kLeafSize || std::max({ext.x, ext.y, ext.z}) <= 0.0f) {
        nodes[t.node].first = t.begin;
        nodes[t.node].count = count;
        return;
    }
    const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
    const uint32_t mid = t.begin + count / 2;
    std::nth_element(order.begin() + t.begin, order.begin() + mid, order.begin() + t.end,
                     [&](uint32_t a, uint32_t b) { return centers[a][axis] < centers[b][axis]; });
    const uint32_t left = uint32_t(nodes.size());
    nodes.push_back({});
    nodes.push_back({});
    nodes[t.node].first = left;
    nodes[t.node].count = 0;
    push(BuildTask{left, t.begin, mid});
    push(BuildTask{left + 1, mid, t.end});
}

} // namespace

void FlatBVH::build(const std::vector<AABB>& boxes) {
    nodes_.clear();
    order_.resize(boxes.size());
    for (uint32_t i = 0; i < order_.size(); ++i) order_[i] = i;
    if (boxes.empty()) return;
    std::vector<glm::vec3> centers(boxes.size());
    poolFor(boxes.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) centers[i] = boxes[i].center();
    }, 16384);

    nodes_.reserve(2 * boxes.size() / kLeafSize + 2);
    nodes_.push_back({});
    // Big meshes: the top of the tree breadth first (one range after another, each box pass on all threads) until
    // there are a few ranges per thread, then each range's subtree on its own thread, into its own nodes, appended
    // in order. Every split is the same nth_element of the same range either way, so the tree (and so every query)
    // is the same as one built on one thread; only the node numbering differs.
    constexpr uint32_t kSubtree = 1u << 15; // smaller ranges: built whole on one thread
    std::vector<BuildTask> top{{0, 0, uint32_t(boxes.size())}}, subtrees;
    const size_t wantTasks = size_t(std::max(1, ThreadPool::shared().threads())) * 4;
    while (!top.empty()) {
        std::vector<BuildTask> next;
        for (const BuildTask& t : top) {
            if (t.end - t.begin <= kSubtree || subtrees.size() + next.size() + top.size() >= wantTasks * 2) {
                subtrees.push_back(t);
                continue;
            }
            buildNode(nodes_, order_, boxes, centers, t, true, [&](const BuildTask& c) { next.push_back(c); });
        }
        top.swap(next);
    }
    std::vector<std::vector<Node>> local(subtrees.size());
    poolFor(subtrees.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            std::vector<Node>& nodes = local[k];
            nodes.reserve(2 * size_t(subtrees[k].end - subtrees[k].begin) / kLeafSize + 2);
            nodes.push_back({});
            std::vector<BuildTask> stack{{0, subtrees[k].begin, subtrees[k].end}};
            while (!stack.empty()) {
                const BuildTask t = stack.back();
                stack.pop_back();
                buildNode(nodes, order_, boxes, centers, t, false, [&](const BuildTask& c) { stack.push_back(c); });
            }
        }
    }, 1, 2);
    // Stitch: a subtree's root goes in its placeholder, the rest after the nodes so far (inner nodes' children
    // shifted to match; leaves index order_, which every subtree filled in place).
    for (size_t k = 0; k < subtrees.size(); ++k) {
        const std::vector<Node>& nodes = local[k];
        const uint32_t shift = uint32_t(nodes_.size()) - 1;
        auto moved = [&](Node n) {
            if (n.count == 0) n.first += shift;
            return n;
        };
        nodes_[subtrees[k].node] = moved(nodes[0]);
        for (size_t i = 1; i < nodes.size(); ++i) nodes_.push_back(moved(nodes[i]));
    }
}

glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                 glm::vec3& bary) {
    // Ericson, Real-Time Collision Detection 5.1.5.
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        bary = {1, 0, 0};
        return a;
    }
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        bary = {0, 1, 0};
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f && d1 - d3 > 0.0f) {
        const float v = d1 / (d1 - d3);
        bary = {1 - v, v, 0};
        return a + ab * v;
    }
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        bary = {0, 0, 1};
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f && d2 - d6 > 0.0f) {
        const float w = d2 / (d2 - d6);
        bary = {1 - w, 0, w};
        return a + ac * w;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f && (d4 - d3) + (d5 - d6) > 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        bary = {0, 1 - w, w};
        return b + (c - b) * w;
    }
    const float sum = va + vb + vc;
    if (!(std::abs(sum) > 0.0f)) { // degenerate triangle: nearest of its edges
        float u = 0.0f;
        glm::vec3 best = closestPointOnSegment(p, a, b, u);
        bary = {1 - u, u, 0};
        float bestD = glm::dot(best - p, best - p);
        const glm::vec3 q1 = closestPointOnSegment(p, b, c, u);
        if (const float d = glm::dot(q1 - p, q1 - p); d < bestD) {
            best = q1, bestD = d, bary = {0, 1 - u, u};
        }
        const glm::vec3 q2 = closestPointOnSegment(p, a, c, u);
        if (glm::dot(q2 - p, q2 - p) < bestD) best = q2, bary = {1 - u, 0, u};
        return best;
    }
    const float denom = 1.0f / sum;
    const float v = vb * denom, w = vc * denom;
    bary = {1 - v - w, v, w};
    return a + ab * v + ac * w;
}

void TriangleBVH::build(std::vector<glm::vec3> positions, std::vector<glm::uvec3> triangles) {
    positions_ = std::move(positions);
    triangles_ = std::move(triangles);
    std::vector<AABB> boxes(triangles_.size());
    parallelFor(triangles_.size(), [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            AABB box;
            for (int k = 0; k < 3; ++k) box.expand(positions_[triangles_[t][k]]);
            boxes[t] = box;
        }
    });
    bvh_.build(boxes);
}

glm::vec3 TriangleBVH::faceNormal(uint32_t t) const {
    const glm::uvec3 tri = triangles_[t];
    const glm::vec3 n = glm::cross(positions_[tri.y] - positions_[tri.x], positions_[tri.z] - positions_[tri.x]);
    const float len = glm::length(n);
    return len > 0.0f ? n / len : glm::vec3(0.0f);
}

bool TriangleBVH::closestPoint(const glm::vec3& p, float maxDistance, SurfaceHit& hit) const {
    if (bvh_.empty()) return false;
    const auto& nodes = bvh_.nodes();
    const auto& order = bvh_.order();
    float best2 = maxDistance * maxDistance;
    bool found = false;
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const FlatBVH::Node& n = nodes[stack[--sp]];
        if (boxDistance2(n.box, p) > best2) continue;
        if (n.count > 0) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const uint32_t t = order[i];
                const glm::uvec3 tri = triangles_[t];
                glm::vec3 bary;
                const glm::vec3 q = closestPointOnTriangle(p, positions_[tri.x], positions_[tri.y], positions_[tri.z], bary);
                const float d2 = glm::dot(q - p, q - p);
                if (d2 <= best2) {
                    best2 = d2;
                    found = true;
                    hit.prim = t;
                    hit.position = q;
                    hit.bary = bary;
                }
            }
            continue;
        }
        // Nearer child last, so it is popped first.
        const float dl = boxDistance2(nodes[n.first].box, p), dr = boxDistance2(nodes[n.first + 1].box, p);
        if (sp + 2 > 64) continue; // cannot happen with a median split of < 2^60 primitives
        if (dl < dr) {
            stack[sp++] = n.first + 1;
            stack[sp++] = n.first;
        } else {
            stack[sp++] = n.first;
            stack[sp++] = n.first + 1;
        }
    }
    if (found) hit.distance = std::sqrt(best2);
    return found;
}

bool TriangleBVH::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxT, SurfaceHit& hit) const {
    if (bvh_.empty()) return false;
    const auto& nodes = bvh_.nodes();
    const auto& order = bvh_.order();
    // (no 0 * inf = NaN in the slab test for an axis-aligned ray, as in BVH::raycast)
    glm::vec3 safe = dir;
    for (int i = 0; i < 3; ++i)
        if (std::abs(safe[i]) < 1e-20f) safe[i] = std::copysign(1e-20f, safe[i]);
    const glm::vec3 inv = 1.0f / safe;
    float bestT = maxT;
    bool found = false;
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const FlatBVH::Node& n = nodes[stack[--sp]];
        if (rayBox(n.box, origin, inv, bestT) < 0.0f) continue;
        if (n.count > 0) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const uint32_t t = order[i];
                const glm::uvec3 tri = triangles_[t];
                const glm::vec3 a = positions_[tri.x], e1 = positions_[tri.y] - a, e2 = positions_[tri.z] - a;
                const glm::vec3 pv = glm::cross(dir, e2);
                const float det = glm::dot(e1, pv);
                if (std::abs(det) < 1e-20f) continue;
                const float invDet = 1.0f / det;
                const glm::vec3 tv = origin - a;
                const float u = glm::dot(tv, pv) * invDet;
                if (u < 0.0f || u > 1.0f) continue;
                const glm::vec3 qv = glm::cross(tv, e1);
                const float v = glm::dot(dir, qv) * invDet;
                if (v < 0.0f || u + v > 1.0f) continue;
                const float tt = glm::dot(e2, qv) * invDet;
                if (tt < 0.0f || tt > bestT) continue;
                bestT = tt;
                found = true;
                hit.prim = t;
                hit.bary = {1 - u - v, u, v};
                hit.position = origin + dir * tt;
            }
            continue;
        }
        const float tl = rayBox(nodes[n.first].box, origin, inv, bestT);
        const float tr = rayBox(nodes[n.first + 1].box, origin, inv, bestT);
        if (sp + 2 > 64) continue;
        if (tl >= 0.0f && tr >= 0.0f) {
            if (tl < tr) {
                stack[sp++] = n.first + 1;
                stack[sp++] = n.first;
            } else {
                stack[sp++] = n.first;
                stack[sp++] = n.first + 1;
            }
        } else if (tl >= 0.0f) {
            stack[sp++] = n.first;
        } else if (tr >= 0.0f) {
            stack[sp++] = n.first + 1;
        }
    }
    if (found) hit.distance = bestT;
    return found;
}

void SegmentBVH::build(std::vector<glm::vec3> a, std::vector<glm::vec3> b) {
    a_ = std::move(a);
    b_ = std::move(b);
    std::vector<AABB> boxes(a_.size());
    for (size_t i = 0; i < a_.size(); ++i) {
        boxes[i].expand(a_[i]);
        boxes[i].expand(b_[i]);
    }
    bvh_.build(boxes);
}

bool SegmentBVH::closestPoint(const glm::vec3& p, float maxDistance, SurfaceHit& hit) const {
    if (bvh_.empty()) return false;
    const auto& nodes = bvh_.nodes();
    const auto& order = bvh_.order();
    float best2 = maxDistance * maxDistance;
    bool found = false;
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const FlatBVH::Node& n = nodes[stack[--sp]];
        if (boxDistance2(n.box, p) > best2) continue;
        if (n.count > 0) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const uint32_t s = order[i];
                float u = 0.0f;
                const glm::vec3 q = closestPointOnSegment(p, a_[s], b_[s], u);
                const float d2 = glm::dot(q - p, q - p);
                if (d2 <= best2) {
                    best2 = d2;
                    found = true;
                    hit.prim = s;
                    hit.position = q;
                    hit.bary = {1 - u, u, 0};
                }
            }
            continue;
        }
        if (sp + 2 > 64) continue;
        stack[sp++] = n.first;
        stack[sp++] = n.first + 1;
    }
    if (found) hit.distance = std::sqrt(best2);
    return found;
}

SurfaceDeviation measureDeviation(const Mesh& mesh, const Mesh& reference, bool symmetric) {
    SurfaceDeviation out;
    double sum = 0.0, sum2 = 0.0;
    size_t count = 0;
    std::atomic<uint32_t> maxBits{0}; // non-negative floats order like their bits
    auto oneWay = [&](const Mesh& from, const Mesh& to) {
        TriangleBVH bvh;
        bvh.build(to);
        const size_t n = from.vertexCount();
        std::vector<float> d(n, 0.0f);
        parallelFor(n, [&](size_t b, size_t e) {
            float localMax = 0.0f;
            for (size_t v = b; v < e; ++v) {
                SurfaceHit hit;
                d[v] = bvh.closestPoint(from.positions[v], std::numeric_limits<float>::max(), hit) ? hit.distance : 0.0f;
                localMax = std::max(localMax, d[v]);
            }
            uint32_t bits;
            std::memcpy(&bits, &localMax, 4);
            uint32_t cur = maxBits.load();
            while (bits > cur && !maxBits.compare_exchange_weak(cur, bits)) {
            }
        });
        for (float x : d) {
            sum += x;
            sum2 += double(x) * x;
        }
        count += n;
    };
    if (mesh.vertexCount() == 0 || reference.triangleCount() == 0) return out;
    oneWay(mesh, reference);
    if (symmetric && reference.vertexCount() > 0 && mesh.triangleCount() > 0) oneWay(reference, mesh);
    const uint32_t bits = maxBits.load();
    std::memcpy(&out.max, &bits, 4);
    out.mean = count ? float(sum / double(count)) : 0.0f;
    out.rms = count ? float(std::sqrt(sum2 / double(count))) : 0.0f;
    return out;
}

} // namespace sf
