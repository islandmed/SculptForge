// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh 2's proxy surface: the sculpt brought to about three triangle edges per quad edge, with even triangles,
// so the cross field and the integer-grid map have a well-shaped mesh to live on whatever the sculpt's density.
//
//  1. Decimation: rounds of shortest-edge-first collapses (each round touches a vertex's neighbourhood once, so the
//     density stays even), collapsed vertices settling at the middle of the edge and back onto the sculpt.
//  2. Isotropic remeshing (after Botsch & Kobbelt, "A remeshing approach to multiresolution modeling", 2004):
//     split long edges, collapse short ones, flip edges towards valence 6, smooth tangentially and project onto
//     the sculpt, towards a per-vertex target length (smaller where Adaptive Size or polypaint want small quads).
//
// Feature lines (open edges, polygroup borders with Keep Groups, hard edges with Detect Edges) are kept: their
// vertices only collapse along the line, slide along it when smoothed and project onto the source's line; where
// lines meet or turn sharply the vertex is pinned.
#include "core/QRemesh2.h"

#include "core/Parallel.h"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <numeric>
#include <thread>
#include <unordered_map>

namespace sf::qr2 {

static thread_local bool tInJob = false;

void parallelJobs(size_t n, const std::function<void(size_t)>& fn) {
    const size_t threads = tInJob ? 1 : std::min<size_t>(n, size_t(parallelThreads()));
    if (threads <= 1) {
        for (size_t i = 0; i < n; ++i) fn(i);
        return;
    }
    std::atomic<size_t> next{0};
    auto work = [&] {
        tInJob = true;
        for (size_t i = next++; i < n; i = next++) fn(i);
        tInJob = false;
    };
    std::vector<std::thread> pool;
    for (size_t t = 1; t < threads; ++t) pool.emplace_back(work);
    work();
    for (auto& th : pool) th.join();
}

void Proxy::buildOpposites() {
    const size_t nt = T.size();
    opp.assign(3 * nt, kNone);
    std::unordered_map<uint64_t, uint32_t> first; // edge -> first corner seen (kNone once a third shows up)
    std::unordered_map<uint64_t, int> count;
    first.reserve(3 * nt / 2 + 16);
    count.reserve(3 * nt / 2 + 16);
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            const uint64_t key = edgeKey(T[t][k], T[t][(k + 1) % 3]);
            if (++count[key] == 1) first[key] = 3 * t + uint32_t(k);
        }
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = T[t][k], b = T[t][(k + 1) % 3];
            const uint64_t key = edgeKey(a, b);
            if (count[key] != 2) continue;
            const uint32_t c = first[key];
            if (c == 3 * t + uint32_t(k)) continue;
            const uint32_t ot = c / 3, ok = c % 3;
            // Consistently wound neighbours run the edge the other way.
            if (T[ot][ok] != b || T[ot][(ok + 1) % 3] != a) continue;
            opp[3 * t + uint32_t(k)] = c;
            opp[c] = 3 * t + uint32_t(k);
        }
}

struct ProxyBuilder::Impl {
    const qr::WorkMesh& src;
    const TriangleBVH* surface;
    const SegmentBVH* features;
    std::unordered_set<uint64_t> sharp;
    bool groups;

    std::vector<vec3> P;
    std::vector<glm::uvec3> T;
    std::vector<uint32_t> G;
    std::vector<uint8_t> deadT, deadV;
    std::vector<std::vector<uint32_t>> VT;
    std::vector<float> Lt; // per vertex target length (remesh)
    std::vector<uint8_t> locked; // pre-decimation blocks: vertices shared with other blocks never go
    float reach = 1.0f;

