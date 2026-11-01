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
    parallelFor(blocks, [&](size_t b0, size_t b1) {
        std::vector<std::pair<uint32_t, uint32_t>> loc;
        for (size_t blk = b0; blk < b1; ++blk)
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
    }, 1);
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
        {
            std::vector<vec3> a, b;
            for (size_t i = 0; i + 1 < g.points.size(); ++i) a.push_back(g.points[i]), b.push_back(g.points[i + 1]);
            if (g.closed) a.push_back(g.points.back()), b.push_back(g.points.front());
            segA.insert(segA.end(), a.begin(), a.end());
            segB.insert(segB.end(), b.begin(), b.end());
            curve.build(a, b);
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
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            const auto it = corners.find(edgeKey(chain[i], chain[i + 1]));
            if (it == corners.end()) continue;
            for (uint32_t c : it->second) p.feature[c] = 1;
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
    const float reach = h * (0.5f + 5.5f * std::clamp(strength, 0.0f, 1.0f));
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
Poly extractLattice(const Proxy& p, const Frames& fr, const GridMap& map) {
    const size_t nv = p.P.size(), nt = p.T.size();
    std::vector<ivec2> L(3 * nt);
    for (size_t c = 0; c < 3 * nt; ++c) L[c] = {int(std::lround(map.uv[c].x)), int(std::lround(map.uv[c].y))};
    std::vector<uint8_t> flipped(nt, 0);
    for (uint32_t t = 0; t < nt; ++t) {
        const vec2 a = map.uv[3 * t], b = map.uv[3 * t + 1], c = map.uv[3 * t + 2];
        flipped[t] = !((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) > 0.0f);
    }
    UF uf(nv);
    for (uint32_t t = 0; t < nt; ++t)
        for (int i = 0; i < 3; ++i) {
            const int j = (i + 1) % 3;
            if (L[3 * t + size_t(i)] == L[3 * t + size_t(j)]) uf.unite(p.T[t][i], p.T[t][j]);
        }
    std::vector<uint32_t> cluster(nv, kNone);
    uint32_t nc = 0;
    for (uint32_t v = 0; v < nv; ++v) {
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
    for (auto& n : nrm) n = safeNormalize(n);
    // Lattice edges.
    std::vector<std::vector<uint32_t>> adj(nc);
    for (uint32_t t = 0; t < nt; ++t)
        for (int i = 0; i < 3; ++i) {
            const int j = (i + 1) % 3;
            const ivec2 d = L[3 * t + size_t(i)] - L[3 * t + size_t(j)];
            if (std::abs(d.x) + std::abs(d.y) != 1) continue;
            const uint32_t a = cluster[p.T[t][i]], b = cluster[p.T[t][j]];
            if (a == b) continue;
            adj[a].push_back(b);
            adj[b].push_back(a);
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
    std::vector<uint32_t> remap(nc, kNone);
    for (auto& f : faces) {
        if (f.size() < 3 || f.size() > 32) continue;
        if (f.size() >= 5) {
            size_t onB = 0;
            for (uint32_t v : f) onB += openCl[v];
            if (onB * 10 >= f.size() * 6) continue;
        }
        {
            std::unordered_map<uint64_t, int> seen;
            bool twice = false;
            for (size_t a = 0; a < f.size(); ++a)
                if (++seen[edgeKey(f[a], f[(a + 1) % f.size()])] == 2) twice = true;
            if (twice) continue;
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
std::vector<glm::uvec4> allQuads(Poly& pm) {
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
        for (size_t qi = 0; qi < queue.size() && goal == kNone && queue.size() < 4000; ++qi) {
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
        else if (f.size() == 3) { // left over (no partner): a degenerate quad would be worse; split into three
            const uint32_t c = uint32_t(pm.P.size());
            pm.P.push_back((pm.P[f[0]] + pm.P[f[1]] + pm.P[f[2]]) / 3.0f);
            const uint32_t m01 = uint32_t(pm.P.size());
            (void)m01;
            quads.push_back({f[0], f[1], f[2], c}); // keeps the mesh closed; rare
        }
    }
    return quads;
}

// Doublets (an inside vertex with two quads) merge the two quads; unused vertices go.
void cleanQuads(std::vector<vec3>& P, std::vector<glm::uvec4>& quads) {
    for (int pass = 0; pass < 4; ++pass) {
        const size_t nv = P.size();
        std::vector<std::vector<uint32_t>> vq(nv);
        for (uint32_t f = 0; f < quads.size(); ++f)
            for (int k = 0; k < 4; ++k) vq[quads[f][k]].push_back(f);
        std::vector<uint8_t> dead(quads.size(), 0), touched(nv, 0);
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
    P.swap(Q);
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

bool remeshPieceV2(const WorkMesh& piece, int axis, double wantQuads, const QRemeshOptions& opt, float diag,
                   const std::vector<QRemeshGuide>& guides, const std::function<bool(float)>& report,
                   FieldPieceResult& out, bool& cancelled) {
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
    ProxyBuilder pb(ps);
    // About three proxy edges per quad edge, at most ~400k proxy triangles.
    const float minEdge = float(std::sqrt(area / (0.433 * 400000.0)));
    const float L0 = std::max(h0 / tune("SF_QR2_DENSITY", 2.5f), minEdge);
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
    auto computeRel = [&]() {
        rel.assign(proxy.P.size(), 1.0f);
        if (opt.adaptive > 0.01f) {
            const std::vector<float> k = vertexAverage(curv.magnitude);
            std::vector<float> sorted(k);
            std::nth_element(sorted.begin(), sorted.begin() + long(sorted.size() / 2), sorted.end());
            const float ref = std::max(sorted[sorted.size() / 2], 0.2f / h0); // flat models stay even
            for (size_t v = 0; v < rel.size(); ++v)
                rel[v] = std::clamp(std::pow(std::max(k[v], 1e-9f) / ref, -0.6f * opt.adaptive), 0.45f, 2.2f);
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
                rel[v] *= std::pow(2.0f, blue - red);
            }
        }
        // Smooth the log size so it changes gradually.
        VertexFaces vf;
        vf.build(proxy.P.size(), proxy.T);
        for (int it = 0; it < 10; ++it) {
            std::vector<float> lr(rel.size());
            for (size_t v = 0; v < rel.size(); ++v) {
                double s = 0.0;
                int c = 0;
                for (uint32_t k = vf.off[v]; k < vf.off[v + 1]; ++k)
                    for (int j = 0; j < 3; ++j) s += std::log(double(rel[proxy.T[vf.tri[k]][j]])), ++c;
                lr[v] = c ? float(std::exp(s / double(c))) : rel[v];
            }
            rel.swap(lr);
        }
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
            if (smooth.feature[c]) pinned[smooth.T[c / 3][c % 3]] = pinned[smooth.T[c / 3][(c % 3 + 1) % 3]] = 1;
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
        Poly poly = extractLattice(proxy, frames, map);
        if (timing) {
            std::map<size_t, size_t> sizes;
            for (const auto& f : poly.faces) ++sizes[f.size()];
            std::fprintf(stderr, "  v2 extracted: flipped %zu, faces", map.flipped);
            for (auto [k, c] : sizes) std::fprintf(stderr, " %zu:%zu", k, c);
            std::fprintf(stderr, "\n");
        }
        quads = allQuads(poly);
        qp = std::move(poly.P);
        cleanQuads(qp, quads);
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

    // 6. Loop-aware relaxation and projection.
    RelaxTargets rt;
    rt.surface = &surface;
    rt.features = proxyFeatures.empty() ? nullptr : &proxyFeatures;
    rt.proxy = &proxy;
    rt.curvature = &curv;
    rt.proxyBvh = &proxyBvh;
    rt.h = h0;
    std::vector<uint8_t> feat;
    relaxLoops(qp, quads, rt, std::max(opt.smoothIterations, 0), feat);
    mark("relax");

    out.mesh.P = std::move(qp);
    out.mesh.quads = std::move(quads);
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
        }
    }
    (void)diag;
    return step(0.95f);
}

} // namespace sf::qr
