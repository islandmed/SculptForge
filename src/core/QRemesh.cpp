// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh: field-aligned quad remeshing (see QRemesh.h for the outline). The field formulas (extrinsic 4-RoSy
// orientation and position compatibility, the middle point of two tangent planes) follow Instant Meshes
// (Jakob et al. 2015); hierarchy, extraction, quad splitting and relaxation are our own. Instant Meshes is
// Copyright (c) 2015 Wenzel Jakob, Daniele Panozzo, Marco Tarini, and Olga Sorkine-Hornung, BSD-3-Clause; see
// THIRD_PARTY_NOTICES.md.
#include "core/QRemesh.h"

#include "core/Parallel.h"
#include "core/QRemeshInternal.h"
#include "core/SurfaceQuery.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace sf {

namespace {

using glm::ivec2;
using glm::vec3;

constexpr uint32_t kNone = ~0u;


uint64_t edgeKey(uint32_t a, uint32_t b) { return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a; }

vec3 safeNormalize(const vec3& v, const vec3& fallback = vec3(0, 0, 1)) {
    const float l = glm::length(v);
    return l > 1e-20f ? v / l : fallback;
}

// Any unit vector perpendicular to n.
vec3 anyTangent(const vec3& n) {
    const vec3 a = std::abs(n.x) < 0.6f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    return safeNormalize(glm::cross(n, a));
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    float next() { // 0..1
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return float((s >> 40) & 0xFFFFFF) / float(0x1000000);
    }
};

// --- Instant Meshes field formulas (4-RoSy, 4-PoSy, extrinsic) -------------------------------------------

// The pair of representatives of q0's and q1's crosses (each a direction and its 90 degree rotations) that
// are closest to each other.
inline std::pair<vec3, vec3> compatOrientation(const vec3& q0, const vec3& n0, const vec3& q1, const vec3& n1) {
    const vec3 A[2] = {q0, glm::cross(n0, q0)};
    const vec3 B[2] = {q1, glm::cross(n1, q1)};
    float best = -1.0f;
    int ba = 0, bb = 0;
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b) {
            const float s = std::abs(glm::dot(A[a], B[b]));
            if (s > best) best = s, ba = a, bb = b;
        }
    const float dp = glm::dot(A[ba], B[bb]);
    return {A[ba], B[bb] * (dp < 0.0f ? -1.0f : 1.0f)};
}

// q1's cross member that best matches q0 (q1 rotated into q0's frame).
inline vec3 alignTo(const vec3& q0, const vec3& n0, const vec3& q1, const vec3& n1) {
    const vec3 t1 = glm::cross(n1, q1);
    const vec3 cands[4] = {q1, t1, -q1, -t1};
    int best = 0;
    float bd = -2.0f;
    for (int k = 0; k < 4; ++k)
        if (const float d = glm::dot(q0, cands[k]); d > bd) bd = d, best = k;
    return cands[best];
}

// The point minimising the distance to p0 and p1 while lying on both tangent planes (Instant Meshes).
inline vec3 middlePoint(const vec3& p0, const vec3& n0, const vec3& p1, const vec3& n1) {
    const float n0p0 = glm::dot(n0, p0), n0p1 = glm::dot(n0, p1), n1p0 = glm::dot(n1, p0), n1p1 = glm::dot(n1, p1);
    const float n0n1 = glm::dot(n0, n1), denom = 1.0f / (1.0f - n0n1 * n0n1 + 1e-4f);
    const float l0 = 2.0f * (n0p1 - n0p0 - n0n1 * (n1p0 - n1p1)) * denom;
    const float l1 = 2.0f * (n1p0 - n1p1 - n0n1 * (n0p1 - n0p0)) * denom;
    return 0.5f * (p0 + p1) - 0.25f * (n0 * l0 + n1 * l1);
}

inline ivec2 floorIndex(const vec3& o, const vec3& q, const vec3& t, float inv, const vec3& p) {
    const vec3 d = p - o;
    return {int(std::floor(glm::dot(q, d) * inv)), int(std::floor(glm::dot(t, d) * inv))};
}

inline vec3 latticePoint(const vec3& o, const vec3& q, const vec3& t, float s, ivec2 i) {
    return o + q * (float(i.x) * s) + t * (float(i.y) * s);
}

// The lattice corners of vertex 0 and vertex 1 (q1 already aligned with q0) nearest each other around the
// middle of the two vertices, as lattice indices relative to o0 / o1, and their squared distance.
inline float compatIndex(const vec3& v0, const vec3& n0, const vec3& q0, const vec3& o0, float s0, const vec3& v1,
                         const vec3& n1, const vec3& q1, const vec3& o1, float s1, ivec2& i0, ivec2& i1) {
    const vec3 t0 = glm::cross(n0, q0), t1 = glm::cross(n1, q1);
    const vec3 mid = middlePoint(v0, n0, v1, n1);
    const ivec2 b0 = floorIndex(o0, q0, t0, 1.0f / s0, mid), b1 = floorIndex(o1, q1, t1, 1.0f / s1, mid);
    vec3 p1[4];
    for (int b = 0; b < 4; ++b) p1[b] = latticePoint(o1, q1, t1, s1, b1 + ivec2(b & 1, b >> 1));
    float best = std::numeric_limits<float>::max();
    for (int a = 0; a < 4; ++a) {
        const ivec2 c0 = b0 + ivec2(a & 1, a >> 1);
        const vec3 p0 = latticePoint(o0, q0, t0, s0, c0);
        for (int b = 0; b < 4; ++b) {
            const vec3 d = p0 - p1[b];
            const float cost = glm::dot(d, d);
            if (cost < best) best = cost, i0 = c0, i1 = b1 + ivec2(b & 1, b >> 1);
        }
    }
    return best;
}

inline vec3 positionRound(const vec3& o, const vec3& q, const vec3& n, const vec3& p, float s) {
    const vec3 t = glm::cross(n, q), d = p - o;
    const float inv = 1.0f / s;
    return o + q * (std::round(glm::dot(q, d) * inv) * s) + t * (std::round(glm::dot(t, d) * inv) * s);
}

// --- Working surface ------------------------------------------------------------------------------------

using qr::WorkMesh;

// Keeps the side of the plane (axis coordinate >= 0), splitting the triangles that cross it.
WorkMesh cutAtPlane(const WorkMesh& in, int axis, float eps) {
    WorkMesh out;
    std::vector<vec3> P = in.P;
    for (vec3& p : P)
        if (std::abs(p[axis]) <= eps) p[axis] = 0.0f;
    std::vector<uint32_t> remap(P.size(), kNone);
    auto keep = [&](uint32_t v) {
        if (remap[v] == kNone) {
            remap[v] = uint32_t(out.P.size());
            out.P.push_back(P[v]);
            if (!in.color.empty()) out.color.push_back(in.color[v]);
        }
        return remap[v];
    };
    std::unordered_map<uint64_t, uint32_t> cuts;
    auto cutPoint = [&](uint32_t a, uint32_t b) {
        auto [it, inserted] = cuts.try_emplace(edgeKey(a, b), 0);
        if (inserted) {
            const float t = P[a][axis] / (P[a][axis] - P[b][axis]);
            vec3 p = glm::mix(P[a], P[b], t);
            p[axis] = 0.0f;
            it->second = uint32_t(out.P.size());
            out.P.push_back(p);
            if (!in.color.empty()) out.color.push_back(glm::mix(in.color[a], in.color[b], t));
        }
        return it->second;
    };
    for (size_t t = 0; t < in.T.size(); ++t) {
        const glm::uvec3 tri = in.T[t];
        int side[3];
        bool anyPos = false, anyNeg = false;
        for (int k = 0; k < 3; ++k) {
            const float x = P[tri[k]][axis];
            side[k] = x > 0.0f ? 1 : x < 0.0f ? -1 : 0;
            anyPos |= side[k] > 0;
            anyNeg |= side[k] < 0;
        }
        if (!anyPos) continue;
        uint32_t poly[4];
        int n = 0;
        if (!anyNeg) {
            for (int k = 0; k < 3; ++k) poly[n++] = keep(tri[k]);
        } else {
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = tri[k], b = tri[(k + 1) % 3];
                if (side[k] >= 0) poly[n++] = keep(a);
                if ((side[k] > 0 && side[(k + 1) % 3] < 0) || (side[k] < 0 && side[(k + 1) % 3] > 0)) poly[n++] = cutPoint(a, b);
            }
        }
        for (int k = 1; k + 1 < n; ++k) {
            out.T.push_back({poly[0], poly[k], poly[k + 1]});
            out.group.push_back(in.group[t]);
        }
    }
    return out;
}

// Splits edges longer than `maxEdge` at their midpoints (each triangle into 2, 3 or 4, conforming), pass after
// pass, so coarse parts of the input (a low-poly base under a dense sculpt) are dense enough for the fields.
// Stops early when the triangle count would pass `maxTriangles`.
WorkMesh refineLongEdges(WorkMesh m, float maxEdge, size_t maxTriangles) {
    const float max2 = maxEdge * maxEdge;
    for (int pass = 0; pass < 12; ++pass) {
        std::unordered_map<uint64_t, uint32_t> mids;
        auto longEdge = [&](uint32_t a, uint32_t b) { return glm::dot(m.P[a] - m.P[b], m.P[a] - m.P[b]) > max2; };
        size_t extra = 0;
        for (const glm::uvec3& t : m.T)
            for (int k = 0; k < 3; ++k)
                if (longEdge(t[k], t[(k + 1) % 3])) ++extra;
        if (extra == 0 || m.T.size() + extra > maxTriangles) break;
        auto mid = [&](uint32_t a, uint32_t b) {
            auto [it, inserted] = mids.try_emplace(edgeKey(a, b), uint32_t(m.P.size()));
            if (inserted) {
                m.P.push_back(0.5f * (m.P[a] + m.P[b]));
                if (!m.color.empty()) m.color.push_back(0.5f * (m.color[a] + m.color[b]));
            }
            return it->second;
        };
        std::vector<glm::uvec3> T;
        std::vector<uint32_t> G;
        T.reserve(m.T.size() + extra);
        G.reserve(m.T.size() + extra);
        for (size_t i = 0; i < m.T.size(); ++i) {
            const glm::uvec3 t = m.T[i];
            const uint32_t g = m.group[i];
            uint32_t mk[3];
            int marked = 0, first = -1;
            for (int k = 0; k < 3; ++k) {
                mk[k] = longEdge(t[k], t[(k + 1) % 3]) ? mid(t[k], t[(k + 1) % 3]) : kNone;
                if (mk[k] != kNone) {
                    ++marked;
                    if (first < 0) first = k;
                }
            }
            auto emit = [&](uint32_t a, uint32_t b, uint32_t c) {
                T.push_back({a, b, c});
                G.push_back(g);
            };
            if (marked == 0) {
                emit(t.x, t.y, t.z);
            } else if (marked == 3) {
                emit(t.x, mk[0], mk[2]);
                emit(mk[0], t.y, mk[1]);
                emit(mk[2], mk[1], t.z);
                emit(mk[0], mk[1], mk[2]);
            } else if (marked == 1) {
                const int k = first;
                const uint32_t a = t[k], b = t[(k + 1) % 3], c = t[(k + 2) % 3], ab = mk[k];
                emit(a, ab, c);
                emit(ab, b, c);
            } else { // two edges: the unmarked one is (c, a) after rotating so edges k and k+1 are marked
                int k = 0;
                while (!(mk[k] != kNone && mk[(k + 1) % 3] != kNone)) ++k;
                const uint32_t a = t[k], b = t[(k + 1) % 3], c = t[(k + 2) % 3], ab = mk[k], bc = mk[(k + 1) % 3];
                emit(ab, b, bc);
                emit(a, ab, bc);
                emit(a, bc, c);
            }
        }
        m.T.swap(T);
        m.group.swap(G);
    }
    return m;
}

// --- Hierarchy ------------------------------------------------------------------------------------------

enum : uint8_t { kFree = 0, kLine = 1, kPoint = 2 };

struct Level {
    std::vector<vec3> V, N;
    std::vector<float> A;
    std::vector<uint32_t> off, adj; // CSR adjacency
    std::vector<float> w;
    std::vector<uint32_t> up;       // index of the merged vertex in the next (coarser) level
    // Constraints: kLine = the lattice has a line through cpos along cdir; kPoint = a lattice corner at cpos.
    std::vector<uint8_t> ctype;
    std::vector<vec3> cdir, cpos;
    std::vector<uint8_t> boundary;  // on an open edge of the mesh
    std::vector<vec3> color;        // polypaint (usePolypaint only)
    // QRemesh Guides: how strongly (0..1) the orientation turns to the nearest curve's direction gdir (empty
    // without guides).
    std::vector<float> gw;
    std::vector<vec3> gdir;
    // Line constraints on a closed guide (empty without closed guides): which (-1: none), where along it, and
    // whether cpos is pinned as a lattice corner (pinClosedGuides).
    std::vector<int32_t> gid;
    std::vector<float> garc;
    std::vector<uint8_t> cfix;
    // Fields.
    std::vector<vec3> Q, O;
    std::vector<float> S;
    std::vector<std::vector<uint32_t>> phases; // independent sets for parallel Gauss-Seidel
    float meanEdge = 0.0f;

    size_t size() const { return V.size(); }
};

