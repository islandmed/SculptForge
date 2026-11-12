// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The planar arrangement pieces the exact booleans share (core/Cut.cpp: a mesh against a screen shape extruded along
// the view; core/MeshBoolean.cpp: a mesh against a mesh): a polygon split along segments into regions, each ear-clipped
// and improved without touching its outline (so neighbours stay watertight), and the zero-area triangles a boolean
// can leave where two surfaces run along each other cleaned up afterwards. Internal: include from those files only.
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sf::arrange {

using glm::dvec2;
using glm::dvec3;

inline double cross2(dvec2 a, dvec2 b) { return a.x * b.y - a.y * b.x; }
inline double orient2(dvec2 a, dvec2 b, dvec2 c) { return cross2(b - a, c - a); }

// --- Planar regions: a polygon split by segments, holes bridged to it --------------------------------------

struct PEdge {
    uint32_t a, b;
    int32_t tag; // what made the edge (the other surface), -1 = the polygon's own outline or a bridge
};

struct PRegion {
    std::vector<uint32_t> cycle; // counter-clockwise, interior on the left
    std::vector<int32_t> tags;   // tag of edge cycle[i] -> cycle[i + 1]
};

inline bool planarRegions(const std::vector<dvec2>& p, std::vector<PEdge> edges, std::vector<PRegion>& out) {
    out.clear();
    const uint32_t n = uint32_t(p.size());
    // Drop repeats (keeping a tagged copy) and loops.
    {
        std::unordered_map<uint64_t, size_t> seen;
        std::vector<PEdge> unique;
        for (const PEdge& e : edges) {
            if (e.a == e.b) continue;
            const uint64_t key = (uint64_t(std::min(e.a, e.b)) << 32) | std::max(e.a, e.b);
            auto [it, fresh] = seen.emplace(key, unique.size());
            if (fresh) unique.push_back(e);
            else if (unique[it->second].tag < 0) unique[it->second].tag = e.tag;
        }
        edges.swap(unique);
    }
    // Connected pieces; every piece but the outline's (vertex 0) is a hole to bridge.
    std::vector<uint32_t> parent(n);
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    for (const PEdge& e : edges) parent[find(e.a)] = find(e.b);
    std::vector<uint8_t> hasEdge(n, 0);
    for (const PEdge& e : edges) hasEdge[e.a] = hasEdge[e.b] = 1;
    const uint32_t outer = find(0);
    std::unordered_map<uint32_t, uint32_t> leftmost; // piece -> its leftmost vertex
    for (uint32_t v = 0; v < n; ++v) {
        if (!hasEdge[v] || find(v) == outer) continue;
        auto [it, fresh] = leftmost.emplace(find(v), v);
        if (!fresh && (p[v].x < p[it->second].x || (p[v].x == p[it->second].x && p[v].y < p[it->second].y))) it->second = v;
    }
    std::vector<uint32_t> holes;
    for (auto& [piece, v] : leftmost) holes.push_back(v);
    std::sort(holes.begin(), holes.end(), [&](uint32_t a, uint32_t b) { return p[a].x < p[b].x; });
    std::vector<uint8_t> joined(n, 0);
    for (uint32_t v = 0; v < n; ++v) joined[v] = hasEdge[v] && find(v) == outer;
    for (uint32_t h : holes) {
        // Ray to the left: the nearest joined edge it hits, then the vertex there that h sees (as earcut does).
        const dvec2 hp = p[h];
        double bestX = -1e300;
        int best = -1;
        for (size_t i = 0; i < edges.size(); ++i) {
            const PEdge& e = edges[i];
            if (!joined[e.a]) continue;
            const dvec2 a = p[e.a], b = p[e.b];
            if ((a.y > hp.y) == (b.y > hp.y)) continue;
            const double x = a.x + (hp.y - a.y) / (b.y - a.y) * (b.x - a.x);
            if (x <= hp.x && x > bestX) {
                bestX = x;
                best = int(i);
            }
        }
        if (best < 0) return false;
        uint32_t m = p[edges[best].a].x < p[edges[best].b].x ? edges[best].a : edges[best].b;
        const dvec2 q(bestX, hp.y), mp = p[m];
        double bestTan = 1e300;
        for (uint32_t v = 0; v < n; ++v) {
            if (!joined[v] || v == m) continue;
            const dvec2 vp = p[v];
            if (vp.x < mp.x || vp.x > hp.x) continue;
            const double o1 = orient2(hp, q, vp), o2 = orient2(q, mp, vp), o3 = orient2(mp, hp, vp);
            const bool inside = (o1 >= 0 && o2 >= 0 && o3 >= 0) || (o1 <= 0 && o2 <= 0 && o3 <= 0);
            if (!inside) continue;
            const double tan = std::abs(hp.y - vp.y) / std::max(hp.x - vp.x, 1e-300);
            if (tan < bestTan || (tan == bestTan && vp.x > p[m].x)) {
                bestTan = tan;
                m = v;
            }
        }
        edges.push_back({h, m, -1});
        const uint32_t piece = find(h);
        for (uint32_t v = 0; v < n; ++v)
            if (hasEdge[v] && find(v) == piece) joined[v] = 1;
    }
    // Half-edges, sorted by angle around each vertex; walking "next clockwise from the twin" traces each region
    // counter-clockwise.
    struct Half {
        uint32_t to;
        int32_t tag;
        double angle;
    };
    std::vector<std::vector<Half>> around(n);
    for (const PEdge& e : edges) {
        around[e.a].push_back({e.b, e.tag, std::atan2(p[e.b].y - p[e.a].y, p[e.b].x - p[e.a].x)});
        around[e.b].push_back({e.a, e.tag, std::atan2(p[e.a].y - p[e.b].y, p[e.a].x - p[e.b].x)});
    }
    for (auto& list : around) std::sort(list.begin(), list.end(), [](const Half& a, const Half& b) { return a.angle < b.angle; });
    std::vector<std::vector<uint8_t>> used(n);
    for (uint32_t v = 0; v < n; ++v) used[v].assign(around[v].size(), 0);
    auto indexOf = [&](uint32_t from, uint32_t to) {
        for (size_t i = 0; i < around[from].size(); ++i)
            if (around[from][i].to == to) return i;
        return size_t(0);
    };
    for (uint32_t v0 = 0; v0 < n; ++v0)
        for (size_t i0 = 0; i0 < around[v0].size(); ++i0) {
            if (used[v0][i0]) continue;
            PRegion r;
            uint32_t v = v0;
            size_t i = i0;
            for (size_t guard = 0; guard < 4 * edges.size() + 8 && !used[v][i]; ++guard) {
                used[v][i] = 1;
                const Half& h = around[v][i];
                r.cycle.push_back(v);
                r.tags.push_back(h.tag);
                const uint32_t w = h.to;
                const size_t twin = indexOf(w, v), deg = around[w].size();
                i = (twin + deg - 1) % deg;
                v = w;
            }
            double area = 0.0;
            for (size_t k = 0; k < r.cycle.size(); ++k) area += cross2(p[r.cycle[k]], p[r.cycle[(k + 1) % r.cycle.size()]]);
            if (area > 0 && r.cycle.size() >= 3) out.push_back(std::move(r));
        }
    return true;
}

// Ear clipping of a (weakly) simple counter-clockwise cycle. Never adds vertices, so the outline's edges are
// exactly the triangles' boundary even where the geometry is near-degenerate.
inline void earClip(const std::vector<dvec2>& p, const std::vector<uint32_t>& cyc, std::vector<std::array<uint32_t, 3>>& out) {
    const size_t n = cyc.size();
    if (n < 3) return;
    std::vector<size_t> prev(n), next(n);
    for (size_t i = 0; i < n; ++i) {
        prev[i] = (i + n - 1) % n;
        next[i] = (i + 1) % n;
    }
    auto pt = [&](size_t i) { return p[cyc[i]]; };
    auto isEar = [&](size_t b) {
        const size_t a = prev[b], c = next[b];
        const dvec2 pa = pt(a), pb = pt(b), pc = pt(c);
        if (orient2(pa, pb, pc) <= 0) return false;
        for (size_t j = next[c]; j != a; j = next[j]) {
            const uint32_t id = cyc[j];
            if (id == cyc[a] || id == cyc[b] || id == cyc[c]) continue;
            const dvec2 q = pt(j);
            if (orient2(pt(prev[j]), q, pt(next[j])) > 0) continue; // only reflex vertices can block an ear
            if (orient2(pa, pb, q) >= 0 && orient2(pb, pc, q) >= 0 && orient2(pc, pa, q) >= 0) return false;
        }
        return true;
    };
    size_t count = n, i = 0, stall = 0;
    while (count > 3) {
        if (isEar(i) || stall > count) {
            if (stall > count) { // nothing clean left (near-degenerate): clip the most convex corner
                size_t bestI = i;
                double best = -1e300;
                size_t j = i;
                for (size_t k = 0; k < count; ++k, j = next[j]) {
                    const double o = orient2(pt(prev[j]), pt(j), pt(next[j]));
                    if (o > best) {
                        best = o;
                        bestI = j;
                    }
                }
                i = bestI;
            }
            out.push_back({cyc[prev[i]], cyc[i], cyc[next[i]]});
            next[prev[i]] = next[i];
            prev[next[i]] = prev[i];
            i = prev[i];
            --count;
            stall = 0;
        } else {
            i = next[i];
            ++stall;
        }
    }
    out.push_back({cyc[prev[i]], cyc[i], cyc[next[i]]});
}

// Smallest corner angle of triangle abc (radians).
inline double minAngle(const dvec3& a, const dvec3& b, const dvec3& c) {
    auto at = [](const dvec3& o, const dvec3& x, const dvec3& y) {
        return std::atan2(glm::length(glm::cross(x - o, y - o)), glm::dot(x - o, y - o));
    };
    return std::min({at(a, b, c), at(b, c, a), at(c, a, b)});
}

// The ear-clipped triangles of one region, made better without touching the region's outline, so every edge it
// shares with the rest of the mesh stays exactly as it was (watertight for free): Lawson flips of the inside edges
// towards the largest smallest angle, measured in world space, and optionally points inserted inside (a big cap
// gets triangles about the mesh's size instead of a fan of long needles between its outline points). A flip or
// an insertion happens only where every triangle involved stays strictly counter-clockwise in `p`.
struct RegionTris {
    std::vector<dvec2>& p; // the region's plane (local ids), as ear-clipped
    std::vector<dvec3>& w; // world positions (local ids), where angles are measured
    std::vector<std::array<uint32_t, 3>>& T;
    std::vector<std::array<int32_t, 3>> nb; // across side i (corner i -> i + 1); -1 = the outline
    // A flat frame of the region in world space (a prism face's), when given: flips then follow the Delaunay
    // test there (the same largest smallest angle, far cheaper).
    const std::vector<dvec2>* flat = nullptr;
    std::vector<std::pair<int32_t, int>> todo;
    // Undirected sides, kept only when the ear clipping left a triangle that isn't counter-clockwise (a flip could
    // then meet a side that exists elsewhere already).
    bool watch = false;
    std::unordered_map<uint64_t, uint8_t> edges;
    size_t flips = 0;

    static uint64_t key(uint32_t a, uint32_t b) { return (uint64_t(std::min(a, b)) << 32) | std::max(a, b); }

    RegionTris(std::vector<dvec2>& p_, std::vector<dvec3>& w_, std::vector<std::array<uint32_t, 3>>& T_,
               const std::vector<uint32_t>& cycle, const std::vector<dvec2>* flat_ = nullptr)
        : p(p_), w(w_), T(T_), flat(flat_) {
        std::unordered_map<uint64_t, uint8_t> outline; // (a bridge is outline on both sides)
        outline.reserve(cycle.size());
        for (size_t k = 0; k < cycle.size(); ++k) outline[key(cycle[k], cycle[(k + 1) % cycle.size()])] = 1;
        std::unordered_map<uint64_t, int32_t> half;
        half.reserve(T.size() * 3);
        nb.assign(T.size(), {-1, -1, -1});
        for (int32_t t = 0; t < int32_t(T.size()); ++t) {
            watch = watch || orient(T[t][0], T[t][1], T[t][2]) <= 0;
            for (int i = 0; i < 3; ++i) half[(uint64_t(T[t][i]) << 32) | T[t][(i + 1) % 3]] = t;
        }
        for (int32_t t = 0; t < int32_t(T.size()); ++t)
            for (int i = 0; i < 3; ++i) {
                const uint32_t a = T[t][i], b = T[t][(i + 1) % 3];
                if (watch) edges.emplace(key(a, b), 0);
                if (outline.count(key(a, b))) continue;
                const auto it = half.find((uint64_t(b) << 32) | a);
                if (it != half.end()) nb[t][i] = it->second;
            }
    }
    // Whether side a-b of (a, b, c), with d across it, should become c-d.
    bool better(uint32_t a, uint32_t b, uint32_t c, uint32_t d) const {
        if (flat) {
            const std::vector<dvec2>& q = *flat;
            const dvec2 A = q[a] - q[d], B = q[b] - q[d], C = q[c] - q[d];
            const double det = (glm::dot(A, A) * cross2(B, C) - glm::dot(B, B) * cross2(A, C) + glm::dot(C, C) * cross2(A, B));
            const double s = orient2(q[a], q[b], q[c]) > 0 ? 1.0 : -1.0, L = glm::dot(A - B, A - B) + glm::dot(C, C);
            return det * s > 1e-9 * L * L;
        }
        const double before = std::min(minAngle(w[a], w[b], w[c]), minAngle(w[b], w[a], w[d]));
        const double after = std::min(minAngle(w[c], w[a], w[d]), minAngle(w[d], w[b], w[c]));
        return after > before + 1e-9;
    }
    double orient(uint32_t a, uint32_t b, uint32_t c) const { return orient2(p[a], p[b], p[c]); }
    void relink(int32_t x, uint32_t u, uint32_t v, int32_t to) {
        if (x < 0) return;
        for (int k = 0; k < 3; ++k)
            if (T[x][k] == u && T[x][(k + 1) % 3] == v) nb[x][k] = to;
    }
    void pushSides(int32_t t) {
        for (int i = 0; i < 3; ++i)
            if (nb[t][i] >= 0) todo.push_back({t, i});
    }
    // Side i of t, between t = (a, b, c) and o = (b, a, d), becomes c-d when that raises the smallest angle.
    bool tryFlip(int32_t t, int i) {
        const int32_t o = nb[t][i];
        if (o < 0) return false;
        const uint32_t a = T[t][i], b = T[t][(i + 1) % 3], c = T[t][(i + 2) % 3];
        int j = -1;
        for (int k = 0; k < 3; ++k)
            if (T[o][k] == b && T[o][(k + 1) % 3] == a) j = k;
        if (j < 0) return false;
        const uint32_t d = T[o][(j + 2) % 3];
        if (c == d || (watch && edges.count(key(c, d)))) return false;
        if (orient(a, b, c) <= 0 || orient(b, a, d) <= 0 || orient(c, a, d) <= 0 || orient(d, b, c) <= 0) return false;
        if (!better(a, b, c, d)) return false;
        const int32_t nbc = nb[t][(i + 1) % 3], nca = nb[t][(i + 2) % 3], nad = nb[o][(j + 1) % 3], ndb = nb[o][(j + 2) % 3];
        T[t] = {c, a, d};
        T[o] = {d, b, c};
        nb[t] = {nca, nad, o};
        nb[o] = {ndb, nbc, t};
        relink(nad, d, a, t);
        relink(nbc, c, b, o);
        if (watch) {
            edges.erase(key(a, b));
            edges[key(c, d)] = 0;
        }
        ++flips;
        for (int k = 0; k < 2; ++k) {
            todo.push_back({t, k});
            todo.push_back({o, k});
        }
        return true;
    }
    void legalize() {
        const size_t cap = 64 * T.size() + 64;
        while (!todo.empty() && flips < cap) {
            const auto [t, i] = todo.back();
            todo.pop_back();
            tryFlip(t, i);
        }
        todo.clear();
    }
    void improve() {
        for (int32_t t = 0; t < int32_t(T.size()); ++t) pushSides(t);
        legalize();
    }
    // The triangle holding x, walking from `t`; -1 when the walk is blocked (then a scan when `scan`).
    int32_t locate(const dvec2& x, int32_t t, bool scan) const {
        for (size_t step = 0; t >= 0 && step < 4096; ++step) {
            int k = -1;
            for (int s = 0; s < 3 && k < 0; ++s) {
                const int i = int((s + step) % 3);
                if (orient2(p[T[t][i]], p[T[t][(i + 1) % 3]], x) < 0) k = i;
            }
            if (k < 0) return t;
            t = nb[t][k];
        }
        if (!scan) return -1;
        for (int32_t u = 0; u < int32_t(T.size()); ++u)
            if (orient2(p[T[u][0]], p[T[u][1]], x) >= 0 && orient2(p[T[u][1]], p[T[u][2]], x) >= 0 &&
                orient2(p[T[u][2]], p[T[u][0]], x) >= 0)
                return u;
        return -1;
    }
    // Splits triangle t at the new local point x (already in p and w); false when x isn't well inside it.
    bool insert(int32_t t, uint32_t x) {
        const uint32_t a = T[t][0], b = T[t][1], c = T[t][2];
        const double area = orient(a, b, c);
        if (area <= 0 || orient(a, b, x) <= 1e-6 * area || orient(b, c, x) <= 1e-6 * area || orient(c, a, x) <= 1e-6 * area)
            return false;
        const int32_t t1 = int32_t(T.size()), t2 = t1 + 1, n1 = nb[t][1], n2 = nb[t][2];
        T[t] = {a, b, x};
        T.push_back({b, c, x});
        T.push_back({c, a, x});
        nb[t] = {nb[t][0], t1, t2};
        nb.push_back({n1, t2, t});
        nb.push_back({n2, t, t1});
        relink(n1, c, b, t1);
        relink(n2, a, c, t2);
        if (watch)
            for (uint32_t v : {a, b, c}) edges[key(v, x)] = 0;
        for (int32_t u : {t, t1, t2}) pushSides(u);
        legalize();
        return true;
    }
};


// Removes the zero-area triangles a cut can leave where the prism runs along a surface it already made (a symmetric
// cut whose mirror image overlaps it, a second cut through an earlier cap): a needle (two corners at one point)
// collapses its short edge, a cap (a corner on the opposite side) flips that side with the triangle across it. The
// next cut skipped such triangles and cracked (Discord, dm4nd: "the cap sometimes fails to close"). `pos` gives an
// id's position. Each change is checked against the surface as it is then (a collapse only where the two ends share
// no neighbour but the two triangles on the edge), and later passes look at the flat triangles only. Keeps the
// triangles as they were unless the result is still closed and manifold.
template <class Pos>
void cleanSlivers(std::vector<std::array<uint32_t, 3>>& tris, std::vector<int64_t>& parents, std::vector<uint8_t>& touched,
                  const Pos& pos) {
    constexpr double kFlat = 1e-6;    // twice the area / the longest side squared
    constexpr double kShort = 1e-5;   // the shortest side / the longest
    auto key = [](uint32_t a, uint32_t b) { return (uint64_t(a) << 32) | b; };
    auto isFlat = [&](const std::array<uint32_t, 3>& t) {
        const dvec3 p0 = pos(t[0]), p1 = pos(t[1]), p2 = pos(t[2]);
        const double l = std::max({glm::length(p1 - p0), glm::length(p2 - p1), glm::length(p0 - p2)});
        return l > 0.0 && glm::length(glm::cross(p1 - p0, p2 - p0)) <= kFlat * l * l;
    };
    std::vector<uint32_t> work;
    for (uint32_t t = 0; t < tris.size(); ++t)
        if (isFlat(tris[t])) work.push_back(t);
    if (work.empty()) return;
    std::vector<std::array<uint32_t, 3>> T = tris;
    std::vector<int64_t> P = parents;
    std::vector<int64_t> changedParents;
    std::vector<uint8_t> dead(T.size(), 0);
    std::unordered_map<uint64_t, uint32_t> edge; // directed side -> its triangle
    edge.reserve(T.size() * 3);
    for (uint32_t t = 0; t < T.size(); ++t)
        for (int i = 0; i < 3; ++i) edge[key(T[t][i], T[t][(i + 1) % 3])] = t;
    auto unlink = [&](uint32_t t) {
        for (int i = 0; i < 3; ++i)
            if (const auto it = edge.find(key(T[t][i], T[t][(i + 1) % 3])); it != edge.end() && it->second == t) edge.erase(it);
    };
    auto link = [&](uint32_t t) {
        for (int i = 0; i < 3; ++i) edge[key(T[t][i], T[t][(i + 1) % 3])] = t;
    };
    // The triangles around g, turning from t, and the ids next to g (false where the surface isn't closed).
    auto ringOf = [&](uint32_t g, uint32_t t, std::vector<uint32_t>& ids, std::vector<uint32_t>& faces) {
        ids.clear();
        faces.clear();
        for (uint32_t u = t, guard = 0; guard < 4096; ++guard) {
            int i = -1;
            for (int k = 0; k < 3; ++k)
                if (T[u][k] == g) i = k;
            if (i < 0) return false;
            const uint32_t next = T[u][(i + 1) % 3];
            ids.push_back(next);
            faces.push_back(u);
            const auto it = edge.find(key(next, g));
            if (it == edge.end()) return false;
            u = it->second;
            if (u == t) return true;
        }
        return false;
    };
    std::vector<uint32_t> ringA, ringB, facesA, facesB, later;
    bool any = false;
    for (int pass = 0; pass < 64 && !work.empty(); ++pass) {
        bool changed = false;
        later.clear();
        for (uint32_t t : work) {
            if (dead[t] || !isFlat(T[t])) continue;
            const dvec3 p[3] = {pos(T[t][0]), pos(T[t][1]), pos(T[t][2])};
            double len[3];
            for (int i = 0; i < 3; ++i) len[i] = glm::length(p[(i + 1) % 3] - p[i]); // side i: corner i -> i + 1
            const int lo = int(std::min_element(len, len + 3) - len), hi = int(std::max_element(len, len + 3) - len);
            if (len[lo] < kShort * len[hi]) { // needle: side lo's end merges into its start
                const uint32_t a = T[t][lo], b = T[t][(lo + 1) % 3], c = T[t][(lo + 2) % 3];
                const auto across = edge.find(key(b, a));
                if (a == b || across == edge.end() || !ringOf(a, t, ringA, facesA) || !ringOf(b, t, ringB, facesB)) {
                    later.push_back(t);
                    continue;
                }
                const uint32_t o = across->second;
                uint32_t d = ~0u;
                for (int i = 0; i < 3; ++i)
                    if (T[o][i] == b && T[o][(i + 1) % 3] == a) d = T[o][(i + 2) % 3];
                // Only the two triangles on a -> b may share a neighbour of both (else the merge pinches the surface).
                size_t shared = 0;
                for (uint32_t x : ringA)
                    if (std::find(ringB.begin(), ringB.end(), x) != ringB.end()) shared += (x == c || x == d) ? 1 : 100;
                if (shared != 2 || c == d || ringA.size() < 4 || ringB.size() < 4) {
                    later.push_back(t);
                    continue;
                }
                for (uint32_t u : facesB) {
                    unlink(u);
                    changedParents.push_back(P[u]);
                    if (u == t || u == o) {
                        dead[u] = 1;
                        continue;
                    }
                    for (uint32_t& g : T[u])
                        if (g == b) g = a;
                }
                for (uint32_t u : facesB)
                    if (!dead[u]) {
                        link(u);
                        if (isFlat(T[u])) later.push_back(u);
                    }
                changed = true;
                continue;
            }
            // cap: side hi (a -> b) has c on it; the triangle across it is (b, a, d)
            const uint32_t a = T[t][hi], b = T[t][(hi + 1) % 3], c = T[t][(hi + 2) % 3];
            const auto it = edge.find(key(b, a));
            if (it == edge.end()) {
                later.push_back(t);
                continue;
            }
            const uint32_t o = it->second;
            uint32_t d = ~0u;
            for (int i = 0; i < 3; ++i)
                if (T[o][i] == b && T[o][(i + 1) % 3] == a) d = T[o][(i + 2) % 3];
            if (d == ~0u || d == c || edge.count(key(c, d)) || edge.count(key(d, c))) {
                later.push_back(t);
                continue;
            }
            const int64_t parent = P[o];
            changedParents.push_back(P[t]);
            changedParents.push_back(P[o]);
            unlink(t);
            unlink(o);
            T[t] = {a, d, c};
            T[o] = {d, b, c};
            P[t] = P[o] = parent;
            link(t);
            link(o);
            for (uint32_t u : {t, o})
                if (isFlat(T[u])) later.push_back(u);
            changed = true;
        }
        any = any || changed;
        if (!changed) break;
        std::sort(later.begin(), later.end());
        later.erase(std::unique(later.begin(), later.end()), later.end());
        work.swap(later);
    }
    if (!any) return;
    size_t k = 0;
    for (size_t t = 0; t < T.size(); ++t)
        if (!dead[t]) {
            T[k] = T[t];
            P[k] = P[t];
            ++k;
        }
    T.resize(k);
    P.resize(k);
    if (T.empty()) return;
    // Closed and manifold still: every directed side once, with its twin.
    std::unordered_map<uint64_t, int> count;
    count.reserve(T.size() * 3);
    for (const auto& t : T)
        for (int i = 0; i < 3; ++i) count[key(t[i], t[(i + 1) % 3])] += 1;
    for (const auto& [k2, n] : count) {
        const auto twin = count.find((k2 << 32) | (k2 >> 32));
        if (n != 1 || twin == count.end() || twin->second != 1) return;
    }
    tris.swap(T);
    parents.swap(P);
    for (int64_t p : changedParents)
        if (p >= 0 && size_t(p) < touched.size()) touched[size_t(p)] = 1; // (its corners come from the weights now)
}

// Every directed edge of the triangles appears once, and so does its twin (a closed, consistently wound, manifold
// surface). `get(t)` gives triangle t's corners. Linear: the edges leaving each vertex, in vertex order.
template <class Get>
bool closedTriangles(size_t count, const Get& get) {
    if (count == 0) return false;
    uint32_t nv = 0;
    for (size_t t = 0; t < count; ++t) {
        const auto tri = get(t);
        nv = std::max({nv, uint32_t(tri[0]) + 1, uint32_t(tri[1]) + 1, uint32_t(tri[2]) + 1});
    }
    std::vector<uint32_t> start(size_t(nv) + 1, 0), to(count * 3);
    for (size_t t = 0; t < count; ++t) {
        const auto tri = get(t);
        for (int i = 0; i < 3; ++i) ++start[size_t(tri[i]) + 1];
    }
    for (size_t v = 0; v < nv; ++v) start[v + 1] += start[v];
    std::vector<uint32_t> fill(start.begin(), start.end() - 1);
    for (size_t t = 0; t < count; ++t) {
        const auto tri = get(t);
        for (int i = 0; i < 3; ++i) to[fill[tri[i]]++] = uint32_t(tri[(i + 1) % 3]);
    }
    auto times = [&](uint32_t from, uint32_t target) {
        int n = 0;
        for (uint32_t k = start[from]; k < start[size_t(from) + 1]; ++k) n += to[k] == target ? 1 : 0;
        return n;
    };
    for (uint32_t a = 0; a < nv; ++a)
        for (uint32_t k = start[a]; k < start[size_t(a) + 1]; ++k)
            if (times(a, to[k]) != 1 || times(to[k], a) != 1) return false;
    return true;
}

inline bool closedMesh(const Mesh& mesh) {
    return closedTriangles(mesh.triangleCount(), [&](size_t t) { return mesh.triangles[t]; });
}

} // namespace sf::arrange