    explicit Impl(const ProxySource& s)
        : src(*s.mesh), surface(s.surface), features(s.features), sharp(s.sharp), groups(s.keepGroups) {
        P = src.P;
        T = src.T;
        G = src.group;
        if (G.size() != T.size()) G.assign(T.size(), 0);
        deadT.assign(T.size(), 0);
        deadV.assign(P.size(), 0);
        VT.resize(P.size());
        std::vector<uint32_t> deg(P.size(), 0);
        for (const auto& t : T)
            for (int k = 0; k < 3; ++k) ++deg[t[k]];
        for (size_t v = 0; v < P.size(); ++v) VT[v].reserve(deg[v]);
        for (uint32_t t = 0; t < T.size(); ++t) {
            const auto& tri = T[t];
            if (tri.x == tri.y || tri.y == tri.z || tri.x == tri.z) {
                deadT[t] = 1;
                continue;
            }
            for (int k = 0; k < 3; ++k) VT[tri[k]].push_back(t);
        }
        for (size_t v = 0; v < P.size(); ++v)
            if (VT[v].empty()) deadV[v] = 1;
        Lt.assign(P.size(), 1.0f);
    }

    static bool has(const glm::uvec3& t, uint32_t v) { return t.x == v || t.y == v || t.z == v; }

    int edgeTris(uint32_t a, uint32_t b, uint32_t* out, int max) const {
        int n = 0;
        for (uint32_t t : VT[a])
            if (has(T[t], b)) {
                if (n < max) out[n] = t;
                ++n;
            }
        return n;
    }

    // a -> b runs forwards in t.
    static bool forwards(const glm::uvec3& t, uint32_t a, uint32_t b) {
        return (t.x == a && t.y == b) || (t.y == a && t.z == b) || (t.z == a && t.x == b);
    }

    bool featureEdge(uint32_t a, uint32_t b) const {
        uint32_t t[4];
        const int n = edgeTris(a, b, t, 4);
        if (n != 2) return true;
        if (forwards(T[t[0]], a, b) == forwards(T[t[1]], a, b)) return true; // wound inconsistently
        if (groups && G[t[0]] != G[t[1]]) return true;
        return !sharp.empty() && sharp.count(edgeKey(a, b)) > 0;
    }

