// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh 2: quad remeshing by an integer-grid map, the default method of Tool > Remesh > QRemesh.
//
// The legacy method (Instant Meshes style, core/QRemesh.cpp) rounds a local position field: every place where the
// local lattices disagree becomes an extra pole, and its result is extracted at twice the spacing and split, so
// its loops wander. This method solves one global map instead, so poles appear only where the cross field needs
// them (eight on a sphere-like shape, a handful more per feature) and edge loops run unbroken along the form.
//
//  1. Proxy surface (QRemesh2Proxy.cpp): the piece decimated and remeshed to about three triangle edges per quad
//     edge, feature lines kept. Sizes: the target count, Adaptive Size (curvature) and polypaint density set a
//     wanted quad size per vertex; the proxy is refined where quads will be small.
//  2. Features as hard constraints: open edges (and the symmetry cut), polygroup borders (Keep Groups), hard edges
//     (Detect Edges, chains shorter than a quad dropped), QRemesh Guides snapped onto the proxy as edge chains.
//  3. Cross field (QRemesh2Field.cpp) following smoothed principal curvature directions and the features.
//  4. Integer-grid map (QRemesh2Param.cpp): its integer lattice lines are the edge loops, feature lines and guides
//     lie exactly on lattice lines (so a guide is an exact edge loop) and singularities on lattice points.
//  5. Extraction: proxy vertices rounding to the same lattice point form one quad vertex (placed exactly on that
//     point), lattice neighbours are joined, faces traced. The rare non-quad faces (where the map folds) are
//     paired off along short paths and split, so the result is all quads at the target density, not a split mesh.
//  6. Loop-aware relaxation: smoothing on the sculpt that only lets a vertex slide along the loops running over
//     ridges (so they stay on the ridge), then projection onto the sculpt.
//
// Symmetry is handled by the caller: the half on the + side is remeshed with the cut as a feature line (so the
// seam is a lattice line) and mirrored, which makes the field and the result exactly symmetric. Everything is
// deterministic: no random starts, fixed-block parallel reductions.
#include "core/QRemesh2.h"

#include "core/Parallel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <numeric>
#include <unordered_map>