void computePhases(Level& l) {
    l.phases.clear();
    const size_t n = l.size();
    if (n < 4000) { // sequential sweeps: one phase in index order
        l.phases.emplace_back(n);
        std::iota(l.phases[0].begin(), l.phases[0].end(), 0u);
        return;
    }
    std::vector<uint8_t> color(n, 255);
    std::vector<uint8_t> used;
    for (uint32_t i = 0; i < n; ++i) {
        used.assign(64, 0);
        for (uint32_t k = l.off[i]; k < l.off[i + 1]; ++k)
            if (color[l.adj[k]] < 64) used[color[l.adj[k]]] = 1;
        uint8_t c = 0;
        while (c < 63 && used[c]) ++c;
        color[i] = c;
        if (c >= l.phases.size()) l.phases.resize(c + 1);
        l.phases[c].push_back(i);
    }
}

void computeMeanEdge(Level& l) {
    double sum = 0.0;
    size_t cnt = 0;
    for (uint32_t i = 0; i < l.size(); ++i)
        for (uint32_t k = l.off[i]; k < l.off[i + 1]; ++k) {
            sum += glm::length(l.V[i] - l.V[l.adj[k]]);
            ++cnt;
        }
    l.meanEdge = cnt ? float(sum / double(cnt)) : 0.0f;
}

// Level 0 from the working mesh: vertex graph, area-weighted normals, constraints on open edges and (with
// keepGroups) polygroup borders.
Level buildFinest(const WorkMesh& m, bool keepGroups, float sharpCos, std::vector<std::pair<vec3, vec3>>& boundarySegs,
                  std::vector<uint8_t>& boundarySegIsCut, std::vector<std::pair<vec3, vec3>>& borderSegs, int symAxis) {
    Level l;
    const size_t nv = m.P.size();
    l.V = m.P;
    l.N.assign(nv, vec3(0.0f));
    l.A.assign(nv, 0.0f);
    for (const glm::uvec3& t : m.T) {
        const vec3 n = glm::cross(m.P[t.y] - m.P[t.x], m.P[t.z] - m.P[t.x]);
        const float a = 0.5f * glm::length(n);
        for (int k = 0; k < 3; ++k) {
            l.N[t[k]] += n;
            l.A[t[k]] += a / 3.0f;
        }
    }
    for (auto& n : l.N) n = safeNormalize(n);

    // Edges with the triangles on each side.
    struct EdgeInfo {
        uint32_t a, b;
        uint32_t t0, t1;
        int count;
    };
    // Each edge found from its lower vertex: the triangles around a vertex, their edges to higher vertices.
    std::vector<uint32_t> vtOff(nv + 1, 0), vt(m.T.size() * 3);
    for (const glm::uvec3& t : m.T)
        for (int k = 0; k < 3; ++k) ++vtOff[t[k] + 1];
    for (size_t i = 0; i < nv; ++i) vtOff[i + 1] += vtOff[i];
    {
        std::vector<uint32_t> fill(vtOff.begin(), vtOff.end() - 1);
        for (uint32_t t = 0; t < m.T.size(); ++t)
            for (int k = 0; k < 3; ++k) vt[fill[m.T[t][k]]++] = t;
    }
    std::vector<uint32_t> edgeOff(nv + 1, 0);
    std::vector<EdgeInfo> edges;
    auto vertexEdges = [&](uint32_t a, auto&& emit) {
        std::pair<uint32_t, uint32_t> local[64]; // (other vertex, triangle)
        std::vector<std::pair<uint32_t, uint32_t>> big;
        size_t n = 0;
        auto push = [&](uint32_t b, uint32_t t) {
            if (n < 64) local[n++] = {b, t};
            else {
                if (big.empty()) big.assign(local, local + 64);
                big.push_back({b, t});
                ++n;
            }
        };
        for (uint32_t k = vtOff[a]; k < vtOff[a + 1]; ++k) {
            const uint32_t t = vt[k];
            for (int e = 0; e < 3; ++e) {
                const uint32_t x = m.T[t][e], y = m.T[t][(e + 1) % 3];
                if (x == y) continue;
                if (x == a && y > a) push(y, t);
                else if (y == a && x > a) push(x, t);
            }
        }
        std::pair<uint32_t, uint32_t>* p = big.empty() ? local : big.data();
        std::sort(p, p + n);
        for (size_t i = 0; i < n;) {
            size_t j = i;
            while (j < n && p[j].first == p[i].first) ++j;
            emit(EdgeInfo{a, p[i].first, p[i].second, j - i > 1 ? p[i + 1].second : kNone, int(j - i)});
            i = j;
        }
    };
    parallelFor(nv, [&](size_t b0, size_t e0) {
        for (size_t a = b0; a < e0; ++a) {
            uint32_t count = 0;
            vertexEdges(uint32_t(a), [&](const EdgeInfo&) { ++count; });
            edgeOff[a + 1] = count;
        }
    });
    for (size_t i = 0; i < nv; ++i) edgeOff[i + 1] += edgeOff[i];
    edges.resize(edgeOff[nv]);
    parallelFor(nv, [&](size_t b0, size_t e0) {
        for (size_t a = b0; a < e0; ++a) {
            uint32_t at = edgeOff[a];
            vertexEdges(uint32_t(a), [&](const EdgeInfo& e) { edges[at++] = e; });
        }
    });
    // CSR adjacency.
    l.off.assign(nv + 1, 0);
    for (const EdgeInfo& e : edges) ++l.off[e.a + 1], ++l.off[e.b + 1];
    for (size_t i = 0; i < nv; ++i) l.off[i + 1] += l.off[i];
    l.adj.resize(l.off[nv]);
    l.w.assign(l.off[nv], 1.0f);
    {
        std::vector<uint32_t> fill(l.off.begin(), l.off.end() - 1);
        for (const EdgeInfo& e : edges) {
            l.adj[fill[e.a]++] = e.b;
            l.adj[fill[e.b]++] = e.a;
        }
    }

    // Feature edges: open edges and polygroup borders.
    l.ctype.assign(nv, kFree);
    l.cdir.assign(nv, vec3(0.0f));
    l.cpos = l.V;
    l.boundary.assign(nv, 0);
    std::vector<std::vector<uint32_t>> featureNb(nv);
    for (const EdgeInfo& e : edges) {
        const bool open = e.count == 1;
        bool border = keepGroups && e.count == 2 && m.group[e.t0] != m.group[e.t1];
        if (!border && sharpCos < 1.0f && e.count == 2) { // a hard edge (Detect Edges)
            const glm::uvec3 a = m.T[e.t0], b = m.T[e.t1];
            const vec3 na = safeNormalize(glm::cross(m.P[a.y] - m.P[a.x], m.P[a.z] - m.P[a.x]));
            const vec3 nb = safeNormalize(glm::cross(m.P[b.y] - m.P[b.x], m.P[b.z] - m.P[b.x]));
            border = glm::dot(na, nb) < sharpCos;
        }
        if (!open && !border) continue;
        featureNb[e.a].push_back(e.b);
        featureNb[e.b].push_back(e.a);
        if (open) {
            l.boundary[e.a] = l.boundary[e.b] = 1;
            boundarySegs.push_back({m.P[e.a], m.P[e.b]});
            boundarySegIsCut.push_back(symAxis >= 0 && m.P[e.a][symAxis] == 0.0f && m.P[e.b][symAxis] == 0.0f);
        } else {
            borderSegs.push_back({m.P[e.a], m.P[e.b]});
        }
    }
    // Tangent of the feature curve at each vertex, smoothed along the curve (borders on triangle meshes zigzag).
    std::vector<vec3> dir(nv, vec3(0.0f));
    for (uint32_t v = 0; v < nv; ++v) {
        const auto& f = featureNb[v];
        if (f.size() == 2) dir[v] = safeNormalize(m.P[f[0]] - m.P[f[1]], vec3(0.0f));
    }
    for (int it = 0; it < 4; ++it) {
        std::vector<vec3> next = dir;
        for (uint32_t v = 0; v < nv; ++v) {
            if (featureNb[v].size() != 2) continue;
            vec3 sum = dir[v];
            for (uint32_t u : featureNb[v])
                if (featureNb[u].size() == 2) sum += glm::dot(dir[u], sum) < 0.0f ? -dir[u] : dir[u];
            next[v] = safeNormalize(sum, dir[v]);
        }
        dir.swap(next);
    }
    for (uint32_t v = 0; v < nv; ++v) {
        const size_t k = featureNb[v].size();
        if (k == 0) continue;
        if (k == 2) {
            const vec3 d = dir[v] - l.N[v] * glm::dot(l.N[v], dir[v]);
            if (glm::length(d) > 1e-6f) {
                l.ctype[v] = kLine;
                l.cdir[v] = glm::normalize(d);
                continue;
            }
        }
        l.ctype[v] = kPoint; // junction of several borders, or the end of one
    }
    if (!m.color.empty()) l.color = m.color;
    computeMeanEdge(l);
    return l;
}

// --- QRemesh Guides --------------------------------------------------------------------------------------

// A closed guide (or, with symmetry, a curve from the seam to the seam: half of a loop across the middle): its
// points (in order), the arc length at each, and how many lattice steps go round (or along) it.
struct ClosedGuide {
    std::vector<vec3> points;
    std::vector<float> arc; // arc[i]: length from points[0] to points[i]
    float length = 0.0f;
    SegmentBVH bvh;
    float steps = 0.0f;     // set by closedGuideSize
    bool loop = true;       // false: from the seam to the seam, both ends lattice corners
    size_t lines = 0;       // finest-level vertices with a line constraint on it (none: not on this piece)

    vec3 at(float s) const { // the point at arc length s (wrapping round a loop)
        if (!loop) {
            s = std::clamp(s, 0.0f, length);
            if (s >= length) return points.back();
        }
        s = std::fmod(s, length);
        if (s < 0.0f) s += length;
        const size_t i = size_t(std::upper_bound(arc.begin(), arc.end(), s) - arc.begin()) - 1;
        const size_t j = (i + 1) % points.size();
        const float len = (j ? arc[j] : length) - arc[i];
        return glm::mix(points[i], points[j], len > 0.0f ? (s - arc[i]) / len : 0.0f);
    }
};

// QRemesh Guides on the finest level: vertices on a curve (within about half an edge of it) get a line
// constraint along it, so the lattice puts a line, and the result an edge loop, there; vertices within `radius`
// turn their orientation towards the curve's direction with a weight falling off from 1 on the curve. Vertices
// with a constraint already (open edges, borders, hard edges) keep theirs. The curves' segments are added to
// `guideSegs`, so relaxation keeps the loop's vertices on the curve. Line vertices on a closed guide remember
// which (Level::gid) and where along it (Level::garc), for pinClosedGuides; so do those on a curve with both
// ends on the symmetry plane (`axis`; the half of a loop across the middle, cut there by qremesh).
void applyGuides(Level& l, const std::vector<QRemeshGuide>& guides, float radius, int axis,
                 std::vector<std::pair<vec3, vec3>>& guideSegs, std::vector<ClosedGuide>& closed) {
    std::vector<vec3> a, b, ta, tb; // segments and the curve's tangent at each end
    std::vector<int32_t> segClosed; // per segment: its closed guide, or -1
    std::vector<float> segArc;      // arc length at the segment's start (closed guides)
    closed.clear();
    for (const QRemeshGuide& g : guides) {
        const size_t n = g.points.size();
        if (n < 2) continue;
        auto tangent = [&](size_t i) {
            const size_t prev = i > 0 ? i - 1 : g.closed ? n - 1 : 0, next = i + 1 < n ? i + 1 : g.closed ? 0 : n - 1;
            return safeNormalize(g.points[next] - g.points[prev], vec3(0.0f));
        };
        const bool loop = g.closed && n > 2;
        const bool seamToSeam = !g.closed && axis >= 0 && g.points.front()[axis] == 0.0f && g.points.back()[axis] == 0.0f;
        const bool pinned = loop || seamToSeam;
        ClosedGuide c;
        c.loop = loop;
        const size_t segs = loop ? n : n - 1;
        for (size_t i = 0; i < segs; ++i) {
            const size_t j = (i + 1) % n;
            a.push_back(g.points[i]);
            b.push_back(g.points[j]);
            ta.push_back(tangent(i));
            tb.push_back(tangent(j));
            guideSegs.push_back({g.points[i], g.points[j]});
            segClosed.push_back(pinned ? int32_t(closed.size()) : -1);
            segArc.push_back(c.length);
            if (pinned) {
                c.points.push_back(g.points[i]);
                c.arc.push_back(c.length);
                c.length += glm::length(g.points[j] - g.points[i]);
            }
        }
        if (seamToSeam) c.points.push_back(g.points.back()), c.arc.push_back(c.length);
        if (pinned) {
            c.bvh.build(std::vector<vec3>(a.end() - long(segs), a.end()), std::vector<vec3>(b.end() - long(segs), b.end()));
            closed.push_back(std::move(c));
        }
    }
    if (a.empty()) return;
    SegmentBVH bvh;
    bvh.build(a, b);
    const size_t nv = l.size();
    // Where a curve from the seam to the seam meets it: a lattice corner (the seam vertex nearest each end), so
    // the loop crosses the middle there (relaxation pins a quad vertex to it, as at the source's corners).
    for (const ClosedGuide& c : closed) {
        if (c.loop) continue;
        for (const vec3& end : {c.points.front(), c.points.back()}) {
            uint32_t best = kNone;
            float bestD = radius * radius;
            for (uint32_t v = 0; v < nv; ++v) {
                const float d = glm::dot(l.V[v] - end, l.V[v] - end);
                if (l.boundary[v] && d < bestD) bestD = d, best = v;
            }
            if (best == kNone) continue;
            l.ctype[best] = kPoint;
            l.cpos[best] = end;
        }
    }
    l.gw.assign(nv, 0.0f);
    l.gdir.assign(nv, vec3(0.0f));
    if (!closed.empty()) {
        l.gid.assign(nv, -1);
        l.garc.assign(nv, 0.0f);
    }
    parallelFor(nv, [&](size_t b0, size_t e0) {
        for (size_t v = b0; v < e0; ++v) {
            SurfaceHit hit;
            if (!bvh.closestPoint(l.V[v], radius, hit)) continue;
            vec3 t = ta[hit.prim] * hit.bary.x + tb[hit.prim] * hit.bary.y;
            if (glm::dot(t, ta[hit.prim]) < 0.0f) t = ta[hit.prim];
            const vec3 n = l.N[v];
            t -= n * glm::dot(n, t);
            if (glm::length(t) < 1e-6f) continue;
            t = glm::normalize(t);
            const float f = 1.0f - hit.distance / radius;
            l.gw[v] = f * f;
            l.gdir[v] = t;
            if (l.ctype[v] != kFree) continue;
            float edge = 0.0f;
            for (uint32_t k = l.off[v]; k < l.off[v + 1]; ++k) edge += glm::length(l.V[l.adj[k]] - l.V[v]);
            const uint32_t deg = l.off[v + 1] - l.off[v];
            if (deg && hit.distance < 0.6f * edge / float(deg)) {
                l.ctype[v] = kLine;
                l.cdir[v] = t;
                l.cpos[v] = hit.position;
                if (const int32_t c = segClosed[hit.prim]; c >= 0) {
                    l.gid[v] = c;
                    l.garc[v] = segArc[hit.prim] + hit.bary.y * glm::length(b[hit.prim] - a[hit.prim]);
                }
            }
        }
    });
    for (int32_t c : l.gid)
        if (c >= 0) ++closed[size_t(c)].lines;
}