    void ring(uint32_t v, std::vector<uint32_t>& out) const {
        out.clear();
        for (uint32_t t : VT[v])
            for (int k = 0; k < 3; ++k)
                if (T[t][k] != v) out.push_back(T[t][k]);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

    // Feature edges at v: how many, and the first two other ends.
    int featureInfo(uint32_t v, uint32_t* ends, std::vector<uint32_t>& scratch) const {
        ring(v, scratch);
        int n = 0;
        for (uint32_t u : scratch)
            if (featureEdge(v, u)) {
                if (n < 2) ends[n] = u;
                ++n;
            }
        return n;
    }

    // 0 free, 1 on a feature line, 2 pinned (lines meet, end, or turn by more than 45 degrees).
    int kind(uint32_t v, uint32_t* ends, std::vector<uint32_t>& scratch) const {
        if (!locked.empty() && locked[v]) return 2;
        const int n = featureInfo(v, ends, scratch);
        if (n == 0) return 0;
        if (n != 2) return 2;
        const vec3 a = safeNormalize(P[ends[0]] - P[v]), b = safeNormalize(P[ends[1]] - P[v]);
        return glm::dot(a, b) > -0.7f ? 2 : 1;
    }

    vec3 triNormal(const glm::uvec3& t) const { return glm::cross(P[t.y] - P[t.x], P[t.z] - P[t.x]); }

    vec3 projectSurface(const vec3& p) const {
        SurfaceHit hit;
        return surface && surface->closestPoint(p, reach, hit) ? hit.position : p;
    }
    vec3 projectFeature(const vec3& p) const {
        SurfaceHit hit;
        if (features && features->closestPoint(p, reach, hit)) return hit.position;
        return projectSurface(p);
    }

    // Collapses b into a, a moving to `to`. maxLen: the longest edge the collapse may make.
    bool collapse(uint32_t b, uint32_t a, const vec3& to, float maxLen, std::vector<uint32_t>& ra,
                  std::vector<uint32_t>& rb, std::vector<uint32_t>& scratch) {
        uint32_t ends[2];
        const int kb = kind(b, ends, scratch);
        if (kb == 2) return false;
        if (kb == 1 && !featureEdge(a, b)) return false;
        uint32_t et[4];
        const int ne = edgeTris(a, b, et, 4);
        if (ne < 1 || ne > 2) return false;
        ring(a, ra);
        ring(b, rb);
        if (ra.size() <= 3 && rb.size() <= 3) return false; // a tetrahedron
        size_t common = 0;
        for (uint32_t x : rb)
            if (std::binary_search(ra.begin(), ra.end(), x)) ++common;
        if (common != size_t(ne)) return false; // link condition: the collapse would pinch the surface
        const float max2 = maxLen * maxLen;
        auto check = [&](uint32_t v) {
            for (uint32_t t : VT[v]) {
                const glm::uvec3 tri = T[t];
                if (has(tri, a) && has(tri, b)) continue;
                vec3 q[3];
                for (int k = 0; k < 3; ++k) q[k] = (tri[k] == a || tri[k] == b) ? to : P[tri[k]];
                const vec3 nNew = glm::cross(q[1] - q[0], q[2] - q[0]), nOld = triNormal(tri);
                const float ln = glm::length(nNew), lo = glm::length(nOld);
                if (ln <= 1e-12f * (lo + 1e-30f) || ln <= 0.0f) return false;
                if (glm::dot(nNew, nOld) < 0.2f * ln * lo) return false;
                for (int k = 0; k < 3; ++k)
                    if ((tri[k] == a || tri[k] == b) && maxLen > 0.0f) {
                        for (int j = 1; j < 3; ++j) {
                            const vec3 d = q[(k + j) % 3] - to;
                            if (glm::dot(d, d) > max2) return false;
                        }
                    }
                // Degenerate (needle) triangles: smallest height against the longest edge.
                float longest = 0.0f;
                for (int k = 0; k < 3; ++k) longest = std::max(longest, glm::length(q[(k + 1) % 3] - q[k]));
                if (ln < 0.02f * longest * longest) return false;
            }
            return true;
        };
        if (!check(b)) return false;
        if (to != P[a] && !check(a)) return false;
        // Do it.
        for (uint32_t t : VT[b]) {
            glm::uvec3& tri = T[t];
            if (has(tri, a)) {
                deadT[t] = 1;
                for (int k = 0; k < 3; ++k)
                    if (tri[k] != a && tri[k] != b) {
                        auto& l = VT[tri[k]];
                        l.erase(std::remove(l.begin(), l.end(), t), l.end());
                    }
                continue;
            }
            for (int k = 0; k < 3; ++k)
                if (tri[k] == b) tri[k] = a;
            VT[a].push_back(t);
        }
        {
            auto& l = VT[a];
            l.erase(std::remove_if(l.begin(), l.end(), [&](uint32_t t) { return deadT[t] != 0; }), l.end());
        }
        VT[b].clear();
        VT[b].shrink_to_fit();
        deadV[b] = 1;
        P[a] = to;
        Lt[a] = std::min(Lt[a], Lt[b]);
        if (!sharp.empty())
            for (uint32_t x : rb)
                if (sharp.erase(edgeKey(b, x)) && x != a) sharp.insert(edgeKey(a, x));
        return true;
    }

    // Tries to collapse the edge (a, b) whichever way the feature rules allow; the survivor, or kNone.
    uint32_t collapseEdge(uint32_t a, uint32_t b, float maxLen, std::vector<uint32_t>& ra, std::vector<uint32_t>& rb,
                          std::vector<uint32_t>& scratch) {
        uint32_t ends[2];
        const int ka = kind(a, ends, scratch), kbb = kind(b, ends, scratch);
        if (ka == 0 && kbb == 0) {
            const vec3 mid = 0.5f * (P[a] + P[b]);
            if (collapse(b, a, mid, maxLen, ra, rb, scratch)) return a;
            return kNone;
        }
        if (ka >= kbb) {
            if (collapse(b, a, P[a], maxLen, ra, rb, scratch)) return a;
            if (ka != 2 && collapse(a, b, P[b], maxLen, ra, rb, scratch)) return b;
        } else {
            if (collapse(a, b, P[b], maxLen, ra, rb, scratch)) return b;
        }
        return kNone;
    }

    struct Cand {
        float len2;
        uint32_t a, b;
    };
    // Edges (a < b) with length^2 in [lo2, hi2) scaled by the per-vertex target when `scaled`.
    std::vector<Cand> edges(float lo, float hi, bool scaled) const {
        const size_t nv = P.size();
        const size_t blocks = (nv + 4095) / 4096;
        std::vector<std::vector<Cand>> parts(blocks);
        parallelJobs(blocks, [&](size_t blk) {
            std::vector<uint32_t> r;
            {
                auto& out = parts[blk];
                for (size_t v = blk * 4096, e = std::min(nv, v + 4096); v < e; ++v) {
                    if (deadV[v]) continue;
                    ring(uint32_t(v), r);
                    for (uint32_t u : r) {
                        if (u <= v) continue;
                        const float l2 = glm::dot(P[u] - P[v], P[u] - P[v]);
                        const float s = scaled ? 0.5f * (Lt[v] + Lt[u]) : 1.0f;
                        if (l2 >= lo * lo * s * s && l2 < hi * hi * s * s) out.push_back({l2 / (s * s), uint32_t(v), u});
                    }
                }
            }
        });
        size_t total = 0;
        for (auto& p : parts) total += p.size();
        std::vector<Cand> all;
        all.reserve(total);
        for (auto& p : parts) all.insert(all.end(), p.begin(), p.end());
        return all;
    }

    size_t alive() const {
        size_t n = 0;
        for (uint8_t d : deadV) n += !d;
        return n;
    }

    void projectMoved(std::vector<uint32_t>& moved) {
        std::sort(moved.begin(), moved.end());
        moved.erase(std::unique(moved.begin(), moved.end()), moved.end());
        parallelFor(moved.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i)
                if (!deadV[moved[i]]) P[moved[i]] = projectSurface(P[moved[i]]);
        }, 2000);
        moved.clear();
    }

    void decimate(float edge) {
        reach = 3.0f * edge;
        std::vector<uint32_t> ra, rb, scratch, moved;
        std::vector<uint8_t> touched(P.size(), 0);
        for (int pass = 0; pass < 60; ++pass) {
            std::vector<Cand> c = edges(0.0f, 0.8f * edge, false);
            if (c.empty()) break;
            std::sort(c.begin(), c.end(), [](const Cand& x, const Cand& y) {
                return x.len2 < y.len2 || (x.len2 == y.len2 && (x.a < y.a || (x.a == y.a && x.b < y.b)));
            });
            std::fill(touched.begin(), touched.end(), 0);
            size_t done = 0;
            for (const Cand& e : c) {
                if (deadV[e.a] || deadV[e.b] || touched[e.a] || touched[e.b]) continue;
                const uint32_t s = collapseEdge(e.a, e.b, 1.25f * edge, ra, rb, scratch);
                if (s == kNone) continue;
                ++done;
                moved.push_back(s);
                touched[s] = 1;
                ring(s, ra);
                for (uint32_t u : ra) touched[u] = 1;
            }
            projectMoved(moved);
            if (done == 0 || done < c.size() / 400) break;
        }
    }

    // Splits the edge (a, b) at its middle; the new vertex, or kNone.
    uint32_t split(uint32_t a, uint32_t b) {
        uint32_t et[4];
        const int ne = edgeTris(a, b, et, 4);
        if (ne < 1 || ne > 2) return kNone;
        const bool feature = featureEdge(a, b);
        const uint32_t m = uint32_t(P.size());
        const vec3 mid = 0.5f * (P[a] + P[b]);
        P.push_back(feature ? projectFeature(mid) : projectSurface(mid));
        deadV.push_back(0);
        VT.emplace_back();
        Lt.push_back(0.5f * (Lt[a] + Lt[b]));
        for (int i = 0; i < ne; ++i) {
            const uint32_t t = et[i];
            glm::uvec3 tri = T[t];
            // Rotate so the edge is (tri.x, tri.y).
            for (int r = 0; r < 3 && !((tri.x == a && tri.y == b) || (tri.x == b && tri.y == a)); ++r) tri = {tri.y, tri.z, tri.x};
            const uint32_t x = tri.x, y = tri.y, z = tri.z;
            T[t] = {x, m, z};
            const uint32_t n = uint32_t(T.size());
            T.push_back({m, y, z});
            G.push_back(G[t]);
            deadT.push_back(0);
            auto& ly = VT[y];
            std::replace(ly.begin(), ly.end(), t, n);
            VT[m].push_back(t);
            VT[m].push_back(n);
            VT[z].push_back(n);
        }
        if (!sharp.empty() && sharp.erase(edgeKey(a, b))) {
            sharp.insert(edgeKey(a, m));
            sharp.insert(edgeKey(m, b));
        }
        return m;
    }

    void splitLong() {
        std::vector<Cand> c = edges(4.0f / 3.0f, 1e30f, true);
        std::sort(c.begin(), c.end(), [](const Cand& x, const Cand& y) {
            return x.len2 > y.len2 || (x.len2 == y.len2 && (x.a < y.a || (x.a == y.a && x.b < y.b)));
        });
        for (const Cand& e : c) {
            uint32_t et[4];
            if (edgeTris(e.a, e.b, et, 4) == 0) continue; // gone (split already from the other side)
            split(e.a, e.b);
        }
    }

    void collapseShort() {
        std::vector<Cand> c = edges(0.0f, 0.8f, true);
        std::sort(c.begin(), c.end(), [](const Cand& x, const Cand& y) {
            return x.len2 < y.len2 || (x.len2 == y.len2 && (x.a < y.a || (x.a == y.a && x.b < y.b)));
        });
        std::vector<uint32_t> ra, rb, scratch, moved;
        for (const Cand& e : c) {
            if (deadV[e.a] || deadV[e.b]) continue;
            const float s = 0.5f * (Lt[e.a] + Lt[e.b]);
            const float l2 = glm::dot(P[e.a] - P[e.b], P[e.a] - P[e.b]);
            if (l2 >= 0.64f * s * s) continue;
            uint32_t et[4];
            if (edgeTris(e.a, e.b, et, 4) == 0) continue;
            const uint32_t sv = collapseEdge(e.a, e.b, 4.0f / 3.0f * s, ra, rb, scratch);
            if (sv != kNone) moved.push_back(sv);
        }
        projectMoved(moved);
    }

    void flipEdges() {
        const size_t nv = P.size();
        std::vector<int> val(nv, 0), target(nv, 6);
        {
            std::vector<uint32_t> r, scratch;
            uint32_t ends[2];
            for (uint32_t v = 0; v < nv; ++v) {
                if (deadV[v]) continue;
                ring(v, r);
                val[v] = int(r.size());
                const int k = kind(v, ends, scratch);
                if (k != 0) target[v] = 4;
            }
        }
        std::vector<Cand> c = edges(0.0f, 1e30f, false);
        for (const Cand& e : c) {
            const uint32_t a = e.a, b = e.b;
            uint32_t et[4];
            if (edgeTris(a, b, et, 4) != 2 || featureEdge(a, b)) continue;
            glm::uvec3 t1 = T[et[0]], t2 = T[et[1]];
            if (!forwards(t1, a, b)) std::swap(t1, t2), std::swap(et[0], et[1]);
            if (!forwards(t1, a, b) || !forwards(t2, b, a)) continue;
            uint32_t c1 = kNone, d1 = kNone;
            for (int k = 0; k < 3; ++k) {
                if (t1[k] != a && t1[k] != b) c1 = t1[k];
                if (t2[k] != a && t2[k] != b) d1 = t2[k];
            }
            if (c1 == kNone || d1 == kNone || c1 == d1) continue;
            if (val[a] <= 3 || val[b] <= 3) continue;
            auto dev = [&](uint32_t v, int dv) { return std::abs(val[v] + dv - target[v]); };
            const int before = dev(a, 0) + dev(b, 0) + dev(c1, 0) + dev(d1, 0);
            const int after = dev(a, -1) + dev(b, -1) + dev(c1, 1) + dev(d1, 1);
            if (after >= before) continue;
            // c-d must not be an edge already.
            uint32_t tmp[1];
            if (edgeTris(c1, d1, tmp, 1) > 0) continue;
            const vec3 n1 = triNormal(t1), n2 = triNormal(t2);
            const vec3 nOld = safeNormalize(n1) + safeNormalize(n2);
            if (glm::dot(safeNormalize(n1), safeNormalize(n2)) < 0.85f) continue; // not across a fold
            const glm::uvec3 u1{a, d1, c1}, u2{d1, b, c1};
            const vec3 m1 = triNormal(u1), m2 = triNormal(u2);
            if (glm::dot(m1, nOld) <= 0.0f || glm::dot(m2, nOld) <= 0.0f) continue;
            if (glm::dot(safeNormalize(m1), safeNormalize(m2)) < 0.85f) continue;
            T[et[0]] = u1;
            T[et[1]] = u2;
            // VT: a loses et[1], b loses et[0], c gains et[1], d gains et[0].
            auto& la = VT[a];
            la.erase(std::remove(la.begin(), la.end(), et[1]), la.end());
            auto& lb = VT[b];
            lb.erase(std::remove(lb.begin(), lb.end(), et[0]), lb.end());
            VT[c1].push_back(et[1]);
            VT[d1].push_back(et[0]);
            --val[a], --val[b], ++val[c1], ++val[d1];
        }
    }

    void smooth(int rounds) {
        const size_t nv = P.size();
        std::vector<vec3> N(nv, vec3(0.0f)), next(nv);
        std::vector<int8_t> kinds(nv, 0);
        std::vector<glm::uvec2> lineNb(nv, glm::uvec2(kNone));
        parallelFor(nv, [&](size_t b, size_t e) {
            std::vector<uint32_t> scratch;
            uint32_t ends[2];
            for (size_t v = b; v < e; ++v) {
                if (deadV[v]) continue;
                kinds[v] = int8_t(kind(uint32_t(v), ends, scratch));
                if (kinds[v] == 1) lineNb[v] = {ends[0], ends[1]};
            }
        }, 2000);
        for (int r = 0; r < rounds; ++r) {
            parallelFor(nv, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    vec3 n(0.0f);
                    for (uint32_t t : VT[v]) n += triNormal(T[t]);
                    N[v] = safeNormalize(n);
                }
            }, 2000);
            parallelFor(nv, [&](size_t b, size_t e) {
                std::vector<uint32_t> rg;
                for (size_t v = b; v < e; ++v) {
                    next[v] = P[v];
                    if (deadV[v] || kinds[v] == 2) continue;
                    if (kinds[v] == 1) {
                        const vec3 c = 0.5f * (P[lineNb[v].x] + P[lineNb[v].y]);
                        next[v] = projectFeature(P[v] + 0.5f * (c - P[v]));
                        continue;
                    }
                    ring(uint32_t(v), rg);
                    if (rg.empty()) continue;
                    // Weighted towards the target sizes: a neighbour wanting small edges pulls less.
                    vec3 c(0.0f);
                    float w = 0.0f;
                    for (uint32_t u : rg) {
                        const float wu = Lt[u];
                        c += wu * P[u];
                        w += wu;
                    }
                    c /= w;
                    vec3 d = c - P[v];
                    d -= N[v] * glm::dot(N[v], d);
                    next[v] = projectSurface(P[v] + 0.5f * d);
                }
            }, 2000);
            // Keep a move only if no triangle around turns over.
            parallelFor(nv, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    if (deadV[v] || next[v] == P[v]) continue;
                    bool ok = true;
                    for (uint32_t t : VT[v]) {
                        const glm::uvec3 tri = T[t];
                        vec3 q[3];
                        for (int k = 0; k < 3; ++k) q[k] = tri[k] == v ? next[v] : P[tri[k]];
                        const vec3 nn = glm::cross(q[1] - q[0], q[2] - q[0]);
                        if (glm::dot(nn, triNormal(tri)) <= 0.0f) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) next[v] = P[v];
                }
            }, 2000);
            P.swap(next);
        }
    }

    void remesh(float edge, const std::vector<float>& rel, int iterations) {
        reach = 3.0f * edge;
        for (size_t v = 0; v < P.size(); ++v) Lt[v] = edge * (v < rel.size() ? rel[v] : 1.0f);
        for (int it = 0; it < iterations; ++it) {
            splitLong();
            collapseShort();
            flipEdges();
            smooth(2);
        }
    }
};

