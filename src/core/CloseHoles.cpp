// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fill Holes (see CloseHoles.h).
#include "core/CloseHoles.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/PaintLayer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace sf {

namespace {

using dvec2 = glm::dvec2;
using dvec3 = glm::dvec3;

uint64_t directed(uint32_t a, uint32_t b) { return (uint64_t(a) << 32) | b; }
uint64_t undirected(uint32_t a, uint32_t b) { return a < b ? directed(a, b) : directed(b, a); }

constexpr size_t kBestEarLimit = 300; // loops up to this many edges pick the best ear each step (O(n^3))

// One hole's patch, in local indices: 0..n-1 are the loop's vertices (fixed), the rest are new. `ring[i]` holds
// the positions of loop vertex i's neighbours on the Part (fixed), for fairing the patch into the surface around.
struct Patch {
    std::vector<dvec3> p;
    std::vector<std::array<uint32_t, 3>> tris;
    size_t fixed = 0;
    std::vector<std::vector<dvec3>> ring;
};

// Ear clipping in the plane of the loop, wound like `normal`. False when some step finds no ear (the loop folds
// over itself in that plane).
bool earClip(Patch& patch, const dvec3& normal) {
    const size_t n = patch.fixed;
    dvec3 u = std::abs(normal.x) < 0.9 ? glm::cross(normal, dvec3(1, 0, 0)) : glm::cross(normal, dvec3(0, 1, 0));
    u = glm::normalize(u);
    const dvec3 w = glm::cross(normal, u);
    std::vector<dvec2> q(n);
    for (size_t i = 0; i < n; ++i) q[i] = dvec2(glm::dot(patch.p[i], u), glm::dot(patch.p[i], w));
    auto orient = [&](uint32_t a, uint32_t b, uint32_t c) {
        const dvec2 e = q[b] - q[a], f = q[c] - q[a];
        return e.x * f.y - e.y * f.x;
    };
    std::vector<uint32_t> poly(n);
    std::iota(poly.begin(), poly.end(), 0u);
    std::vector<std::array<uint32_t, 3>> out;
    const bool best = n <= kBestEarLimit;
    while (poly.size() > 3) {
        const size_t m = poly.size();
        double bestScore = -1.0;
        size_t bestAt = m;
        for (size_t i = 0; i < m; ++i) {
            const uint32_t a = poly[(i + m - 1) % m], b = poly[i], c = poly[(i + 1) % m];
            const double area = orient(a, b, c);
            if (area <= 1e-14 * (glm::dot(q[c] - q[a], q[c] - q[a]) + 1e-30)) continue; // reflex or flat
            bool empty = true;
            for (size_t j = 0; j < m && empty; ++j) {
                const uint32_t v = poly[j];
                if (v == a || v == b || v == c) continue;
                if (q[v] == q[a] || q[v] == q[b] || q[v] == q[c]) continue; // a pinch: the same point twice
                empty = !(orient(a, b, v) >= 0.0 && orient(b, c, v) >= 0.0 && orient(c, a, v) >= 0.0);
            }
            if (!empty) continue;
            // Score: the triangle's smallest angle (well-shaped ears first).
            const dvec3 A = patch.p[a], B = patch.p[b], C = patch.p[c];
            const double la = glm::length(B - C), lb = glm::length(C - A), lc = glm::length(A - B);
            const double area3 = glm::length(glm::cross(B - A, C - A));
            const double score = area3 / std::max({la * lb, lb * lc, lc * la, 1e-30});
            if (score > bestScore) {
                bestScore = score;
                bestAt = i;
            }
            if (!best) break;
        }
        if (bestAt == m) return false;
        out.push_back({poly[(bestAt + m - 1) % m], poly[bestAt], poly[(bestAt + 1) % m]});
        poly.erase(poly.begin() + std::ptrdiff_t(bestAt));
    }
    out.push_back({poly[0], poly[1], poly[2]});
    patch.tris = std::move(out);
    return true;
}

// A fan around the loop's centre (a new vertex).
void fan(Patch& patch) {
    const size_t n = patch.fixed;
    dvec3 c(0.0);
    for (size_t i = 0; i < n; ++i) c += patch.p[i];
    patch.p.push_back(c / double(n));
    const uint32_t centre = uint32_t(patch.p.size() - 1);
    patch.tris.clear();
    for (uint32_t i = 0; i < n; ++i) patch.tris.push_back({i, uint32_t((i + 1) % n), centre});
}

using EdgeMap = std::unordered_map<uint64_t, std::array<int64_t, 2>>; // edge -> the (up to) two triangles on it

EdgeMap edgeMap(const Patch& patch) {
    EdgeMap e;
    e.reserve(patch.tris.size() * 2);
    for (size_t t = 0; t < patch.tris.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            auto [it, fresh] = e.try_emplace(undirected(patch.tris[t][k], patch.tris[t][(k + 1) % 3]), std::array<int64_t, 2>{-1, -1});
            (it->second[0] < 0 ? it->second[0] : it->second[1]) = int64_t(t);
        }
    return e;
}

int cornerOf(const std::array<uint32_t, 3>& t, uint32_t v) { return t[0] == v ? 0 : t[1] == v ? 1 : t[2] == v ? 2 : -1; }

// Splits every inside edge longer than `limit` at its middle (one edge per triangle per pass). Returns the splits.
size_t splitPass(Patch& patch, double limit) {
    const EdgeMap edges = edgeMap(patch);
    std::vector<std::pair<double, uint64_t>> longEdges;
    for (const auto& [key, tris] : edges) {
        if (tris[1] < 0) continue; // the loop: never split (it is shared with the Part)
        const uint32_t a = uint32_t(key >> 32), b = uint32_t(key);
        const double len = glm::length(patch.p[a] - patch.p[b]);
        if (len > limit) longEdges.push_back({len, key});
    }
    std::sort(longEdges.begin(), longEdges.end(), std::greater<>());
    std::vector<uint8_t> touched(patch.tris.size(), 0);
    size_t splits = 0;
    for (const auto& [len, key] : longEdges) {
        const auto& tris = edges.at(key);
        if (touched[size_t(tris[0])] || touched[size_t(tris[1])]) continue;
        const uint32_t a = uint32_t(key >> 32), b = uint32_t(key);
        patch.p.push_back((patch.p[a] + patch.p[b]) * 0.5);
        const uint32_t m = uint32_t(patch.p.size() - 1);
        for (int64_t ti : tris) {
            std::array<uint32_t, 3> t = patch.tris[size_t(ti)];
            // Rotate so the split edge is t[0] -> t[1].
            int k = 0;
            while (!((t[k] == a && t[(k + 1) % 3] == b) || (t[k] == b && t[(k + 1) % 3] == a))) ++k;
            const uint32_t x = t[k], y = t[(k + 1) % 3], z = t[(k + 2) % 3];
            patch.tris[size_t(ti)] = {x, m, z};
            patch.tris.push_back({m, y, z});
            touched[size_t(ti)] = 1;
            touched.push_back(1);
        }
        ++splits;
    }
    return splits;
}

// Flips inside edges whose two opposite angles add up to more than pi (towards a Delaunay triangulation), unless
// the flip would fold the patch or duplicate an edge.
void flipPass(Patch& patch) {
    EdgeMap edges = edgeMap(patch);
    std::vector<uint64_t> keys;
    keys.reserve(edges.size());
    for (const auto& [key, tris] : edges)
        if (tris[1] >= 0) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    auto angle = [&](uint32_t at, uint32_t x, uint32_t y) {
        const dvec3 e = glm::normalize(patch.p[x] - patch.p[at]), f = glm::normalize(patch.p[y] - patch.p[at]);
        return std::acos(std::clamp(glm::dot(e, f), -1.0, 1.0));
    };
    for (uint64_t key : keys) {
        auto it = edges.find(key);
        if (it == edges.end() || it->second[1] < 0) continue;
        const uint32_t a0 = uint32_t(key >> 32), b0 = uint32_t(key);
        const size_t t1 = size_t(it->second[0]), t2 = size_t(it->second[1]);
        std::array<uint32_t, 3> T1 = patch.tris[t1], T2 = patch.tris[t2];
        // Name them so T1 = (a, b, c) and T2 = (b, a, d).
        int k = cornerOf(T1, a0);
        uint32_t a = a0, b = b0;
        if (T1[(k + 1) % 3] != b0) std::swap(a, b);
        k = cornerOf(T1, a);
        if (T1[(k + 1) % 3] != b) continue; // inconsistent winding: leave it
        const uint32_t c = T1[(k + 2) % 3];
        const int k2 = cornerOf(T2, b);
        if (k2 < 0 || T2[(k2 + 1) % 3] != a) continue;
        const uint32_t d = T2[(k2 + 2) % 3];
        if (c == d || edges.count(undirected(c, d))) continue;
        if (angle(c, a, b) + angle(d, b, a) <= 3.14159265358979 + 1e-9) continue;
        const dvec3 nOld = glm::cross(patch.p[b] - patch.p[a], patch.p[c] - patch.p[a]) +
                           glm::cross(patch.p[a] - patch.p[b], patch.p[d] - patch.p[b]);
        const dvec3 n1 = glm::cross(patch.p[d] - patch.p[a], patch.p[c] - patch.p[a]);
        const dvec3 n2 = glm::cross(patch.p[b] - patch.p[d], patch.p[c] - patch.p[d]);
        if (glm::dot(n1, nOld) <= 0.0 || glm::dot(n2, nOld) <= 0.0) continue;
        patch.tris[t1] = {a, d, c};
        patch.tris[t2] = {d, b, c};
        // Keep the map current for the edges this flip touched.
        edges.erase(key);
        edges[undirected(c, d)] = {int64_t(t1), int64_t(t2)};
        auto retarget = [&](uint32_t x, uint32_t y, size_t from, size_t to) {
            auto e = edges.find(undirected(x, y));
            if (e == edges.end()) return;
            for (int64_t& t : e->second)
                if (t == int64_t(from)) t = int64_t(to);
        };
        retarget(a, d, t2, t1); // (a, d) moved from T2 to T1
        retarget(b, c, t1, t2); // (b, c) moved from T1 to T2
    }
}

// Fairs the new vertices so the patch continues the curvature around it: they minimise the squared umbrella
// Laplacian summed over the new vertices and the loop's (whose neighbours include the Part's ring around the
// hole, held fixed). A least-squares problem, solved by conjugate gradients on its normal equations per axis.
void fair(Patch& patch, int maxIterations) {
    const size_t nv = patch.p.size(), n = patch.fixed, m = nv - n;
    if (m == 0) return;
    std::vector<std::vector<uint32_t>> nbr(nv);
    for (const auto& t : patch.tris)
        for (int k = 0; k < 3; ++k) {
            nbr[t[k]].push_back(t[(k + 1) % 3]);
            nbr[t[(k + 1) % 3]].push_back(t[k]);
        }
    for (auto& r : nbr) {
        std::sort(r.begin(), r.end());
        r.erase(std::unique(r.begin(), r.end()), r.end());
    }
    // Row u of the Laplacian: deg * p_u - sum of neighbours. A loop vertex's neighbours: the Part's ring plus the
    // patch's new vertices and the diagonals the patch added between loop vertices (the loop's own edges are on
    // the Part's side already).
    struct Row {
        double deg = 0.0;
        std::vector<uint32_t> unknown; // neighbours among the new vertices (index - n)
        dvec3 known{0.0};              // deg * p_u (if fixed) less the fixed neighbours
        int self = -1;                 // the row's own unknown, or -1 for a loop vertex
    };
    std::vector<Row> rows(nv);
    for (size_t u = 0; u < nv; ++u) {
        Row& r = rows[u];
        if (u < n) {
            const std::vector<dvec3>& ring = patch.ring[u];
            r.deg = double(ring.size());
            r.known = dvec3(0.0);
            for (const dvec3& q : ring) r.known -= q;
            for (uint32_t w : nbr[u]) {
                if (w >= n) {
                    r.unknown.push_back(uint32_t(w - n));
                    r.deg += 1.0;
                } else {
                    bool onPart = false; // the loop's edges (and any edge the Part has) are in the ring
                    for (const dvec3& q : ring) onPart = onPart || q == patch.p[w];
                    if (!onPart) {
                        r.known -= patch.p[w];
                        r.deg += 1.0;
                    }
                }
            }
            r.known += r.deg * patch.p[u];
        } else {
            r.self = int(u - n);
            r.deg = double(nbr[u].size());
            for (uint32_t w : nbr[u]) {
                if (w >= n) r.unknown.push_back(uint32_t(w - n));
                else r.known -= patch.p[w];
            }
        }
    }
    // B x + known: the rows' Laplacians for the unknowns x.
    auto applyB = [&](const std::vector<double>& x, std::vector<double>& out) {
        out.assign(nv, 0.0);
        for (size_t u = 0; u < nv; ++u) {
            const Row& r = rows[u];
            double v = r.self >= 0 ? r.deg * x[size_t(r.self)] : 0.0;
            for (uint32_t w : r.unknown) v -= x[w];
            out[u] = v;
        }
    };
    auto applyBt = [&](const std::vector<double>& y, std::vector<double>& out) {
        out.assign(m, 0.0);
        for (size_t u = 0; u < nv; ++u) {
            const Row& r = rows[u];
            if (r.self >= 0) out[size_t(r.self)] += r.deg * y[u];
            for (uint32_t w : r.unknown) out[w] -= y[u];
        }
    };
    for (int axis = 0; axis < 3; ++axis) {
        std::vector<double> x(m), c(nv), tmp, b, r, d, q, Bd;
        for (size_t i = 0; i < m; ++i) x[i] = patch.p[n + i][axis];
        for (size_t u = 0; u < nv; ++u) c[u] = -rows[u].known[axis];
        applyBt(c, b); // normal equations: Bt B x = Bt c
        applyB(x, tmp);
        applyBt(tmp, r);
        for (size_t i = 0; i < m; ++i) r[i] = b[i] - r[i];
        d = r;
        double rr = 0.0, bb = 0.0;
        for (size_t i = 0; i < m; ++i) rr += r[i] * r[i], bb += b[i] * b[i];
        const double tol = 1e-24 * std::max(bb, 1e-30);
        for (int it = 0; it < maxIterations && rr > tol; ++it) {
            applyB(d, Bd);
            double dAd = 0.0;
            for (double v : Bd) dAd += v * v;
            if (dAd <= 0.0) break;
            const double alpha = rr / dAd;
            applyBt(Bd, q);
            double rrNew = 0.0;
            for (size_t i = 0; i < m; ++i) {
                x[i] += alpha * d[i];
                r[i] -= alpha * q[i];
                rrNew += r[i] * r[i];
            }
            const double beta = rrNew / rr;
            rr = rrNew;
            for (size_t i = 0; i < m; ++i) d[i] = r[i] + beta * d[i];
        }
        for (size_t i = 0; i < m; ++i) patch.p[n + i][axis] = x[i];
    }
}

// Triangulates one loop (wound for the patch) and refines it to `target` edge length.
void buildPatch(Patch& patch, bool refine) {
    const size_t n = patch.fixed;
    if (n == 3) {
        patch.tris = {{0, 1, 2}};
        return;
    }
    dvec3 normal(0.0); // Newell's normal of the loop
    for (size_t i = 0; i < n; ++i) normal += glm::cross(patch.p[i], patch.p[(i + 1) % n]);
    const bool planar = glm::length(normal) > 1e-20;
    if (!planar || !earClip(patch, glm::normalize(normal))) fan(patch);
    if (!refine) return;
    double target = 0.0;
    for (size_t i = 0; i < n; ++i) target += glm::length(patch.p[i] - patch.p[(i + 1) % n]);
    target /= double(n);
    if (target <= 0.0) return;
    for (int pass = 0; pass < 14; ++pass) {
        const size_t splits = splitPass(patch, target * 1.33);
        flipPass(patch);
        fair(patch, 60); // coarse to fine: each level starts from the last one's surface, so the last fairing is short
        if (splits == 0 || patch.tris.size() > 4'000'000) break;
    }
    flipPass(patch);
    fair(patch, 400); // into the curvature around the hole
    flipPass(patch);
}

} // namespace