// A loop's lattice closes up only when its length is a whole number of lattice steps; otherwise the position
// field slips somewhere along it (a kink in the edge loop). Picks that number for each closed guide from the
// mean size on it, and scales the size field to fit, blended out over `reach`.
void closedGuideSize(const Level& L, std::vector<float>& S, std::vector<ClosedGuide>& closed, float reach) {
    for (ClosedGuide& c : closed) {
        double sum = 0.0;
        size_t cnt = 0;
        std::vector<std::pair<uint32_t, float>> near; // (vertex, distance)
        for (uint32_t v = 0; v < L.size(); ++v) {
            SurfaceHit hit;
            if (!c.bvh.closestPoint(L.V[v], reach, hit)) continue;
            near.push_back({v, hit.distance});
            if (hit.distance < 0.25f * reach) sum += S[v], ++cnt;
        }
        c.steps = 0.0f;
        if (!cnt) continue;
        const float s = float(sum / double(cnt));
        // An even number round a loop: a disk of quads has an even number of sides, so with an odd number the
        // lattice inside needs an odd face, and it ends up on the loop (a gap in it). Along a curve from the
        // seam to the seam any whole number does: its mirror image doubles it.
        c.steps = c.loop ? std::max(4.0f, 2.0f * std::round(0.5f * c.length / s)) : std::max(1.0f, std::round(c.length / s));
        const float f = c.length / (c.steps * s);
        for (auto [v, d] : near) {
            const float w = d < 0.25f * reach ? 1.0f : 1.0f - (d - 0.25f * reach) / (0.75f * reach);
            S[v] *= 1.0f + (f - 1.0f) * w;
        }
    }
}

// Puts lattice corners at `steps` even places round each closed guide: every line vertex on it is pinned to the
// one nearest along the curve, so the corners round the loop agree with each other and it closes up cleanly.
void pinClosedGuides(Level& l, const std::vector<ClosedGuide>& closed) {
    if (l.gid.empty()) return;
    l.cfix.assign(l.size(), 0);
    for (uint32_t v = 0; v < l.size(); ++v) {
        if (l.gid[v] < 0 || l.ctype[v] != kLine) continue;
        const ClosedGuide& c = closed[size_t(l.gid[v])];
        if (c.steps <= 0.0f) continue;
        const float step = c.length / c.steps;
        l.cpos[v] = c.at(std::round(l.garc[v] / step) * step);
        l.cfix[v] = 1;
    }
}

// The next coarser level: greedy matching of neighbours with similar normals (Instant Meshes' ordering).
bool downsample(Level& f, Level& c) {
    const size_t n = f.size();
    struct Entry {
        uint32_t i, j;
        float order;
    };
    std::vector<Entry> entries;
    entries.reserve(f.adj.size() / 2);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t k = f.off[i]; k < f.off[i + 1]; ++k) {
            const uint32_t j = f.adj[k];
            if (j <= i) continue;
            const float ratio = f.A[i] > f.A[j] ? f.A[i] / std::max(f.A[j], 1e-30f) : f.A[j] / std::max(f.A[i], 1e-30f);
            entries.push_back({i, j, glm::dot(f.N[i], f.N[j]) * ratio});
        }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.order > b.order; });

    std::vector<uint32_t>& up = f.up;
    up.assign(n, kNone);
    const bool colors = !f.color.empty();
    const bool guided = !f.gw.empty();
    auto addVertex = [&](uint32_t i) {
        up[i] = uint32_t(c.V.size());
        c.V.push_back(f.V[i]);
        c.N.push_back(f.N[i]);
        c.A.push_back(f.A[i]);
        c.ctype.push_back(f.ctype[i]);
        c.cdir.push_back(f.cdir[i]);
        c.cpos.push_back(f.cpos[i]);
        c.boundary.push_back(f.boundary[i]);
        if (colors) c.color.push_back(f.color[i]);
        if (guided) c.gw.push_back(f.gw[i]), c.gdir.push_back(f.gdir[i]);
        if (!f.gid.empty()) c.gid.push_back(f.gid[i]), c.garc.push_back(f.garc[i]);
    };
    for (const Entry& e : entries) {
        if (up[e.i] != kNone || up[e.j] != kNone) continue;
        addVertex(e.i);
        up[e.j] = up[e.i];
        const uint32_t v = up[e.i];
        const float ai = f.A[e.i], aj = f.A[e.j], a = std::max(ai + aj, 1e-30f);
        c.V[v] = (f.V[e.i] * ai + f.V[e.j] * aj) / a;
        c.N[v] = safeNormalize(f.N[e.i] * ai + f.N[e.j] * aj, f.N[e.i]);
        c.A[v] = ai + aj;
        if (colors) c.color[v] = (f.color[e.i] * ai + f.color[e.j] * aj) / a;
        // Constraints: a point beats a line; otherwise keep the first child's.
        if (f.ctype[e.j] > f.ctype[e.i]) {
            c.ctype[v] = f.ctype[e.j];
            c.cdir[v] = f.cdir[e.j];
            c.cpos[v] = f.cpos[e.j];
            if (!f.gid.empty()) c.gid[v] = f.gid[e.j], c.garc[v] = f.garc[e.j];
        }
        c.boundary[v] |= f.boundary[e.j];
        if (guided && f.gw[e.j] > c.gw[v]) c.gw[v] = f.gw[e.j], c.gdir[v] = f.gdir[e.j]; // the stronger pull
    }
    for (uint32_t i = 0; i < n; ++i)
        if (up[i] == kNone) addVertex(i);
    const size_t nc = c.V.size();
    if (nc >= n) return false;

    // Coarse adjacency: the children's neighbours' parents, weights summed (per coarse vertex, in parallel).
    std::vector<uint32_t> chOff(nc + 1, 0), ch(n);
    for (uint32_t i = 0; i < n; ++i) ++chOff[up[i] + 1];
    for (size_t v = 0; v < nc; ++v) chOff[v + 1] += chOff[v];
    {
        std::vector<uint32_t> fill(chOff.begin(), chOff.end() - 1);
        for (uint32_t i = 0; i < n; ++i) ch[fill[up[i]]++] = i;
    }
    auto gather = [&](uint32_t v, std::vector<std::pair<uint32_t, float>>& l) {
        l.clear();
        for (uint32_t k = chOff[v]; k < chOff[v + 1]; ++k) {
            const uint32_t i = ch[k];
            for (uint32_t a = f.off[i]; a < f.off[i + 1]; ++a)
                if (const uint32_t b = up[f.adj[a]]; b != v) l.push_back({b, f.w[a]});
        }
        std::sort(l.begin(), l.end(), [](auto& x, auto& y) { return x.first < y.first; });
        size_t w = 0;
        for (size_t k = 0; k < l.size(); ++k) {
            if (w > 0 && l[w - 1].first == l[k].first) l[w - 1].second += l[k].second;
            else l[w++] = l[k];
        }
        l.resize(w);
    };
    c.off.assign(nc + 1, 0);
    parallelFor(nc, [&](size_t b0, size_t e0) {
        std::vector<std::pair<uint32_t, float>> l;
        for (size_t v = b0; v < e0; ++v) {
            gather(uint32_t(v), l);
            c.off[v + 1] = uint32_t(l.size());
        }
    });
    for (size_t v = 0; v < nc; ++v) c.off[v + 1] += c.off[v];
    c.adj.resize(c.off[nc]);
    c.w.resize(c.off[nc]);
    parallelFor(nc, [&](size_t b0, size_t e0) {
        std::vector<std::pair<uint32_t, float>> l;
        for (size_t v = b0; v < e0; ++v) {
            gather(uint32_t(v), l);
            for (size_t k = 0; k < l.size(); ++k) {
                c.adj[c.off[v] + k] = l[k].first;
                c.w[c.off[v] + k] = l[k].second;
            }
        }
    });
    computeMeanEdge(c);
    return true;
}

// --- Field solvers --------------------------------------------------------------------------------------

void applyOrientationConstraint(const Level& l, uint32_t i, vec3& q) {
    if (l.ctype[i] != kLine) return;
    q = alignTo(q, l.N[i], l.cdir[i], l.N[i]);
}

void smoothOrientations(Level& l, int iterations) {
    const auto& phases = l.phases;
    for (int it = 0; it < iterations; ++it)
        for (const auto& phase : phases)
            parallelFor(phase.size(), [&](size_t b, size_t e) {
                for (size_t k = b; k < e; ++k) {
                    const uint32_t i = phase[k];
                    const vec3 n = l.N[i];
                    vec3 sum = l.Q[i];
                    float wsum = 0.0f;
                    for (uint32_t a = l.off[i]; a < l.off[i + 1]; ++a) {
                        const uint32_t j = l.adj[a];
                        const float w = l.w[a];
                        const auto [x, y] = compatOrientation(sum, n, l.Q[j], l.N[j]);
                        sum = x * wsum + y * w;
                        sum -= n * glm::dot(n, sum);
                        wsum += w;
                        const float len = glm::length(sum);
                        if (len > 1e-20f) sum /= len;
                    }
                    if (glm::length(sum) < 0.5f) sum = l.Q[i];
                    if (!l.gw.empty() && l.gw[i] > 0.0f) { // turn towards the guide's direction
                        const vec3 g = l.gdir[i] - n * glm::dot(n, l.gdir[i]);
                        if (glm::length(g) > 1e-6f) {
                            const vec3 a = alignTo(sum, n, glm::normalize(g), n);
                            const vec3 m = glm::mix(sum, a, l.gw[i]);
                            sum = safeNormalize(m - n * glm::dot(n, m), sum);
                        }
                    }
                    applyOrientationConstraint(l, i, sum);
                    l.Q[i] = sum;
                }
            }, phases.size() > 1 ? 1000 : ~size_t(0)); // one phase = plain sequential sweep
}

void applyPositionConstraint(const Level& l, uint32_t i, const vec3& q, vec3& o) {
    if (l.ctype[i] == kPoint) {
        o = l.cpos[i] - l.N[i] * glm::dot(l.N[i], l.cpos[i] - l.V[i]);
    } else if (l.ctype[i] == kLine && !l.cfix.empty() && l.cfix[i]) { // a pinned corner on a closed guide
        o = l.cpos[i] - l.N[i] * glm::dot(l.N[i], l.cpos[i] - l.V[i]);
    } else if (l.ctype[i] == kLine) {
        const vec3 perp = glm::cross(l.N[i], l.cdir[i]); // across the line, in the tangent plane
        o -= perp * glm::dot(o - l.cpos[i], perp);
    }
}

void smoothPositions(Level& l, int iterations) {
    const auto& phases = l.phases;
    for (int it = 0; it < iterations; ++it)
        for (const auto& phase : phases)
            parallelFor(phase.size(), [&](size_t b, size_t e) {
                for (size_t k = b; k < e; ++k) {
                    const uint32_t i = phase[k];
                    const vec3 n = l.N[i], v = l.V[i], q = l.Q[i];
                    const float s = l.S[i];
                    vec3 sum = l.O[i];
                    float wsum = 0.0f;
                    for (uint32_t a = l.off[i]; a < l.off[i + 1]; ++a) {
                        const uint32_t j = l.adj[a];
                        const float w = l.w[a];
                        const vec3 qj = alignTo(q, n, l.Q[j], l.N[j]);
                        ivec2 i0, i1;
                        compatIndex(v, n, q, sum, s, l.V[j], l.N[j], qj, l.O[j], l.S[j], i0, i1);
                        const vec3 p0 = latticePoint(sum, q, glm::cross(n, q), s, i0);
                        const vec3 p1 = latticePoint(l.O[j], qj, glm::cross(l.N[j], qj), l.S[j], i1);
                        sum = (p0 * wsum + p1 * w) / (wsum + w);
                        wsum += w;
                        sum -= n * glm::dot(n, sum - v);
                    }
                    applyPositionConstraint(l, i, q, sum);
                    l.O[i] = positionRound(sum, q, n, v, s);
                }
            }, phases.size() > 1 ? 1000 : ~size_t(0)); // one phase = plain sequential sweep
}