qr::WorkMesh preDecimate(const ProxySource& source, float edge, std::unordered_set<uint64_t>& sharpOut) {
    const qr::WorkMesh& m = *source.mesh;
    const size_t nt = m.T.size(), nv = m.P.size();
    // Spatial blocks: recursive median splits of the triangle centres along the longest axis.
    const int levels = 6; // 64 blocks
    std::vector<uint32_t> order(nt);
    std::iota(order.begin(), order.end(), 0u);
    std::vector<vec3> centre(nt);
    parallelFor(nt, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) centre[t] = (m.P[m.T[t].x] + m.P[m.T[t].y] + m.P[m.T[t].z]) / 3.0f;
    });
    std::vector<std::pair<size_t, size_t>> ranges{{0, nt}};
    for (int l = 0; l < levels; ++l) {
        std::vector<std::pair<size_t, size_t>> next;
        for (auto [b, e] : ranges) {
            if (e - b < 2) {
                next.push_back({b, e});
                continue;
            }
            vec3 lo(1e30f), hi(-1e30f);
            for (size_t i = b; i < e; ++i) lo = glm::min(lo, centre[order[i]]), hi = glm::max(hi, centre[order[i]]);
            const vec3 ext = hi - lo;
            const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
            const size_t mid = (b + e) / 2;
            std::nth_element(order.begin() + long(b), order.begin() + long(mid), order.begin() + long(e),
                             [&](uint32_t x, uint32_t y) { return centre[x][axis] < centre[y][axis]; });
            next.push_back({b, mid});
            next.push_back({mid, e});
        }
        ranges.swap(next);
    }
    std::vector<uint32_t> blockOf(nt);
    for (uint32_t k = 0; k < ranges.size(); ++k)
        for (size_t i = ranges[k].first; i < ranges[k].second; ++i) blockOf[order[i]] = k;
    // Vertices used by more than one block stay.
    std::vector<uint32_t> vblock(nv, kNone);
    std::vector<uint8_t> shared(nv, 0);
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            uint32_t& b = vblock[m.T[t][k]];
            if (b == kNone) b = blockOf[t];
            else if (b != blockOf[t]) shared[m.T[t][k]] = 1;
        }
    struct Part {
        std::vector<vec3> P;
        std::vector<uint32_t> g; // local -> global vertex
        std::vector<glm::uvec3> T;
        std::vector<uint32_t> G;
        std::vector<uint64_t> sharp;
    };
    std::vector<Part> parts(ranges.size());
    std::vector<vec3> P = m.P;
    parallelJobs(ranges.size(), [&](size_t k) {
        {
            qr::WorkMesh local;
            std::unordered_map<uint32_t, uint32_t> l;
            std::vector<uint32_t> g;
            auto id = [&](uint32_t v) {
                auto [it, inserted] = l.try_emplace(v, uint32_t(g.size()));
                if (inserted) g.push_back(v), local.P.push_back(m.P[v]);
                return it->second;
            };
            for (size_t i = ranges[k].first; i < ranges[k].second; ++i) {
                const uint32_t t = order[i];
                local.T.push_back({id(m.T[t].x), id(m.T[t].y), id(m.T[t].z)});
                local.group.push_back(m.group.empty() ? 0u : m.group[t]);
            }
            ProxySource ls = source;
            ls.mesh = &local;
            ls.sharp.clear();
            for (uint64_t key : source.sharp) {
                const auto a = l.find(uint32_t(key >> 32)), b = l.find(uint32_t(key & 0xffffffffu));
                if (a != l.end() && b != l.end()) ls.sharp.insert(edgeKey(a->second, b->second));
            }
            ProxyBuilder::Impl impl(ls);
            impl.locked.assign(local.P.size(), 0);
            for (size_t v = 0; v < g.size(); ++v) impl.locked[v] = shared[g[v]];
            impl.decimate(edge);
            Part& out = parts[k];
            out.g = g;
            out.P = impl.P;
            for (uint32_t t = 0; t < impl.T.size(); ++t)
                if (!impl.deadT[t]) out.T.push_back({g[impl.T[t].x], g[impl.T[t].y], g[impl.T[t].z]}), out.G.push_back(impl.G[t]);
            for (uint64_t key : impl.sharp) out.sharp.push_back(edgeKey(g[uint32_t(key >> 32)], g[uint32_t(key & 0xffffffffu)]));
        }
    });
    // Merge (surviving vertices keep their global ids, moved by midpoint collapses), then compact.
    qr::WorkMesh merged;
    std::vector<uint32_t> remap(nv, kNone);
    for (Part& part : parts) {
        for (size_t v = 0; v < part.g.size(); ++v) P[part.g[v]] = part.P[v];
    }
    sharpOut.clear();
    for (Part& part : parts) {
        for (size_t i = 0; i < part.T.size(); ++i) {
            glm::uvec3 t = part.T[i];
            for (int k = 0; k < 3; ++k) {
                uint32_t& r = remap[t[k]];
                if (r == kNone) r = uint32_t(merged.P.size()), merged.P.push_back(P[t[k]]);
                t[k] = r;
            }
            merged.T.push_back(t);
            merged.group.push_back(part.G[i]);
        }
    }
    for (Part& part : parts)
        for (uint64_t key : part.sharp) {
            const uint32_t a = remap[uint32_t(key >> 32)], b = remap[uint32_t(key & 0xffffffffu)];
            if (a != kNone && b != kNone) sharpOut.insert(edgeKey(a, b));
        }
    if (!m.color.empty()) {
        merged.color.assign(merged.P.size(), vec3(1.0f));
        for (uint32_t v = 0; v < nv; ++v)
            if (remap[v] != kNone) merged.color[remap[v]] = m.color[v];
    }
    return merged;
}

