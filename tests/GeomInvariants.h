// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Geometric invariants: what a brush or tool promises, measured on dense curved meshes (docs/testing-flow.md,
// "Brush and tool promises"). A flat plane hides most edge defects (a plane cut on a plane is a no-op), so the
// fixtures are a 1.3M-triangle sphere and a saddle. Each check returns a number or a report; tests CHECK them and
// print the numbers, so a red says what an artist would see (a vertex rose, a spike at the edge, the mirror drifted).
//
//   maxRise / maxSink     how far any vertex moved along its normal (or a plane's): cut-only = no rise, build = no sink
//   edgeOutliers          spikes and stair-steps in the falloff ring: the displacement's umbrella (d_v minus the mean of
//                         its ring's d_u) over the mean edge; spike = > 0.25 (kSpike). Radial steps: a single dab's
//                         profile, in bins one edge wide, may not climb going outward (a ring or a terrace).
//   symmetryError         the largest distance between a vertex and the mirror image of its mirror vertex (X)
//   volumeChange          relative change of the enclosed volume
//   nonFinite, flipped    NaN/inf positions; triangles whose normal turned more than 90 degrees
//   invertedFaces         triangles turned against all their neighbours (a fold's black speckle; pull brushes too)
//   microSnapshot, maskedMoved, maskedOvershoot
//                         a micro level's surface points before and after a stroke: a fully masked sample (mask 255) may
//                         not move at all (bit-identical), a partly masked one may move at most (1 - mask) of what the
//                         same stroke moves it unmasked (any brush, any path: the grid's or the base cage's)
// The artist close-ups (tools/qa/closeup.py) use an absolute umbrella rule on app meshes, where there is no before.
#pragma once

#include "TestHarness.h"

#include "core/BVH.h"
#include "core/MeshBoolean.h"
#include "core/Micro.h"
#include "core/PathStroke.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace sf::geom {

constexpr float kSpike = 0.25f; // umbrella of the displacement, in mean edges, that counts as a spike

struct Fixture {
    Mesh mesh;
    BVH bvh;
};
inline Document docFrom(const Fixture& f) {
    Mesh m = f.mesh;
    BVH b = f.bvh;
    Document d;
    d.setMesh(std::move(m), "Sculpt", &b);
    return d;
}

// A unit sphere of 1.31M triangles (icosphere level 8: mirror-symmetric in X, Y and Z), built once per process.
inline const Fixture& denseSphereFixture() {
    static const Fixture f = [] {
        Fixture x;
        x.mesh = makeIcosphere(8);
        x.bvh.build(x.mesh);
        return x;
    }();
    return f;
}
inline Document denseSphere() { return docFrom(denseSphereFixture()); }

// A saddle y = k (x^2 - z^2) over 2 x 2 (res x res quads): curved both ways, so a plane brush meets surface above
// and below its plane in every dab.
inline Mesh saddle(int res = 400, float k = 0.35f) {
    Mesh m = makePlane(res, 2.0f);
    for (glm::vec3& p : m.positions) p.y = k * (p.x * p.x - p.z * p.z);
    m.recomputeAllNormals();
    return m;
}
inline const Fixture& saddleFixture() {
    static const Fixture f = [] {
        Fixture x;
        x.mesh = saddle();
        x.bvh.build(x.mesh);
        return x;
    }();
    return f;
}

inline float meanEdge(const Mesh& m) {
    double sum = 0.0;
    const size_t n = std::min<size_t>(m.triangleCount(), 200000);
    for (size_t t = 0; t < n; ++t) {
        const glm::uvec3 f = m.triangles[t];
        sum += glm::distance(m.positions[f.x], m.positions[f.y]);
    }
    return n ? float(sum / double(n)) : 0.0f;
}