namespace sf::qr {

using namespace qr2;
using glm::ivec2;

namespace {

struct UF {
    std::vector<uint32_t> parent;
    explicit UF(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32_t find(uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    }
    void unite(uint32_t a, uint32_t b) {
        a = find(a), b = find(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
};

vec3 anyTangent(const vec3& n) {
    const vec3 a = std::abs(n.x) < 0.6f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    return safeNormalize(glm::cross(n, a));
}

// Edges of a triangle mesh with the triangles on each side (count 1: open, 2: interior, more: non-manifold).
struct SrcEdge {
    uint32_t a, b, t0, t1;
    int count;
};
std::vector<SrcEdge> meshEdges(const std::vector<vec3>& P, const std::vector<glm::uvec3>& T) {
    const size_t nv = P.size();
    std::vector<uint32_t> off(nv + 1, 0), vt(T.size() * 3);
    for (const auto& t : T)
        for (int k = 0; k < 3; ++k) ++off[t[k] + 1];
    for (size_t i = 0; i < nv; ++i) off[i + 1] += off[i];
    {
        std::vector<uint32_t> fill(off.begin(), off.end() - 1);
        for (uint32_t t = 0; t < T.size(); ++t)
            for (int k = 0; k < 3; ++k) vt[fill[T[t][k]]++] = t;
    }
    const size_t blocks = (nv + 8191) / 8192;
    std::vector<std::vector<SrcEdge>> parts(blocks);
    parallelJobs(blocks, [&](size_t blk) {
        std::vector<std::pair<uint32_t, uint32_t>> loc;
            for (size_t a = blk * 8192, e = std::min(nv, a + 8192); a < e; ++a) {
                loc.clear();
                for (uint32_t k = off[a]; k < off[a + 1]; ++k) {
                    const uint32_t t = vt[k];
                    for (int j = 0; j < 3; ++j) {
                        const uint32_t x = T[t][j], y = T[t][(j + 1) % 3];
                        if (x == y) continue;
                        if (x == a && y > a) loc.push_back({y, t});
                        else if (y == a && x > a) loc.push_back({x, t});
                    }
                }
                std::sort(loc.begin(), loc.end());
                for (size_t i = 0; i < loc.size();) {
                    size_t j = i;
                    while (j < loc.size() && loc[j].first == loc[i].first) ++j;
                    parts[blk].push_back({uint32_t(a), loc[i].first, loc[i].second, j - i > 1 ? loc[i + 1].second : kNone,
                                          int(j - i)});
                    i = j;
                }
            }
    });
    std::vector<SrcEdge> all;
    for (auto& p : parts) all.insert(all.end(), p.begin(), p.end());
    return all;
}

double meshArea(const std::vector<vec3>& P, const std::vector<glm::uvec3>& T) {
    double a = 0.0;
    for (const auto& t : T) a += 0.5 * double(glm::length(glm::cross(P[t.y] - P[t.x], P[t.z] - P[t.x])));
    return a;
}

// Vertex -> triangles (CSR).
struct VertexFaces {
    std::vector<uint32_t> off, tri;
    void build(size_t nv, const std::vector<glm::uvec3>& T) {
        off.assign(nv + 1, 0);
        for (const auto& t : T)
            for (int k = 0; k < 3; ++k) ++off[t[k] + 1];
        for (size_t i = 0; i < nv; ++i) off[i + 1] += off[i];
        tri.resize(off[nv]);
        std::vector<uint32_t> fill(off.begin(), off.end() - 1);
        for (uint32_t t = 0; t < T.size(); ++t)
            for (int k = 0; k < 3; ++k) tri[fill[T[t][k]]++] = t;
    }
};

// --- Guides ---------------------------------------------------------------------------------------------------------

// Snaps each guide onto the proxy as a chain of edges (marked as features, so the map puts a lattice line on it) and
// moves the chain's vertices onto the curve. Also the soft pull of the field towards the curves around them.
void applyGuides(Proxy& p, const std::vector<QRemeshGuide>& guides, float strength, float h, FieldSettings& fs,
                 const Frames& frames) {
    if (guides.empty()) return;
    const size_t nv = p.P.size(), nt = p.T.size();
    VertexFaces vf;
    vf.build(nv, p.T);
    TriangleBVH bvh;
    bvh.build(p.P, p.T);
    // Edge -> corners.
    std::unordered_map<uint64_t, std::vector<uint32_t>> corners;
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) corners[edgeKey(p.T[t][k], p.T[t][(k + 1) % 3])].push_back(3 * t + uint32_t(k));
    std::vector<vec3> segA, segB;
    std::vector<uint8_t> onGuide(nv, 0);
    std::vector<vec3> moveTo(nv);
    for (const QRemeshGuide& g : guides) {
        if (g.points.size() < 2) continue;
        SegmentBVH curve;
        std::vector<vec3> curveA, curveB;
        {
            for (size_t i = 0; i + 1 < g.points.size(); ++i) curveA.push_back(g.points[i]), curveB.push_back(g.points[i + 1]);
            if (g.closed) curveA.push_back(g.points.back()), curveB.push_back(g.points.front());
            segA.insert(segA.end(), curveA.begin(), curveA.end());
            segB.insert(segB.end(), curveB.begin(), curveB.end());
            curve.build(curveA, curveB);
        }
        // Snap samples to proxy vertices.
        std::vector<uint32_t> snapped;
        for (const vec3& q : g.points) {
            SurfaceHit hit;
            if (!bvh.closestPoint(q, 4.0f * h, hit)) continue;
            const glm::uvec3 tri = p.T[hit.prim];
            uint32_t best = tri.x;
            float bd = 1e30f;
            for (int k = 0; k < 3; ++k) {
                const float d = glm::length(p.P[tri[k]] - q);
                if (d < bd) bd = d, best = tri[k];
            }
            if (snapped.empty() || snapped.back() != best) snapped.push_back(best);
        }
        if (g.closed && snapped.size() > 2 && snapped.front() != snapped.back()) snapped.push_back(snapped.front());
        if (snapped.size() < 2) continue;
        // Shortest paths between consecutive snapped vertices, penalising distance from the curve.
        auto offCurve = [&](const vec3& x) {
            SurfaceHit hit;
            return curve.closestPoint(x, 4.0f * h, hit) ? hit.distance : 4.0f * h;
        };
        std::vector<uint32_t> chain{snapped[0]};
        for (size_t i = 0; i + 1 < snapped.size(); ++i) {
            const uint32_t s = snapped[i], goal = snapped[i + 1];
            std::unordered_map<uint32_t, std::pair<float, uint32_t>> dist; // vertex -> (cost, previous)
            using Item = std::pair<float, uint32_t>;
            std::vector<Item> heap{{0.0f, s}};
            dist[s] = {0.0f, kNone};
            bool found = false;
            while (!heap.empty()) {
                std::pop_heap(heap.begin(), heap.end(), std::greater<>());
                const auto [d, v] = heap.back();
                heap.pop_back();
                if (d > dist[v].first) continue;
                if (v == goal) {
                    found = true;
                    break;
                }
                if (d > 8.0f * h) break;
                for (uint32_t k = vf.off[v]; k < vf.off[v + 1]; ++k) {
                    const glm::uvec3 tri = p.T[vf.tri[k]];
                    for (int j = 0; j < 3; ++j) {
                        const uint32_t u = tri[j];
                        if (u == v) continue;
                        const float w = glm::length(p.P[u] - p.P[v]) + 2.0f * offCurve(0.5f * (p.P[u] + p.P[v])) + 2.0f * offCurve(p.P[u]);
                        const float nd = d + w;
                        auto it = dist.find(u);
                        if (it == dist.end() || nd < it->second.first) {
                            dist[u] = {nd, v};
                            heap.push_back({nd, u});
                            std::push_heap(heap.begin(), heap.end(), std::greater<>());
                        }
                    }
                }
            }
            if (!found) continue;
            std::vector<uint32_t> path;
            for (uint32_t v = goal; v != s && v != kNone; v = dist[v].second) path.push_back(v);
            std::reverse(path.begin(), path.end());
            for (uint32_t v : path) {
                if (std::find(chain.end() - std::min<size_t>(chain.size(), 3), chain.end(), v) != chain.end()) continue;
                chain.push_back(v);
            }
        }
        if (std::getenv("SF_QREMESH_TIMING")) {
            double len = 0.0;
            for (size_t i = 0; i + 1 < chain.size(); ++i) len += glm::length(p.P[chain[i]] - p.P[chain[i + 1]]);
            std::fprintf(stderr, "  guide: %zu samples, %zu snapped, chain %zu vertices, length %.3f, closed %d (ends %u %u)\n", g.points.size(), snapped.size(), chain.size(), len, int(g.closed), chain.front(), chain.back());
        }
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            const auto it = corners.find(edgeKey(chain[i], chain[i + 1]));
            if (it == corners.end()) continue;
            for (uint32_t c : it->second) {
                p.feature[c] = 1;
                if (p.guide.size() != 3 * nt) p.guide.assign(3 * nt, 0);
                p.guide[c] = 1;
                SurfaceHit hit;
                const vec3 mid = 0.5f * (p.P[chain[i]] + p.P[chain[i + 1]]);
                if (curve.closestPoint(mid, 4.0f * h, hit)) {
                    vec3 dir = curveB[hit.prim] - curveA[hit.prim];
                    if (glm::dot(dir, p.P[chain[i + 1]] - p.P[chain[i]]) < 0.0f) dir = -dir;
                    if (p.fixedDir.size() != nt) p.fixedDir.assign(nt, vec3(0.0f));
                    p.fixedDir[c / 3] = safeNormalize(dir);
                }
            }
            for (uint32_t v : {chain[i], chain[i + 1]}) {
                if (onGuide[v]) continue;
                SurfaceHit hit;
                if (curve.closestPoint(p.P[v], 2.0f * h, hit)) {
                    onGuide[v] = 1;
                    moveTo[v] = hit.position;
                }
            }
        }
    }
    // Move the chains onto the curves where that turns no triangle over.
    for (uint32_t v = 0; v < nv; ++v) {
        if (!onGuide[v]) continue;
        bool ok = true;
        for (uint32_t k = vf.off[v]; k < vf.off[v + 1] && ok; ++k) {
            const glm::uvec3 tri = p.T[vf.tri[k]];
            vec3 q[3];
            for (int j = 0; j < 3; ++j) q[j] = tri[j] == v ? moveTo[v] : p.P[tri[j]];
            const vec3 nNew = glm::cross(q[1] - q[0], q[2] - q[0]);
            const vec3 nOld = glm::cross(p.P[tri.y] - p.P[tri.x], p.P[tri.z] - p.P[tri.x]);
            ok = glm::dot(nNew, nOld) > 0.2f * glm::length(nNew) * glm::length(nOld);
        }
        if (ok) p.P[v] = moveTo[v];
    }
    // Soft pull around the curves: Curves Strength 0 = only the loop itself, 1 = about six quads to each side.
    if (segA.empty()) return;
    SegmentBVH all;
    all.build(segA, segB);
    const float reach = h * (0.5f + 5.5f * std::clamp(strength, 0.0f, 1.0f)) * (std::getenv("SF_QR2_GREACH") ? float(std::atof(std::getenv("SF_QR2_GREACH"))) : 1.0f);
    fs.guideAngle.assign(nt, std::numeric_limits<float>::quiet_NaN());
    fs.guideWeight.assign(nt, 0.0f);
    for (uint32_t t = 0; t < nt; ++t) {
        const glm::uvec3 tri = p.T[t];
        const vec3 c = (p.P[tri.x] + p.P[tri.y] + p.P[tri.z]) / 3.0f;
        SurfaceHit hit;
        if (!all.closestPoint(c, 2.0f * reach, hit)) continue;
        const vec3 dir = segB[hit.prim] - segA[hit.prim];
        fs.guideAngle[t] = std::atan2(glm::dot(dir, frames.e2[t]), glm::dot(dir, frames.e1[t]));
        const float x = hit.distance / std::max(reach, 1e-12f);
        fs.guideWeight[t] = std::exp(-2.0f * x * x);
    }
}

// --- Extraction -----------------------------------------------------------------------------------------------------

struct Poly {
    std::vector<vec3> P;
    std::vector<std::vector<uint32_t>> faces;
};

// The integer lattice of the map as a polygon mesh: proxy vertices rounding to the same lattice point (within a
// triangle's chart) are one vertex, placed on the point; lattice neighbours are joined; faces traced counter-clockwise.
Poly extractLattice(const Proxy& p, const Frames& fr, const GridMap& map, float h) {
    const size_t nv = p.P.size(), nt = p.T.size();
    std::vector<ivec2> L(3 * nt);
    for (size_t c = 0; c < 3 * nt; ++c) L[c] = {int(std::lround(map.uv[c].x)), int(std::lround(map.uv[c].y))};
    std::vector<uint8_t> flipped(nt, 0);
    for (uint32_t t = 0; t < nt; ++t) {
        const vec2 a = map.uv[3 * t], b = map.uv[3 * t + 1], c = map.uv[3 * t + 2];
        flipped[t] = !((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) > 0.0f);
    }
    // Lattice points no proxy corner rounds to (where the map stretches, often along a seam or a feature line next
    // to a singularity) become extra nodes: every integer point inside a triangle that none of its corners
    // represents, the same point from neighbouring triangles merged by position.
    struct Virt {
        uint32_t tri, node;
        ivec2 g;
    };
    std::vector<Virt> virt;
    std::vector<vec3> virtPos, virtNrm;
    std::vector<uint8_t> virtOpen;
    {
        std::unordered_map<uint64_t, uint32_t> byPos;
        const float q = 1e-4f * std::max(h, 1e-12f);
        for (uint32_t t = 0; t < nt; ++t) {
            if (flipped[t]) continue;
            const vec2 a = map.uv[3 * t], b = map.uv[3 * t + 1], c = map.uv[3 * t + 2];
            const float det = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (!(det > 1e-9f)) continue;
            const vec2 lo = glm::min(a, glm::min(b, c)), hi = glm::max(a, glm::max(b, c));
            if (hi.x - lo.x > 64.0f || hi.y - lo.y > 64.0f) continue;
            for (int gy = int(std::ceil(lo.y - 1e-4f)); gy <= int(std::floor(hi.y + 1e-4f)); ++gy)
                for (int gx = int(std::ceil(lo.x - 1e-4f)); gx <= int(std::floor(hi.x + 1e-4f)); ++gx) {
                    const ivec2 g(gx, gy);
                    if (L[3 * t] == g || L[3 * t + 1] == g || L[3 * t + 2] == g) continue;
                    const vec2 pt{float(gx), float(gy)};
                    const float w1 = ((pt.x - a.x) * (c.y - a.y) - (c.x - a.x) * (pt.y - a.y)) / det;
                    const float w2 = ((b.x - a.x) * (pt.y - a.y) - (pt.x - a.x) * (b.y - a.y)) / det;
                    const float w0 = 1.0f - w1 - w2;
                    if (std::min({w0, w1, w2}) < -1e-4f) continue;
                    const vec3 at = w0 * p.P[p.T[t].x] + w1 * p.P[p.T[t].y] + w2 * p.P[p.T[t].z];
                    const uint64_t key = (uint64_t(uint32_t(int64_t(std::floor(at.x / q)) & 0x1fffff)) << 42) |
                                         (uint64_t(uint32_t(int64_t(std::floor(at.y / q)) & 0x1fffff)) << 21) |
                                         uint64_t(uint32_t(int64_t(std::floor(at.z / q)) & 0x1fffff));
                    auto [it, inserted] = byPos.try_emplace(key, uint32_t(virtPos.size()));
                    if (inserted) {
                        virtPos.push_back(at);
                        virtNrm.push_back(fr.n[t]);
                        virtOpen.push_back(0);
                    }
                    // On an open edge of the proxy (the weight of the opposite corner is ~0)?
                    const float w[3] = {w0, w1, w2};
                    for (int k = 0; k < 3; ++k)
                        if (w[(k + 2) % 3] < 1e-4f && p.opp[3 * t + uint32_t(k)] == kNone) virtOpen[it->second] = 1;
                    virt.push_back({t, it->second, g});
                }
        }
    }
    const size_t nn = nv + virtPos.size();
    // Folded triangles (the map turned over) would merge unrelated lattice points: they don't join anything.
    UF uf(nn);
    for (uint32_t t = 0; t < nt; ++t) {
        if (flipped[t]) continue;
        for (int i = 0; i < 3; ++i) {
            const int j = (i + 1) % 3;
            if (L[3 * t + size_t(i)] == L[3 * t + size_t(j)]) uf.unite(p.T[t][i], p.T[t][j]);
        }
    }
    // A virtual node whose lattice point a vertex one ring away already rounds to (in the same chart: the shared
    // vertex has the same map value in both triangles) is that vertex's lattice point.
    if (!virt.empty()) {
        VertexFaces vf;
        vf.build(nv, p.T);
        for (const Virt& v : virt)
            for (int k = 0; k < 3; ++k) {
                const uint32_t x = p.T[v.tri][k];
                const vec2 at = map.uv[3 * v.tri + uint32_t(k)];
                for (uint32_t i = vf.off[x]; i < vf.off[x + 1]; ++i) {
                    const uint32_t t2 = vf.tri[i];
                    if (t2 == v.tri || flipped[t2]) continue;
                    int kx = 0;
                    while (kx < 3 && p.T[t2][kx] != x) ++kx;
                    if (glm::length(map.uv[3 * t2 + uint32_t(kx)] - at) > 1e-4f) continue; // across a seam
                    for (int j = 0; j < 3; ++j)
                        if (L[3 * t2 + uint32_t(j)] == v.g) uf.unite(uint32_t(nv + v.node), p.T[t2][j]);
                }
            }
    }
    std::vector<uint32_t> cluster(nn, kNone);
    uint32_t nc = 0;
    for (uint32_t v = 0; v < nn; ++v) {
        const uint32_t r = uf.find(v);
        if (cluster[r] == kNone) cluster[r] = nc++;
        cluster[v] = cluster[r];
    }
    // Positions: the lattice point inside (or nearest) a triangle around the cluster, by its barycentric weights.
    std::vector<vec3> pos(nc, vec3(0.0f));
    std::vector<float> fit(nc, -1e30f);
    std::vector<vec3> nrm(nc, vec3(0.0f));
    for (uint32_t t = 0; t < nt; ++t) {
        const vec2 a = map.uv[3 * t], b = map.uv[3 * t + 1], c = map.uv[3 * t + 2];
        const float det = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        for (int k = 0; k < 3; ++k) {
            const uint32_t cl = cluster[p.T[t][k]];
            nrm[cl] += fr.n[t] * fr.area[t];
            if (std::abs(det) < 1e-12f) continue;
            const vec2 q(float(L[3 * t + size_t(k)].x), float(L[3 * t + size_t(k)].y));
            const float w1 = ((q.x - a.x) * (c.y - a.y) - (c.x - a.x) * (q.y - a.y)) / det;
            const float w2 = ((b.x - a.x) * (q.y - a.y) - (q.x - a.x) * (b.y - a.y)) / det;
            const float w0 = 1.0f - w1 - w2;
            float score = std::min({w0, w1, w2});
            if (flipped[t]) score -= 10.0f;
            if (score > fit[cl]) {
                fit[cl] = score;
                const float c0 = std::clamp(w0, 0.0f, 1.0f), c1 = std::clamp(w1, 0.0f, 1.0f), c2 = std::clamp(w2, 0.0f, 1.0f);
                const float s = c0 + c1 + c2;
                pos[cl] = s > 0.0f ? (c0 * p.P[p.T[t].x] + c1 * p.P[p.T[t].y] + c2 * p.P[p.T[t].z]) / s : p.P[p.T[t][k]];
            }
        }
    }
    for (size_t i = 0; i < virtPos.size(); ++i) {
        const uint32_t cl = cluster[nv + i];
        pos[cl] = virtPos[i];
        fit[cl] = 0.0f;
        nrm[cl] += virtNrm[i];
    }
    for (auto& n : nrm) n = safeNormalize(n);
    // Lattice edges.
    std::vector<std::vector<uint32_t>> adj(nc);
    auto link = [&](uint32_t a, uint32_t b) {
        if (a == b) return;
        adj[a].push_back(b);
        adj[b].push_back(a);
    };
    for (uint32_t t = 0; t < nt; ++t)
        for (int i = 0; i < 3 && !flipped[t]; ++i) {
            const int j = (i + 1) % 3;
            const ivec2 d = L[3 * t + size_t(i)] - L[3 * t + size_t(j)];
            if (std::abs(d.x) + std::abs(d.y) != 1) continue;
            link(cluster[p.T[t][i]], cluster[p.T[t][j]]);
        }
    for (size_t i = 0; i < virt.size(); ++i) {
        const Virt& v = virt[i];
        const uint32_t a = cluster[nv + v.node];
        for (int k = 0; k < 3; ++k) {
            const ivec2 d = L[3 * v.tri + uint32_t(k)] - v.g;
            if (std::abs(d.x) + std::abs(d.y) == 1) link(a, cluster[p.T[v.tri][k]]);
        }
        for (size_t j = i + 1; j < virt.size() && virt[j].tri == v.tri; ++j) {
            const ivec2 d = virt[j].g - v.g;
            if (std::abs(d.x) + std::abs(d.y) == 1) link(a, cluster[nv + virt[j].node]);
        }
    }
    for (auto& a : adj) {
        std::sort(a.begin(), a.end());
        a.erase(std::unique(a.begin(), a.end()), a.end());
    }
    auto removeEdge = [&](uint32_t a, uint32_t b) {
        adj[a].erase(std::remove(adj[a].begin(), adj[a].end(), b), adj[a].end());
        adj[b].erase(std::remove(adj[b].begin(), adj[b].end(), a), adj[b].end());
    };
    auto hasEdge = [&](uint32_t a, uint32_t b) { return std::binary_search(adj[a].begin(), adj[a].end(), b); };
    // A quad lattice has no 3-cycles: drop the longest edge of each.
    for (int round = 0; round < 6; ++round) {
        std::vector<std::pair<uint32_t, uint32_t>> drop;
        for (uint32_t u = 0; u < nc; ++u)
            for (uint32_t v : adj[u]) {
                if (v <= u) continue;
                for (uint32_t w : adj[u]) {
                    if (w <= v || !hasEdge(v, w)) continue;
                    const float luv = glm::length(pos[u] - pos[v]), lvw = glm::length(pos[v] - pos[w]), luw = glm::length(pos[u] - pos[w]);
                    if (luv >= lvw && luv >= luw) drop.push_back({u, v});
                    else if (lvw >= luw) drop.push_back({v, w});
                    else drop.push_back({u, w});
                }
            }
        if (drop.empty()) break;
        for (auto [a, b] : drop)
            if (hasEdge(a, b)) removeEdge(a, b);
    }
    Poly out;
    std::vector<std::vector<uint32_t>> faces;
    for (int round = 0; round < 12; ++round) {
        std::vector<uint32_t> stack;
        for (uint32_t u = 0; u < nc; ++u)
            if (adj[u].size() == 1) stack.push_back(u);
        while (!stack.empty()) {
            const uint32_t u = stack.back();
            stack.pop_back();
            if (adj[u].size() != 1) continue;
            const uint32_t v = adj[u][0];
            removeEdge(u, v);
            if (adj[v].size() == 1) stack.push_back(v);
        }
        std::vector<std::vector<uint32_t>> ring(nc);
        std::vector<uint32_t> heOff(nc + 1, 0);
        for (uint32_t u = 0; u < nc; ++u) {
            const vec3 n = nrm[u];
            const vec3 q = anyTangent(n), t = glm::cross(n, q);
            std::vector<std::pair<float, uint32_t>> ang;
            for (uint32_t v : adj[u]) {
                const vec3 d = pos[v] - pos[u];
                ang.push_back({std::atan2(glm::dot(d, t), glm::dot(d, q)), v});
            }
            std::sort(ang.begin(), ang.end());
            for (auto& [a, v] : ang) ring[u].push_back(v);
            heOff[u + 1] = heOff[u] + uint32_t(ring[u].size());
        }
        auto slot = [&](uint32_t u, uint32_t v) {
            const auto& r = ring[u];
            return uint32_t(std::find(r.begin(), r.end(), v) - r.begin());
        };
        std::vector<uint8_t> used(heOff[nc], 0);
        faces.clear();
        std::vector<std::pair<uint32_t, uint32_t>> bad;
        for (uint32_t u0 = 0; u0 < nc; ++u0)
            for (uint32_t k0 = 0; k0 < ring[u0].size(); ++k0) {
                if (used[heOff[u0] + k0]) continue;
                std::vector<uint32_t> face;
                uint32_t u = u0, k = k0;
                while (!used[heOff[u] + k]) {
                    used[heOff[u] + k] = 1;
                    face.push_back(u);
                    const uint32_t v = ring[u][k];
                    const uint32_t back = slot(v, u);
                    const uint32_t deg = uint32_t(ring[v].size());
                    k = (back + deg - 1) % deg;
                    u = v;
                    if (face.size() > 64) break;
                }
                std::unordered_map<uint64_t, int> seen;
                for (size_t a = 0; a < face.size(); ++a) {
                    const uint64_t key = edgeKey(face[a], face[(a + 1) % face.size()]);
                    if (++seen[key] == 2) bad.push_back({face[a], face[(a + 1) % face.size()]});
                }
                faces.push_back(std::move(face));
            }
        if (bad.empty() || round == 11) break;
        for (auto [a, b] : bad)
            if (hasEdge(a, b)) removeEdge(a, b);
    }
    // The outside of an open surface traces as one big face (mostly on the boundary): leave it out. A face whose
    // vertices are mostly on open proxy edges is that.
    std::vector<uint8_t> openCl(nc, 0);
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k)
            if (p.opp[3 * t + uint32_t(k)] == kNone) openCl[cluster[p.T[t][k]]] = openCl[cluster[p.T[t][(k + 1) % 3]]] = 1;
    for (size_t i = 0; i < virtOpen.size(); ++i)
        if (virtOpen[i]) openCl[cluster[nv + i]] = 1;
    // The outside walk (along the open edges) can bite into the surface where links along the open edge are missing:
    // each run of inside vertices between two open-edge vertices is a face of its own, else it would be a hole.
    {
        std::vector<std::vector<uint32_t>> bites;
        for (const auto& f : faces) {
            size_t onB = 0;
            for (uint32_t v : f) onB += openCl[v];
            if (f.size() <= 32 && onB != f.size()) continue;
            if (onB * 10 < f.size() * 6 && f.size() <= 400) continue; // kept whole above
            if (onB == 0 || onB == f.size()) continue;
            const size_t n = f.size();
            size_t start = 0;
            while (!openCl[f[start]]) ++start;
            for (size_t k = 0; k < n;) {
                const size_t i = (start + k) % n;
                if (!openCl[f[(i + 1) % n]]) {
                    std::vector<uint32_t> bite{f[i]};
                    size_t j = (i + 1) % n, steps = 0;
                    while (!openCl[f[j]] && steps < n) bite.push_back(f[j]), j = (j + 1) % n, ++steps;
                    bite.push_back(f[j]);
                    if (bite.size() >= 3 && bite.size() <= 16 && bite.front() != bite.back()) bites.push_back(bite);
                    k += steps + 1;
                } else {
                    ++k;
                }
            }
        }
        if (std::getenv("SF_QREMESH_TIMING") && !bites.empty()) std::fprintf(stderr, "  v2 %zu bites refilled\n", bites.size());
        faces.insert(faces.end(), bites.begin(), bites.end());
    }
    std::vector<uint32_t> remap(nc, kNone);
    for (auto& f : faces) {
        if (f.size() < 3) continue;
        if (f.size() > 32) { // the outside of an open surface, or a big hole in a closed one (kept, split later)
            size_t onB = 0;
            for (uint32_t v : f) onB += openCl[v];
            if (onB * 10 >= f.size() * 6 || f.size() > 400) {
                if (std::getenv("SF_QREMESH_TIMING")) std::fprintf(stderr, "  v2 dropped a face of %zu sides\n", f.size());
                continue;
            }
        }
        if (f.size() >= 5) { // the outside traces along the open edges only; a face by them is kept (else a hole)
            size_t onB = 0;
            for (uint32_t v : f) onB += openCl[v];
            if (onB == f.size() || (f.size() > 12 && onB * 10 >= f.size() * 8)) {
                if (std::getenv("SF_QREMESH_TIMING")) {
                    std::fprintf(stderr, "  v2 outside face: %zu sides, %zu open:", f.size(), onB);
                    for (uint32_t v : f) std::fprintf(stderr, " (%.3f %.3f %.3f)", pos[v].x, pos[v].y, pos[v].z);
                    std::fprintf(stderr, "\n");
                }
                continue;
            }
        }
        {
            std::unordered_map<uint64_t, int> seen;
            bool twice = false;
            for (size_t a = 0; a < f.size(); ++a)
                if (++seen[edgeKey(f[a], f[(a + 1) % f.size()])] == 2) twice = true;
            if (twice) {
                if (std::getenv("SF_QREMESH_TIMING")) {
                    std::fprintf(stderr, "  v2 face with a repeated edge: %zu sides:", f.size());
                    for (uint32_t v : f) std::fprintf(stderr, " (%.3f %.3f %.3f)", pos[v].x, pos[v].y, pos[v].z);
                    std::fprintf(stderr, "\n");
                }
                continue;
            }
        }
        for (uint32_t& v : f) {
            if (remap[v] == kNone) {
                remap[v] = uint32_t(out.P.size());
                out.P.push_back(pos[v]);
            }
            v = remap[v];
        }
        out.faces.push_back(f);
    }
    return out;
}

// --- All quads ------------------------------------------------------------------------------------------------------

// Splits an even polygon into quads by diagonals (shortest first, both halves even).
void splitEven(const std::vector<vec3>& P, const std::vector<uint32_t>& f, std::vector<glm::uvec4>& quads) {
    const size_t n = f.size();
    if (n < 4) return;
    if (n == 4) {
        quads.push_back({f[0], f[1], f[2], f[3]});
        return;
    }
    float best = 1e30f;
    size_t bi = 0, bj = 3;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 3; j < n; j += 2) {
            if (n - (j - i) < 3) continue;
            if (f[i] == f[j]) continue;
            const float l = glm::length(P[f[i]] - P[f[j]]);
            if (l < best) best = l, bi = i, bj = j;
        }
    std::vector<uint32_t> a(f.begin() + long(bi), f.begin() + long(bj) + 1), b(f.begin() + long(bj), f.end());
    b.insert(b.end(), f.begin(), f.begin() + long(bi) + 1);
    splitEven(P, a, quads);
    splitEven(P, b, quads);
}

// Odd faces (triangles, pentagons) are paired along short paths of faces: each edge crossed gets a middle vertex,
// so every face on the path gains two (or the ends one) and becomes even; then even faces are split into quads.
std::vector<glm::uvec4> allQuads(Poly& pm, std::vector<glm::uvec3>& leftover) {
    // Big faces first: cut faces of 6+ sides into even pieces along short diagonals where possible.
    {
        std::vector<std::vector<uint32_t>> next;
        for (auto& f : pm.faces) {
            if (f.size() <= 5) {
                next.push_back(f);
                continue;
            }
            std::vector<std::vector<uint32_t>> todo{f};
            while (!todo.empty()) {
                auto g = std::move(todo.back());
                todo.pop_back();
                if (g.size() <= 5) {
                    next.push_back(std::move(g));
                    continue;
                }
                const size_t n = g.size();
                float best = 1e30f;
                size_t bi = 0, bj = 0;
                for (size_t i = 0; i < n; ++i)
                    for (size_t j = i + 3; j < n; ++j) {
                        const size_t a = j - i + 1, b = n - (j - i) + 1;
                        if (b < 4 || g[i] == g[j]) continue;
                        float s = glm::length(pm.P[g[i]] - pm.P[g[j]]);
                        if (a % 2) s *= 1.5f;
                        if (b % 2) s *= 1.5f;
                        if (s < best) best = s, bi = i, bj = j;
                    }
                if (best == 1e30f) {
                    next.push_back(std::move(g));
                    continue;
                }
                std::vector<uint32_t> fa(g.begin() + long(bi), g.begin() + long(bj) + 1), fb(g.begin() + long(bj), g.end());
                fb.insert(fb.end(), g.begin(), g.begin() + long(bi) + 1);
                todo.push_back(std::move(fa));
                todo.push_back(std::move(fb));
            }
        }
        pm.faces.swap(next);
    }
    // Edge -> faces.
    std::unordered_map<uint64_t, std::vector<uint32_t>> ef;
    auto rebuildEdges = [&] {
        ef.clear();
        for (uint32_t i = 0; i < pm.faces.size(); ++i) {
            const auto& f = pm.faces[i];
            for (size_t k = 0; k < f.size(); ++k) ef[edgeKey(f[k], f[(k + 1) % f.size()])].push_back(i);
        }
    };
    rebuildEdges();
    auto insertMid = [&](uint32_t a, uint32_t b) {
        const uint32_t m = uint32_t(pm.P.size());
        pm.P.push_back(0.5f * (pm.P[a] + pm.P[b]));
        const auto it = ef.find(edgeKey(a, b));
        std::vector<uint32_t> fs = it != ef.end() ? it->second : std::vector<uint32_t>{};
        ef.erase(edgeKey(a, b));
        for (uint32_t fi : fs) {
            auto& f = pm.faces[fi];
            for (size_t k = 0; k < f.size(); ++k) {
                const uint32_t x = f[k], y = f[(k + 1) % f.size()];
                if ((x == a && y == b) || (x == b && y == a)) {
                    f.insert(f.begin() + long(k) + 1, m);
                    break;
                }
            }
            ef[edgeKey(a, m)].push_back(fi);
            ef[edgeKey(m, b)].push_back(fi);
        }
        return m;
    };
    std::vector<uint8_t> matched(pm.faces.size(), 0);
    for (uint32_t start = 0; start < pm.faces.size(); ++start) {
        if (pm.faces[start].size() % 2 == 0 || matched[start]) continue;
        // Breadth-first search over faces for the nearest other unmatched odd face (or an open edge).
        std::unordered_map<uint32_t, std::pair<uint32_t, uint64_t>> prev; // face -> (previous face, edge crossed)
        std::vector<uint32_t> queue{start};
        prev[start] = {kNone, 0};
        uint32_t goal = kNone;
        uint64_t openEdge = 0;
        uint32_t openFace = kNone;
        for (size_t qi = 0; qi < queue.size() && goal == kNone; ++qi) {
            const uint32_t fi = queue[qi];
            const auto& f = pm.faces[fi];
            for (size_t k = 0; k < f.size() && goal == kNone; ++k) {
                const uint64_t key = edgeKey(f[k], f[(k + 1) % f.size()]);
                const auto& nb = ef[key];
                if (nb.size() == 1 && openFace == kNone) openFace = fi, openEdge = key;
                for (uint32_t g : nb) {
                    if (g == fi || prev.count(g)) continue;
                    prev[g] = {fi, key};
                    if (pm.faces[g].size() % 2 == 1 && !matched[g]) {
                        goal = g;
                        break;
                    }
                    queue.push_back(g);
                }
            }
        }
        std::vector<uint64_t> crossed;
        if (goal != kNone) {
            for (uint32_t g = goal; prev[g].first != kNone; g = prev[g].first) crossed.push_back(prev[g].second);
            matched[goal] = 1;
        } else if (openFace != kNone) {
            for (uint32_t g = openFace; prev[g].first != kNone; g = prev[g].first) crossed.push_back(prev[g].second);
            crossed.push_back(openEdge);
        } else {
            continue;
        }
        matched[start] = 1;
        for (uint64_t key : crossed) insertMid(uint32_t(key >> 32), uint32_t(key & 0xffffffffu));
    }
    std::vector<glm::uvec4> quads;
    for (const auto& f : pm.faces) {
        if (f.size() % 2 == 0) splitEven(pm.P, f, quads);
        else { // an odd face left without a partner (rare): quads and one triangle, so the surface stays closed
            const size_t n = f.size();
            size_t i = 1;
            for (; i + 3 < n; i += 2) quads.push_back({f[0], f[i], f[i + 1], f[i + 2]});
            leftover.push_back({f[0], f[i], f[i + 1]});
        }
    }
    return quads;
}

// Doublets (an inside vertex with two quads) merge the two quads; unused vertices go.
void cleanQuads(std::vector<vec3>& P, std::vector<glm::uvec4>& quads, std::vector<glm::uvec3>& tris) {
    for (int pass = 0; pass < 4; ++pass) {
        const size_t nv = P.size();
        std::vector<std::vector<uint32_t>> vq(nv);
        for (uint32_t f = 0; f < quads.size(); ++f)
            for (int k = 0; k < 4; ++k) vq[quads[f][k]].push_back(f);
        std::vector<uint8_t> dead(quads.size(), 0), touched(nv, 0);
        for (const auto& t : tris)
            for (int k = 0; k < 3; ++k) touched[t[k]] = 1; // a triangle's corner is no doublet
        auto rotated = [&](uint32_t f, uint32_t v) {
            glm::uvec4 r = quads[f];
            for (int k = 0; k < 4 && r.x != v; ++k) r = {r.y, r.z, r.w, r.x};
            return r;
        };
        bool any = false;
        for (uint32_t v = 0; v < nv; ++v) {
            if (vq[v].size() != 2 || touched[v]) continue;
            const uint32_t fa = vq[v][0], fb = vq[v][1];
            const glm::uvec4 A = rotated(fa, v), B = rotated(fb, v);
            if (A.x != v || B.x != v || B.y != A.w || B.w != A.y || A.z == B.z) continue;
            bool busy = false;
            for (int k = 0; k < 4; ++k) busy |= touched[A[k]] || touched[B[k]];
            if (busy) continue;
            quads[fa] = {A.y, A.z, A.w, B.z};
            dead[fb] = 1;
            for (int k = 0; k < 4; ++k) touched[A[k]] = touched[B[k]] = 1;
            any = true;
        }
        if (!any) break;
        size_t w = 0;
        for (size_t f = 0; f < quads.size(); ++f)
            if (!dead[f]) quads[w++] = quads[f];
        quads.resize(w);
    }
    std::vector<uint32_t> remap(P.size(), kNone);
    std::vector<vec3> Q;
    for (auto& f : quads)
        for (int k = 0; k < 4; ++k) {
            if (remap[f[k]] == kNone) remap[f[k]] = uint32_t(Q.size()), Q.push_back(P[f[k]]);
            f[k] = remap[f[k]];
        }
    for (auto& f : tris)
        for (int k = 0; k < 3; ++k) {
            if (remap[f[k]] == kNone) remap[f[k]] = uint32_t(Q.size()), Q.push_back(P[f[k]]);
            f[k] = remap[f[k]];
        }
    P.swap(Q);
}

// --- Manifold repair ------------------------------------------------------------------------------------------------

// Makes the quads a 2-manifold where extraction left defects (rare, around folds of the map): quads on an edge used
// more than twice or twice the same way are taken out and the holes they leave refilled as polygons (paired off
// into quads like any odd face), and a vertex whose quads form several fans gets one copy per fan.
// Returns how many defects were repaired.
size_t repairManifold(std::vector<vec3>& P, std::vector<glm::uvec4>& quads, std::vector<glm::uvec3>& tris) {
    size_t repaired = 0;
    // Edges used badly (triangles left over count as faces too).
    {
        std::vector<std::vector<uint32_t>> faces;
        for (const auto& q : quads) faces.push_back({q.x, q.y, q.z, q.w});
        for (const auto& t : tris) faces.push_back({t.x, t.y, t.z});
        std::unordered_map<uint64_t, std::vector<std::pair<uint32_t, bool>>> use; // edge -> (face, a<b forwards)
        for (uint32_t f = 0; f < faces.size(); ++f)
            for (size_t k = 0; k < faces[f].size(); ++k) {
                const uint32_t x = faces[f][k], y = faces[f][(k + 1) % faces[f].size()];
                use[edgeKey(x, y)].push_back({f, x < y});
            }
        std::vector<uint8_t> drop(faces.size(), 0);
        for (auto& [key, list] : use) {
            const bool bad = list.size() > 2 || (list.size() == 2 && list[0].second == list[1].second);
            if (!bad) continue;
            for (auto [f, fw] : list) drop[f] = 1;
            ++repaired;
        }
        for (uint32_t f = 0; f < faces.size(); ++f) {
            std::vector<uint32_t> s2 = faces[f];
            std::sort(s2.begin(), s2.end());
            if (std::adjacent_find(s2.begin(), s2.end()) != s2.end()) drop[f] = 1, ++repaired;
        }
        if (repaired) {
            std::unordered_map<uint64_t, int> keptUse;
            std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> keptDir;
            for (uint32_t f = 0; f < faces.size(); ++f) {
                if (drop[f]) continue;
                for (size_t k = 0; k < faces[f].size(); ++k) {
                    const uint32_t x = faces[f][k], y = faces[f][(k + 1) % faces[f].size()];
                    ++keptUse[edgeKey(x, y)];
                    keptDir[edgeKey(x, y)] = {x, y};
                }
            }
            std::unordered_map<uint32_t, std::vector<uint32_t>> next;
            std::unordered_set<uint64_t> seen;
            for (uint32_t f = 0; f < faces.size(); ++f) {
                if (!drop[f]) continue;
                for (size_t k = 0; k < faces[f].size(); ++k) {
                    const uint64_t key = edgeKey(faces[f][k], faces[f][(k + 1) % faces[f].size()]);
                    if (!seen.insert(key).second) continue;
                    const auto it = keptUse.find(key);
                    if (it == keptUse.end() || it->second != 1) continue;
                    const auto [x, y] = keptDir[key];
                    next[y].push_back(x);
                }
            }
            Poly pm;
            pm.P = P;
            for (uint32_t f = 0; f < faces.size(); ++f)
                if (!drop[f]) pm.faces.push_back(faces[f]);
            std::unordered_map<uint32_t, size_t> used;
            for (auto& [start, outs] : next) {
                while (used[start] < outs.size()) {
                    std::vector<uint32_t> loop{start};
                    uint32_t v = start;
                    bool closed = false;
                    for (int steps = 0; steps < 200; ++steps) {
                        auto it = next.find(v);
                        if (it == next.end() || used[v] >= it->second.size()) break;
                        const uint32_t w = it->second[used[v]++];
                        if (w == start) {
                            closed = true;
                            break;
                        }
                        const auto again = std::find(loop.begin(), loop.end(), w);
                        if (again != loop.end()) { // a figure eight: emit the inner loop, keep walking
                            std::vector<uint32_t> inner(again, loop.end());
                            if (inner.size() >= 3) pm.faces.push_back(inner);
                            loop.erase(again + 1, loop.end());
                            v = w;
                            continue;
                        }
                        loop.push_back(w);
                        v = w;
                    }
                    if (closed && loop.size() >= 3) pm.faces.push_back(loop);
                    if (!closed) break;
                }
            }
            std::vector<glm::uvec3> left;
            quads = allQuads(pm, left);
            P = std::move(pm.P);
            tris = std::move(left);
        }
    }
    // Pinched vertices: one copy per fan.
    {
        const size_t nv = P.size();
        std::vector<std::vector<std::pair<uint32_t, int>>> corners(nv); // (quad, corner index)
        for (uint32_t f = 0; f < quads.size(); ++f)
            for (int k = 0; k < 4; ++k) corners[quads[f][k]].push_back({f, k});
        for (uint32_t v = 0; v < nv; ++v) {
            const auto& c = corners[v];
            if (c.size() < 2) continue;
            // Union corners sharing an edge at v (the next vertex of one is the previous of another).
            std::vector<uint32_t> parent(c.size());
            std::iota(parent.begin(), parent.end(), 0u);
            auto find = [&](uint32_t x) {
                while (parent[x] != x) x = parent[x] = parent[parent[x]];
                return x;
            };
            for (size_t i = 0; i < c.size(); ++i)
                for (size_t j = 0; j < c.size(); ++j) {
                    if (i == j) continue;
                    const glm::uvec4 qi = quads[c[i].first], qj = quads[c[j].first];
                    if (qi[(c[i].second + 1) % 4] == qj[(c[j].second + 3) % 4]) parent[find(uint32_t(i))] = find(uint32_t(j));
                }
            std::unordered_map<uint32_t, uint32_t> copyOf;
            for (size_t i = 0; i < c.size(); ++i) copyOf.emplace(find(uint32_t(i)), 0);
            if (copyOf.size() < 2) continue;
            ++repaired;
            bool firstFan = true;
            for (auto& [root, id] : copyOf) {
                if (firstFan) {
                    id = v;
                    firstFan = false;
                } else {
                    id = uint32_t(P.size());
                    P.push_back(P[v]);
                }
            }
            for (size_t i = 0; i < c.size(); ++i) quads[c[i].first][c[i].second] = copyOf[find(uint32_t(i))];
        }
    }
    return repaired;
}

// --- Relaxation -----------------------------------------------------------------------------------------------------

struct RelaxTargets {
    const TriangleBVH* surface = nullptr;
    const SegmentBVH* features = nullptr; // feature lines of the proxy (incl. open edges and guides)
    const Proxy* proxy = nullptr;
    const Curvature* curvature = nullptr;
    const TriangleBVH* proxyBvh = nullptr;
    float h = 1.0f;
};

// Loop-aware relaxation: tangential smoothing on the sculpt in which a vertex may not move across a loop that runs
// along a ridge (the curvature across the loop is high), so loops placed on ridges by the field stay on them.
// Vertices on feature lines slide along them; where feature lines meet they stay.
void relaxLoops(std::vector<vec3>& P, const std::vector<glm::uvec4>& quads, const RelaxTargets& rt, int iterations,
                std::vector<uint8_t>& featureOut) {
    const size_t nv = P.size();
    std::vector<std::vector<uint32_t>> nb(nv);
    std::unordered_map<uint64_t, int> use;
    for (const auto& q : quads)
        for (int k = 0; k < 4; ++k) {
            const uint32_t a = q[k], b = q[(k + 1) % 4];
            if (++use[edgeKey(a, b)] == 1) nb[a].push_back(b), nb[b].push_back(a);
        }
    // Feature vertices: on a proxy feature line (within a small distance); corners: 3+ feature neighbours or a turn.
    std::vector<uint8_t> feat(nv, 0);
    if (rt.features)
        for (uint32_t v = 0; v < nv; ++v) {
            SurfaceHit hit;
            if (rt.features->closestPoint(P[v], 0.05f * rt.h, hit)) feat[v] = 1;
        }
    std::vector<glm::uvec2> line(nv, glm::uvec2(kNone));
    for (uint32_t v = 0; v < nv; ++v) {
        if (!feat[v]) continue;
        std::vector<uint32_t> f;
        for (uint32_t u : nb[v])
            if (feat[u]) {
                // The edge runs along the line: its middle is on it too.
                SurfaceHit hit;
                if (rt.features->closestPoint(0.5f * (P[u] + P[v]), 0.05f * rt.h, hit)) f.push_back(u);
            }
        if (f.size() == 2) {
            const vec3 a = safeNormalize(P[f[0]] - P[v]), b = safeNormalize(P[f[1]] - P[v]);
            if (glm::dot(a, b) < -0.7f) {
                line[v] = {f[0], f[1]};
                continue;
            }
        }
        feat[v] = 2;
    }
    featureOut = feat;
    // Per vertex: the curvature tensor's strongest direction and anisotropy (from the proxy triangle under it).
    std::vector<vec3> ridgeDir(nv, vec3(0.0f));
    std::vector<float> ridge(nv, 0.0f);
    if (rt.curvature && rt.proxyBvh)
        parallelFor(nv, [&](size_t b, size_t e) {
            for (size_t v = b; v < e; ++v) {
                SurfaceHit hit;
                if (!rt.proxyBvh->closestPoint(P[v], 2.0f * rt.h, hit)) continue;
                const uint32_t t = hit.prim;
                const glm::uvec3 tri = rt.proxy->T[t];
                const vec3 a = rt.proxy->P[tri.x], bb = rt.proxy->P[tri.y], c = rt.proxy->P[tri.z];
                const vec3 n = safeNormalize(glm::cross(bb - a, c - a));
                const vec3 e1 = safeNormalize(bb - a), e2 = glm::cross(n, e1);
                const float ang = rt.curvature->angle[t];
                ridgeDir[v] = std::cos(ang) * e1 + std::sin(ang) * e2; // direction of strongest bending
                ridge[v] = std::min(1.0f, rt.curvature->anisotropy[t] * rt.h / 0.5f);
            }
        }, 2000);
    std::vector<vec3> next(nv), N(nv);
    for (int it = 0; it < iterations; ++it) {
        // Normals from the quads.
        std::fill(N.begin(), N.end(), vec3(0.0f));
        for (const auto& q : quads) {
            const vec3 n = glm::cross(P[q.z] - P[q.x], P[q.w] - P[q.y]);
            for (int k = 0; k < 4; ++k) N[q[k]] += n;
        }
        for (auto& n : N) n = safeNormalize(n);
        parallelFor(nv, [&](size_t b, size_t e) {
            for (size_t v = b; v < e; ++v) {
                next[v] = P[v];
                if (feat[v] == 2 || nb[v].empty()) continue;
                if (feat[v] == 1) {
                    const vec3 c = 0.5f * (P[line[v].x] + P[line[v].y]);
                    vec3 q = P[v] + 0.5f * (c - P[v]);
                    SurfaceHit hit;
                    if (rt.features->closestPoint(q, rt.h, hit)) q = hit.position;
                    next[v] = q;
                    continue;
                }
                vec3 c(0.0f);
                for (uint32_t u : nb[v]) c += P[u];
                c /= float(nb[v].size());
                vec3 d = c - P[v];
                d -= N[v] * glm::dot(N[v], d);
                // Moving along the strongest bending direction would pull a loop lying along the ridge off it.
                if (ridge[v] > 0.0f) {
                    vec3 rd = ridgeDir[v] - N[v] * glm::dot(N[v], ridgeDir[v]);
                    rd = safeNormalize(rd, vec3(0.0f));
                    d -= 0.85f * ridge[v] * rd * glm::dot(rd, d);
                }
                vec3 q = P[v] + 0.5f * d;
                SurfaceHit hit;
                if (rt.surface->closestPoint(q, 2.0f * rt.h, hit)) q = hit.position;
                next[v] = q;
            }
        }, 4000);
        P.swap(next);
    }
    // Final projection of everything onto the sculpt (feature vertices onto their lines).
    parallelFor(nv, [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            SurfaceHit hit;
            if (feat[v] && rt.features && rt.features->closestPoint(P[v], 0.25f * rt.h, hit)) P[v] = hit.position;
            else if (rt.surface->closestPoint(P[v], 2.0f * rt.h, hit)) P[v] = hit.position;
        }
    }, 4000);
}

} // namespace

