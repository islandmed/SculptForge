// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kelvinlet brushes (see Kelvinlet.h for the math) and the Detail Clone brush (VDM stamps along a stroke).
#include "core/Kelvinlet.h"

#include "core/Commands.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"
#include "core/TierConfig.h"
#include "core/Vdm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <unordered_map>
#include <vector>

namespace sf {

size_t KelvinletSettings::budget() const {
    return vertexBudget ? vertexBudget : std::max<size_t>(size_t(currentTierConfig().activeTriangleBudget / 3), 100000);
}

float KelvinletSettings::poisson() const { return 0.49f * std::clamp(squash, 0.0f, 1.0f); }

// ---------------------------------------------------------------------------
// Kernels
// ---------------------------------------------------------------------------

void KelvinletKernel::grabTerms(float r2, float& A, float& B) const {
    A = B = 0.0f;
    for (int i = 0; i < scales; ++i) {
        const float e2 = eps[i] * eps[i];
        const float re2 = r2 + e2;
        const float inv = 1.0f / std::sqrt(re2);
        const float inv3 = inv * inv * inv;
        A += w[i] * ((1.0f - beta) * inv + 0.5f * e2 * inv3);
        B += w[i] * (beta * inv3);
    }
    A *= norm;
    B *= norm;
}

float KelvinletKernel::profile(float r2) const {
    float t = 0.0f;
    for (int i = 0; i < scales; ++i) {
        const float e2 = eps[i] * eps[i];
        const float re2 = r2 + e2;
        const float inv = 1.0f / std::sqrt(re2);
        const float inv3 = inv * inv * inv;
        t += w[i] * (inv3 + 1.5f * e2 * inv3 * inv * inv);
    }
    return t * norm;
}

float KelvinletKernel::envelope(float r) const {
    const float r2 = r * r;
    if (kind == KelvinletKind::Grab) {
        float A, B;
        grabTerms(r2, A, B);
        return std::max(std::abs(A), std::abs(A + B * r2));
    }
    return std::abs(profile(r2)) * r; // displacement per radian (or per unit log scale) at distance r
}

float KelvinletKernel::cutoff(float amount, float epsilon, float maxDist) const {
    if (!(amount > 0.0f) || !(maxDist > 0.0f)) return 0.0f;
    // Walk in from the far end: the first sample whose displacement reaches epsilon (the field is not
    // monotonic: multi-scale kernels dip slightly below zero before they fade).
    constexpr int kSteps = 96;
    for (int i = kSteps; i > 0; --i) {
        const float r = maxDist * float(i) / float(kSteps);
        if (envelope(r) * amount >= epsilon) return std::min(maxDist, r + maxDist / float(kSteps));
    }
    return maxDist / float(kSteps);
}

KelvinletKernel makeKelvinletKernel(KelvinletKind kind, float radius, const KelvinletSettings& settings, bool tight) {
    KelvinletKernel k;
    k.kind = kind;
    const float nu = settings.poisson();
    k.beta = 1.0f / (4.0f * (1.0f - nu));
    const float sharp = std::clamp(settings.sharpness, 0.0f, 1.0f);
    radius = std::max(radius, 1e-6f);
    if (kind == KelvinletKind::Grab && !tight) {
        // Tri-scale (1/r^5 far field). The weights cancel the 1/r and 1/r^3 terms of the far field.
        const float e1 = 0.75f * radius;
        k.scales = 3;
        k.eps[0] = e1;
        k.eps[1] = 1.5f * e1;
        k.eps[2] = 2.25f * e1;
    } else {
        // Bi-scale: Snake's narrower pull (1/r^3 far field), and Twist / Scale (whose single-scale field already
        // falls off as 1/r^3, so the pair falls off as 1/r^5).
        const float e1 = (kind == KelvinletKind::Grab ? 0.6f : 0.8f) * radius;
        k.scales = 2;
        k.eps[0] = e1;
        k.eps[1] = 1.5f * e1;
    }
    // Sharpness: a smaller inner scale gives the cusped, localized peak of the sharp Kelvinlets while the outer
    // scales keep the far field (the weights are recomputed, so the far-field terms still cancel).
    k.eps[0] *= 1.0f - 0.6f * sharp;
    if (k.scales == 3) {
        const float a = k.eps[0] * k.eps[0], b = k.eps[1] * k.eps[1], c = k.eps[2] * k.eps[2];
        k.w[0] = 1.0f;
        k.w[1] = -(c - a) / (c - b);
        k.w[2] = (b - a) / (c - b);
    } else {
        k.w[0] = 1.0f;
        k.w[1] = -1.0f;
        k.w[2] = 0.0f;
    }
    // Normalize: K(0) = I for Grab, t(0) = 1 for Twist and Scale.
    float at0 = 0.0f;
    for (int i = 0; i < k.scales; ++i)
        at0 += k.w[i] * (kind == KelvinletKind::Grab ? (1.5f - k.beta) / k.eps[i] : 2.5f / (k.eps[i] * k.eps[i] * k.eps[i]));
    k.norm = std::abs(at0) > 1e-30f ? 1.0f / at0 : 0.0f;
    return k;
}

glm::vec3 kelvinletGrab(const KelvinletKernel& k, const glm::vec3& r, const glm::vec3& delta) {
    float A, B;
    k.grabTerms(glm::dot(r, r), A, B);
    return A * delta + B * glm::dot(r, delta) * r;
}

glm::vec3 kelvinletTwist(const KelvinletKernel& k, const glm::vec3& r, const glm::vec3& axis, float angle) {
    const float theta = angle * k.profile(glm::dot(r, r));
    // Rodrigues: an exact rotation of this shell about the axis through the centre.
    const float c = std::cos(theta), s = std::sin(theta);
    const glm::vec3 rotated = r * c + glm::cross(axis, r) * s + axis * (glm::dot(axis, r) * (1.0f - c));
    return rotated - r;
}

glm::vec3 kelvinletScale(const KelvinletKernel& k, const glm::vec3& r, float logScale) {
    // Integrates the radial flow dr/dt = s t(r) r (midpoint steps): a pinch never folds a vertex past the centre.
    const float len0 = glm::length(r);
    if (len0 < 1e-12f) return glm::vec3(0.0f);
    const int steps = std::clamp(int(std::ceil(std::abs(logScale) / 0.2f)), 1, 8); // midpoint steps: plenty
    const float h = logScale / float(steps);
    float len = len0;
    for (int i = 0; i < steps; ++i) {
        const float mid = len * std::exp(0.5f * h * k.profile(len * len));
        len *= std::exp(h * k.profile(mid * mid));
    }
    return r * (len / len0 - 1.0f);
}

// ---------------------------------------------------------------------------
// The brushes
// ---------------------------------------------------------------------------

namespace {

KelvinletKind kindOf(BrushType type) {
    switch (type) {
    case BrushType::KelvinTwist: return KelvinletKind::Twist;
    case BrushType::KelvinScale: return KelvinletKind::Scale;
    default: return KelvinletKind::Grab;
    }
}

float window(float r, float cut) {
    // 1 inside 70% of the cutoff, then a smoothstep to 0 at the cutoff.
    const float t = std::clamp((r - 0.7f * cut) / (0.3f * cut), 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

} // namespace

namespace {

// The kernel's radial terms tabulated over u = r^2 / cut^2 (they depend on |r| only), the window folded in: a
// lookup and a lerp per vertex instead of three square roots and divisions per scale.
struct RadialTable {
    static constexpr int kSize = 2048;
    float scale = 0.0f;      // kSize / cut^2
    std::vector<float> a, b; // Grab: window * A, window * B; Twist / Scale: profile, window
    void build(const KelvinletKernel& k, float cut) {
        a.resize(kSize + 2);
        b.resize(kSize + 2);
        scale = float(kSize) / (cut * cut);
        for (int i = 0; i <= kSize + 1; ++i) {
            const float r2 = float(std::min(i, kSize)) / scale;
            const float w = window(std::sqrt(r2), cut);
            if (k.kind == KelvinletKind::Grab) {
                float A, B;
                k.grabTerms(r2, A, B);
                a[size_t(i)] = A * w;
                b[size_t(i)] = B * w;
            } else {
                a[size_t(i)] = k.profile(r2);
                b[size_t(i)] = w;
            }
        }
    }
    // r2 < cut^2.
    void at(float r2, float& outA, float& outB) const {
        const float x = r2 * scale;
        const int i = std::min(int(x), kSize);
        const float t = x - float(i);
        outA = a[size_t(i)] + (a[size_t(i) + 1] - a[size_t(i)]) * t;
        outB = b[size_t(i)] + (b[size_t(i) + 1] - b[size_t(i)]) * t;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The deferred far field (dense meshes)
// ---------------------------------------------------------------------------
//
// Over a large region, most vertices are in the far field, where the move is a small fraction of the step and
// smooth. There the field (times 1 - the near weight) is added to a coarse lattice in world space, kept once per
// group of vertices (vertex index mod kGroups); each frame one group's vertices take what their lattice gathered
// since their last turn and that lattice starts over. Every vertex gets the near part of every move at once and
// the far part within kGroups frames (all of it when the stroke ends or the pointer rests: flushKelvinletFar).

struct KelvinFarField {
    static constexpr int kGroups = 6;
    uint64_t stroke = 0;
    float h = 0.0f;         // lattice spacing (world)
    double meshVolume = 1.0; // the model's bounding box volume at the stroke's start
    // Spatial groups: blocks of kBlockTriangles consecutive triangles (a compact patch: the renderer's chunks and
    // clusters are made of runs of consecutive triangles too), block b in group b % kGroups; a vertex belongs to
    // the block of its first triangle (fixed for the stroke: Kelvinlets don't change the topology). So a group's
    // turn moves whole patches and dirties few render clusters.
    static constexpr uint32_t kBlockShift = 12; // 4096 triangles
    std::vector<uint32_t> firstTriangle; // per vertex (its block is its group's)
    std::vector<AABB> blockBox;      // per block, at the stroke's start, grown by the radius
    void prepare(const Mesh& mesh, float margin) {
        const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount(), blocks = (nt + (1u << kBlockShift) - 1) >> kBlockShift;
        firstTriangle.assign(nv, ~0u);
        poolFor(nv, [&](size_t b, size_t e) {
            for (size_t v = b; v < e; ++v) {
                const IndexRing tris = mesh.vertexTriangles(uint32_t(v));
                if (tris.begin() != tris.end()) firstTriangle[v] = *tris.begin();
            }
        }, 1 << 14);
        blockBox.assign(blocks, AABB{});
        poolFor(blocks, [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k) {
                AABB box;
                const size_t t1 = std::min(nt, (k + 1) << kBlockShift);
                for (size_t t = k << kBlockShift; t < t1; ++t)
                    for (int c = 0; c < 3; ++c) box.expand(mesh.positions[mesh.triangles[t][c]]);
                box.min -= glm::vec3(margin);
                box.max += glm::vec3(margin);
                blockBox[k] = box;
            }
        }, 16);
    }
    int turn = 0;           // the group whose turn is next
    std::unordered_map<uint64_t, uint32_t> index; // lattice point -> slot
    std::vector<glm::ivec3> point;               // slot -> lattice point
    std::vector<std::array<glm::vec3, kGroups>> acc;
    std::vector<uint64_t> listed;                // slot -> a bit per group: in that group's list since its turn
    std::vector<uint32_t> slots[kGroups];        // per group: the slots gathered since its turn
    glm::vec3 lo[kGroups], hi[kGroups];          // per group: the bounds of what it gathered since its turn
    bool any[kGroups] = {};
    static uint64_t key(const glm::ivec3& p) {
        return (uint64_t(uint32_t(p.x + (1 << 20)) & 0x1fffff) << 42) | (uint64_t(uint32_t(p.y + (1 << 20)) & 0x1fffff) << 21) |
               uint64_t(uint32_t(p.z + (1 << 20)) & 0x1fffff);
    }
    bool pending() const {
        for (bool a : any)
            if (a) return true;
        return false;
    }
};

namespace {

// 1 within half the radius of the load's path, a smoothstep to 0 at the radius: the near field.
float nearWeight(float d, float radius) {
    const float t = std::clamp((d - 0.5f * radius) / (0.5f * radius), 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

} // namespace

void Sculptor::gatherKelvinletFar(const std::function<glm::vec3(const glm::vec3&)>& displacement, const glm::vec3& a,
                                  const glm::vec3& ab, float radius, const glm::vec3& mid, float reach) {
    KelvinFarField& f = *kelvinFar_;
    const auto t0 = std::chrono::steady_clock::now();
    const float h = f.h;
    const glm::ivec3 i0 = glm::ivec3(glm::floor((mid - glm::vec3(reach)) / h));
    const glm::ivec3 i1 = glm::ivec3(glm::ceil((mid + glm::vec3(reach)) / h));
    const glm::ivec3 dim = i1 - i0 + glm::ivec3(1);
    const size_t count = size_t(dim.x) * size_t(dim.y) * size_t(dim.z);
    const float ab2 = glm::dot(ab, ab), reach2 = (reach + 1.8f * h) * (reach + 1.8f * h);
    std::vector<glm::vec3> value(count, glm::vec3(0.0f));
    std::vector<uint8_t> used(count, 0);
    poolFor(count, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const glm::ivec3 q = i0 + glm::ivec3(int(i % size_t(dim.x)), int((i / size_t(dim.x)) % size_t(dim.y)),
                                                  int(i / (size_t(dim.x) * size_t(dim.y))));
            const glm::vec3 p = glm::vec3(q) * h;
            if (glm::dot(p - mid, p - mid) > reach2) continue;
            const float t = ab2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / ab2, 0.0f, 1.0f) : 0.0f;
            const float w = 1.0f - nearWeight(glm::distance(p, a + ab * t), radius);
            if (w <= 0.0f) continue;
            value[i] = displacement(p) * w;
            used[i] = 1;
        }
    }, 1024);
    for (size_t i = 0; i < count; ++i) {
        if (!used[i]) continue;
        const glm::ivec3 q = i0 + glm::ivec3(int(i % size_t(dim.x)), int((i / size_t(dim.x)) % size_t(dim.y)),
                                              int(i / (size_t(dim.x) * size_t(dim.y))));
        auto [it, fresh] = f.index.try_emplace(KelvinFarField::key(q), uint32_t(f.point.size()));
        if (fresh) {
            f.point.push_back(q);
            f.acc.emplace_back();
            f.acc.back().fill(glm::vec3(0.0f));
            f.listed.push_back(0);
        }
        const uint32_t slot = it->second;
        for (glm::vec3& a : f.acc[slot]) a += value[i];
        if (f.listed[slot] != (uint64_t(1) << KelvinFarField::kGroups) - 1) {
            for (int g = 0; g < KelvinFarField::kGroups; ++g)
                if (!(f.listed[slot] & (uint64_t(1) << g))) f.slots[g].push_back(slot);
            f.listed[slot] = (uint64_t(1) << KelvinFarField::kGroups) - 1;
        }
    }
    if (std::getenv("SF_KELVIN_LOG"))
        std::fprintf(stderr, "kelvin far: gathered %zu lattice points (%zu in all) in %.1f ms\n", count, f.point.size(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    const glm::vec3 lo = glm::vec3(i0) * h, hi = glm::vec3(i1) * h;
    for (int g = 0; g < KelvinFarField::kGroups; ++g) {
        f.lo[g] = f.any[g] ? glm::min(f.lo[g], lo) : lo;
        f.hi[g] = f.any[g] ? glm::max(f.hi[g], hi) : hi;
        f.any[g] = true;
    }
}

// One group's turn: its vertices in the bounds its lattice gathered take the interpolated field.
void Sculptor::applyKelvinletFarGroup(Mesh& mesh, BVH& bvh, int g) {
    KelvinFarField& f = *kelvinFar_;
    if (!f.any[g]) return;
    auto clock = std::chrono::steady_clock::now();
    const float h = f.h;
    const glm::ivec3 i0 = glm::ivec3(glm::floor(f.lo[g] / h)), i1 = glm::ivec3(glm::ceil(f.hi[g] / h));
    const glm::ivec3 dim = i1 - i0 + glm::ivec3(2);
    std::vector<glm::vec3> dense(size_t(dim.x) * size_t(dim.y) * size_t(dim.z), glm::vec3(0.0f));
    for (const uint32_t s : f.slots[g]) {
        f.listed[s] &= ~(uint64_t(1) << g);
        const glm::ivec3 q = f.point[s] - i0;
        glm::vec3& v = f.acc[s][size_t(g)];
        if (q.x < 0 || q.y < 0 || q.z < 0 || q.x >= dim.x || q.y >= dim.y || q.z >= dim.z) {
            v = glm::vec3(0.0f); // (outside every bound gathered since the turn: nothing there)
            continue;
        }
        dense[(size_t(q.z) * size_t(dim.y) + size_t(q.y)) * size_t(dim.x) + size_t(q.x)] = v;
        v = glm::vec3(0.0f);
    }
    f.slots[g].clear();
    f.any[g] = false;
    const auto& pos = mesh.positions;
    const glm::vec3 c = 0.5f * (f.lo[g] + f.hi[g]), ext = f.hi[g] - f.lo[g];
    const double volume = double(ext.x) * double(ext.y) * double(ext.z);
    const double meshVolume = f.meshVolume; // (measured once per stroke: bounds() reads every vertex)
    (void)c;
    (void)volume;
    (void)meshVolume;
    const double denseMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count();
    // This group's blocks that meet the bounds it gathered: their triangles' corners that belong to the group.
    const size_t nt = mesh.triangleCount(), blocks = f.blockBox.size();
    const glm::vec3 lo = f.lo[g], hi = f.hi[g];
    std::vector<std::vector<uint32_t>> parts(blocks);
    poolFor(blocks, [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            if (int(k % size_t(KelvinFarField::kGroups)) != g) continue;
            const AABB& box = f.blockBox[k];
            if (box.max.x < lo.x || box.max.y < lo.y || box.max.z < lo.z || box.min.x > hi.x || box.min.y > hi.y || box.min.z > hi.z)
                continue;
            auto& out = parts[k];
            const size_t t1 = std::min(nt, (k + 1) << KelvinFarField::kBlockShift);
            for (size_t t = k << KelvinFarField::kBlockShift; t < t1; ++t)
                for (int corner = 0; corner < 3; ++corner) {
                    const uint32_t v = mesh.triangles[t][corner];
                    // (each vertex once: from its first triangle, which is in a block of its group)
                    if (f.firstTriangle[v] == uint32_t(t) && !(mesh.hasHidden() && mesh.vertexHidden(v))) out.push_back(v);
                }
        }
    }, 1);
    verts_.clear();
    for (const auto& part : parts) verts_.insert(verts_.end(), part.begin(), part.end());
    const double selectMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count();
    scratch_.resize(verts_.size());
    poolFor(verts_.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const glm::vec3 gp = pos[verts_[i]] / h - glm::vec3(i0);
            const int x = int(std::floor(gp.x)), y = int(std::floor(gp.y)), z = int(std::floor(gp.z));
            if (x < 0 || y < 0 || z < 0 || x >= dim.x - 1 || y >= dim.y - 1 || z >= dim.z - 1) {
                scratch_[i] = glm::vec3(0.0f);
                continue;
            }
            const glm::vec3 fr = gp - glm::vec3(float(x), float(y), float(z));
            auto at = [&](int dx, int dy, int dz) {
                return dense[(size_t(z + dz) * size_t(dim.y) + size_t(y + dy)) * size_t(dim.x) + size_t(x + dx)];
            };
            const glm::vec3 c00 = glm::mix(at(0, 0, 0), at(1, 0, 0), fr.x), c10 = glm::mix(at(0, 1, 0), at(1, 1, 0), fr.x);
            const glm::vec3 c01 = glm::mix(at(0, 0, 1), at(1, 0, 1), fr.x), c11 = glm::mix(at(0, 1, 1), at(1, 1, 1), fr.x);
            scratch_[i] = glm::mix(glm::mix(c00, c10, fr.y), glm::mix(c01, c11, fr.y), fr.z);
        }
    }, 4096);
    size_t kept = 0;
    const float keep2 = (1e-4f * h) * (1e-4f * h);
    for (size_t i = 0; i < verts_.size(); ++i)
        if (glm::dot(scratch_[i], scratch_[i]) > keep2) {
            verts_[kept] = verts_[i];
            scratch_[kept++] = scratch_[i];
        }
    verts_.resize(kept);
    scratch_.resize(kept);
    if (std::getenv("SF_KELVIN_LOG"))
        std::fprintf(stderr, "kelvin far: group %d, %zu vertices, lattice %.1f, select %.1f, total %.1f ms\n", g, kept, denseMs,
                     selectMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock).count());
    if (verts_.empty()) return;
    displaceAll(mesh, verts_, scratch_);
    timings.kernelMs += lapMs(clock);
    bvh.refit(mesh, verts_);
    timings.bvhMs += lapMs(clock);
    timings.vertices += verts_.size();
    dabVerts_.insert(dabVerts_.end(), verts_.begin(), verts_.end());
    kelvinRingNormals_ = true;
}

bool Sculptor::kelvinletFarPending() const {
    return active_ && kelvinFar_ && kelvinFar_->stroke == strokeId_ && kelvinFar_->pending();
}

void Sculptor::flushKelvinletFar(Mesh& mesh, BVH& bvh, bool all) {
    if (!kelvinletFarPending()) return;
    dabVerts_.clear();
    dabLeaves_.clear();
    KelvinFarField& f = *kelvinFar_;
    for (int i = 0; i < KelvinFarField::kGroups; ++i) {
        applyKelvinletFarGroup(mesh, bvh, f.turn);
        f.turn = (f.turn + 1) % KelvinFarField::kGroups;
        if (!all) break;
    }
    dabLeaves_.clear();
    kelvinRingNormals_ = false;
    updateDabNormals(mesh, bvh);
}

void Sculptor::kelvinletSingle(Mesh& mesh, BVH& bvh, const KelvinletKernel& k, const KelvinletStep& s) {
    auto clock = std::chrono::steady_clock::now();
    float amount = 0.0f, moveScale = 0.0f; // the step's size, and about how far it moves the centre (world)
    switch (k.kind) {
    case KelvinletKind::Grab: amount = moveScale = glm::length(s.delta); break;
    case KelvinletKind::Twist: amount = std::abs(s.angle), moveScale = amount * s.radius; break;
    case KelvinletKind::Scale: amount = std::abs(s.logScale), moveScale = amount * s.radius; break;
    }
    if (!(amount > 0.0f)) return;
    if (kelvinStroke_ != strokeId_) { // per stroke: the region size the last move found
        kelvinStroke_ = strokeId_;
        kelvinLastVerts_ = 0;
        kelvinLastCut_ = 0.0f;
    }
    // Adaptive cutoff: where this step's displacement falls below epsilon = 4% of its largest (the window takes the
    // rest smoothly to 0), so a slow drag of tiny steps still moves the surface and a fast one doesn't drag the whole
    // mesh into the pass. Over the vertex budget (the last move's density says how many the cutoff will find) the
    // fraction rises toward 10%: a tighter region on very dense meshes.
    float peak = 0.0f;
    for (int i = 0; i <= 32; ++i) peak = std::max(peak, k.envelope(1.5f * s.radius * float(i) / 32.0f));
    const glm::vec3 travel0 = k.kind == KelvinletKind::Grab ? (glm::length(s.travel) > 0.0f ? s.travel : s.delta) : glm::vec3(0.0f);
    const float maxDist = 3.0f * s.radius;
    float fraction = 0.04f;
    float cut = k.cutoff(amount, std::max(fraction * amount * peak, 1e-7f * s.radius), maxDist);
    const size_t budget = kelvinlet.budget();
    if (kelvinLastVerts_ > 0 && kelvinLastCut_ > 0.0f) {
        const float reach = cut + 0.5f * glm::length(travel0), lastReach = kelvinLastCut_;
        const double predicted = double(kelvinLastVerts_) * double(reach * reach) / double(lastReach * lastReach);
        if (predicted > double(budget)) {
            fraction = std::min(0.10f, float(0.04 * predicted / double(budget)));
            cut = k.cutoff(amount, std::max(fraction * amount * peak, 1e-7f * s.radius), maxDist);
        }
    }
    if (!(cut > 0.0f)) return;
    // Grab: the load point travels from `center` to `center + travel` in `substeps` pieces, each a linear Kelvinlet
    // applied to where the vertex is by then (forward Euler on the flow), all in one pass: one query, one move,
    // one normal and BVH update however far the cursor jumped since the last frame.
    const bool grab = k.kind == KelvinletKind::Grab;
    const glm::vec3 travel = travel0;
    const int n = grab ? std::clamp(s.substeps, 1, 64) : 1;
    RadialTable table;
    table.build(k, cut);
    const float cut2 = cut * cut;
    const glm::vec3 dStep = s.delta / float(n), cStep = travel / float(n);
    // The displacement of a point that starts at x0 (exact: the kernel through the table).
    auto displacement = [&](const glm::vec3& x0) {
        glm::vec3 x = x0;
        switch (k.kind) {
        case KelvinletKind::Grab: {
            glm::vec3 c = s.center;
            for (int j = 0; j < n; ++j, c += cStep) {
                const glm::vec3 r = x - c;
                const float r2 = glm::dot(r, r);
                if (r2 >= cut2) continue;
                float A, B;
                table.at(r2, A, B);
                x += A * dStep + B * glm::dot(r, dStep) * r;
            }
            break;
        }
        case KelvinletKind::Twist: {
            const glm::vec3 r = x - s.center;
            const float r2 = glm::dot(r, r);
            if (r2 >= cut2) break;
            float t, w;
            table.at(r2, t, w);
            const float theta = s.angle * t, co = std::cos(theta), si = std::sin(theta);
            const glm::vec3 rot = r * co + glm::cross(s.axis, r) * si + s.axis * (glm::dot(s.axis, r) * (1.0f - co));
            x += (rot - r) * w;
            break;
        }
        case KelvinletKind::Scale: {
            const glm::vec3 r = x - s.center;
            const float r2 = glm::dot(r, r);
            if (r2 >= cut2 || r2 < 1e-24f) break;
            // The radial flow dr/dt = s t(r) r in midpoint steps, t from the table (clamped at the cutoff).
            const int steps = std::clamp(int(std::ceil(amount / 0.2f)), 1, 8);
            const float h = s.logScale / float(steps);
            const float len0 = std::sqrt(r2), lim = cut2 * 0.999f;
            float len = len0, t, w;
            for (int j = 0; j < steps; ++j) {
                table.at(std::min(len * len, lim), t, w);
                const float midLen = len * std::exp(0.5f * h * t);
                table.at(std::min(midLen * midLen, lim), t, w);
                len *= std::exp(h * t);
            }
            table.at(r2, t, w);
            x += r * ((len / len0 - 1.0f) * w);
            break;
        }
        }
        return x - x0;
    };
    const glm::vec3 mid = s.center + 0.5f * travel;
    const float reach = cut + 0.5f * glm::length(travel);
    const auto& pos = mesh.positions;
    // A region bigger than kelvinlet.deferMinVertices (from the last move's density): the far field goes to the
    // lattice (gatherKelvinletFar), only the near field's vertices are moved now.
    const float nearReach = s.radius + 0.5f * glm::length(travel);
    const bool defer = kelvinLastVerts_ > 0 && kelvinLastCut_ > 0.0f && reach > nearReach &&
                       double(kelvinLastVerts_) * double(reach * reach) / double(kelvinLastCut_ * kelvinLastCut_) >
                           double(kelvinlet.deferMinVertices);
    if (defer) {
        if (!kelvinFar_) kelvinFar_ = std::make_shared<KelvinFarField>();
        if (kelvinFar_->stroke != strokeId_) {
            *kelvinFar_ = KelvinFarField{};
            kelvinFar_->stroke = strokeId_;
            kelvinFar_->h = 0.1f * s.radius;
            const glm::vec3 e = mesh.bounds().extent();
            kelvinFar_->meshVolume = std::max(1e-30, double(e.x) * double(e.y) * double(e.z));
            kelvinFar_->prepare(mesh, s.radius);
        }
        gatherKelvinletFar(displacement, s.center, travel, s.radius, mid, reach);
        brushVertices(mesh, bvh, mid, nearReach, verts_);
        kelvinLastVerts_ = size_t(double(verts_.size()) * double(reach * reach) / double(nearReach * nearReach));
    } else {
        brushVertices(mesh, bvh, mid, reach, verts_);
        kelvinLastVerts_ = verts_.size();
    }
    kelvinLastCut_ = reach;
    kelvinOverBudget_ = kelvinOverBudget_ || kelvinLastVerts_ > budget;
    if (std::getenv("SF_KELVIN_LOG"))
        std::fprintf(stderr, "kelvin move: %s, %zu vertices queried, reach %.3f (radius %.3f, travel %.3f), cut %.2f%%\n",
                     defer ? "near field now" : "all now", verts_.size(), reach, s.radius, glm::length(travel), fraction * 100.0f);
    if (verts_.empty()) return;
    if (s.frontFacesOnly) {
        const auto& nrm = mesh.normals;
        std::erase_if(verts_, [&](uint32_t v) { return glm::dot(nrm[v], s.viewDir) > 0.0f; });
        if (verts_.empty()) return;
    }
    scratch_.resize(verts_.size());
    if (defer) {
        const glm::vec3 a = s.center, ab = travel;
        const float ab2 = glm::dot(ab, ab);
        poolFor(verts_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const glm::vec3 x0 = pos[verts_[i]];
                const float t = ab2 > 0.0f ? std::clamp(glm::dot(x0 - a, ab) / ab2, 0.0f, 1.0f) : 0.0f;
                const float w = nearWeight(glm::distance(x0, a + ab * t), s.radius);
                scratch_[i] = w > 0.0f ? displacement(x0) * w : glm::vec3(0.0f);
            }
        }, 2048);
    } else
    // Many vertices: the smooth part of the field (beyond half the radius from the load's path) is evaluated on a
    // coarse lattice of points around the region and interpolated (trilinear); the near field stays exact. The
    // lattice spacing is a fortieth of the region, a tenth of the radius at most: the interpolation error is about
    // (h / e)^2 / 8 of the move (well under 1% of it, a fraction of a pixel; tests/test_kelvinlet.cpp checks it).
    if (verts_.size() > kelvinlet.proxyMinVertices) {
        const float h = std::min(0.06f * s.radius, 2.0f * reach / 48.0f);
        const glm::vec3 lo = mid - glm::vec3(reach + h);
        const int dim = std::min(128, int(std::ceil(2.0f * (reach + h) / h)) + 2);
        const float cell = 2.0f * (reach + h) / float(dim - 1);
        std::vector<glm::vec3> lattice(size_t(dim) * size_t(dim) * size_t(dim));
        poolFor(lattice.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const size_t ix = i % size_t(dim), iy = (i / size_t(dim)) % size_t(dim), iz = i / (size_t(dim) * size_t(dim));
                lattice[i] = displacement(lo + glm::vec3(float(ix), float(iy), float(iz)) * cell);
            }
        }, 1024);
        const float near = (k.kind == KelvinletKind::Scale ? 0.75f : 0.5f) * s.radius + cell;
        const glm::vec3 a = s.center, ab = travel;
        const float ab2 = glm::dot(ab, ab);
        poolFor(verts_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const glm::vec3 x0 = pos[verts_[i]];
                const float t = ab2 > 0.0f ? std::clamp(glm::dot(x0 - a, ab) / ab2, 0.0f, 1.0f) : 0.0f;
                if (glm::distance(x0, a + ab * t) < near) {
                    scratch_[i] = displacement(x0);
                    continue;
                }
                const glm::vec3 g = (x0 - lo) / cell;
                const int x = std::clamp(int(g.x), 0, dim - 2), y = std::clamp(int(g.y), 0, dim - 2), z = std::clamp(int(g.z), 0, dim - 2);
                const glm::vec3 f = glm::clamp(g - glm::vec3(float(x), float(y), float(z)), 0.0f, 1.0f);
                auto at = [&](int dx, int dy, int dz) {
                    return lattice[(size_t(z + dz) * size_t(dim) + size_t(y + dy)) * size_t(dim) + size_t(x + dx)];
                };
                const glm::vec3 c00 = glm::mix(at(0, 0, 0), at(1, 0, 0), f.x), c10 = glm::mix(at(0, 1, 0), at(1, 1, 0), f.x);
                const glm::vec3 c01 = glm::mix(at(0, 0, 1), at(1, 0, 1), f.x), c11 = glm::mix(at(0, 1, 1), at(1, 1, 1), f.x);
                scratch_[i] = glm::mix(glm::mix(c00, c10, f.y), glm::mix(c01, c11, f.y), f.z);
            }
        }, 4096);
    } else {
        poolFor(verts_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) scratch_[i] = displacement(pos[verts_[i]]);
        }, 2048);
    }
    // Only what moves noticeably this step goes on (undo, normals, the BVH): the far part of the region moves
    // less than 0.5% of the step, which adds up to less than 0.5% of the whole drag.
    const float keep = 5e-3f * moveScale, keep2 = keep * keep;
    const size_t total = verts_.size();
    size_t kept = 0;
    for (size_t i = 0; i < total; ++i)
        if (glm::dot(scratch_[i], scratch_[i]) > keep2) {
            verts_[kept] = verts_[i];
            scratch_[kept++] = scratch_[i];
        }
    verts_.resize(kept);
    scratch_.resize(kept);
    if (verts_.empty()) return;
    displaceAll(mesh, verts_, scratch_);
    if (kept * 4 > total) {
        finishDab(mesh, bvh, clock); // much of the region moved: refit and renormal the queried leaves
        return;
    }
    timings.kernelMs += lapMs(clock);
    bvh.refit(mesh, verts_);
    timings.bvhMs += lapMs(clock);
    ++timings.dabs;
    timings.vertices += verts_.size();
    dabVerts_.insert(dabVerts_.end(), verts_.begin(), verts_.end());
    kelvinRingNormals_ = true; // updateDabNormals: the moved vertices' rings, not every queried leaf
}