std::vector<std::vector<uint32_t>> boundaryLoops(const Mesh& mesh) {
    std::unordered_map<uint64_t, uint32_t> count;
    count.reserve(mesh.triangleCount() * 3);
    for (const glm::uvec3& t : mesh.triangles)
        for (int k = 0; k < 3; ++k) count[directed(t[k], t[(k + 1) % 3])] += 1;
    std::unordered_map<uint32_t, std::vector<uint32_t>> out; // a -> b for every open edge a -> b
    for (const auto& [key, c] : count) {
        const uint32_t a = uint32_t(key >> 32), b = uint32_t(key);
        if (c == 1 && a != b && !count.count(directed(b, a))) out[a].push_back(b);
    }
    for (auto& [a, list] : out) std::sort(list.begin(), list.end()); // (the same loops on every run)
    std::vector<uint32_t> starts;
    for (const auto& [a, list] : out) starts.push_back(a);
    std::sort(starts.begin(), starts.end());
    std::vector<std::vector<uint32_t>> loops;
    for (uint32_t start : starts) {
        while (!out[start].empty()) {
            std::vector<uint32_t> loop{start};
            uint32_t cur = out[start].front();
            out[start].erase(out[start].begin());
            bool closed = false;
            while (true) {
                if (cur == start) {
                    closed = true;
                    break;
                }
                loop.push_back(cur);
                auto it = out.find(cur);
                if (it == out.end() || it->second.empty()) break; // an open chain (non-manifold edges cut it)
                const uint32_t next = it->second.front();
                it->second.erase(it->second.begin());
                cur = next;
            }
            if (closed && loop.size() >= 3) loops.push_back(std::move(loop));
        }
    }
    return loops;
}