// The largest move of any vertex along `dirs[v]` (its own normal before the stroke) or along one plane normal.
inline float maxRise(const std::vector<glm::vec3>& before, const Mesh& after, const std::vector<glm::vec3>& dirs) {
    float r = 0.0f;
    for (size_t v = 0; v < before.size(); ++v) r = std::max(r, glm::dot(after.positions[v] - before[v], dirs[v]));
    return r;
}
inline float maxRise(const std::vector<glm::vec3>& before, const Mesh& after, glm::vec3 n) {
    float r = 0.0f;
    for (size_t v = 0; v < before.size(); ++v) r = std::max(r, glm::dot(after.positions[v] - before[v], n));
    return r;
}
inline float maxSink(const std::vector<glm::vec3>& before, const Mesh& after, const std::vector<glm::vec3>& dirs) {
    float r = 0.0f;
    for (size_t v = 0; v < before.size(); ++v) r = std::max(r, -glm::dot(after.positions[v] - before[v], dirs[v]));
    return r;
}
inline float maxMove(const std::vector<glm::vec3>& before, const Mesh& after) {
    float r = 0.0f;
    for (size_t v = 0; v < before.size(); ++v) r = std::max(r, glm::distance(after.positions[v], before[v]));
    return r;
}
// The largest move of a vertex farther than `reach` from every point of `centers` (nothing outside the brush).
inline float maxMoveOutside(const std::vector<glm::vec3>& before, const Mesh& after, const std::vector<glm::vec3>& centers,
                            float reach) {
    float r = 0.0f;
    for (size_t v = 0; v < before.size(); ++v) {
        const float moved = glm::distance(after.positions[v], before[v]);
        if (moved <= r) continue;
        bool far = true;
        for (const glm::vec3& c : centers)
            if (glm::distance(before[v], c) <= reach) { far = false; break; }
        if (far) r = moved;
    }
    return r;
}

inline size_t nonFinite(const Mesh& m) {
    size_t n = 0;
    for (const glm::vec3& p : m.positions) n += !(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    return n;
}

// Triangles (same topology before and after) whose normal turned by more than 90 degrees.
inline size_t flippedFaces(const std::vector<glm::vec3>& before, const Mesh& after) {
    size_t n = 0;
    for (const glm::uvec3& f : after.triangles) {
        const glm::vec3 a = glm::cross(before[f.y] - before[f.x], before[f.z] - before[f.x]);
        const glm::vec3 b = glm::cross(after.positions[f.y] - after.positions[f.x], after.positions[f.z] - after.positions[f.x]);
        n += glm::dot(a, b) < 0.0f;
    }
    return n;
}
// The artist close-up's fold count (tools/qa/closeup.py): triangles that touch a moved vertex and lie against a neighbour
// across an edge (their normals more than 120 degrees apart), after the strokes less before them. Stricter than
// invertedFaces: one bad neighbour is enough, the dark crease lines of a crumpled drag count as well as the lone specks.
inline size_t foldedFaces(const std::vector<glm::vec3>& before, const Mesh& m) {
    std::vector<uint8_t> moved(m.vertexCount(), 0);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) moved[v] = m.positions[v] != before[v];
    std::vector<uint32_t> tris;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 f = m.triangles[t];
        if (moved[f.x] || moved[f.y] || moved[f.z]) tris.push_back(t);
    }
    auto count = [&](const std::vector<glm::vec3>& pos) {
        std::vector<glm::vec3> nrm(tris.size());
        std::unordered_map<uint64_t, uint32_t> first;
        std::vector<uint8_t> bad(tris.size(), 0);
        for (uint32_t i = 0; i < tris.size(); ++i) {
            const glm::uvec3 f = m.triangles[tris[i]];
            const glm::vec3 n = glm::cross(pos[f.y] - pos[f.x], pos[f.z] - pos[f.x]);
            const float l = glm::length(n);
            nrm[i] = l > 0.0f ? n / l : glm::vec3(0.0f);
            const uint32_t c[3] = {f.x, f.y, f.z};
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = std::min(c[k], c[(k + 1) % 3]), b = std::max(c[k], c[(k + 1) % 3]);
                const uint64_t key = (uint64_t(a) << 32) | b;
                auto it = first.find(key);
                if (it == first.end()) { first.emplace(key, i); continue; }
                const uint32_t j = it->second;
                if (nrm[i] != glm::vec3(0.0f) && nrm[j] != glm::vec3(0.0f) && glm::dot(nrm[i], nrm[j]) < -0.5f) bad[i] = bad[j] = 1;
            }
        }
        size_t n = 0;
        for (uint8_t b : bad) n += b;
        return n;
    };
    const size_t after = count(m.positions), start = count(before);
    return after > start ? after - start : 0;
}
// Triangles of a closed mesh around the origin that face inwards (after a remesh, where the topology changed).
inline size_t inwardFaces(const Mesh& m, glm::vec3 centre = glm::vec3(0.0f)) {
    size_t n = 0;
    for (const glm::uvec3& f : m.triangles) {
        const glm::vec3 a = m.positions[f.x], b = m.positions[f.y], c = m.positions[f.z];
        n += glm::dot(glm::cross(b - a, c - a), (a + b + c) / 3.0f - centre) < 0.0f;
    }
    return n;
}