// --- Extraction -----------------------------------------------------------------------------------------

struct UnionFind {
    std::vector<uint32_t> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32_t find(uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    }
};

struct PolyMesh {
    std::vector<vec3> P, N;
    std::vector<uint8_t> feature; // per vertex: on a hard edge / border (1) or a corner (2)
    std::vector<vec3> featureDir; // the line's direction (feature 1)
    std::vector<std::vector<uint32_t>> faces;
    size_t cornerCount() const {
        size_t c = 0;
        for (const auto& f : faces) c += f.size();
        return c;
    }
};

constexpr float kApartCos = -0.1f; // clusters whose normals are more than ~95 degrees apart don't merge

// Turns the fields on level `l` into a polygon mesh (faces wound counter-clockwise about the normals).
// Faces that are mostly mesh boundary (the outside of an open surface, the symmetry cut) are left out.
PolyMesh extractPolygons(const Level& l) {
    const size_t n = l.size();
    struct Link {
        uint32_t i, j;
        float cost;
    };
    std::vector<Link> collapse, links;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t a = l.off[i]; a < l.off[i + 1]; ++a) {
            const uint32_t j = l.adj[a];
            if (j <= i) continue;
            const vec3 qj = alignTo(l.Q[i], l.N[i], l.Q[j], l.N[j]);
            ivec2 i0, i1;
            const float err = compatIndex(l.V[i], l.N[i], l.Q[i], l.O[i], l.S[i], l.V[j], l.N[j], qj, l.O[j], l.S[j], i0, i1);
            const float s = 0.5f * (l.S[i] + l.S[j]);
            if (err > 0.25f * s * s) continue; // the two lattices disagree here (near a singularity)
            const ivec2 d = i0 - i1;
            const int l1 = std::abs(d.x) + std::abs(d.y);
            if (l1 == 0) collapse.push_back({i, j, glm::length(l.O[i] - l.O[j])});
            else if (l1 == 1) links.push_back({i, j, 0.0f});
        }
    std::sort(collapse.begin(), collapse.end(), [](const Link& a, const Link& b) { return a.cost < b.cost; });

    // Merge vertices that sit on the same lattice corner, unless that drags a cluster too far.
    // Clusters facing apart (the two sides of a part thinner than a quad, the faces either side of a sharp
    // rim) stay apart: the vertices become neighbours instead, so the thin part keeps its thickness.
    UnionFind uf(n);
    std::vector<vec3> sum(l.O), nsum(n);
    for (uint32_t i = 0; i < n; ++i) nsum[i] = l.N[i] * std::max(l.A[i], 1e-30f);
    std::vector<float> cnt(n, 1.0f);
    for (const Link& c : collapse) {
        uint32_t a = uf.find(c.i), b = uf.find(c.j);
        if (a == b) continue;
        const float s = 0.5f * (l.S[c.i] + l.S[c.j]);
        if (glm::length(sum[a] / cnt[a] - sum[b] / cnt[b]) > 0.45f * s) continue;
        if (glm::dot(safeNormalize(nsum[a]), safeNormalize(nsum[b])) < kApartCos) {
            links.push_back({c.i, c.j, 0.0f});
            continue;
        }
        if (cnt[a] < cnt[b]) std::swap(a, b);
        uf.parent[b] = a;
        sum[a] += sum[b];
        nsum[a] += nsum[b];
        cnt[a] += cnt[b];
    }
    std::vector<uint32_t> cluster(n, kNone);
    PolyMesh pm;
    std::vector<vec3> qOf;
    std::vector<float> area, scale;
    std::vector<uint8_t> boundary, feature; // feature: 1 = on a hard edge / border line, 2 = a corner
    std::vector<vec3> featureDir;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t r = uf.find(i);
        if (cluster[r] == kNone) {
            cluster[r] = uint32_t(pm.P.size());
            pm.P.push_back(sum[r] / cnt[r]);
            pm.N.push_back(vec3(0.0f));
            qOf.push_back(l.Q[i]);
            area.push_back(0.0f);
            scale.push_back(0.0f);
            boundary.push_back(0);
            feature.push_back(0);
            featureDir.push_back(vec3(0.0f));
        }
        const uint32_t c = cluster[r];
        cluster[i] = c;
        if (l.ctype[i] == kPoint) feature[c] = 2;
        else if (l.ctype[i] == kLine && feature[c] == 0) feature[c] = 1, featureDir[c] = l.cdir[i];
        pm.N[c] += l.N[i] * l.A[i];
        area[c] += l.A[i];
        scale[c] = std::max(scale[c], l.S[i]);
        boundary[c] |= l.boundary[i];
    }
    const size_t nc = pm.P.size();
    for (uint32_t c = 0; c < nc; ++c) pm.N[c] = safeNormalize(pm.N[c]);

    // Graph edges between clusters one lattice step apart; tiny clusters (stray vertices at singularities) go.
    std::vector<std::vector<uint32_t>> adj(nc);
    std::vector<uint8_t> alive(nc, 1);
    for (uint32_t c = 0; c < nc; ++c)
        if (area[c] < 0.04f * scale[c] * scale[c]) alive[c] = 0;
    for (const Link& e : links) {
        const uint32_t a = cluster[e.i], b = cluster[e.j];
        if (a == b || !alive[a] || !alive[b]) continue;
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
    auto len2 = [&](uint32_t a, uint32_t b) { return glm::dot(pm.P[a] - pm.P[b], pm.P[a] - pm.P[b]); };

    // An edge running along a hard edge or border (both ends on it, in its direction): the feature's loop.
    auto alongFeature = [&](uint32_t a, uint32_t b) {
        if (!feature[a] || !feature[b]) return false;
        const vec3 d = safeNormalize(pm.P[b] - pm.P[a]);
        return (feature[a] == 1 && std::abs(glm::dot(d, featureDir[a])) > 0.9f) ||
               (feature[b] == 1 && std::abs(glm::dot(d, featureDir[b])) > 0.9f);
    };
    // A quad lattice has no 3-cycles: drop the longest edge of each (a diagonal from a lattice mismatch), but
    // keep the edges along features (across a part thinner than a quad the diagonal can be the shorter).
    for (int round = 0; round < 8; ++round) {
        std::vector<std::pair<uint32_t, uint32_t>> drop;
        for (uint32_t u = 0; u < nc; ++u)
            for (uint32_t v : adj[u]) {
                if (v <= u) continue;
                for (uint32_t w : adj[u]) {
                    if (w <= v || !hasEdge(v, w)) continue;
                    const std::pair<uint32_t, uint32_t> sides[3] = {{u, v}, {v, w}, {u, w}};
                    std::pair<uint32_t, uint32_t> e{u, v};
                    float best = -1.0f;
                    for (int pass = 0; pass < 2 && best < 0.0f; ++pass)
                        for (auto [a, b] : sides)
                            if ((pass == 1 || !alongFeature(a, b)) && len2(a, b) > best) best = len2(a, b), e = {a, b};
                    drop.push_back(e);
                }
            }
        if (drop.empty()) break;
        for (auto [a, b] : drop)
            if (hasEdge(a, b)) removeEdge(a, b);
    }

    std::vector<std::vector<uint32_t>> faces;
    for (int round = 0; round < 16; ++round) {
        // Dangling vertices (fewer than two edges) can't bound a face.
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
        // Rotation system: neighbours counter-clockwise about the vertex normal.
        std::vector<std::vector<uint32_t>> ring(nc);
        std::vector<uint32_t> heOff(nc + 1, 0);
        for (uint32_t u = 0; u < nc; ++u) {
            const vec3 nn = pm.N[u];
            vec3 q = qOf[u] - nn * glm::dot(nn, qOf[u]);
            q = glm::length(q) > 1e-6f ? glm::normalize(q) : anyTangent(nn);
            const vec3 t = glm::cross(nn, q);
            std::vector<std::pair<float, uint32_t>> ang;
            for (uint32_t v : adj[u]) {
                const vec3 d = pm.P[v] - pm.P[u];
                ang.push_back({std::atan2(glm::dot(d, t), glm::dot(d, q)), v});
            }
            std::sort(ang.begin(), ang.end());
            for (auto& [a, v] : ang) ring[u].push_back(v);
            heOff[u + 1] = heOff[u] + uint32_t(ring[u].size());
        }
        auto slot = [&](uint32_t u, uint32_t v) { // index of v in u's ring
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
                    k = (back + deg - 1) % deg; // next face edge: the neighbour before u, clockwise turn
                    u = v;
                }
                // An edge walked both ways in one face is a bridge, not a face side: drop it and retrace.
                std::unordered_map<uint64_t, int> seen;
                for (size_t a = 0; a < face.size(); ++a) {
                    const uint64_t key = edgeKey(face[a], face[(a + 1) % face.size()]);
                    if (++seen[key] == 2) bad.push_back({face[a], face[(a + 1) % face.size()]});
                }
                faces.push_back(std::move(face));
            }
        if (bad.empty() || round == 15) break;
        for (auto [a, b] : bad)
            if (std::find(adj[a].begin(), adj[a].end(), b) != adj[a].end()) removeEdge(a, b);
    }

    // Keep the faces; leave the outside of open surfaces open.
    std::vector<uint32_t> remap(nc, kNone);
    PolyMesh out;
    for (auto& f : faces) {
        if (f.size() < 3) continue;
        if (f.size() >= 5) {
            size_t onBoundary = 0;
            for (uint32_t v : f) onBoundary += boundary[v];
            if (onBoundary * 10 >= f.size() * 6) continue;
        }
        // Drop a face whose edge appears twice (left after the retrace limit).
        bool twice = false;
        {
            std::unordered_map<uint64_t, int> seen;
            for (size_t a = 0; a < f.size(); ++a)
                if (++seen[edgeKey(f[a], f[(a + 1) % f.size()])] == 2) twice = true;
        }
        if (twice) continue;
        for (uint32_t& v : f) {
            if (remap[v] == kNone) {
                remap[v] = uint32_t(out.P.size());
                out.P.push_back(pm.P[v]);
                out.N.push_back(pm.N[v]);
                out.feature.push_back(feature[v]);
                out.featureDir.push_back(featureDir[v]);
            }
            v = remap[v];
        }
        out.faces.push_back(std::move(f));
    }
    return out;
}

// Faces with six or more sides are cut along inside diagonals into quads (and at most one pentagon), so the
// quad split doesn't leave a star of six or more quads around one centre.
void splitLargeFaces(PolyMesh& pm) {
    std::vector<std::vector<uint32_t>> out, todo = std::move(pm.faces);
    std::unordered_map<uint64_t, int> edges; // existing edges: a diagonal must not duplicate one
    for (const auto& f : todo)
        for (size_t k = 0; k < f.size(); ++k) edges[edgeKey(f[k], f[(k + 1) % f.size()])] = 1;
    while (!todo.empty()) {
        std::vector<uint32_t> f = std::move(todo.back());
        todo.pop_back();
        const size_t n = f.size();
        if (n <= 5) {
            out.push_back(std::move(f));
            continue;
        }
        // The face flattened onto its own plane.
        vec3 nrm(0.0f), c(0.0f);
        for (size_t k = 0; k < n; ++k) {
            nrm += glm::cross(pm.P[f[k]], pm.P[f[(k + 1) % n]]);
            c += pm.P[f[k]];
        }
        c /= float(n);
        nrm = safeNormalize(nrm, pm.N[f[0]]);
        const vec3 ax = anyTangent(nrm), ay = glm::cross(nrm, ax);
        std::vector<glm::vec2> q(n);
        float perimeter = 0.0f;
        for (size_t k = 0; k < n; ++k) {
            q[k] = {glm::dot(pm.P[f[k]] - c, ax), glm::dot(pm.P[f[k]] - c, ay)};
            perimeter += glm::length(pm.P[f[k]] - pm.P[f[(k + 1) % n]]);
        }
        auto cross2 = [](glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; };
        auto segmentsCross = [&](glm::vec2 a, glm::vec2 b, glm::vec2 p, glm::vec2 r) {
            const float d1 = cross2(b - a, p - a), d2 = cross2(b - a, r - a);
            const float d3 = cross2(r - p, a - p), d4 = cross2(r - p, b - p);
            return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
        };
        auto inside = [&](glm::vec2 p) {
            bool in = false;
            for (size_t k = 0, l = n - 1; k < n; l = k++)
                if ((q[k].y > p.y) != (q[l].y > p.y) && p.x < (q[l].x - q[k].x) * (p.y - q[k].y) / (q[l].y - q[k].y) + q[k].x)
                    in = !in;
            return in;
        };
        float best = std::numeric_limits<float>::max();
        size_t bi = 0, bj = 0;
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 3; j < n; ++j) {
                const size_t a = j - i + 1, b = n - (j - i) + 1;
                if (b < 4 || f[i] == f[j] || edges.count(edgeKey(f[i], f[j]))) continue;
                bool ok = inside(0.5f * (q[i] + q[j]));
                for (size_t k = 0; k < n && ok; ++k) {
                    const size_t l = (k + 1) % n;
                    if (k == i || l == i || k == j || l == j) continue;
                    if (segmentsCross(q[i], q[j], q[k], q[l])) ok = false;
                }
                if (!ok) continue;
                // Short diagonals and even (quad-friendly) halves first.
                float score = glm::length(pm.P[f[i]] - pm.P[f[j]]) / perimeter;
                if (a % 2) score += 0.25f;
                if (b % 2) score += 0.25f;
                score += 0.02f * float(std::max(a, b) - std::min(a, b));
                if (score < best) best = score, bi = i, bj = j;
            }
        if (best == std::numeric_limits<float>::max()) {
            out.push_back(std::move(f));
            continue;
        }
        edges[edgeKey(f[bi], f[bj])] = 1;
        std::vector<uint32_t> fa(f.begin() + long(bi), f.begin() + long(bj) + 1), fb(f.begin() + long(bj), f.end());
        fb.insert(fb.end(), f.begin(), f.begin() + long(bi) + 1);
        todo.push_back(std::move(fa));
        todo.push_back(std::move(fb));
    }
    pm.faces = std::move(out);
}