void Sculptor::kelvinletStep(Mesh& mesh, BVH& bvh, BrushType type, const KelvinletStep& step, Symmetry sym) {
    if (!active_ || !isKelvinletBrush(type) || type == BrushType::DetailClone) return;
    const KelvinletKernel k = makeKelvinletKernel(kindOf(type), step.radius, kelvinlet, type == BrushType::KelvinSnake);
    kelvinOverBudget_ = false;
    dabVerts_.clear();
    dabLeaves_.clear();
    for (const SymCopy& m : copies(sym)) {
        KelvinletStep ms = step;
        ms.center = m.point(step.center);
        ms.delta = m.dir(step.delta);
        ms.travel = m.dir(step.travel);
        ms.viewDir = m.dir(step.viewDir);
        // The rotation axis is a pseudovector: a mirror flips it once more (det = -1 for an odd mirror count).
        ms.axis = m.dir(step.axis) * m.handedness();
        kelvinletSingle(mesh, bvh, k, ms);
    }
    if (kelvinletFarPending()) { // one group of the far field's vertices catches up (see KelvinFarField)
        applyKelvinletFarGroup(mesh, bvh, kelvinFar_->turn);
        kelvinFar_->turn = (kelvinFar_->turn + 1) % KelvinFarField::kGroups;
    }
    if (kelvinRingNormals_) dabLeaves_.clear(); // (a mirror that moved little: rings for every mirror's vertices)
    kelvinRingNormals_ = false;
    updateDabNormals(mesh, bvh);
}