// Triangles turned inside out against every edge neighbour (each neighbour's normal more than 120 degrees off):
// the isolated black speckle of a fold, whatever the brush means to do with the surface around it. Only triangles
// with a vertex that moved (`before`) are looked at.
inline size_t invertedFaces(const std::vector<glm::vec3>& before, const Mesh& m) {
    std::vector<uint8_t> moved(m.vertexCount(), 0);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) moved[v] = m.positions[v] != before[v];
    auto normal = [&](uint32_t t) {
        const glm::uvec3 f = m.triangles[t];
        const glm::vec3 n = glm::cross(m.positions[f.y] - m.positions[f.x], m.positions[f.z] - m.positions[f.x]);
        const float l = glm::length(n);
        return l > 0.0f ? n / l : n;
    };
    std::unordered_map<uint64_t, uint32_t> edgeTri; // directed edge (a, b) -> triangle
    std::vector<uint32_t> tris;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 f = m.triangles[t];
        if (!moved[f.x] && !moved[f.y] && !moved[f.z]) continue;
        tris.push_back(t);
    }
    // Neighbours of the moved triangles may be unmoved: index every triangle touching a moved vertex's ring.
    std::vector<uint8_t> near(m.vertexCount(), 0);
    for (uint32_t t : tris) {
        const glm::uvec3 f = m.triangles[t];
        near[f.x] = near[f.y] = near[f.z] = 1;
    }
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 f = m.triangles[t];
        if (!near[f.x] && !near[f.y] && !near[f.z]) continue;
        const uint32_t c[3] = {f.x, f.y, f.z};
        for (int k = 0; k < 3; ++k) edgeTri[(uint64_t(c[k]) << 32) | c[(k + 1) % 3]] = t;
    }
    size_t n = 0;
    for (uint32_t t : tris) {
        const glm::uvec3 f = m.triangles[t];
        const uint32_t c[3] = {f.x, f.y, f.z};
        const glm::vec3 nt = normal(t);
        int against = 0, seen = 0;
        for (int k = 0; k < 3; ++k) {
            auto it = edgeTri.find((uint64_t(c[(k + 1) % 3]) << 32) | c[k]);
            if (it == edgeTri.end()) continue;
            ++seen;
            against += glm::dot(nt, normal(it->second)) < -0.5f;
        }
        n += seen > 0 && against == seen;
    }
    return n;
}

inline double volumeChange(double before, double after) { return before != 0.0 ? (after - before) / before : 0.0; }

struct EdgeReport {
    int spikes = 0;      // ring vertices whose displacement umbrella exceeds kSpike mean edges
    float worst = 0.0f;  // the largest umbrella in the ring, in mean edges
    float interior = 0.0f; // the same inside 0.6 radii (for the print: an interior crease is the brush's own)
    int ring = 0;        // vertices measured in the ring
};
// Spikes, debris and terraces along the falloff edge: per vertex in the ring (0.6..1.1 radii from the nearest dab
// centre), the umbrella of the displacement field. The base surface's own curvature cancels (before is subtracted),
// so a clean falloff on any mesh scores near 0, and a vertex left behind or shot out scores its height in edges.
inline EdgeReport edgeOutliers(const Mesh& after, const std::vector<glm::vec3>& before, const std::vector<glm::vec3>& centers,
                               float radius, float edge) {
    EdgeReport e;
    for (uint32_t v = 0; v < after.vertexCount(); ++v) {
        float dmin = 1e30f;
        for (const glm::vec3& c : centers) dmin = std::min(dmin, glm::distance(before[v], c));
        if (dmin > 1.1f * radius) continue;
        auto ring = after.neighbors(v);
        if (ring.empty()) continue;
        glm::vec3 mean(0.0f);
        for (uint32_t u : ring) mean += after.positions[u] - before[u];
        const float o = glm::length((after.positions[v] - before[v]) - mean / float(ring.size())) / edge;
        if (dmin < 0.6f * radius) {
            e.interior = std::max(e.interior, o);
            continue;
        }
        ++e.ring;
        e.worst = std::max(e.worst, o);
        e.spikes += o > kSpike;
    }
    return e;
}