// --- Quads, relaxation, attributes ----------------------------------------------------------------------

using qr::QuadMesh;

// Every n-sided face becomes n quads (corner, edge midpoint, centre, edge midpoint).
QuadMesh splitIntoQuads(const PolyMesh& pm) {
    QuadMesh q;
    q.P = pm.P;
    q.feature = pm.feature;
    std::unordered_map<uint64_t, uint32_t> mids;
    auto mid = [&](uint32_t a, uint32_t b) {
        auto [it, inserted] = mids.try_emplace(edgeKey(a, b), uint32_t(q.P.size()));
        if (inserted) {
            q.P.push_back(0.5f * (pm.P[a] + pm.P[b]));
            // The middle of an edge running along a feature line is on it too.
            const vec3 d = safeNormalize(pm.P[b] - pm.P[a]);
            q.feature.push_back(pm.feature[a] && pm.feature[b] &&
                                ((pm.feature[a] == 1 && std::abs(glm::dot(d, pm.featureDir[a])) > 0.9f) ||
                                 (pm.feature[b] == 1 && std::abs(glm::dot(d, pm.featureDir[b])) > 0.9f)));
        }
        return it->second;
    };
    for (const auto& f : pm.faces) {
        const size_t n = f.size();
        vec3 c(0.0f);
        for (uint32_t v : f) c += pm.P[v];
        const uint32_t center = uint32_t(q.P.size());
        q.P.push_back(c / float(n));
        q.feature.push_back(0);
        for (size_t k = 0; k < n; ++k) {
            const uint32_t prev = f[(k + n - 1) % n], cur = f[k], next = f[(k + 1) % n];
            q.quads.push_back({cur, mid(cur, next), center, mid(prev, cur)});
        }
    }
    return q;
}

// A vertex inside the surface with only two quads (a "doublet", left where a two-sided face met a corner)
// adds nothing: the two quads become one. Unused vertices are dropped afterwards.
void removeDoublets(QuadMesh& q) {
    for (int pass = 0; pass < 6; ++pass) {
        const size_t nv = q.P.size();
        std::vector<std::vector<uint32_t>> vq(nv);
        for (uint32_t f = 0; f < q.quads.size(); ++f)
            for (int k = 0; k < 4; ++k) vq[q.quads[f][k]].push_back(f);
        std::vector<uint8_t> dead(q.quads.size(), 0), touched(nv, 0);
        auto rotated = [&](uint32_t f, uint32_t v) {
            glm::uvec4 r = q.quads[f];
            for (int k = 0; k < 4 && r.x != v; ++k) r = {r.y, r.z, r.w, r.x};
            return r;
        };
        bool any = false;
        for (uint32_t v = 0; v < nv; ++v) {
            if (vq[v].size() != 2 || touched[v]) continue;
            const uint32_t fa = vq[v][0], fb = vq[v][1];
            const glm::uvec4 A = rotated(fa, v), B = rotated(fb, v); // (v, a, x, c) and (v, c, y, a)
            if (A.x != v || B.x != v || B.y != A.w || B.w != A.y || A.z == B.z) continue;
            bool busy = false;
            for (int k = 0; k < 4; ++k) busy |= touched[A[k]] || touched[B[k]];
            if (busy) continue;
            q.quads[fa] = {A.y, A.z, A.w, B.z};
            dead[fb] = 1;
            for (int k = 0; k < 4; ++k) touched[A[k]] = touched[B[k]] = 1;
            any = true;
        }
        if (!any) break;
        size_t w = 0;
        for (size_t f = 0; f < q.quads.size(); ++f)
            if (!dead[f]) q.quads[w++] = q.quads[f];
        q.quads.resize(w);
    }
    std::vector<uint32_t> remap(q.P.size(), kNone);
    std::vector<vec3> P;
    std::vector<uint8_t> feature;
    for (auto& f : q.quads)
        for (int k = 0; k < 4; ++k) {
            if (remap[f[k]] == kNone) {
                remap[f[k]] = uint32_t(P.size());
                P.push_back(q.P[f[k]]);
                feature.push_back(q.feature[f[k]]);
            }
            f[k] = remap[f[k]];
        }
    q.P.swap(P);
    q.feature.swap(feature);
}

struct Snapper {
    const TriangleBVH* surface = nullptr;
    const SegmentBVH* boundary = nullptr; // open edges of the source (incl. the symmetry cut)
    const SegmentBVH* borders = nullptr;  // polygroup borders (Keep Groups) and hard edges
    const SegmentBVH* corners = nullptr;  // where several of those meet (as zero-length segments)
    float reach = 1.0f;
    const SegmentBVH* guides = nullptr;   // QRemesh Guides: only the loops' own vertices snap to them
};

// Tangential relaxation on the source surface; vertices on open edges stay on the source's open edges, those
// on polygroup borders on the borders.
void relax(QuadMesh& q, const Snapper& snap, int iterations, float edge, const QRemeshProgress& progress, float p0,
           float p1, bool& cancelled) {
    const size_t nv = q.P.size();
    // Neighbours and open edges from the quads.
    std::unordered_map<uint64_t, int> edgeUse;
    for (const auto& f : q.quads)
        for (int k = 0; k < 4; ++k) ++edgeUse[edgeKey(f[k], f[(k + 1) % 4])];
    std::vector<std::vector<uint32_t>> nb(nv);
    std::vector<uint8_t> open(nv, 0);
    for (auto [key, uses] : edgeUse) {
        const uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
        nb[a].push_back(b);
        nb[b].push_back(a);
        if (uses == 1) open[a] = open[b] = 2; // 2 = open-edge vertex, 1 = polygroup border vertex
    }
    std::vector<std::vector<uint32_t>> openNb(nv);
    for (auto [key, uses] : edgeUse) {
        if (uses != 1) continue;
        const uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
        openNb[a].push_back(b);
        openNb[b].push_back(a);
    }
    auto project = [&](uint32_t v, const vec3& p, vec3& normal) {
        SurfaceHit hit;
        if (open[v] == 2 && snap.boundary && snap.boundary->closestPoint(p, snap.reach, hit)) {
            normal = vec3(0.0f);
            return hit.position;
        }
        if (open[v] == 1) {
            SurfaceHit g;
            const bool onBorder = snap.borders && snap.borders->closestPoint(p, snap.reach, hit);
            const bool onGuide = snap.guides && snap.guides->closestPoint(p, onBorder ? hit.distance : snap.reach, g);
            if (onBorder || onGuide) {
                normal = vec3(0.0f);
                return onGuide ? g.position : hit.position;
            }
        }
        if (snap.surface->closestPoint(p, snap.reach, hit)) {
            normal = snap.surface->faceNormal(hit.prim);
            return hit.position;
        }
        normal = vec3(0.0f);
        return p;
    };
    std::vector<vec3> normal(nv, vec3(0.0f));
    parallelFor(nv, [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) q.P[v] = project(uint32_t(v), q.P[v], normal[v]);
    });
    // Polygroup-border vertices: within a fifth of an edge of a border.
    // Vertices on a guide's loop are the ones extracted on it (features): a vertex merely near a guide stays free.
    if (snap.borders || snap.guides)
        for (uint32_t v = 0; v < nv; ++v) {
            SurfaceHit hit;
            if (open[v] == 0 && (q.feature[v] || (snap.borders && snap.borders->closestPoint(q.P[v], 0.2f * edge, hit))))
                open[v] = 1;
        }
    // Corners of the source (a box's, a thin slab's) each pin their nearest quad vertex, so they stay sharp.
    std::vector<uint8_t> pinned(nv, 0);
    if (snap.corners) {
        // Closest pairs first, each corner and each vertex used once (a thin part's corners are close together).
        struct Pair {
            float d;
            uint32_t corner, v;
            vec3 at;
        };
        std::vector<Pair> pairs;
        for (uint32_t v = 0; v < nv; ++v) {
            SurfaceHit hit;
            if (snap.corners->closestPoint(q.P[v], 0.75f * edge, hit)) pairs.push_back({hit.distance, hit.prim, v, hit.position});
        }
        std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
        std::unordered_map<uint32_t, uint8_t> cornerUsed;
        for (const Pair& p : pairs) {
            if (pinned[p.v] || cornerUsed[p.corner]) continue;
            cornerUsed[p.corner] = 1;
            pinned[p.v] = 1;
            q.P[p.v] = p.at;
            if (open[p.v] == 0) open[p.v] = 1;
        }
    }
    std::vector<std::vector<uint32_t>> borderNb(nv);
    for (uint32_t v = 0; v < nv; ++v)
        if (open[v] == 1)
            for (uint32_t u : nb[v])
                if (open[u] == 1) borderNb[v].push_back(u);

    std::vector<vec3> next(nv);
    for (int it = 0; it < iterations; ++it) {
        parallelFor(nv, [&](size_t b, size_t e) {
            for (size_t v = b; v < e; ++v) {
                const auto& ring = open[v] == 2 ? openNb[v] : open[v] == 1 && borderNb[v].size() == 2 ? borderNb[v] : nb[v];
                if (pinned[v] || ring.empty() || (open[v] == 1 && borderNb[v].size() != 2)) {
                    next[v] = q.P[v];
                    continue;
                }
                vec3 avg(0.0f);
                for (uint32_t u : ring) avg += q.P[u];
                avg /= float(ring.size());
                vec3 d = avg - q.P[v];
                if (open[v] == 0) d -= normal[v] * glm::dot(normal[v], d);
                next[v] = q.P[v] + 0.6f * d;
            }
        });
        parallelFor(nv, [&](size_t b, size_t e) {
            for (size_t v = b; v < e; ++v)
                if (!pinned[v]) q.P[v] = project(uint32_t(v), next[v], normal[v]);
        });
        if (progress && !progress(p0 + (p1 - p0) * float(it + 1) / float(std::max(iterations, 1)))) {
            cancelled = true;
            return;
        }
    }
}

// Connected pieces of the working surface (triangles sharing vertices): the piece of each triangle.
std::vector<uint32_t> pieceOfTriangles(const WorkMesh& m, size_t& count) {
    UnionFind uf(m.P.size());
    for (const glm::uvec3& t : m.T) {
        const uint32_t a = uf.find(t.x);
        if (const uint32_t b = uf.find(t.y); b != a) uf.parent[b] = a;
        if (const uint32_t c = uf.find(t.z), r = uf.find(t.x); c != r) uf.parent[c] = r;
    }
    std::vector<uint32_t> id(m.P.size(), kNone), piece(m.T.size());
    count = 0;
    for (size_t t = 0; t < m.T.size(); ++t) {
        const uint32_t r = uf.find(m.T[t].x);
        if (id[r] == kNone) id[r] = uint32_t(count++);
        piece[t] = id[r];
    }
    return piece;
}

// Sorts the pieces into meshes: piece i goes to mesh slot[i] (kNone: left out). Vertices keep their order
// (the hierarchy and the field sweeps follow it, so a mesh remeshes as those pieces would alone).
std::vector<WorkMesh> gatherPieces(const WorkMesh& m, const std::vector<uint32_t>& piece, const std::vector<uint32_t>& slot,
                                   size_t slots) {
    std::vector<WorkMesh> out(slots);
    std::vector<uint32_t> slotOfVertex(m.P.size(), kNone), local(m.P.size(), kNone);
    for (size_t t = 0; t < m.T.size(); ++t)
        if (const uint32_t s = slot[piece[t]]; s != kNone)
            for (int k = 0; k < 3; ++k) slotOfVertex[m.T[t][k]] = s;
    for (uint32_t v = 0; v < m.P.size(); ++v) {
        const uint32_t s = slotOfVertex[v];
        if (s == kNone) continue;
        local[v] = uint32_t(out[s].P.size());
        out[s].P.push_back(m.P[v]);
        if (!m.color.empty()) out[s].color.push_back(m.color[v]);
    }
    for (size_t t = 0; t < m.T.size(); ++t) {
        const uint32_t s = slot[piece[t]];
        if (s == kNone) continue;
        const glm::uvec3 tri = m.T[t];
        out[s].T.push_back({local[tri.x], local[tri.y], local[tri.z]});
        out[s].group.push_back(m.group[t]);
    }
    return out;
}