bool closeHoles(Mesh& mesh, const CloseHolesOptions& options, CloseHolesStats& stats, std::string& error) {
    stats = {};
    if (mesh.triangleCount() == 0) {
        error = "Fill Holes: the Part is empty";
        return false;
    }
    const std::vector<std::vector<uint32_t>> loops = boundaryLoops(mesh);
    stats.holes = loops.size();
    if (loops.empty()) return true;
    const size_t nv0 = mesh.vertexCount(), nt0 = mesh.triangleCount();
    std::vector<glm::vec3> newPositions;
    std::vector<glm::uvec3> newTris;
    std::vector<uint32_t> newSource; // per new vertex: a loop vertex whose attributes it takes
    std::vector<glm::vec3> loopColor; // per new vertex: its loop's mean colour
    // The Part's neighbours of every loop vertex (fairing reads the ring around each hole).
    std::unordered_map<uint32_t, std::vector<uint32_t>> partRing;
    if (options.refine) {
        for (const std::vector<uint32_t>& loop : loops)
            for (uint32_t v : loop) partRing[v];
        for (const glm::uvec3& t : mesh.triangles)
            for (int k = 0; k < 3; ++k)
                if (auto it = partRing.find(t[k]); it != partRing.end()) {
                    it->second.push_back(t[(k + 1) % 3]);
                    it->second.push_back(t[(k + 2) % 3]);
                }
        for (auto& [v, r] : partRing) {
            std::sort(r.begin(), r.end());
            r.erase(std::unique(r.begin(), r.end()), r.end());
        }
    }
    for (const std::vector<uint32_t>& loop : loops) {
        if (options.maxEdges && loop.size() > options.maxEdges) {
            ++stats.skipped;
            continue;
        }
        // The patch runs the other way round: its triangles hold the open edges b -> a.
        Patch patch;
        patch.fixed = loop.size();
        std::vector<uint32_t> global(loop.rbegin(), loop.rend());
        for (uint32_t v : global) patch.p.push_back(dvec3(mesh.positions[v]));
        if (options.refine)
            for (uint32_t v : global) {
                patch.ring.emplace_back();
                for (uint32_t w : partRing[v]) patch.ring.back().push_back(dvec3(mesh.positions[w]));
            }
        buildPatch(patch, options.refine);
        glm::vec3 color(1.0f);
        if (mesh.hasColors()) {
            glm::dvec3 s(0.0);
            for (uint32_t v : global) s += glm::dvec3(mesh.colors[v]);
            color = glm::vec3(s / double(global.size()));
        }
        for (size_t i = patch.fixed; i < patch.p.size(); ++i) {
            global.push_back(uint32_t(nv0 + newPositions.size()));
            newPositions.push_back(glm::vec3(patch.p[i]));
            newSource.push_back(global.front());
            loopColor.push_back(color);
        }
        for (const auto& t : patch.tris) newTris.push_back(glm::uvec3(global[t[0]], global[t[1]], global[t[2]]));
        ++stats.filled;
    }
    if (newTris.empty()) return true;
    stats.triangles = newTris.size();

    const size_t nv = nv0 + newPositions.size(), nt = nt0 + newTris.size();
    std::vector<uint32_t> kept(nv);
    std::iota(kept.begin(), kept.begin() + std::ptrdiff_t(nv0), 0u);
    std::copy(newSource.begin(), newSource.end(), kept.begin() + std::ptrdiff_t(nv0));
    const Mesh before = mesh.paintLayers.empty() ? Mesh() : mesh; // (copyPaintLayers reads the old layers)

    mesh.positions.insert(mesh.positions.end(), newPositions.begin(), newPositions.end());
    if (!mesh.layers.empty()) {
        mesh.base.insert(mesh.base.end(), newPositions.begin(), newPositions.end());
        for (SculptLayer& l : mesh.layers) l.delta.resize(nv, glm::vec3(0.0f));
    }
    if (mesh.hasMask()) mesh.mask.resize(nv, 0.0f);
    if (mesh.hasColors()) mesh.colors.insert(mesh.colors.end(), loopColor.begin(), loopColor.end());
    if (!before.paintLayers.empty()) copyPaintLayers(before, mesh, kept);
    mesh.triangles.insert(mesh.triangles.end(), newTris.begin(), newTris.end());
    if (options.newGroup || mesh.hasFaceGroups()) {
        uint32_t group = 0;
        for (uint32_t g : mesh.facegroups) group = std::max(group, g);
        const bool had = mesh.hasFaceGroups();
        mesh.facegroups.resize(nt0, 0u);
        mesh.facegroups.resize(nt, options.newGroup ? (had ? group + 1 : 1u) : 0u);
    }
    if (mesh.hasHidden()) mesh.hidden.resize(nt, 0);
    if (mesh.hasUvs()) mesh.uvs.resize(nt * 3, glm::vec2(0.0f));
    if (mesh.hasUvFlags()) mesh.uvFlags.resize(nt * 3, 0);
    if (mesh.hasQuads()) mesh.quadPartners.resize(nt, Mesh::kNoQuad);
    mesh.rebuildTopology();
    mesh.recomputeAllNormals();
    mesh.sanitizeAttributes();
    return true;
}

void registerCloseHolesCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add(
        {"close_holes",
         "Fill Holes: cap every open boundary of the active Part (a hole, an open bottom, the edge of an extracted "
         "shell) with a new patch of triangles sized like the edges around it, curved to continue the surface around the hole. The "
         "rest of the Part is not changed. The patches get a new face group.",
         {{"max_edges", T::Number, "Only holes with at most this many boundary edges (default 0: every hole).", false},
          {"refine", T::Bool, "Split and curve the patches to the density and shape around them (default true; false: as few "
                              "triangles as possible).", false},
          {"new_group", T::Bool, "Give the patches a new face group (default true).", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.mesh.triangleCount() == 0) return {false, "The Part is empty"};
             if (!doc.mesh.forgespheres.empty()) return {false, "Fill Holes: make Forge Spheres into a mesh first"};
             if (!doc.mesh.tube.empty()) return {false, "Fill Holes: bake the Tube first"};
             if (doc.mesh.micro) return {false, "Fill Holes: apply or remove the micro level first"};
             CloseHolesOptions o;
             o.maxEdges = size_t(std::max(0.0, argNumber(a, "max_edges", 0.0)));
             o.refine = argBool(a, "refine", true);
             o.newGroup = argBool(a, "new_group", true);
             Mesh m = doc.mesh;
             CloseHolesStats s;
             std::string error;
             if (!closeHoles(m, o, s, error)) return {false, error};
             if (s.holes == 0) return {true, "Fill Holes: the Part has no holes"};
             if (s.filled == 0)
                 return {true, "Fill Holes: " + std::to_string(s.holes) + " hole" + (s.holes == 1 ? "" : "s") +
                                   ", none small enough to close (max_edges)"};
             doc.replaceMeshUndoable(std::move(m), "Fill Holes");
             std::string msg = "Fill Holes: closed " + std::to_string(s.filled) + " hole" + (s.filled == 1 ? "" : "s") +
                               " (+" + std::to_string(s.triangles) + " triangles)";
             if (s.skipped) msg += ", left " + std::to_string(s.skipped) + " bigger one" + (s.skipped == 1 ? "" : "s");
             return {true, msg};
         }});
}

} // namespace sf