// A single dab's radial profile: the mean move along the vertex normals in bins one edge wide from 0.5 to 1.05
// radii. Steps = bins that climb (going outward) by more than 2% of the profile's peak plus `noise`: a ring, a
// wall or a terrace, where a round tip's falloff only ever comes down.
struct Profile {
    std::vector<float> h;
    int steps = 0;
    float worstClimb = 0.0f; // fraction of the peak
};
inline Profile radialProfile(const Mesh& after, const std::vector<glm::vec3>& before, const std::vector<glm::vec3>& normals,
                             glm::vec3 centre, float radius, float edge, float noise = 1e-6f) {
    Profile pr;
    const int bins = std::max(4, int(0.55f * radius / edge));
    std::vector<double> sum(size_t(bins), 0.0);
    std::vector<int> count(size_t(bins), 0);
    float peak = 0.0f;
    for (uint32_t v = 0; v < after.vertexCount(); ++v) {
        const float d = glm::distance(before[v], centre) / radius;
        const float h = std::abs(glm::dot(after.positions[v] - before[v], normals[v]));
        if (d < 1.05f) peak = std::max(peak, h);
        if (d < 0.5f || d >= 1.05f) continue;
        const int b = std::min(bins - 1, int((d - 0.5f) / 0.55f * float(bins)));
        sum[size_t(b)] += h, ++count[size_t(b)];
    }
    for (int b = 0; b < bins; ++b) pr.h.push_back(count[size_t(b)] ? float(sum[size_t(b)] / count[size_t(b)]) : -1.0f);
    float prev = -1.0f;
    for (float h : pr.h) {
        if (h < 0.0f) continue;
        if (prev >= 0.0f && h - prev > 0.02f * peak + noise) {
            ++pr.steps;
            pr.worstClimb = std::max(pr.worstClimb, peak > 0.0f ? (h - prev) / peak : 0.0f);
        }
        prev = h;
    }
    return pr;
}

// Each vertex's mirror image across x = `plane` (~0u when it has none within `tol`).
inline std::vector<uint32_t> mirrorMapX(const Mesh& m, float tol = 1e-5f, float plane = 0.0f) {
    auto key = [&](glm::vec3 p) {
        const auto q = [&](float x) { return uint64_t(std::llround(double(x) / (4.0 * tol))); };
        return (q(p.x) * 73856093u) ^ (q(p.y) * 19349663u) ^ (q(p.z) * 83492791u);
    };
    std::unordered_multimap<uint64_t, uint32_t> grid;
    grid.reserve(m.vertexCount());
    for (uint32_t v = 0; v < m.vertexCount(); ++v) grid.emplace(key(m.positions[v]), v);
    std::vector<uint32_t> map(m.vertexCount(), ~0u);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        glm::vec3 p = m.positions[v];
        p.x = 2.0f * plane - p.x;
        auto range = grid.equal_range(key(p));
        for (auto it = range.first; it != range.second; ++it)
            if (glm::distance(m.positions[it->second], p) < tol) { map[v] = it->second; break; }
    }
    return map;
}
inline float symmetryError(const Mesh& m, const std::vector<uint32_t>& map, float plane = 0.0f) {
    float worst = 0.0f;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        if (map[v] == ~0u) continue;
        glm::vec3 q = m.positions[map[v]];
        q.x = 2.0f * plane - q.x;
        worst = std::max(worst, glm::distance(m.positions[v], q));
    }
    return worst;
}