// How far the surface is from its own mirror image about the plane (axis coordinate = c): the distances of
// mirrored sample points (triangle centres) to the surface, area-weighted mean and 98th percentile.
struct MirrorFit {
    float mean = 0.0f, high = 0.0f;
};
MirrorFit mirrorFit(const TriangleBVH& surface, const WorkMesh& m, int axis, float c, float reach) {
    const size_t step = std::max<size_t>(1, m.T.size() / 20000);
    const size_t n = (m.T.size() + step - 1) / step;
    std::vector<std::pair<float, float>> d(n); // (distance, area)
    parallelFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const glm::uvec3 t = m.T[i * step];
            vec3 p = (m.P[t.x] + m.P[t.y] + m.P[t.z]) / 3.0f;
            p[axis] = 2.0f * c - p[axis];
            SurfaceHit hit;
            d[i] = {surface.closestPoint(p, reach, hit) ? hit.distance : reach,
                    0.5f * glm::length(glm::cross(m.P[t.y] - m.P[t.x], m.P[t.z] - m.P[t.x]))};
        }
    });
    double sum = 0.0, area = 0.0;
    for (auto [dist, a] : d) sum += double(dist) * a, area += a;
    MirrorFit f;
    if (area <= 0.0) return f;
    f.mean = float(sum / area);
    std::sort(d.begin(), d.end());
    double acc = 0.0;
    for (auto [dist, a] : d) {
        acc += a;
        f.high = dist;
        if (acc >= 0.98 * area) break;
    }
    return f;
}

// One piece of the working surface, remeshed: its quads (empty when the lattice found none), the vertices on
// the symmetry cut, and the lattice size used.
struct PieceResult {
    QuadMesh mesh;
    std::vector<uint8_t> seam;
    float spacing = 0.0f;
    int attempts = 0;
    size_t guideDefects = 0; // see guideDefects
};

// What went wrong round the QRemesh Guides in a piece's result: vertices on a loop (or a curve from the seam to
// the seam) whose edge loop along it breaks, a loop not followed all the way, and fans (7 or more edges, a seam
// vertex's mirror image counted) within `reach` of a guide. Zero when all is well.
size_t guideDefects(const QuadMesh& q, const SegmentBVH& guides, const std::vector<ClosedGuide>& pinned, int axis,
                    float edge, float reach) {
    const size_t nv = q.P.size();
    std::vector<std::vector<uint32_t>> nb(nv);
    {
        std::unordered_map<uint64_t, uint8_t> seen;
        for (const auto& f : q.quads)
            for (int k = 0; k < 4; ++k) {
                const uint32_t a = f[k], b = f[(k + 1) % 4];
                if (seen.emplace(edgeKey(a, b), 1).second) nb[a].push_back(b), nb[b].push_back(a);
            }
    }
    auto onSeam = [&](uint32_t v) { return axis >= 0 && std::abs(q.P[v][axis]) < 0.01f * edge; };
    size_t defects = 0;
    for (uint32_t v = 0; v < nv; ++v) {
        const size_t k = nb[v].size();
        SurfaceHit hit;
        if ((onSeam(v) ? 2 * k - 2 : k) >= 7 && guides.closestPoint(q.P[v], reach, hit)) ++defects;
    }
    for (const ClosedGuide& c : pinned) {
        if (!c.lines || c.steps <= 0.0f) continue;
        std::vector<uint8_t> on(nv, 0);
        for (uint32_t v = 0; v < nv; ++v) {
            SurfaceHit hit;
            on[v] = q.feature[v] && c.bvh.closestPoint(q.P[v], 0.1f * edge, hit);
        }
        double along = 0.0;
        for (uint32_t v = 0; v < nv; ++v) {
            if (!on[v]) continue;
            int deg = 0;
            for (uint32_t u : nb[v])
                if (on[u]) ++deg, along += 0.5 * double(glm::length(q.P[u] - q.P[v]));
            if (deg != 2 && !(deg == 1 && !c.loop && onSeam(v))) ++defects;
        }
        if (along < 0.9 * double(c.length)) ++defects;
    }
    return defects;
}

// The field-aligned remesh of one connected piece to about `wantQuads` quads. False on cancel.
bool remeshPiece(WorkMesh work, int axis, double wantQuads, const QRemeshOptions& opt, float diag,
                 const std::vector<QRemeshGuide>& guides, const std::function<bool(float)>& report,
                 const std::function<void(const char*)>& mark, PieceResult& res) {
    static const bool timing = std::getenv("SF_QREMESH_TIMING") != nullptr;
    const QRemeshProgress progress = [&](float f) { return report(f); };
    double area = 0.0;
    for (const glm::uvec3& t : work.T) area += 0.5 * glm::length(glm::cross(work.P[t.y] - work.P[t.x], work.P[t.z] - work.P[t.x]));
    // Coarse lattice spacing: the extracted faces (about target / 4 of them, each split into ~4 quads).
    const double faces = std::max(4.0, wantQuads / 4.0);
    float spacing = float(std::sqrt(area / faces));
    const bool adaptive = opt.adaptive > 0.01f || !work.color.empty();
    const float wantEdge = spacing / 4.0f; // about four graph vertices per lattice edge

    // Surface the quads snap to (before any subdivision for density: same surface).
    mark("prepare");
    TriangleBVH surface;
    surface.build(work.P, work.T);
    mark("surface bvh");
    if (!report(0.05f)) return false;

    std::vector<std::pair<vec3, vec3>> boundarySegs, borderSegs, guideSegs;
    std::vector<uint8_t> boundaryIsCut;
    work = refineLongEdges(std::move(work), 1.5f * wantEdge, std::max<size_t>(4'000'000, work.T.size()));
    std::vector<Level> levels;
    std::vector<ClosedGuide> closedGuides;
    const float sharpCos = opt.detectEdges ? std::cos(glm::radians(50.0f)) : 2.0f;
    mark("refine");
    levels.push_back(buildFinest(work, opt.keepGroups, sharpCos, boundarySegs, boundaryIsCut, borderSegs, axis));
    if (!guides.empty()) {
        // The flow follows a curve over 0.5 (Curves Strength 0) to 6 quads (1) to each side; a quad is half the
        // lattice spacing (each extracted face splits into four).
        const float strength = std::clamp(opt.guideStrength, 0.0f, 1.0f);
        applyGuides(levels[0], guides, spacing * (0.25f + 2.75f * strength), axis, guideSegs, closedGuides);
        mark("guides");
    }
    mark("finest level");
    if (!report(0.10f)) return false;
    // Hierarchy down to a few vertices; fields are solved from the top to the finest level dense enough.
    while (levels.back().size() > 64 && levels.size() < 40) {
        Level c;
        if (!downsample(levels.back(), c)) break;
        if (c.size() > levels.back().size() * 95 / 100) {
            levels.push_back(std::move(c));
            break;
        }
        levels.push_back(std::move(c));
    }
    mark("hierarchy");
    size_t base = 0;
    while (base + 1 < levels.size() && levels[base + 1].meanEdge <= wantEdge) ++base;
    if (!report(0.20f)) return false;
    for (size_t i = base; i < levels.size(); ++i) computePhases(levels[i]);

    // Orientation field, coarse to fine.
    Rng rng(opt.seed);
    {
        Level& top = levels.back();
        top.Q.resize(top.size());
        for (uint32_t i = 0; i < top.size(); ++i) {
            const vec3 t0 = anyTangent(top.N[i]), t1 = glm::cross(top.N[i], t0);
            const float a = rng.next() * 6.2831853f;
            top.Q[i] = t0 * std::cos(a) + t1 * std::sin(a);
            applyOrientationConstraint(top, i, top.Q[i]);
        }
    }
    for (size_t li = levels.size(); li-- > base;) {
        Level& l = levels[li];
        if (li + 1 < levels.size()) {
            const Level& c = levels[li + 1];
            l.Q.resize(l.size());
            for (uint32_t i = 0; i < l.size(); ++i) {
                const vec3 q = c.Q[l.up[i]];
                const vec3 p = q - l.N[i] * glm::dot(l.N[i], q);
                l.Q[i] = glm::length(p) > 1e-6f ? glm::normalize(p) : anyTangent(l.N[i]);
                applyOrientationConstraint(l, i, l.Q[i]);
            }
        }
        smoothOrientations(l, li == base ? 12 : 10);
        if (!report(0.20f + 0.20f * float(levels.size() - li) / float(levels.size() - base))) return false;
    }

    // Scale field on the base level: curvature (Adaptive Size) and polypaint density, then normalised to the
    // wanted number of faces and passed up the hierarchy.
    Level& L = levels[base];
    std::vector<float> rel(L.size(), 1.0f); // relative edge length
    if (opt.adaptive > 0.01f) {
        // Curvature: normal change per length (-1 = unknown, filled in from the neighbours).
        std::vector<float> k(L.size(), -1.0f);
        for (uint32_t i = 0; i < L.size(); ++i) {
            if (L.ctype[i] != kFree) continue; // a feature's normal is the average of two sides
            float s = 0.0f;
            int c = 0;
            for (uint32_t a = L.off[i]; a < L.off[i + 1]; ++a) {
                const uint32_t j = L.adj[a];
                const float d = glm::length(L.V[i] - L.V[j]);
                // Normals jumping across a hard edge or a group border are features with their own loops, not
                // curvature to pack quads into.
                if (L.ctype[j] != kFree || glm::dot(L.N[i], L.N[j]) < sharpCos) continue;
                if (d > 0.0f) s += glm::length(L.N[i] - L.N[j]) / d, ++c;
            }
            if (c) k[i] = s / float(c);
        }
        for (int it = 0; it < 6; ++it) {
            std::vector<float> nk = k;
            for (uint32_t i = 0; i < L.size(); ++i) {
                float s = std::max(k[i], 0.0f);
                int c = k[i] >= 0.0f;
                for (uint32_t a = L.off[i]; a < L.off[i + 1]; ++a)
                    if (k[L.adj[a]] >= 0.0f) s += k[L.adj[a]], ++c;
                nk[i] = c ? s / float(c) : -1.0f;
            }
            k.swap(nk);
        }
        // Relative to the median, but never finer than a bend of ~11 degrees per quad: flat models stay even.
        std::vector<float> sorted;
        for (float x : k)
            if (x >= 0.0f) sorted.push_back(x);
        float ref = 0.2f / spacing;
        if (!sorted.empty()) {
            std::nth_element(sorted.begin(), sorted.begin() + long(sorted.size() / 2), sorted.end());
            ref = std::max(ref, sorted[sorted.size() / 2]);
        }
        for (uint32_t i = 0; i < L.size(); ++i)
            rel[i] = k[i] < 0.0f ? 1.0f : std::clamp(std::pow(std::max(k[i], 1e-9f) / ref, -0.6f * opt.adaptive), 0.45f, 2.2f);
    }
    if (!L.color.empty())
        for (uint32_t i = 0; i < L.size(); ++i) {
            const vec3 c = L.color[i];
            const float red = std::clamp(c.x - std::max(c.y, c.z), 0.0f, 1.0f);
            const float blue = std::clamp(c.z - std::max(c.x, c.y), 0.0f, 1.0f);
            rel[i] *= std::pow(2.0f, blue - red); // pure red: half the edge (4x the quads), pure blue: double
        }
    if (adaptive) // smooth the log of the size so it changes gradually
        for (int it = 0; it < 8; ++it) {
            std::vector<float> lr(L.size());
            for (uint32_t i = 0; i < L.size(); ++i) {
                float s = std::log(rel[i]);
                int c = 1;
                for (uint32_t a = L.off[i]; a < L.off[i + 1]; ++a) s += std::log(rel[L.adj[a]]), ++c;
                lr[i] = std::exp(s / float(c));
            }
            rel.swap(lr);
        }
    auto setScale = [&](float sp) {
        double want = 0.0;
        for (uint32_t i = 0; i < L.size(); ++i) want += double(L.A[i]) / double(rel[i] * rel[i]);
        const float c = float(std::sqrt(want / faces)) * (sp / spacing);
        L.S.resize(L.size());
        for (uint32_t i = 0; i < L.size(); ++i) L.S[i] = c * rel[i];
        if (!closedGuides.empty()) {
            closedGuideSize(L, L.S, closedGuides, 2.0f * c);
            for (size_t li = base; li < levels.size(); ++li) pinClosedGuides(levels[li], closedGuides);
        }
        for (size_t li = base + 1; li < levels.size(); ++li) {
            const Level& f = levels[li - 1];
            Level& cl = levels[li];
            std::vector<double> s(cl.size(), 0.0), a(cl.size(), 0.0);
            for (uint32_t i = 0; i < f.size(); ++i) s[f.up[i]] += double(f.S[i]) * f.A[i], a[f.up[i]] += f.A[i];
            cl.S.resize(cl.size());
            for (uint32_t i = 0; i < cl.size(); ++i) cl.S[i] = a[i] > 0.0 ? float(s[i] / a[i]) : c;
        }
    };

    // Position field and extraction; the lattice size is corrected until the quad count is near the target.
    PolyMesh poly;
    QuadMesh qm;
    float sp = spacing;
    for (int attempt = 0; attempt < 4; ++attempt) {
        setScale(sp);
        Level& top = levels.back();
        top.O = top.V;
        for (uint32_t i = 0; i < top.size(); ++i) applyPositionConstraint(top, i, top.Q[i], top.O[i]);
        for (size_t li = levels.size(); li-- > base;) {
            Level& l = levels[li];
            if (li + 1 < levels.size()) {
                const Level& c = levels[li + 1];
                l.O.resize(l.size());
                for (uint32_t i = 0; i < l.size(); ++i) {
                    const vec3 o = c.O[l.up[i]];
                    l.O[i] = o - l.N[i] * glm::dot(l.N[i], o - l.V[i]);
                }
            }
            smoothPositions(l, li == base ? 10 : 10);
        }
        if (!report(0.45f + 0.1f * float(attempt))) return false;
        poly = extractPolygons(L);
        if (timing) {
            std::map<size_t, size_t> sizes;
            for (const auto& f : poly.faces) ++sizes[f.size()];
            std::fprintf(stderr, "faces:");
            for (auto [k, c] : sizes) std::fprintf(stderr, " %zu:%zu", k, c);
            std::fprintf(stderr, "\n");
        }
        splitLargeFaces(poly);
        qm = splitIntoQuads(poly);
        removeDoublets(qm);
        res.attempts = attempt + 1;
        const double quads = double(qm.quads.size());
        if (quads <= 0.0) break;
        const double ratio = quads / wantQuads;
        if (std::abs(ratio - 1.0) < 0.05 || attempt == 3) break;
        sp *= float(std::sqrt(ratio));
    }
    if (qm.quads.empty()) return true; // no quads: the caller tries a finer lattice or keeps the piece as it is
    if (!report(0.85f)) return false;

    // Snap to the sculpt and relax.
    SegmentBVH boundaryBvh, borderBvh, cornerBvh, guideBvh;
    {
        std::vector<vec3> c;
        for (uint32_t i = 0; i < levels[0].size(); ++i)
            if (levels[0].ctype[i] == kPoint) c.push_back(levels[0].V[i]);
        cornerBvh.build(c, c);
        std::vector<vec3> a, b;
        for (auto& s : boundarySegs) a.push_back(s.first), b.push_back(s.second);
        boundaryBvh.build(std::move(a), std::move(b));
        a.clear(), b.clear();
        for (auto& s : borderSegs) a.push_back(s.first), b.push_back(s.second);
        borderBvh.build(std::move(a), std::move(b));
        a.clear(), b.clear();
        for (auto& s : guideSegs) a.push_back(s.first), b.push_back(s.second);
        guideBvh.build(std::move(a), std::move(b));
    }
    Snapper snap{&surface, boundaryBvh.empty() ? nullptr : &boundaryBvh, borderBvh.empty() ? nullptr : &borderBvh,
                 cornerBvh.empty() ? nullptr : &cornerBvh, spacing * 4.0f + 1e-3f * diag, guideBvh.empty() ? nullptr : &guideBvh};
    bool cancelled = false;
    relax(qm, snap, std::max(opt.smoothIterations, 0), sp * 0.5f, progress, 0.85f, 0.95f, cancelled);
    if (cancelled) return false;

    // Vertices on the symmetry cut: open edges lying on the plane.
    res.seam.assign(qm.P.size(), 0);
    if (axis >= 0) {
        std::unordered_map<uint64_t, int> edgeUse;
        for (const auto& f : qm.quads)
            for (int k = 0; k < 4; ++k) ++edgeUse[edgeKey(f[k], f[(k + 1) % 4])];
        for (auto [key, uses] : edgeUse)
            if (uses == 1) {
                const uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
                if (std::abs(qm.P[a][axis]) < 0.3f * sp && std::abs(qm.P[b][axis]) < 0.3f * sp) res.seam[a] = res.seam[b] = 1;
            }
    }
    if (!guideBvh.empty())
        res.guideDefects = guideDefects(qm, guideBvh, closedGuides, axis, 0.5f * sp, spacing * (0.25f + 2.75f * std::clamp(opt.guideStrength, 0.0f, 1.0f)) + sp);
    res.spacing = sp;
    res.mesh = std::move(qm);
    return true;

}

} // namespace