// A plain dab of a Kelvinlet brush (strokes along a path, the Dots stroke, scripts): Grab and Snake follow the
// stroke (a click without motion pulls the surface toward the viewer), Twist turns and Scale pinches by an amount
// set by Z Intensity per dab (Alt reverses).
void Sculptor::dabKelvinlet(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p, Symmetry sym) {
    if (type == BrushType::DetailClone) {
        dabDetailClone(mesh, bvh, p, sym);
        return;
    }
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    const float sign = p.invert ? -1.0f : 1.0f;
    KelvinletStep step;
    step.center = p.center;
    step.radius = p.radius;
    step.viewDir = p.viewDir;
    step.frontFacesOnly = p.frontFacesOnly;
    step.axis = glm::length(p.viewDir) > 1e-6f ? -glm::normalize(p.viewDir) : glm::vec3(0.0f, 0.0f, 1.0f);
    switch (type) {
    case BrushType::KelvinGrab:
    case BrushType::KelvinSnake: {
        if (type == BrushType::KelvinSnake) {
            // The multi-scale Snake: the pull narrows as it travels, so drawn-out shapes taper to a tip.
            if (snakeStroke_ != strokeId_) {
                snakeStroke_ = strokeId_;
                snakeTravel_ = 0.0f;
            }
            snakeTravel_ += glm::length(p.strokeDelta);
            step.radius = p.radius * std::max(0.4f, 1.0f / (1.0f + snakeTravel_ / (4.0f * p.radius)));
        }
        glm::vec3 delta = p.strokeDelta * std::min(1.0f, 2.0f * s);
        if (glm::length(p.strokeDelta) < 1e-9f) delta = step.axis * (sign * 0.05f * s * p.radius);
        // Small pieces: each one is a linear Kelvinlet, so large moves integrate the flow instead of shearing.
        step.delta = delta;
        step.travel = glm::length(p.strokeDelta) < 1e-9f ? delta : p.strokeDelta;
        step.substeps = std::clamp(int(std::ceil(glm::length(step.travel) / (0.4f * step.radius))), 1, 16);
        kelvinletStep(mesh, bvh, type, step, sym);
        return;
    }
    case BrushType::KelvinTwist: step.angle = sign * s * 0.15f; break;
    case BrushType::KelvinScale: step.logScale = -sign * s * 0.06f; break; // pinches; Alt swells
    default: return;
    }
    kelvinletStep(mesh, bvh, type, step, sym);
}