namespace {

// Quads forming a 2-manifold: every edge in at most two quads, wound opposite ways, and each vertex's quads one fan.
bool quadsManifold(size_t nv, const std::vector<glm::uvec4>& quads, const std::vector<glm::uvec3>& tris = {}) {
    std::unordered_map<uint64_t, std::pair<int, int>> edges;
    for (const auto& q : quads)
        for (int k = 0; k < 4; ++k) {
            const uint32_t a = q[k], b = q[(k + 1) % 4];
            if (a == b) return false;
            auto& e = edges[edgeKey(a, b)];
            ++(a < b ? e.first : e.second);
        }
    for (const auto& t : tris)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = t[k], b = t[(k + 1) % 3];
            if (a == b) return false;
            auto& e = edges[edgeKey(a, b)];
            ++(a < b ? e.first : e.second);
        }
    for (auto& [key, e] : edges)
        if (e.first > 1 || e.second > 1) return false;
    // Fans: corners (prev, next) around each vertex must chain into one cycle or path.
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> fan(nv);
    for (const auto& q : quads)
        for (int k = 0; k < 4; ++k) fan[q[k]].push_back({q[(k + 3) % 4], q[(k + 1) % 4]});
    for (const auto& c : fan) {
        if (c.size() < 2) continue;
        std::unordered_map<uint32_t, size_t> byNext;
        for (size_t i = 0; i < c.size(); ++i) byNext[c[i].second] = i;
        size_t start = 0;
        for (size_t i = 0; i < c.size(); ++i) {
            bool hasPrev = false;
            for (size_t j = 0; j < c.size(); ++j) hasPrev |= c[j].first == c[i].second;
            if (!hasPrev) start = i;
        }
        std::vector<uint8_t> seen(c.size(), 0);
        size_t walked = 0, cur = start;
        while (!seen[cur]) {
            seen[cur] = 1;
            ++walked;
            const auto it = byNext.find(c[cur].first);
            if (it == byNext.end()) break;
            cur = it->second;
        }
        if (walked != c.size()) return false;
    }
    return true;
}