bool qremesh(const Mesh& source, const QRemeshOptions& opt, Mesh& out, std::string& error,
             const QRemeshProgress& progress, QRemeshStats* stats) {
    const auto start = std::chrono::steady_clock::now();
    static const bool timing = std::getenv("SF_QREMESH_TIMING") != nullptr; // stage times on stderr
    auto report = [&](float f) {
        if (timing)
            std::fprintf(stderr, "qremesh %.2f: %.3f s\n", f,
                         std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        if (progress && !progress(std::clamp(f, 0.0f, 1.0f))) {
            error = "Quad Remesh cancelled";
            return false;
        }
        return true;
    };
    auto mark = [&](const char* what) {
        if (timing)
            std::fprintf(stderr, "  %-28s %.3f s\n", what,
                         std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    };
    if (source.triangleCount() == 0) {
        error = "Quad Remesh: the Part is empty";
        return false;
    }
    const int target = std::clamp(opt.targetQuads, 16, 2'000'000);
    const int askedAxis = opt.symmetryAxis >= 0 && opt.symmetryAxis <= 2 ? opt.symmetryAxis : -1;
    QRemeshStats st;

    WorkMesh work;
    work.P = source.positions;
    work.T = source.triangles;
    work.group.resize(work.T.size());
    for (uint32_t t = 0; t < work.T.size(); ++t) work.group[t] = source.polygroupAt(t);
    if (opt.usePolypaint && source.hasColors()) work.color = source.colors;
    const AABB box = source.bounds();
    const float diag = glm::length(box.extent());
    if (!(diag > 0.0f)) {
        error = "Quad Remesh: the Part has no size";
        return false;
    }

    // The whole sculpt as a query surface: the symmetry test, attributes, and the snap of mirrored vertices.
    TriangleBVH full;
    auto buildFull = [&] {
        if (full.empty()) full.build(source.positions, source.triangles);
    };

    // Symmetry: find the mirror plane (the world's, or the model's own when it sits off the origin) and check
    // the model really is mirror-symmetric about it; a model that isn't is remeshed without symmetry rather
    // than having one side replaced by the other.
    int axis = -1;
    float center = 0.0f;
    if (askedAxis >= 0) {
        buildFull();
        double totalArea = 0.0;
        for (const glm::uvec3& t : work.T)
            totalArea += 0.5 * glm::length(glm::cross(work.P[t.y] - work.P[t.x], work.P[t.z] - work.P[t.x]));
        const float edge = float(std::sqrt(totalArea / double(target)));
        std::vector<float> candidates;
        if (opt.symmetryAuto) {
            candidates = {0.0f, box.center()[askedAxis]};
            double c = 0.0;
            for (const glm::uvec3& t : work.T) {
                const float a = 0.5f * glm::length(glm::cross(work.P[t.y] - work.P[t.x], work.P[t.z] - work.P[t.x]));
                c += double(a) * double(work.P[t.x][askedAxis] + work.P[t.y][askedAxis] + work.P[t.z][askedAxis]) / 3.0;
            }
            if (totalArea > 0.0) candidates.push_back(float(c / totalArea));
        } else {
            candidates = {opt.symmetryCenter};
        }
        MirrorFit best;
        best.mean = std::numeric_limits<float>::max();
        for (float c : candidates) {
            const MirrorFit f = mirrorFit(full, work, askedAxis, c, 0.25f * diag);
            if (timing)
                std::fprintf(stderr, "  symmetry plane %.4f: mean %.4f, 98%% %.4f (edge %.4f)\n", c, f.mean, f.high, edge);
            if (f.mean < best.mean) best = f, center = c;
        }
        st.symmetryCenter = center;
        st.asymmetry = edge > 0.0f ? best.mean / edge : 0.0f;
        if (best.mean <= 0.2f * edge && best.high <= 1.0f * edge) axis = askedAxis;
        mark("symmetry plane");
    }
    st.symmetryAxis = askedAxis;
    st.symmetryUsed = axis >= 0;
    if (axis >= 0) {
        for (vec3& p : work.P) p[axis] -= center;
        work = cutAtPlane(work, axis, 1e-6f * diag);
        if (work.T.empty()) {
            error = "Quad Remesh: nothing on the positive side of the symmetry plane";
            return false;
        }
    }

    // QRemesh Guides in the working frame: snapped onto the sculpt as it is now (it may have changed since they
    // were drawn) and, with symmetry, mirrored, so a curve drawn on either side guides both.
    // A guide mostly off the surface (the SubTool was moved after it was drawn) is left out.
    std::vector<QRemeshGuide> guides;
    if (!opt.guides.empty()) buildFull();
    for (const QRemeshGuide& g : opt.guides) {
        QRemeshGuide s = g;
        size_t on = 0;
        for (vec3& p : s.points) {
            SurfaceHit hit;
            if (full.closestPoint(p, 0.05f * diag, hit)) p = hit.position, ++on;
            if (axis >= 0) p[axis] -= center;
        }
        if (s.points.size() >= 2 && on * 2 > s.points.size()) guides.push_back(std::move(s));
        else ++st.guidesSkipped;
    }
    st.guides = guides.size();
    if (!guides.empty()) {
        if (axis >= 0) {
            // A curve that is its own mirror image (a loop round the mouth, across the middle) isn't doubled:
            // the two copies would pin different corners on the same loop.
            const size_t n = guides.size();
            for (size_t i = 0; i < n; ++i) {
                QRemeshGuide m = guides[i];
                for (vec3& p : m.points) p[axis] = -p[axis];
                std::vector<vec3> a, b;
                for (size_t k = 0; k + 1 < guides[i].points.size(); ++k)
                    a.push_back(guides[i].points[k]), b.push_back(guides[i].points[k + 1]);
                if (guides[i].closed) a.push_back(guides[i].points.back()), b.push_back(guides[i].points.front());
                SegmentBVH self;
                self.build(std::move(a), std::move(b));
                double off = 0.0;
                for (const vec3& p : m.points) {
                    SurfaceHit hit;
                    off += self.closestPoint(p, 0.05f * diag, hit) ? hit.distance : 0.05f * diag;
                }
                if (off / double(m.points.size()) > 0.004 * diag) guides.push_back(std::move(m));
            }
            // Only the +axis half is remeshed: each guide is cut at the plane. A loop across the middle becomes
            // a curve from the seam to the seam (exactly on it at both ends), so its corners meet the seam's.
            std::vector<QRemeshGuide> half;
            for (const QRemeshGuide& g : guides) {
                const size_t n = g.points.size();
                const bool loop = g.closed && n > 2;
                size_t start = 0; // a loop's walk starts on the far side, so no piece wraps round
                if (loop)
                    while (start < n && g.points[start][axis] >= 0.0f) ++start;
                if (loop && start == n) {
                    half.push_back(g);
                    continue;
                }
                QRemeshGuide run;
                run.stroke = g.stroke;
                auto flush = [&] {
                    if (run.points.size() >= 2) half.push_back(run);
                    run.points.clear();
                };
                if (g.points[start][axis] >= 0.0f) run.points.push_back(g.points[start]);
                for (size_t k = 0, segs = loop ? n : n - 1; k < segs; ++k) {
                    const vec3 a = g.points[(start + k) % n], b = g.points[(start + k + 1) % n];
                    const bool ina = a[axis] >= 0.0f, inb = b[axis] >= 0.0f;
                    if (ina != inb) {
                        vec3 p = glm::mix(a, b, a[axis] / (a[axis] - b[axis]));
                        p[axis] = 0.0f;
                        run.points.push_back(p);
                        if (ina) flush();
                    }
                    if (inb) run.points.push_back(b);
                }
                flush();
            }
            guides = std::move(half);
        }
    }

    // Small separate pieces (eyes, teeth, rivets: fewer than ~100 quads by their area) are remeshed on their
    // own with a few dozen quads at least, so they don't vanish between the lattice corners of the big ones;
    // the rest share one field (and the target, by curvature, as before).
    const double wantQuads = double(target) / (axis >= 0 ? 2.0 : 1.0);
    constexpr double kOwnFieldBelow = 100.0, kMinPieceQuads = 32.0;
    std::vector<WorkMesh> parts;
    std::vector<double> partArea, budget;
    {
        size_t count = 0;
        const std::vector<uint32_t> piece = pieceOfTriangles(work, count);
        st.pieces = count;
        std::vector<double> pieceArea(count, 0.0);
        double area = 0.0;
        for (size_t t = 0; t < work.T.size(); ++t) {
            const glm::uvec3 tri = work.T[t];
            const double a = 0.5 * glm::length(glm::cross(work.P[tri.y] - work.P[tri.x], work.P[tri.z] - work.P[tri.x]));
            pieceArea[piece[t]] += a;
            area += a;
        }
        const double biggest = count ? *std::max_element(pieceArea.begin(), pieceArea.end()) : 0.0;
        std::vector<uint8_t> small(count, 0);
        double smallTotal = 0.0, bigArea = 0.0;
        std::vector<double> smallBudget(count, 0.0);
        for (size_t i = 0; i < count; ++i) {
            const double share = wantQuads * pieceArea[i] / std::max(area, 1e-30);
            if (share >= kOwnFieldBelow || pieceArea[i] >= biggest) {
                bigArea += pieceArea[i];
                continue;
            }
            small[i] = 1;
            // Adaptive Size: a small piece is tightly curved, so it gets denser quads than its area alone gives
            // (up to the 1 / 0.45^2 the curvature allows within a piece); sizes as the radius of a sphere of the
            // same area, against the biggest piece's.
            double weight = 1.0;
            if (opt.adaptive > 0.01f)
                weight = std::clamp(std::pow(std::sqrt(biggest / std::max(pieceArea[i], 1e-30)) / 4.0, 1.2 * double(opt.adaptive)),
                                    1.0, 4.9);
            smallBudget[i] = std::max(share * weight, kMinPieceQuads);
            smallTotal += smallBudget[i];
        }
        if (smallTotal == 0.0) {
            parts.push_back(std::move(work));
            partArea.push_back(bigArea);
            budget.push_back(wantQuads);
        } else {
            std::vector<uint32_t> slot(count, kNone);
            size_t slots = bigArea > 0.0 ? 1 : 0;
            if (bigArea > 0.0) {
                partArea.push_back(bigArea);
                budget.push_back(std::max(wantQuads - smallTotal, 0.5 * wantQuads));
            }
            for (size_t i = 0; i < count; ++i) {
                if (!small[i]) {
                    slot[i] = 0;
                    continue;
                }
                slot[i] = uint32_t(slots++);
                partArea.push_back(pieceArea[i]);
                budget.push_back(smallBudget[i]);
            }
            parts = gatherPieces(work, piece, slot, slots);
        }
        work = WorkMesh{};
    }
    size_t totalTris = 0;
    for (const WorkMesh& p : parts) totalTris += p.T.size();
    if (!report(0.03f)) return false;

    QuadMesh qm;
    std::vector<uint8_t> seam;
    size_t trisDone = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        const float f0 = 0.03f + 0.9f * float(trisDone) / float(std::max<size_t>(totalTris, 1));
        const float f1 = 0.03f + 0.9f * float(trisDone + parts[i].T.size()) / float(std::max<size_t>(totalTris, 1));
        trisDone += parts[i].T.size();
        const std::function<bool(float)> pieceReport = [&](float f) { return report(f0 + (f1 - f0) * f); };
        PieceResult r;
        double want = budget[i];
        bool doneByV2 = false;
        if (opt.method == QRemeshMethod::V2) {
            qr::FieldPieceResult fr;
            bool cancelled = false;
            const bool ok = qr::remeshPieceV2(parts[i], axis, want, opt, diag, guides, pieceReport, fr, cancelled);
            if (cancelled) {
                error = "Quad Remesh cancelled";
                return false;
            }
            st.attempts += fr.attempts;
            st.singularities += fr.singularities;
            if (ok && !fr.mesh.quads.empty()) {
                r.mesh = std::move(fr.mesh);
                r.seam = std::move(fr.seam);
                doneByV2 = true;
            } else {
                ++st.fallbackPieces;
                if (timing) std::fprintf(stderr, "  piece %zu: Quad Remesh 2 fell back to legacy (%s)\n", i, fr.why.c_str());
            }
        }
        // A piece the lattice misses is tried again finer, then kept as it is (its own triangles).
        for (int attempt = 0; attempt < 3 && r.mesh.quads.empty(); ++attempt) {
            r = PieceResult{};
            want = budget[i] * std::pow(2.5, attempt);
            if (!remeshPiece(parts[i], axis, want, opt, diag, guides, pieceReport, mark, r)) {
                error = "Quad Remesh cancelled";
                return false;
            }
            st.attempts += r.attempts;
        }
        // The lattice round a guide can go wrong by chance (where the field's singularities land, which the
        // smallest rounding differences move): a broken loop or a fan by a guide is tried with other starts for
        // the field, keeping the best.
        for (uint32_t seed = 1; seed <= 2 && r.guideDefects > 0 && !r.mesh.quads.empty() && !doneByV2; ++seed) {
            QRemeshOptions again = opt;
            again.seed = opt.seed + seed * 7919u;
            PieceResult other;
            if (!remeshPiece(parts[i], axis, want, again, diag, guides, pieceReport, mark, other)) {
                error = "Quad Remesh cancelled";
                return false;
            }
            st.attempts += other.attempts;
            if (!other.mesh.quads.empty() && other.guideDefects < r.guideDefects) r = std::move(other);
        }
        const uint32_t base = uint32_t(qm.P.size());
        if (r.mesh.quads.empty()) {
            const WorkMesh& p = parts[i];
            qm.P.insert(qm.P.end(), p.P.begin(), p.P.end());
            for (const glm::uvec3& t : p.T) qm.tris.push_back(t + glm::uvec3(base));
            for (const vec3& v : p.P) seam.push_back(axis >= 0 && std::abs(v[axis]) <= 1e-6f * diag);
            continue;
        }
        if (timing)
            std::fprintf(stderr, "  piece %zu: %zu triangles, area %.4g, budget %.0f -> %zu quads\n", i, parts[i].T.size(),
                         partArea[i], budget[i], r.mesh.quads.size());
        qm.P.insert(qm.P.end(), r.mesh.P.begin(), r.mesh.P.end());
        for (const glm::uvec4& q : r.mesh.quads) qm.quads.push_back(q + glm::uvec4(base));
        for (const glm::uvec3& t : r.mesh.tris) qm.tris.push_back(t + glm::uvec3(base));
        seam.insert(seam.end(), r.seam.begin(), r.seam.end());
    }
    parts.clear();
    if (qm.quads.empty() && qm.tris.empty()) {
        error = "Quad Remesh could not build quads on this surface (try a higher Target or turn Keep Groups off)";
        return false;
    }
    if (!report(0.94f)) return false;

    // Symmetry: mirror the half, welding the vertices on the plane.
    if (axis >= 0) {
        const size_t nv = qm.P.size(), nq = qm.quads.size(), nt = qm.tris.size();
        std::vector<uint32_t> mirror(nv);
        for (uint32_t v = 0; v < nv; ++v) {
            if (seam[v]) {
                qm.P[v][axis] = 0.0f;
                mirror[v] = v;
            } else {
                mirror[v] = uint32_t(qm.P.size());
                vec3 p = qm.P[v];
                p[axis] = -p[axis];
                qm.P.push_back(p);
            }
        }
        for (size_t f = 0; f < nq; ++f) {
            const glm::uvec4 q = qm.quads[f];
            qm.quads.push_back({mirror[q.w], mirror[q.z], mirror[q.y], mirror[q.x]});
        }
        for (size_t f = 0; f < nt; ++f) {
            const glm::uvec3 t = qm.tris[f];
            qm.tris.push_back({mirror[t.z], mirror[t.y], mirror[t.x]});
        }
        for (vec3& p : qm.P) p[axis] += center;
        // A model only nearly symmetric: the topology stays mirrored, each side sits on its own surface.
        if (st.asymmetry > 1e-3f)
            parallelFor(qm.P.size(), [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    SurfaceHit hit;
                    if (full.closestPoint(qm.P[v], 0.25f * diag, hit)) qm.P[v] = hit.position;
                }
            });
    }

    // Output mesh: each quad as two triangles split along corner 0 - 2, then any pieces kept as triangles.
    Mesh m;
    m.positions = qm.P;
    m.triangles.reserve(qm.quads.size() * 2 + qm.tris.size());
    // Each quad split along a diagonal that isn't an edge of another face (else that edge would be used by four
    // triangles), the shorter one when both are free.
    std::unordered_map<uint64_t, uint8_t> quadEdges;
    for (const auto& q : qm.quads)
        for (int k = 0; k < 4; ++k) quadEdges[edgeKey(q[k], q[(k + 1) % 4])] = 1;
    for (const glm::uvec3& t : qm.tris)
        for (int k = 0; k < 3; ++k) quadEdges[edgeKey(t[k], t[(k + 1) % 3])] = 1;
    for (const auto& q : qm.quads) {
        const uint32_t t = uint32_t(m.triangles.size());
        const bool xzTaken = quadEdges.count(edgeKey(q.x, q.z)) > 0, ywTaken = quadEdges.count(edgeKey(q.y, q.w)) > 0;
        const bool useYW = (xzTaken && !ywTaken) ||
                           (xzTaken == ywTaken && glm::length(qm.P[q.y] - qm.P[q.w]) < 0.98f * glm::length(qm.P[q.x] - qm.P[q.z]));
        if (useYW) {
            m.triangles.push_back({q.y, q.z, q.w});
            m.triangles.push_back({q.y, q.w, q.x});
            quadEdges[edgeKey(q.y, q.w)] = 1; // two quads with the same diagonal would also share it four ways
        } else {
            m.triangles.push_back({q.x, q.y, q.z});
            m.triangles.push_back({q.x, q.z, q.w});
            quadEdges[edgeKey(q.x, q.z)] = 1;
        }
        m.pairQuad(t, t + 1);
    }
    for (const glm::uvec3& t : qm.tris) m.triangles.push_back(t);
    if (!m.quadPartners.empty()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad); // the triangles after the quads
    if (!report(0.96f)) return false;

    // Attributes from the closest point of the original sculpt.
    const bool needAttr = source.hasMask() || source.hasColors() || opt.keepGroups;
    if (needAttr) {
        buildFull();
        const float reach = diag;
        if (source.hasMask() || source.hasColors()) {
            if (source.hasMask()) m.ensureMask();
            if (source.hasColors()) m.ensureColors();
            parallelFor(m.positions.size(), [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    SurfaceHit hit;
                    if (!full.closestPoint(m.positions[v], reach, hit)) continue;
                    const glm::uvec3 t = source.triangles[hit.prim];
                    if (source.hasMask())
                        m.mask[v] = hit.bary.x * source.mask[t.x] + hit.bary.y * source.mask[t.y] + hit.bary.z * source.mask[t.z];
                    if (source.hasColors())
                        m.colors[v] = hit.bary.x * source.colors[t.x] + hit.bary.y * source.colors[t.y] +
                                      hit.bary.z * source.colors[t.z];
                }
            });
        }
        if (opt.keepGroups && source.hasPolygroups()) {
            m.ensurePolygroups();
            const size_t nq = qm.quads.size();
            parallelFor(nq, [&](size_t b, size_t e) {
                for (size_t q = b; q < e; ++q) {
                    const glm::uvec3 t0 = m.triangles[2 * q], t1 = m.triangles[2 * q + 1];
                    const vec3 c = 0.25f * (m.positions[t0.x] + m.positions[t0.y] + m.positions[t0.z] + m.positions[t1.z]);
                    SurfaceHit hit;
                    const uint32_t g = full.closestPoint(c, reach, hit) ? source.polygroupAt(hit.prim) : 0u;
                    m.polygroups[2 * q] = m.polygroups[2 * q + 1] = g;
                }
            });
            for (size_t t = 2 * nq; t < m.triangles.size(); ++t) {
                const glm::uvec3 tri = m.triangles[t];
                SurfaceHit hit;
                const vec3 c = (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f;
                m.polygroups[t] = full.closestPoint(c, reach, hit) ? source.polygroupAt(hit.prim) : 0u;
            }
        }
    }
    m.rebuildTopology();
    m.recomputeAllNormals();

    const TopologyReport topo = topologyReport(m);
    st.quads = topo.quads;
    st.triangles = topo.triangles;
    st.boundaryEdges = topo.boundaryEdges;
    st.nonManifoldEdges = topo.nonManifoldEdges;
    {
        double len = 0.0;
        size_t cnt = 0;
        for (const auto& q : qm.quads)
            for (int k = 0; k < 4; ++k) len += glm::length(qm.P[q[k]] - qm.P[q[(k + 1) % 4]]), ++cnt;
        st.edgeLength = cnt ? float(len / double(cnt)) : 0.0f;
    }
    st.method = opt.method;
    st.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (opt.measureQuality) st.quality = quadQuality(m, &source);
    if (stats) *stats = st;
    out = std::move(m);
    return report(1.0f);
}