// ---------------------------------------------------------------------------
// Detail Clone: the VDM brush's map stamped along the stroke, each stamp turned to follow the stroke
// ---------------------------------------------------------------------------

void Sculptor::cloneStamp(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym) {
    dabVerts_.clear();
    dabLeaves_.clear();
    for (const SymCopy& m : copies(sym)) {
        dabVdmSingle(mesh, bvh, transformed(p, m), m.handedness());
    }
    updateDabNormals(mesh, bvh);
}

void Sculptor::dabDetailClone(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym) {
    if (!active_) return;
    if (clone_.stroke != strokeId_) {
        clone_ = CloneState{};
        clone_.stroke = strokeId_;
    }
    const float r = p.radius;
    const float spacing = 2.0f * r * std::clamp(kelvinlet.cloneSpacing, 0.25f, 3.0f);
    auto stampAt = [&](DabParams q, const glm::vec3& dir) {
        if (kelvinlet.cloneFollowStroke && glm::length(dir) > 1e-9f) q.alphaUp = glm::normalize(dir);
        cloneStamp(mesh, bvh, q, sym);
        clone_.last = q.center;
        clone_.stamped = true;
    };
    if (!clone_.stamped) {
        if (!clone_.pending) { // the first stamp waits for the stroke's direction
            clone_.pending = true;
            clone_.first = p;
            return;
        }
        const glm::vec3 dir = p.center - clone_.first.center;
        if (glm::length(dir) < 0.02f * r) { // dabs in place (scripts, a Dots click held still): stamp as turned
            stampAt(clone_.first, glm::vec3(0.0f));
            clone_.pending = false;
            return;
        }
        if (glm::length(dir) < 0.25f * r) return; // not far enough yet to say which way the stroke goes
        stampAt(clone_.first, dir);
        clone_.pending = false;
    }
    const glm::vec3 d = p.center - clone_.last;
    if (glm::length(d) >= spacing) stampAt(p, d);
}