bool remeshPieceOnce(const WorkMesh& piece, int axis, double wantQuads, const QRemeshOptions& opt, float diag,
                     const std::vector<QRemeshGuide>& guides, const std::function<bool(float)>& report,
                     FieldPieceResult& out, bool& cancelled, float density);

} // namespace

bool remeshPieceV2(const WorkMesh& piece, int axis, double wantQuads, const QRemeshOptions& opt, float diag,
                   const std::vector<QRemeshGuide>& guides, const std::function<bool(float)>& report,
                   FieldPieceResult& out, bool& cancelled) {
    for (const QRemeshGuide& g : guides)
        if (!std::getenv("SF_QR2_LOOPS") && (g.closed || (axis >= 0 && g.points.size() >= 2 && std::abs(g.points.front()[axis]) < 1e-5f * diag &&
                         std::abs(g.points.back()[axis]) < 1e-5f * diag))) { // a loop across the symmetry plane, halved
            // Closed loops (round an eye) don't yet come out clean in the map; the legacy method handles them.
            out.why = "closed guide loop";
            return false;
        }
    const float densities[2] = {2.5f, 3.2f};
    for (float d : densities) {
        out = FieldPieceResult{};
        const bool ok = remeshPieceOnce(piece, axis, wantQuads, opt, diag, guides, report, out, cancelled, d);
        if (cancelled) return false;
        if (ok && quadsManifold(out.mesh.P.size(), out.mesh.quads, out.mesh.tris)) return true;
        if (ok) out.why = "not manifold";
    }
    return false;
}