// Dabs along a great-circle arc of the unit sphere from `from` toward `toward`, `count` of them `step` radians
// apart, pressure easing from p0 to p1 (a pen stroke), viewed straight on; strokeDelta set for dragging brushes.
inline std::vector<DabParams> arc(glm::vec3 from, glm::vec3 toward, int count, float step, float radius, float strength,
                                  float p0, float p1) {
    from = glm::normalize(from);
    const glm::vec3 t = glm::normalize(toward - from * glm::dot(toward, from));
    std::vector<DabParams> out;
    glm::vec3 prev = from;
    for (int i = 0; i < count; ++i) {
        const float a = step * float(i);
        DabParams p;
        p.center = std::cos(a) * from + std::sin(a) * t;
        p.viewDir = -p.center;
        p.radius = radius;
        p.strength = strength;
        p.pressure = p0 + (p1 - p0) * float(i) / float(std::max(1, count - 1));
        p.strokeDelta = p.center - prev;
        prev = p.center;
        out.push_back(p);
    }
    return out;
}
// Two strokes crossing at `at` (the second at right angles): pressure 0.2 -> 1 then 1 -> 0.2, overlapping by
// half or more of each dab, as a pen stroke laid over another.
// `spacing`: dab spacing as a fraction of the brush size (BrushRecipe::spacing; a stamp's 1.6 gives two dabs).
inline std::vector<std::vector<DabParams>> overlapStrokes(glm::vec3 at, float radius, float strength = 0.5f,
                                                          float spacing = 0.125f) {
    at = glm::normalize(at);
    const glm::vec3 u = glm::normalize(glm::cross(at, std::abs(at.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    const glm::vec3 w = glm::cross(at, u);
    const float step = std::max(0.25f, 2.0f * spacing) * radius; // (radians on the unit sphere = world units)
    const int count = std::clamp(1 + int(3.0f * radius / step), 2, 13);
    const float half = step * float(count - 1) / 2.0f;
    auto start = [&](glm::vec3 dir) { return glm::normalize(std::cos(half) * at - std::sin(half) * dir); };
    return {arc(start(u), at, count, step, radius, strength, 0.2f, 1.0f),
            arc(start(w), at, count, step, radius, strength, 1.0f, 0.2f)};
}
inline std::vector<glm::vec3> centersOf(const std::vector<std::vector<DabParams>>& strokes) {
    std::vector<glm::vec3> c;
    for (const auto& s : strokes)
        for (const DabParams& p : s) c.push_back(p.center);
    return c;
}

// --- Micro levels (core/Micro.h) ---
// Every surface point of the base triangles whose centre faces `facing` (cosine above `minCos`; a patch of the sphere).
struct MicroSnapshot {
    std::vector<uint32_t> tris;
    std::vector<glm::vec3> points; // tris.size() * samples
};
inline MicroSnapshot microSnapshot(const Mesh& m, const glm::vec3& facing, float minCos = 0.2f) {
    MicroSnapshot s;
    const MicroMesh& mm = *m.micro;
    const glm::vec3 f = glm::normalize(facing);
    for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
        const glm::uvec3 tr = m.triangles[t];
        const glm::vec3 c = (m.positions[tr.x] + m.positions[tr.y] + m.positions[tr.z]) / 3.0f;
        if (glm::dot(glm::normalize(c), f) >= minCos) s.tris.push_back(t);
    }
    s.points.reserve(s.tris.size() * mm.samples);
    for (uint32_t t : s.tris)
        for (uint32_t j = 0; j <= mm.n; ++j)
            for (uint32_t i = 0; i + j <= mm.n; ++i) s.points.push_back(microPoint(m, t, i, j));
    return s;
}
// Samples with mask `fullAt` or more (255 = fully masked) that moved at all; `compared` counts those checked.
inline size_t maskedMoved(const Mesh& m, const MicroSnapshot& before, const MicroSnapshot& after, size_t* compared = nullptr,
                          int fullAt = 255) {
    const MicroMesh& mm = *m.micro;
    size_t moved = 0, seen = 0;
    if (!mm.hasMask()) return 0;
    for (size_t k = 0; k < before.tris.size(); ++k)
        for (uint32_t s = 0; s < mm.samples; ++s) {
            if (mm.mask[size_t(before.tris[k]) * mm.samples + s] < fullAt) continue;
            ++seen;
            const glm::vec3 a = before.points[k * mm.samples + s], b = after.points[k * mm.samples + s];
            if (std::memcmp(&a, &b, sizeof a) != 0) ++moved;
        }
    if (compared) *compared = seen;
    return moved;
}
// The worst excess, in world units, of a sample's move over (1 - mask) times what `reference` (the same stroke without
// the mask) moved it: <= 0 when partial masks scale the move down as promised.
inline float maskedOvershoot(const Mesh& m, const MicroSnapshot& before, const MicroSnapshot& after, const MicroSnapshot& reference,
                             int* worstMask = nullptr) {
    const MicroMesh& mm = *m.micro;
    float worst = 0.0f;
    if (!mm.hasMask()) return 0.0f;
    for (size_t k = 0; k < before.tris.size(); ++k)
        for (uint32_t s = 0; s < mm.samples; ++s) {
            const float free = 1.0f - float(mm.mask[size_t(before.tris[k]) * mm.samples + s]) / 255.0f;
            const size_t at = k * mm.samples + s;
            const float moved = glm::distance(before.points[at], after.points[at]);
            const float allowed = free * glm::distance(before.points[at], reference.points[at]);
            if (moved - allowed > worst) {
                worst = moved - allowed;
                if (worstMask) *worstMask = mm.mask[size_t(before.tris[k]) * mm.samples + s];
            }
        }
    return worst;
}

// --- strokes as the app lays them ---
// A brush's own dabs are not enough for the brushes that drag the surface: in the app each dab lands where the cursor's ray
// meets the surface the earlier dabs left, so a stroke feeds on its own result (and the next stroke on the last one's).
// `replayStrokes` plays recorded cursor paths (tests/data/drag_paths/*.txt: the samples of a close-up session, with its
// brush radius, pen pressure and symmetry) through core's strokePath, which does the same ray projection, and reports
// after each stroke. An invariant test of a dragging brush uses this, never dabs placed on the unmoved surface (11-11: a
// Tendril and a Nudge test on such arcs said 0 folds while the app folded thousands).
struct StrokeFixture {
    float radius = 0.2f, strength = 0.5f;
    Symmetry symmetry{false, false, false};
    glm::vec3 view{0.0f, 0.0f, -1.0f};
    struct One { std::vector<glm::vec3> points; float p0 = 1.0f, p1 = 1.0f; };
    std::vector<One> strokes;
};
inline StrokeFixture loadStrokeFixture(const std::string& name) {
    StrokeFixture f;
    std::ifstream in(std::string(SF_TEST_DATA_DIR) + "/drag_paths/" + name + ".txt");
    std::string line;
    f.strokes.resize(8);
    size_t count = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon), value = line.substr(colon + 1);
        std::istringstream v(value);
        if (key == "radius_world") v >> f.radius;
        else if (key == "strength") v >> f.strength;
        else if (key == "symmetry") f.symmetry.x = value.find('X') != std::string::npos;
        else if (key.rfind("stroke", 0) == 0 && key.size() > 6) {
            const size_t k = size_t(key[6] - '1');
            if (k >= f.strokes.size()) continue;
            count = std::max(count, k + 1);
            if (key.find("_points") != std::string::npos) {
                std::string tok;
                while (v >> tok) {
                    glm::vec3 p;
                    if (std::sscanf(tok.c_str(), "%f,%f,%f", &p.x, &p.y, &p.z) == 3) f.strokes[k].points.push_back(p);
                }
            } else if (key.find("_pressure") != std::string::npos) {
                v >> f.strokes[k].p0 >> f.strokes[k].p1;
            }
        }
    }
    f.strokes.resize(count);
    return f;
}
// Plays the fixture's strokes on `d` (one undo step each); `after(k)` runs after stroke k (0-based).
inline void replayStrokes(Document& d, const BrushRecipe* recipe, BrushType type, const StrokeFixture& f,
                          const std::function<void(size_t)>& after = {}) {
    for (size_t k = 0; k < f.strokes.size(); ++k) {
        PathStroke s;
        s.points = f.strokes[k].points;
        s.view = f.view;
        s.radius = f.radius;
        s.strength = f.strength;
        s.pressure = f.strokes[k].p0;
        s.pressureEnd = f.strokes[k].p1;
        s.curve = false;
        s.symmetry = f.symmetry;
        std::string error;
        strokePath(d, recipe, type, s, error);
        if (after) after(k);
    }
}

} // namespace sf::geom