void Sculptor::flushDetailClone(Mesh& mesh, BVH& bvh, Symmetry sym) {
    flushKelvinletFar(mesh, bvh, true); // (and the deferred far field of a Kelvinlet stroke)
    if (!active_ || clone_.stroke != strokeId_ || !clone_.pending || clone_.stamped) return;
    DabParams q = clone_.first;
    cloneStamp(mesh, bvh, q, sym);
    clone_.pending = false;
    clone_.stamped = true;
}

void registerKelvinletCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add({"kelvinlet_settings",
                  "Set the Kelvinlet brushes' material (Brush > Kelvinlet & Capture): Squash (Poisson's ratio: 0 "
                  "rubbery, 1 incompressible clay that bulges), Sharpness, and Detail Clone's spacing. Unset values stay.",
                  {{"squash", T::Number, "0-1 (default 0.5).", false},
                   {"sharpness", T::Number, "0-1: a tighter, cusped peak (default 0).", false},
                   {"clone_spacing", T::Number, "Detail Clone: stamp spacing in stamp widths, 0.25-3 (default 1).", false},
                   {"clone_follow", T::Bool, "Detail Clone: stamps turn with the stroke (default true).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      KelvinletSettings& k = doc.sculptor.kelvinlet;
                      k.squash = float(std::clamp(argNumber(a, "squash", k.squash), 0.0, 1.0));
                      k.sharpness = float(std::clamp(argNumber(a, "sharpness", k.sharpness), 0.0, 1.0));
                      k.cloneSpacing = float(std::clamp(argNumber(a, "clone_spacing", k.cloneSpacing), 0.25, 3.0));
                      k.cloneFollowStroke = argBool(a, "clone_follow", k.cloneFollowStroke);
                      char buf[160];
                      std::snprintf(buf, sizeof buf, "Kelvinlet: Squash %.0f (Poisson's ratio %.2f), Sharpness %.0f, clone spacing %.2f",
                                    k.squash * 100.0f, k.poisson(), k.sharpness * 100.0f, k.cloneSpacing);
                      return {true, buf};
                  }});
}

} // namespace sf