namespace {

bool remeshPieceOnce(const WorkMesh& piece, int axis, double wantQuads, const QRemeshOptions& opt, float diag,
                     const std::vector<QRemeshGuide>& guides, const std::function<bool(float)>& report,
                     FieldPieceResult& out, bool& cancelled, float density) {
    static const bool timing = std::getenv("SF_QREMESH_TIMING") != nullptr;
    const auto start = std::chrono::steady_clock::now();
    auto mark = [&](const char* what) {
        if (timing)
            std::fprintf(stderr, "  v2 %-24s %.3f s\n", what,
                         std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    };
    cancelled = false;
    auto step = [&](float f) {
        if (report && !report(f)) {
            cancelled = true;
            return false;
        }
        return true;
    };
    const std::function<bool()> isCancelled = [&] { return cancelled; };
    if (piece.T.empty()) {
        out.why = "empty";
        return false;
    }
    const double area = meshArea(piece.P, piece.T);
    if (!(area > 0.0) || wantQuads < 1.0) {
        out.why = "no area";
        return false;
    }
    const float h0 = float(std::sqrt(area / wantQuads));
    // Development overrides (SF_QR2_*), for tuning against the benchmark meshes.
    auto tune = [](const char* name, float fallback) {
        const char* v = std::getenv(name);
        return v ? float(std::atof(v)) : fallback;
    };
    const float curvRadius = tune("SF_QR2_CURV", 1.0f) * h0;

    // Source structures: the surface, its feature lines.
    TriangleBVH surface;
    surface.build(piece.P, piece.T);
    mark("surface bvh");
    const float sharpCos = opt.detectEdges ? std::cos(glm::radians(60.0f)) : 2.0f;
    std::vector<vec3> segA, segB;
    std::unordered_set<uint64_t> sharp;
    {
        const std::vector<SrcEdge> edges = meshEdges(piece.P, piece.T);
        std::vector<vec3> fn(piece.T.size());
        parallelFor(piece.T.size(), [&](size_t b, size_t e) {
            for (size_t t = b; t < e; ++t) {
                const glm::uvec3 tri = piece.T[t];
                fn[t] = safeNormalize(glm::cross(piece.P[tri.y] - piece.P[tri.x], piece.P[tri.z] - piece.P[tri.x]));
            }
        });
        std::vector<uint32_t> sharpEdges;
        for (uint32_t i = 0; i < edges.size(); ++i) {
            const SrcEdge& e = edges[i];
            const bool open = e.count != 2;
            const bool border = !open && opt.keepGroups && piece.group[e.t0] != piece.group[e.t1];
            if (open || border) {
                segA.push_back(piece.P[e.a]), segB.push_back(piece.P[e.b]);
                continue;
            }
            if (sharpCos < 1.0f && glm::dot(fn[e.t0], fn[e.t1]) < sharpCos) sharpEdges.push_back(i);
        }
        if (!sharpEdges.empty()) {
            // Hard-edge chains shorter than about a quad are sculpt detail, not features.
            std::unordered_map<uint32_t, uint32_t> id;
            std::vector<uint32_t> verts;
            for (uint32_t i : sharpEdges)
                for (uint32_t v : {edges[i].a, edges[i].b})
                    if (id.emplace(v, uint32_t(verts.size())).second) verts.push_back(v);
            UF uf(verts.size());
            for (uint32_t i : sharpEdges) uf.unite(id[edges[i].a], id[edges[i].b]);
            std::vector<double> len(verts.size(), 0.0);
            for (uint32_t i : sharpEdges) len[uf.find(id[edges[i].a])] += double(glm::length(piece.P[edges[i].a] - piece.P[edges[i].b]));
            for (uint32_t i : sharpEdges) {
                if (len[uf.find(id[edges[i].a])] < 1.5 * double(h0)) continue;
                sharp.insert(edgeKey(edges[i].a, edges[i].b));
                segA.push_back(piece.P[edges[i].a]), segB.push_back(piece.P[edges[i].b]);
            }
        }
    }
    SegmentBVH features;
    if (!segA.empty()) features.build(segA, segB);
    mark("features");
    if (!step(0.08f)) return false;

    // 1. Proxy.
    ProxySource ps;
    ps.mesh = &piece;
    ps.surface = &surface;
    ps.features = features.empty() ? nullptr : &features;
    ps.sharp = std::move(sharp);
    ps.keepGroups = opt.keepGroups;
    // About three proxy edges per quad edge, at most ~400k proxy triangles.
    const float minEdge = float(std::sqrt(area / (0.433 * 400000.0)));
    const float L0 = std::max(h0 / tune("SF_QR2_DENSITY", density), minEdge);
    // Dense input (many times the proxy's triangles): decimate in parallel blocks first.
    qr::WorkMesh pre;
    const double proxyTris = area / (0.433 * double(L0) * double(L0));
    if (double(piece.T.size()) > 8.0 * proxyTris && piece.T.size() > 300000) {
        std::unordered_set<uint64_t> sharpPre;
        pre = preDecimate(ps, L0, sharpPre);
        ps.mesh = &pre;
        ps.sharp = std::move(sharpPre);
        mark("parallel pre-decimate");
        if (timing) std::fprintf(stderr, "  v2 pre-decimated %zu -> %zu triangles\n", piece.T.size(), pre.T.size());
    }
    ProxyBuilder pb(ps);
    pb.decimate(L0);
    mark("decimate");
    if (!step(0.25f)) return false;
    pb.remesh(L0, {}, 2);
    mark("remesh");
    std::vector<uint32_t> vmap;
    Proxy proxy = pb.result(&vmap);
    // Hard edges checked at the proxy's scale: a chain whose faces meet at less than ~40 degrees there (on average),
    // or shorter than two quads, is sculpt detail (a scan's crease), not a hard edge, and stops being a feature.
    auto demoteSoftEdges = [&](Proxy& px) {
        const size_t nc = px.sharp.size();
        UF uf(px.P.size());
        for (uint32_t c = 0; c < nc; ++c)
            if (px.sharp[c] && px.opp[c] != kNone) uf.unite(px.T[c / 3][c % 3], px.T[c / 3][(c % 3 + 1) % 3]);
        std::unordered_map<uint32_t, std::pair<double, double>> chain; // root -> (length, length-weighted dihedral)
        for (uint32_t c = 0; c < nc; ++c) {
            const uint32_t o = px.opp[c];
            if (!px.sharp[c] || o == kNone || o < c) continue;
            const glm::uvec3 a = px.T[c / 3], b = px.T[o / 3];
            const vec3 na = safeNormalize(glm::cross(px.P[a.y] - px.P[a.x], px.P[a.z] - px.P[a.x]));
            const vec3 nb = safeNormalize(glm::cross(px.P[b.y] - px.P[b.x], px.P[b.z] - px.P[b.x]));
            const double len = glm::length(px.P[a[(c % 3 + 1) % 3]] - px.P[a[c % 3]]);
            auto& e = chain[uf.find(a[c % 3])];
            e.first += len;
            e.second += len * std::acos(std::clamp(double(glm::dot(na, nb)), -1.0, 1.0));
        }
        size_t demoted = 0;
        for (uint32_t c = 0; c < nc; ++c) {
            if (!px.sharp[c]) continue;
            const uint32_t o = px.opp[c];
            if (o == kNone) continue;
            const auto& e = chain[uf.find(px.T[c / 3][c % 3])];
            const double meanAngle = e.first > 0.0 ? e.second / e.first : 0.0;
            if (e.first < 3.0 * double(h0) || meanAngle < glm::radians(55.0)) {
                bool other = (opt.keepGroups && px.group[c / 3] != px.group[o / 3]);
                if (!other) px.feature[c] = 0, ++demoted;
                px.sharp[c] = 0;
            }
        }
        return demoted;
    };
    if (timing) {
        size_t fc = 0, sc = 0;
        for (size_t c = 0; c < proxy.feature.size(); ++c) fc += proxy.feature[c], sc += proxy.sharp[c];
        std::fprintf(stderr, "  v2 proxy features: %zu corners (%zu hard-edge), %zu source hard edges kept\n", fc, sc, ps.sharp.size());
    }
    {
        const size_t d = demoteSoftEdges(proxy);
        if (timing && d) std::fprintf(stderr, "  v2 %zu soft hard-edge corners demoted\n", d);
    }
    Frames frames;
    frames.build(proxy);
    proxy.size.assign(proxy.P.size(), h0);
    Curvature curv = computeCurvature(proxy, frames, curvRadius);
    mark("curvature");
    if (!step(0.35f)) return false;

    // Sizes: Adaptive Size (curvature) and polypaint, relative; then normalised to the target.
    const bool adaptive = opt.adaptive > 0.01f || !piece.color.empty();
    std::vector<float> rel(proxy.P.size(), 1.0f);
    auto vertexAverage = [&](const std::vector<float>& perTri) {
        std::vector<float> s(proxy.P.size(), 0.0f), w(proxy.P.size(), 0.0f);
        for (uint32_t t = 0; t < proxy.T.size(); ++t)
            for (int k = 0; k < 3; ++k) s[proxy.T[t][k]] += perTri[t] * frames.area[t], w[proxy.T[t][k]] += frames.area[t];
        for (size_t v = 0; v < s.size(); ++v) s[v] = w[v] > 0.0f ? s[v] / w[v] : 0.0f;
        return s;
    };
    std::vector<float> relPaint;
    auto computeRel = [&]() {
        rel.assign(proxy.P.size(), 1.0f);
        relPaint.assign(proxy.P.size(), 1.0f);
        if (opt.adaptive > 0.01f) {
            const std::vector<float> k = vertexAverage(curv.magnitude);
            std::vector<float> sorted(k);
            std::nth_element(sorted.begin(), sorted.begin() + long(sorted.size() / 2), sorted.end());
            const float ref = std::max(sorted[sorted.size() / 2], 0.2f / h0); // flat models stay even
            for (size_t v = 0; v < rel.size(); ++v)
                rel[v] = std::clamp(std::pow(std::max(k[v], 1e-9f) / ref, tune("SF_QR2_ADAPT", 0.6f) * -opt.adaptive), 0.45f, 2.2f);
        }
        if (!piece.color.empty()) {
            proxy.color.assign(proxy.P.size(), vec3(1.0f));
            for (size_t v = 0; v < proxy.P.size(); ++v) {
                SurfaceHit hit;
                if (!surface.closestPoint(proxy.P[v], 2.0f * h0, hit)) continue;
                const glm::uvec3 t = piece.T[hit.prim];
                const vec3 c = hit.bary.x * piece.color[t.x] + hit.bary.y * piece.color[t.y] + hit.bary.z * piece.color[t.z];
                proxy.color[v] = c;
                const float red = std::clamp(c.x - std::max(c.y, c.z), 0.0f, 1.0f);
                const float blue = std::clamp(c.z - std::max(c.x, c.y), 0.0f, 1.0f);
                relPaint[v] = std::pow(2.0f, blue - red);
            }
        }
        // The size the cross field turns with (QRemesh2Field.cpp): the polypaint density, which asks for real changes
        // of density (pole pairs where it changes). Adaptive Size only stretches the map (a noisy sculpt's curvature
        // would otherwise ask for many pole pairs).
        if (!piece.color.empty()) proxy.fieldSize = relPaint;
        else proxy.fieldSize.clear();
        for (size_t v = 0; v < rel.size(); ++v) rel[v] *= relPaint[v];
        // Smooth the log sizes so they change gradually.
        VertexFaces vf;
        vf.build(proxy.P.size(), proxy.T);
        auto smoothLog = [&](std::vector<float>& x, int rounds) {
            for (int it = 0; it < rounds; ++it) {
                std::vector<float> lr(x.size());
                for (size_t v = 0; v < x.size(); ++v) {
                    double s = 0.0;
                    int c = 0;
                    for (uint32_t k = vf.off[v]; k < vf.off[v + 1]; ++k)
                        for (int j = 0; j < 3; ++j) s += std::log(double(x[proxy.T[vf.tri[k]][j]])), ++c;
                    lr[v] = c ? float(std::exp(s / double(c))) : x[v];
                }
                x.swap(lr);
            }
        };
        smoothLog(rel, 10);
        if (!proxy.fieldSize.empty()) smoothLog(proxy.fieldSize, 30);
    };
    if (adaptive) {
        computeRel();
        // Refine the proxy where quads will be small.
        std::vector<float> relB(pb.vertexCount(), 1.0f);
        for (size_t v = 0; v < vmap.size(); ++v) relB[vmap[v]] = std::max(rel[v], minEdge / L0);
        pb.remesh(L0, relB, 2);
        vmap.clear();
        proxy = pb.result(&vmap);
        demoteSoftEdges(proxy);
        frames.build(proxy);
        proxy.size.assign(proxy.P.size(), h0);
        curv = computeCurvature(proxy, frames, curvRadius);
        computeRel();
        mark("adaptive proxy");
    }
    out.proxyTriangles = proxy.T.size();
    auto setSizes = [&](double scale) {
        double sum = 0.0;
        std::vector<float> va(proxy.P.size(), 0.0f);
        for (uint32_t t = 0; t < proxy.T.size(); ++t)
            for (int k = 0; k < 3; ++k) va[proxy.T[t][k]] += frames.area[t] / 3.0f;
        for (size_t v = 0; v < va.size(); ++v) sum += double(va[v]) / double(rel[v] * rel[v]);
        const double c = std::sqrt(sum / wantQuads) * scale;
        for (size_t v = 0; v < proxy.size.size(); ++v) proxy.size[v] = float(c * double(rel[v]));
    };
    setSizes(1.0);
    if (!step(0.45f)) return false;

    // 2. Guides as feature chains.
    FieldSettings fs;
    fs.alignWeight = tune("SF_QR2_ALIGN", 0.01f);
    applyGuides(proxy, guides, opt.guideStrength, h0, fs, frames);
    if (!guides.empty()) {
        frames.build(proxy);
        mark("guides");
    }

    // 3. Cross field.
    // Solved on a copy of the proxy smoothed at the quad scale: the field's holonomy (where it must turn, hence the
    // singularities) then comes from the form, not from bumps smaller than a quad. Angles are relative to each
    // triangle's first edge, so they carry over to the real proxy unchanged.
    CrossField field;
    Proxy smooth = proxy;
    Frames sframes;
    {
        const float edgeLen = L0;
        const int rounds = std::clamp(int(tune("SF_QR2_SMOOTH", 1.5f) * (h0 / edgeLen) * (h0 / edgeLen)), 0, 200);
        VertexFaces vf;
        vf.build(smooth.P.size(), smooth.T);
        std::vector<uint8_t> pinned(smooth.P.size(), 0);
        for (uint32_t c = 0; c < smooth.feature.size(); ++c)
            if (smooth.feature[c] && (smooth.guide.empty() || !smooth.guide[c])) pinned[smooth.T[c / 3][c % 3]] = pinned[smooth.T[c / 3][(c % 3 + 1) % 3]] = 1;
        std::vector<vec3> next(smooth.P.size());
        for (int r = 0; r < rounds; ++r) {
            parallelFor(smooth.P.size(), [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    if (pinned[v]) {
                        next[v] = smooth.P[v];
                        continue;
                    }
                    vec3 s(0.0f);
                    int n = 0;
                    for (uint32_t k = vf.off[v]; k < vf.off[v + 1]; ++k)
                        for (int j = 0; j < 3; ++j) s += smooth.P[smooth.T[vf.tri[k]][j]], ++n;
                    next[v] = n ? 0.5f * smooth.P[v] + 0.5f * s / float(n) : smooth.P[v];
                }
            }, 4000);
            smooth.P.swap(next);
        }
        sframes.build(smooth);
        const Curvature scurv = computeCurvature(smooth, sframes, tune("SF_QR2_SCURV", 0.5f) * curvRadius);
        if (!solveCrossField(smooth, sframes, scurv, fs, field, isCancelled)) return false;
    }
    out.singularities = field.singularities;
    mark("cross field");
    if (timing) std::fprintf(stderr, "  v2 proxy %zu triangles, %zu singularities\n", proxy.T.size(), field.singularities);
    if (std::getenv("SF_QR2_FIELD_ONLY")) {
        out.why = "field only";
        return false;
    }
    if (!step(0.55f)) return false;

    // 4 + 5. Map and extraction; the count is corrected once when it is far off.
    TriangleBVH proxyBvh;
    proxyBvh.build(proxy.P, proxy.T);
    std::vector<vec3> fa, fb;
    for (uint32_t t = 0; t < proxy.T.size(); ++t)
        for (int k = 0; k < 3; ++k)
            if (proxy.feature[3 * t + uint32_t(k)]) fa.push_back(proxy.P[proxy.T[t][k]]), fb.push_back(proxy.P[proxy.T[t][(k + 1) % 3]]);
    SegmentBVH proxyFeatures;
    if (!fa.empty()) proxyFeatures.build(fa, fb);

    std::vector<glm::uvec4> quads;
    std::vector<glm::uvec3> tris;
    std::vector<vec3> qp;
    const CrossField fieldStart = field;
    // The map is solved on the smoothed proxy, which has less area than the real one: scale the sizes to match.
    double scale = 1.0;
    {
        double ar = 0.0, as = 0.0;
        for (size_t t = 0; t < frames.area.size(); ++t) ar += frames.area[t], as += sframes.area[t];
        if (ar > 0.0 && as > 0.0) scale = std::sqrt(as / ar);
        setSizes(scale);
    }
    double countFix = 1.0; // the map's area aims at wantQuads * countFix unit squares
    for (int attempt = 0; attempt < 2; ++attempt) {
        field = fieldStart;
        GridMap map;
        map.wantQuads = wantQuads * countFix;
        smooth.size = proxy.size;
        if (!solveGridMap(smooth, sframes, field, map, isCancelled)) {
            if (cancelled) return false;
            out.why = "map";
            return false;
        }
        out.flippedTriangles = map.flipped;
        out.attempts = attempt + 1;
        mark("grid map");
        Poly poly = extractLattice(proxy, frames, map, h0);
        if (timing) {
            std::map<size_t, size_t> sizes;
            for (const auto& f : poly.faces) ++sizes[f.size()];
            std::fprintf(stderr, "  v2 extracted: flipped %zu, faces", map.flipped);
            for (auto [k, c] : sizes) std::fprintf(stderr, " %zu:%zu", k, c);
            std::fprintf(stderr, "\n");
        }
        tris.clear();
        quads = allQuads(poly, tris);
        if (timing && !tris.empty()) std::fprintf(stderr, "  v2 %zu odd faces left without a partner\n", tris.size());
        qp = std::move(poly.P);
        cleanQuads(qp, quads, tris);
        for (int round = 0; round < 3 && !quadsManifold(qp.size(), quads, tris); ++round) {
            const size_t n = repairManifold(qp, quads, tris);
            cleanQuads(qp, quads, tris);
            if (timing) std::fprintf(stderr, "  v2 repaired %zu manifold defects: %zu quads, %zu triangles, %zu vertices\n", n, quads.size(), tris.size(), qp.size());
        }
        mark("extract");
        const double ratio = double(quads.size()) / wantQuads;
        if (quads.empty() || attempt == 1 || std::abs(ratio - 1.0) < 0.12) break;
        countFix /= ratio;
        if (!step(0.7f)) return false;
    }
    if (quads.size() < 4) {
        out.why = "no quads";
        return false;
    }
    if (!step(0.85f)) return false;

    // Symmetry: the half's only open edges should lie on the plane. Where the map left lattice points on the plane
    // out, the open boundary bites into the surface (a path of open edges leaving the plane and coming back): close
    // each bite with a face (a middle vertex on the plane when it is odd) and split it into quads.
    if (axis >= 0) {
        auto onPlane = [&](uint32_t v) { return std::abs(qp[v][axis]) < 0.05f * h0; };
        SegmentBVH modelOpen; // the proxy's open edges off the plane: the model's own outline
        {
            std::vector<vec3> a, b;
            for (uint32_t c = 0; c < proxy.opp.size(); ++c) {
                if (proxy.opp[c] != kNone) continue;
                const vec3 x = proxy.P[proxy.T[c / 3][c % 3]], y = proxy.P[proxy.T[c / 3][(c % 3 + 1) % 3]];
                if (std::abs(x[axis]) < 1e-5f * diag && std::abs(y[axis]) < 1e-5f * diag) continue;
                a.push_back(x), b.push_back(y);
            }
            if (!a.empty()) modelOpen.build(a, b);
        }
        std::unordered_map<uint64_t, int> use;
        std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> dir; // open edge -> (from, to) as wound
        for (const auto& q : quads)
            for (int k = 0; k < 4; ++k) {
                ++use[edgeKey(q[k], q[(k + 1) % 4])];
                dir[edgeKey(q[k], q[(k + 1) % 4])] = {q[k], q[(k + 1) % 4]};
            }
        std::unordered_map<uint32_t, uint32_t> nextOf; // reversed winding: the hole's own order
        for (auto [key, n] : use) {
            if (n != 1) continue;
            const auto [a, b] = dir[key];
            if (onPlane(a) && onPlane(b)) continue;
            nextOf[b] = a;
        }
        size_t fixedBites = 0;
        std::unordered_set<uint32_t> done;
        for (auto [startV, unused] : nextOf) {
            (void)unused;
            if (!onPlane(startV) || done.count(startV)) continue;
            std::vector<uint32_t> path{startV};
            uint32_t v = startV;
            while (nextOf.count(v) && path.size() < 64) {
                v = nextOf[v];
                path.push_back(v);
                if (onPlane(v)) break;
            }
            done.insert(startV);
            if (path.size() < 3 || !onPlane(path.back())) continue;
            // A bite is a hole; a path along the model's own open edges reaching the plane (a bust's base) is not.
            if (path.size() > 48) continue;
            size_t alongOpen = 0;
            for (size_t i = 1; i + 1 < path.size(); ++i) {
                SurfaceHit hit;
                if (!modelOpen.empty() && modelOpen.closestPoint(qp[path[i]], 0.1f * h0, hit)) ++alongOpen;
            }
            if (alongOpen * 2 > path.size() - 2) continue;
            if (path.size() % 2 == 1) { // odd: a middle vertex on the plane
                vec3 m = 0.5f * (qp[path.front()] + qp[path.back()]);
                m[axis] = 0.0f;
                path.push_back(uint32_t(qp.size()));
                qp.push_back(m);
            }
            splitEven(qp, path, quads);
            ++fixedBites;
        }
        if (timing && fixedBites) std::fprintf(stderr, "  v2 %zu bites at the symmetry plane closed\n", fixedBites);
    }

    // 6. Loop-aware relaxation and projection.
    RelaxTargets rt;
    rt.surface = &surface;
    rt.features = proxyFeatures.empty() ? nullptr : &proxyFeatures;
    rt.proxy = &proxy;
    rt.curvature = &curv;
    rt.proxyBvh = &proxyBvh;
    rt.h = h0;
    std::vector<uint8_t> feat;
    // Vertices on the symmetry plane's open edges stay exactly on the plane through the relaxation.
    std::vector<uint32_t> planeVerts;
    if (axis >= 0) {
        std::unordered_map<uint64_t, int> use;
        for (const auto& q : quads)
            for (int k = 0; k < 4; ++k) ++use[edgeKey(q[k], q[(k + 1) % 4])];
        for (auto [key, n] : use) {
            if (n != 1) continue;
            const uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
            if (std::abs(qp[a][axis]) < 0.05f * h0 && std::abs(qp[b][axis]) < 0.05f * h0) planeVerts.push_back(a), planeVerts.push_back(b);
        }
    }
    relaxLoops(qp, quads, rt, std::max(opt.smoothIterations, 0), feat);
    // Untangle: a quad turned against its neighbours (a fold left by a repair) gets its vertices moved to their
    // neighbours' average and back onto the sculpt.
    {
        const size_t nv = qp.size();
        std::vector<std::vector<uint32_t>> nbq(nv), vq(nv);
        for (uint32_t f = 0; f < quads.size(); ++f)
            for (int k = 0; k < 4; ++k) {
                vq[quads[f][k]].push_back(f);
                nbq[quads[f][k]].push_back(quads[f][(k + 1) % 4]);
                nbq[quads[f][k]].push_back(quads[f][(k + 3) % 4]);
            }
        std::vector<uint8_t> isFeat(nv, 0), onPlaneV(nv, 0);
        for (size_t v = 0; v < nv && v < feat.size(); ++v) isFeat[v] = feat[v];
        for (uint32_t v : planeVerts) isFeat[v] = 0, onPlaneV[v] = 1; // these move within the plane
        size_t untangled = 0;
        for (int it = 0; it < 8; ++it) {
            std::vector<vec3> qn(quads.size());
            for (uint32_t f = 0; f < quads.size(); ++f) {
                const glm::uvec4 q = quads[f];
                qn[f] = safeNormalize(glm::cross(qp[q.z] - qp[q.x], qp[q.w] - qp[q.y]), vec3(0.0f));
            }
            std::vector<uint32_t> bad;
            for (uint32_t f = 0; f < quads.size(); ++f) {
                vec3 around(0.0f);
                for (int k = 0; k < 4; ++k)
                    for (uint32_t g : vq[quads[f][k]]) around += qn[g];
                if (glm::dot(qn[f], around - 4.0f * qn[f]) < 0.0f)
                    for (int k = 0; k < 4; ++k) bad.push_back(quads[f][k]);
            }
            if (bad.empty()) break;
            std::sort(bad.begin(), bad.end());
            bad.erase(std::unique(bad.begin(), bad.end()), bad.end());
            for (uint32_t v : bad) {
                if (isFeat[v] || nbq[v].empty()) continue;
                vec3 c(0.0f);
                for (uint32_t u : nbq[v]) c += qp[u];
                c /= float(nbq[v].size());
                if (onPlaneV[v]) c[axis] = 0.0f;
                SurfaceHit hit;
                qp[v] = surface.closestPoint(c, 2.0f * h0, hit) ? hit.position : c;
                if (onPlaneV[v]) qp[v][axis] = 0.0f;
                ++untangled;
            }
        }
        if (timing && untangled) std::fprintf(stderr, "  v2 untangled %zu vertex moves\n", untangled);
    }
    for (uint32_t v : planeVerts) { // onto the sculpt, then exactly onto the plane (a symmetric sculpt meets it there)
        for (int it = 0; it < 3; ++it) {
            qp[v][axis] = 0.0f;
            SurfaceHit hit;
            if (surface.closestPoint(qp[v], 2.0f * h0, hit)) qp[v] = hit.position;
        }
        qp[v][axis] = 0.0f;
    }
    mark("relax");

    out.mesh.P = std::move(qp);
    out.mesh.quads = std::move(quads);
    out.mesh.tris = std::move(tris);
    out.mesh.feature = feat;
    out.seam.assign(out.mesh.P.size(), 0);
    if (axis >= 0) {
        std::unordered_map<uint64_t, int> use;
        for (const auto& q : out.mesh.quads)
            for (int k = 0; k < 4; ++k) ++use[edgeKey(q[k], q[(k + 1) % 4])];
        for (auto [key, n] : use) {
            if (n != 1) continue;
            const uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
            if (std::abs(out.mesh.P[a][axis]) < 0.05f * h0 && std::abs(out.mesh.P[b][axis]) < 0.05f * h0)
                out.seam[a] = out.seam[b] = 1;
            else if (timing)
                std::fprintf(stderr, "  v2 open edge off the seam: (%.3f %.3f %.3f) (%.3f %.3f %.3f)\n", out.mesh.P[a].x, out.mesh.P[a].y,
                             out.mesh.P[a].z, out.mesh.P[b].x, out.mesh.P[b].y, out.mesh.P[b].z);
        }
    }
    (void)diag;
    return step(0.95f);
}

} // namespace

} // namespace sf::qr