TopologyReport topologyReport(const Mesh& mesh) {
    TopologyReport r;
    // Faces as exported: quads once, triangles.
    std::vector<std::vector<uint32_t>> faces;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        glm::uvec4 f;
        const int n = mesh.exportFace(t, f);
        if (n == 0) continue;
        faces.push_back(n == 4 ? std::vector<uint32_t>{f.x, f.y, f.z, f.w} : std::vector<uint32_t>{f.x, f.y, f.z});
        ++(n == 4 ? r.quads : r.triangles);
    }
    r.faces = faces.size();
    // Directed edge counts.
    std::unordered_map<uint64_t, std::pair<int, int>> edges; // key (min,max) -> (uses a->b with a<b, uses b->a)
    for (const auto& f : faces)
        for (size_t k = 0; k < f.size(); ++k) {
            const uint32_t a = f[k], b = f[(k + 1) % f.size()];
            auto& e = edges[edgeKey(a, b)];
            ++(a < b ? e.first : e.second);
        }
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> fan(mesh.vertexCount()); // per vertex: (prev, next) corners
    for (const auto& f : faces)
        for (size_t k = 0; k < f.size(); ++k)
            fan[f[k]].push_back({f[(k + f.size() - 1) % f.size()], f[(k + 1) % f.size()]});
    for (auto& [key, e] : edges) {
        const int total = e.first + e.second;
        if (total == 1) ++r.boundaryEdges;
        else if (total > 2 || e.first != 1 || e.second != 1) ++r.nonManifoldEdges;
    }
    // A manifold vertex's corners chain into one fan (a cycle inside, a path on a boundary).
    for (uint32_t v = 0; v < fan.size(); ++v) {
        const auto& c = fan[v];
        if (c.size() < 2) continue;
        std::unordered_map<uint32_t, size_t> byNext; // corner whose "next" is u
        for (size_t i = 0; i < c.size(); ++i) byNext[c[i].second] = i;
        std::vector<uint8_t> seen(c.size(), 0);
        size_t start = 0;
        // Start from a corner whose prev neighbour isn't any corner's next (a boundary end), if any.
        for (size_t i = 0; i < c.size(); ++i) {
            bool hasPrev = false;
            for (size_t j = 0; j < c.size(); ++j) hasPrev |= c[j].first == c[i].second;
            if (!hasPrev) start = i;
        }
        size_t walked = 0, cur = start;
        while (!seen[cur]) {
            seen[cur] = 1;
            ++walked;
            // The corner across the edge (v, c[cur].first): it has that vertex as its "next".
            auto it = byNext.find(c[cur].first);
            if (it == byNext.end()) break;
            cur = it->second;
        }
        if (walked != c.size()) ++r.nonManifoldVertices;
    }
    return r;
}

} // namespace sf