ProxyBuilder::ProxyBuilder(const ProxySource& source) : impl_(std::make_unique<Impl>(source)) {}
ProxyBuilder::~ProxyBuilder() = default;
void ProxyBuilder::decimate(float edge) { impl_->decimate(edge); }
void ProxyBuilder::remesh(float edge, const std::vector<float>& relSize, int iterations) {
    impl_->remesh(edge, relSize, iterations);
}
size_t ProxyBuilder::vertexCount() const { return impl_->P.size(); }

Proxy ProxyBuilder::result(std::vector<uint32_t>* vertexMap) const {
    const Impl& m = *impl_;
    Proxy p;
    std::vector<uint32_t> remap(m.P.size(), kNone);
    for (uint32_t t = 0; t < m.T.size(); ++t) {
        if (m.deadT[t]) continue;
        glm::uvec3 tri = m.T[t];
        for (int k = 0; k < 3; ++k) {
            uint32_t& r = remap[tri[k]];
            if (r == kNone) {
                r = uint32_t(p.P.size());
                p.P.push_back(m.P[tri[k]]);
                if (vertexMap) vertexMap->push_back(tri[k]);
            }
        }
        p.T.push_back({remap[tri.x], remap[tri.y], remap[tri.z]});
        p.group.push_back(m.G[t]);
        p.feature.push_back(0), p.feature.push_back(0), p.feature.push_back(0);
        p.sharp.push_back(0), p.sharp.push_back(0), p.sharp.push_back(0);
        const size_t f = p.T.size() - 1;
        for (int k = 0; k < 3; ++k) {
            p.feature[3 * f + size_t(k)] = m.featureEdge(tri[k], tri[(k + 1) % 3]) ? 1 : 0;
            p.sharp[3 * f + size_t(k)] = !m.sharp.empty() && m.sharp.count(edgeKey(tri[k], tri[(k + 1) % 3])) ? 1 : 0;
        }
    }
    p.buildOpposites();
    for (size_t c = 0; c < p.opp.size(); ++c)
        if (p.opp[c] == kNone) p.feature[c] = 1;
    p.size.assign(p.P.size(), 1.0f);
    return p;
}

} // namespace sf::qr2
