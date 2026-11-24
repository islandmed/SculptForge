// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mesh booleans (see MeshBoolean.h).
//
// Exact path. Mesh 0 is `a`, mesh 1 is `b` (nudged for the decisions). Intersection points come in one kind, keyed
// so every triangle that meets one finds the same id: an edge (u, v) of one mesh crossing triangle t of the other.
// Each decision is an orientation test on the input coordinates, computed one way only (the edge-against-edge test
// with both edges in ascending vertex order), so the two triangles beside an edge, and the two beside the edge it
// crosses, always agree. A test within its rounding error of zero is a degenerate configuration: the run stops and
// starts again with `b` nudged (a little more each time). Where two triangles cross, the crossing segment joins the
// two points that bound it; each triangle with segments is split along them (Arrangement.h) and each region is kept
// or dropped by which side of the other mesh's triangle it lies on, read off at one of its segments. Triangles with
// no segment take the side of their vertices, which spreads along the edges no segment crosses; a piece the other
// mesh never touches is inside or outside by its winding number.

#include "core/MeshBoolean.h"

#include "core/Arrangement.h"
#include "core/CloseHoles.h"
#include "core/ThreadPool.h"
#include "core/Voxel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <mutex>
#include <numeric>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace sf {

const char* booleanOpName(BooleanOp op) {
    switch (op) {
    case BooleanOp::Union: return "union";
    case BooleanOp::Subtract: return "subtract";
    case BooleanOp::Intersect: return "intersect";
    }
    return "union";
}

bool parseBooleanOp(const std::string& name, BooleanOp& out) {
    if (name == "union" || name == "add") out = BooleanOp::Union;
    else if (name == "subtract" || name == "difference") out = BooleanOp::Subtract;
    else if (name == "intersect" || name == "intersection") out = BooleanOp::Intersect;
    else return false;
    return true;
}

double meshVolume(const Mesh& mesh) {
    double v = 0.0;
    for (const glm::uvec3& t : mesh.triangles)
        v += glm::dot(glm::dvec3(mesh.positions[t.x]), glm::cross(glm::dvec3(mesh.positions[t.y]), glm::dvec3(mesh.positions[t.z])));
    return v / 6.0;
}

namespace {

using glm::dvec2;
using glm::dvec3;
using namespace arrange;

double orient3(const dvec3& a, const dvec3& b, const dvec3& c, const dvec3& d) {
    return glm::dot(glm::cross(b - a, c - a), d - a);
}

// Rounding bound of orient3 on these points (a generous multiple of the textbook one).
double orientBound(const dvec3& a, const dvec3& b, const dvec3& c, const dvec3& d) {
    const dvec3 x = glm::abs(b - a), y = glm::abs(c - a), z = glm::abs(d - a);
    const double perm = x.x * (y.y * z.z + y.z * z.y) + x.y * (y.x * z.z + y.z * z.x) + x.z * (y.x * z.y + y.y * z.x);
    return 1e-13 * perm;
}

struct Box {
    dvec3 lo{1e300}, hi{-1e300};
    void add(const dvec3& p) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    bool overlaps(const Box& o) const {
        return lo.x <= o.hi.x && hi.x >= o.lo.x && lo.y <= o.hi.y && hi.y >= o.lo.y && lo.z <= o.hi.z && hi.z >= o.lo.z;
    }
};

// A bounding volume tree over triangle boxes (median splits, four triangles a leaf).
struct TriTree {
    struct Node {
        Box box;
        uint32_t first = 0, count = 0; // leaf: idx[first, first + count); inner (count 0): children first, first + 1
    };
    std::vector<Node> nodes;
    std::vector<uint32_t> idx;

    struct Job {
        uint32_t node, begin, end;
    };
    // Splits job j's triangles in `out` (its node is out[j.node]); the halves go on `stack`.
    void splitJob(const Job& j, const std::vector<Box>& boxes, const std::vector<dvec3>& centre, std::vector<Node>& out,
                  std::vector<Job>& stack) {
        Box b, cb;
        for (uint32_t i = j.begin; i < j.end; ++i) {
            b.add(boxes[idx[i]].lo);
            b.add(boxes[idx[i]].hi);
            cb.add(centre[idx[i]]);
        }
        out[j.node].box = b;
        if (j.end - j.begin <= 4) {
            out[j.node].first = j.begin;
            out[j.node].count = j.end - j.begin;
            return;
        }
        const dvec3 ext = cb.hi - cb.lo;
        const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
        const uint32_t mid = (j.begin + j.end) / 2;
        std::nth_element(idx.begin() + j.begin, idx.begin() + mid, idx.begin() + j.end,
                         [&](uint32_t p, uint32_t q) { return centre[p][axis] < centre[q][axis]; });
        const uint32_t left = uint32_t(out.size());
        out.push_back({});
        out.push_back({});
        out[j.node].first = left;
        out[j.node].count = 0;
        stack.push_back({left, j.begin, mid});
        stack.push_back({left + 1, mid, j.end});
    }
    void build(const std::vector<Box>& boxes) {
        nodes.clear();
        idx.clear();
        if (boxes.empty()) return;
        std::vector<dvec3> centre(boxes.size());
        Box cb;
        for (size_t i = 0; i < boxes.size(); ++i) {
            centre[i] = (boxes[i].lo + boxes[i].hi) * 0.5;
            cb.add(centre[i]);
        }
        // Triangles into an 8 x 8 x 8 grid of cells by centre (a counting sort); a subtree per cell, built in parallel;
        // then a small tree over the cells on top.
        constexpr int G = 8;
        const dvec3 span = glm::max(cb.hi - cb.lo, dvec3(1e-300));
        auto cellOf = [&](size_t i) {
            const dvec3 f = (centre[i] - cb.lo) / span * double(G);
            const glm::ivec3 c = glm::clamp(glm::ivec3(f), glm::ivec3(0), glm::ivec3(G - 1));
            return uint32_t(c.x + G * (c.y + G * c.z));
        };
        std::vector<uint32_t> cell(boxes.size()), start(G * G * G + 1, 0);
        for (size_t i = 0; i < boxes.size(); ++i) ++start[(cell[i] = cellOf(i)) + 1];
        for (size_t c = 0; c < size_t(G * G * G); ++c) start[c + 1] += start[c];
        idx.resize(boxes.size());
        {
            std::vector<uint32_t> fill(start.begin(), start.end() - 1);
            for (size_t i = 0; i < boxes.size(); ++i) idx[fill[cell[i]]++] = uint32_t(i);
        }
        std::vector<Job> cells;
        for (uint32_t c = 0; c < uint32_t(G * G * G); ++c)
            if (start[c + 1] > start[c]) cells.push_back({0, start[c], start[c + 1]});
        std::vector<std::vector<Node>> sub(cells.size());
        poolFor(cells.size(), [&](size_t b, size_t e) {
            for (size_t k = b; k < e; ++k) {
                std::vector<Node>& out = sub[k];
                out.push_back({});
                std::vector<Job> stack{{0, cells[k].begin, cells[k].end}};
                while (!stack.empty()) {
                    const Job j = stack.back();
                    stack.pop_back();
                    splitJob(j, boxes, centre, out, stack);
                }
            }
        }, 1, 2);
        // The top tree: cells split at the median of their box centres; a lone cell's slot takes its subtree's root.
        std::vector<uint32_t> order(cells.size());
        std::iota(order.begin(), order.end(), 0u);
        nodes.reserve(2 * boxes.size() / 4 + 2 * cells.size() + 2);
        nodes.push_back({});
        auto place = [&](uint32_t slot, uint32_t k) {
            const uint32_t offset = uint32_t(nodes.size()) - 1;
            for (Node& n : sub[k])
                if (n.count == 0) n.first += offset;
            nodes[slot] = sub[k][0];
            nodes.insert(nodes.end(), sub[k].begin() + 1, sub[k].end());
        };
        struct Top {
            uint32_t slot, begin, end; // cells order[begin, end)
        };
        std::vector<Top> stack{{0, 0, uint32_t(cells.size())}};
        while (!stack.empty()) {
            const Top t = stack.back();
            stack.pop_back();
            if (t.end - t.begin == 1) {
                place(t.slot, order[t.begin]);
                continue;
            }
            Box b, cc;
            for (uint32_t i = t.begin; i < t.end; ++i) {
                const Box& kb = sub[order[i]][0].box;
                b.add(kb.lo);
                b.add(kb.hi);
                cc.add((kb.lo + kb.hi) * 0.5);
            }
            const dvec3 ext = cc.hi - cc.lo;
            const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
            const uint32_t mid = (t.begin + t.end) / 2;
            std::nth_element(order.begin() + t.begin, order.begin() + mid, order.begin() + t.end, [&](uint32_t p, uint32_t q) {
                return sub[p][0].box.lo[axis] + sub[p][0].box.hi[axis] < sub[q][0].box.lo[axis] + sub[q][0].box.hi[axis];
            });
            const uint32_t left = uint32_t(nodes.size());
            nodes.push_back({});
            nodes.push_back({});
            nodes[t.slot].box = b;
            nodes[t.slot].first = left;
            nodes[t.slot].count = 0;
            stack.push_back({left, t.begin, mid});
            stack.push_back({left + 1, mid, t.end});
        }
    }
    template <class F>
    void query(const Box& q, F&& f) const {
        if (nodes.empty()) return;
        uint32_t stack[128];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const Node& n = nodes[stack[--top]];
            if (!n.box.overlaps(q)) continue;
            if (n.count > 0) {
                for (uint32_t i = n.first; i < n.first + n.count; ++i) f(idx[i]);
            } else if (top + 2 <= 128) {
                stack[top++] = n.first;
                stack[top++] = n.first + 1;
            }
        }
    }
};

// An intersection point: edge (u, v) (u < v) of mesh `side` crossing triangle t of the other mesh.
struct XKey {
    uint32_t u, v, t;
    uint8_t side;
    bool operator==(const XKey& o) const { return u == o.u && v == o.v && t == o.t && side == o.side; }
};
struct XKeyHash {
    size_t operator()(const XKey& k) const {
        uint64_t h = (uint64_t(k.u) << 32 | k.v) * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t(k.t) << 1 | k.side) + 0x632BE59BD9B4E019ull + (h << 6) + (h >> 2);
        return size_t(h);
    }
};
struct XPt {
    XKey key{};
    double lam = 0.0; // along u -> v
    dvec3 bary{0.0};  // in triangle t of the other mesh, by its corners in order
};
struct Seg {
    uint32_t p, q;  // point ids (global)
    uint32_t other; // the other mesh's triangle
};

constexpr int8_t kUnknown = -1, kIn = 0, kOut = 1;

// SF_BOOLEAN_TRACE=1: each phase's time on stderr (tests/test_mesh_boolean.cpp's bench).
struct Trace {
    bool on = std::getenv("SF_BOOLEAN_TRACE") != nullptr;
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    void operator()(const char* what) {
        if (!on) return;
        const auto now = std::chrono::steady_clock::now();
        std::fprintf(stderr, "  boolean %-12s %7.1f ms\n", what, std::chrono::duration<double, std::milli>(now - t).count());
        t = now;
    }
};
inline Trace& trace() {
    static thread_local Trace t;
    return t;
}

struct Solver {
    const Mesh* M[2];
    std::vector<dvec3> P[2]; // decision coordinates (mesh 1 nudged)
    std::vector<dvec3> W[2]; // output coordinates (the input's)
    uint32_t nv[2] = {0, 0}, nt[2] = {0, 0};
    BooleanOp op = BooleanOp::Union;
    std::atomic<bool> degenerate{false};
    std::atomic<bool> selfCross{false};

    // Results of a run.
    std::vector<XPt> pts;                        // id = nv[0] + nv[1] + i
    std::vector<std::array<uint32_t, 3>> outTris; // global ids
    std::vector<int64_t> outParent;               // triangle of mesh 0, or nt[0] + triangle of mesh 1
    size_t cutTriangles = 0;
    std::string error; // a run that fails for a reason a nudge can't fix

    uint32_t base(int s) const { return s == 0 ? 0 : nv[0]; }
    uint32_t firstPoint() const { return nv[0] + nv[1]; }

    // Orientation of vertex v of mesh 1 - s against triangle t of mesh s (> 0: on its outward side).
    double side(int s, uint32_t t, uint32_t v) {
        const glm::uvec3 tri = M[s]->triangles[t];
        const dvec3 &a = P[s][tri.x], &b = P[s][tri.y], &c = P[s][tri.z], &d = P[1 - s][v];
        const double o = orient3(a, b, c, d);
        if (std::abs(o) <= orientBound(a, b, c, d)) degenerate = true;
        return o;
    }
    // Edge (au, av) of mesh 0 against edge (bk, bl) of mesh 1, both ascending: orient3 of the four points.
    double edgeEdge(uint32_t au, uint32_t av, uint32_t bk, uint32_t bl) {
        const dvec3 &a = P[0][au], &b = P[0][av], &c = P[1][bk], &d = P[1][bl];
        const double o = orient3(a, b, c, d);
        if (std::abs(o) <= orientBound(a, b, c, d)) degenerate = true;
        return o;
    }
    // The signed edge-edge value of edge (u, v) of mesh s (any order) against directed edge (k -> l) of the other mesh,
    // with the first edge taken ascending.
    double crossing(int s, uint32_t u, uint32_t v, uint32_t k, uint32_t l) {
        const uint32_t lo = std::min(u, v), hi = std::max(u, v);
        const double sign = k < l ? 1.0 : -1.0;
        const uint32_t klo = std::min(k, l), khi = std::max(k, l);
        // orient3(p, q, r, s) = orient3(r, s, p, q): the value is symmetric in which edge is mesh 0's.
        return sign * (s == 0 ? edgeEdge(lo, hi, klo, khi) : edgeEdge(klo, khi, lo, hi));
    }

    // Edge (u, v) of mesh s through triangle t of the other mesh: the point, or false.
    bool edgeThrough(int s, uint32_t u, uint32_t v, double su, double sv, uint32_t t, XPt& out) {
        if ((su > 0) == (sv > 0)) return false;
        const glm::uvec3 tri = M[1 - s]->triangles[t];
        const uint32_t c[3] = {tri.x, tri.y, tri.z};
        double w[3];
        for (int j = 0; j < 3; ++j) w[j] = crossing(s, u, v, c[j], c[(j + 1) % 3]);
        if (!((w[0] > 0 && w[1] > 0 && w[2] > 0) || (w[0] < 0 && w[1] < 0 && w[2] < 0))) return false;
        out.key = {std::min(u, v), std::max(u, v), t, uint8_t(s)};
        const double a = u < v ? su : sv, b = u < v ? sv : su;
        out.lam = a / (a - b);
        // A line through a triangle: the barycentric weight of each corner goes with the edge-edge value of the opposite side.
        const double sum = w[0] + w[1] + w[2];
        out.bary = dvec3(w[1], w[2], w[0]) / sum;
        return true;
    }

    // Where triangle ta of mesh 0 and tb of mesh 1 cross: the two points bounding the segment, or none.
    int pairPoints(uint32_t ta, uint32_t tb, XPt out[2]) {
        const glm::uvec3 A = M[0]->triangles[ta], B = M[1]->triangles[tb];
        const uint32_t ai[3] = {A.x, A.y, A.z}, bi[3] = {B.x, B.y, B.z};
        double sa[3], sb[3]; // B's corners against ta, A's corners against tb
        for (int i = 0; i < 3; ++i) sa[i] = side(0, ta, bi[i]);
        if ((sa[0] > 0) == (sa[1] > 0) && (sa[1] > 0) == (sa[2] > 0)) return 0;
        for (int i = 0; i < 3; ++i) sb[i] = side(1, tb, ai[i]);
        if ((sb[0] > 0) == (sb[1] > 0) && (sb[1] > 0) == (sb[2] > 0)) return 0;
        int n = 0;
        XPt x;
        for (int i = 0; i < 3; ++i) {
            if (edgeThrough(0, ai[i], ai[(i + 1) % 3], sb[i], sb[(i + 1) % 3], tb, x)) {
                if (n < 2) out[n] = x;
                ++n;
            }
            if (edgeThrough(1, bi[i], bi[(i + 1) % 3], sa[i], sa[(i + 1) % 3], ta, x)) {
                if (n < 2) out[n] = x;
                ++n;
            }
        }
        if (n != 0 && n != 2) degenerate = true;
        return n == 2 ? 2 : 0;
    }

    dvec3 outPos(uint32_t g) const {
        if (g < nv[0]) return W[0][g];
        if (g < firstPoint()) return W[1][g - nv[0]];
        // Every point of the cut lies on mesh 0's surface: on its edge, or where mesh 1's edge meets its triangle.
        // Where the surfaces coincide (the nudge was all that told them apart) mesh 1's pieces between the cut and its
        // own vertices then have no width, and the clean-up takes them away.
        const XPt& p = pts[g - firstPoint()];
        if (p.key.side == 0) return W[0][p.key.u] + (W[0][p.key.v] - W[0][p.key.u]) * p.lam;
        const glm::uvec3 t = M[0]->triangles[p.key.t];
        return W[0][t.x] * p.bary.x + W[0][t.y] * p.bary.y + W[0][t.z] * p.bary.z;
    }

    struct Piece { // a triangle of the result from a split triangle, and its side
        std::array<uint32_t, 3> tri;
        int64_t parent;
        int8_t status;
    };
    struct CornerStatus {
        uint32_t vertex; // global id
        int8_t status;
    };

    // Splits triangle t of mesh s along its segments: its pieces and the status of its corners.
    void splitTriangle(int s, uint32_t t, const Seg* segs, size_t count, std::vector<Piece>& pieces,
                       std::vector<CornerStatus>& corners) {
        const glm::uvec3 tri = M[s]->triangles[t];
        const uint32_t vi[3] = {tri.x, tri.y, tri.z};
        const uint32_t gi[3] = {base(s) + tri.x, base(s) + tri.y, base(s) + tri.z};
        const dvec3 rn = glm::cross(W[s][vi[1]] - W[s][vi[0]], W[s][vi[2]] - W[s][vi[0]]);
        int axis = 0;
        if (std::abs(rn.y) > std::abs(rn[axis])) axis = 1;
        if (std::abs(rn.z) > std::abs(rn[axis])) axis = 2;
        int ia = (axis + 1) % 3, ib = (axis + 2) % 3;
        if (rn[axis] < 0) std::swap(ia, ib);
        dvec2 c2[3];
        for (int i = 0; i < 3; ++i) c2[i] = {W[s][vi[i]][ia], W[s][vi[i]][ib]};
        std::vector<uint32_t> ids;
        std::vector<dvec2> p2;
        std::unordered_map<uint32_t, uint32_t> local;
        auto loc = [&](uint32_t g) {
            auto [it, fresh] = local.emplace(g, uint32_t(ids.size()));
            if (fresh) {
                ids.push_back(g);
                dvec2 q;
                if (g < firstPoint()) {
                    int k = 0;
                    while (k < 2 && gi[k] != g) ++k;
                    q = c2[k];
                } else {
                    const XPt& x = pts[g - firstPoint()];
                    if (x.key.side == s) { // on one of this triangle's edges
                        int ku = 0, kv = 0;
                        for (int k = 0; k < 3; ++k) {
                            if (vi[k] == x.key.u) ku = k;
                            if (vi[k] == x.key.v) kv = k;
                        }
                        q = c2[ku] + (c2[kv] - c2[ku]) * x.lam;
                    } else { // inside it
                        q = c2[0] * x.bary.x + c2[1] * x.bary.y + c2[2] * x.bary.z;
                    }
                }
                p2.push_back(q);
            }
            return it->second;
        };
        for (uint32_t g : gi) loc(g);
        std::vector<PEdge> edges;
        for (int i = 0; i < 3; ++i) {
            const uint32_t u = vi[i], w = vi[(i + 1) % 3];
            std::vector<std::pair<double, uint32_t>> on;
            for (size_t k = 0; k < count; ++k)
                for (uint32_t g : {segs[k].p, segs[k].q}) {
                    const XPt& x = pts[g - firstPoint()];
                    if (x.key.side == s && x.key.u == std::min(u, w) && x.key.v == std::max(u, w))
                        on.push_back({u < w ? x.lam : 1.0 - x.lam, g});
                }
            std::sort(on.begin(), on.end());
            on.erase(std::unique(on.begin(), on.end()), on.end());
            for (size_t k = 1; k < on.size(); ++k)
                if (on[k].first == on[k - 1].first) degenerate = true;
            uint32_t prev = loc(gi[i]);
            for (const auto& [param, g] : on) {
                const uint32_t l = loc(g);
                edges.push_back({prev, l, -1});
                prev = l;
            }
            edges.push_back({prev, loc(gi[(i + 1) % 3]), -1});
        }
        const size_t firstSeg = edges.size();
        for (size_t k = 0; k < count; ++k) edges.push_back({loc(segs[k].p), loc(segs[k].q), int32_t(segs[k].other)});
        // Segments that cross without sharing a point: the other mesh runs through itself here.
        if (count >= 2 && count <= 256)
            for (size_t i = firstSeg; i < edges.size() && !selfCross; ++i)
                for (size_t j = i + 1; j < edges.size(); ++j) {
                    const PEdge &e = edges[i], &f = edges[j];
                    if (e.a == f.a || e.a == f.b || e.b == f.a || e.b == f.b) continue;
                    const double o1 = orient2(p2[e.a], p2[e.b], p2[f.a]), o2 = orient2(p2[e.a], p2[e.b], p2[f.b]);
                    const double o3 = orient2(p2[f.a], p2[f.b], p2[e.a]), o4 = orient2(p2[f.a], p2[f.b], p2[e.b]);
                    if (((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) && ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0))) {
                        selfCross = true;
                        break;
                    }
                }
        if (selfCross) return;
        std::vector<PRegion> regions;
        if (!planarRegions(p2, edges, regions)) {
            degenerate = true;
            return;
        }
        std::vector<dvec3> w(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) w[i] = outPos(ids[i]);
        std::vector<std::array<uint32_t, 3>> tris;
        const int64_t parent = s == 0 ? int64_t(t) : int64_t(nt[0]) + t;
        for (const PRegion& reg : regions) {
            int which = -1;
            for (size_t k = 0; k < reg.tags.size() && which < 0; ++k)
                if (reg.tags[k] >= 0) which = int(k);
            if (which < 0) {
                degenerate = true;
                return;
            }
            // The region lies left of P -> Q, which runs along triangle `o` of the other mesh. The corner of this
            // triangle farthest from o's plane tells which side of it the left is.
            const uint32_t o = uint32_t(reg.tags[size_t(which)]);
            const dvec2 from = p2[reg.cycle[size_t(which)]], to = p2[reg.cycle[(size_t(which) + 1) % reg.cycle.size()]];
            int best = 0;
            double bestSide = 0.0, bestAbs = -1.0;
            const glm::uvec3 ot = M[1 - s]->triangles[o];
            for (int k = 0; k < 3; ++k) {
                const dvec3 &a = P[1 - s][ot.x], &b = P[1 - s][ot.y], &c = P[1 - s][ot.z];
                const double d = orient3(a, b, c, P[s][vi[k]]) / std::max(glm::length(glm::cross(b - a, c - a)), 1e-300);
                if (std::abs(d) > bestAbs) {
                    bestAbs = std::abs(d);
                    bestSide = d;
                    best = k;
                }
            }
            const double left = orient2(from, to, c2[best]);
            if (left == 0.0 || bestSide == 0.0) {
                degenerate = true;
                return;
            }
            const int8_t status = (left > 0) == (bestSide > 0) ? kOut : kIn;
            for (uint32_t l : reg.cycle)
                if (ids[l] == gi[0] || ids[l] == gi[1] || ids[l] == gi[2]) corners.push_back({ids[l], status});
            tris.clear();
            earClip(p2, reg.cycle, tris);
            if (tris.size() >= 2) {
                RegionTris rt(p2, w, tris, reg.cycle);
                rt.improve();
            }
            for (const auto& tr : tris) pieces.push_back({{ids[tr[0]], ids[tr[1]], ids[tr[2]]}, parent, status});
        }
    }

    // Winding number of mesh s around point x (1 inside a closed mesh, 0 outside).
    double winding(int s, const dvec3& x) const {
        const Mesh& m = *M[s];
        std::mutex lock;
        double total = 0.0;
        poolFor(m.triangleCount(), [&](size_t b, size_t e) {
            double sum = 0.0;
            for (size_t i = b; i < e; ++i) {
                const glm::uvec3 tri = m.triangles[i];
                const dvec3 A = P[s][tri.x] - x, B = P[s][tri.y] - x, C = P[s][tri.z] - x;
                const double la = glm::length(A), lb = glm::length(B), lc = glm::length(C);
                const double num = glm::dot(A, glm::cross(B, C));
                const double den = la * lb * lc + glm::dot(A, B) * lc + glm::dot(B, C) * la + glm::dot(C, A) * lb;
                sum += 2.0 * std::atan2(num, den);
            }
            std::lock_guard<std::mutex> g(lock);
            total += sum;
        }, 65536);
        return total / (4.0 * 3.14159265358979323846);
    }

    // One attempt with the current decision coordinates; false when degenerate (or `error` is set).
    bool run() {
        degenerate = false;
        pts.clear();
        outTris.clear();
        outParent.clear();
        cutTriangles = 0;
        // Candidate pairs: triangle boxes that touch.
        // (Only b's triangles in a's box go into the tree: where the operands are apart, nothing is looked up.)
        Box allB, allA;
        for (const dvec3& p : P[0]) allA.add(p);
        for (const dvec3& p : P[1]) allB.add(p);
        const double size = std::max({allA.hi.x - allA.lo.x, allA.hi.y - allA.lo.y, allA.hi.z - allA.lo.z,
                                      allB.hi.x - allB.lo.x, allB.hi.y - allB.lo.y, allB.hi.z - allB.lo.z, 1e-30});
        const double pad = 1e-9 * size;
        Box nearA = allA;
        nearA.lo -= dvec3(2.0 * pad);
        nearA.hi += dvec3(2.0 * pad);
        std::vector<Box> boxesB;
        std::vector<uint32_t> treeTri;
        for (uint32_t t = 0; t < nt[1]; ++t) {
            const glm::uvec3 tri = M[1]->triangles[t];
            Box b;
            for (uint32_t v : {tri.x, tri.y, tri.z}) b.add(P[1][v]);
            b.lo -= dvec3(pad);
            b.hi += dvec3(pad);
            if (!b.overlaps(nearA)) continue;
            boxesB.push_back(b);
            treeTri.push_back(t);
        }
        allB = Box();
        for (const Box& b : boxesB) {
            allB.add(b.lo);
            allB.add(b.hi);
        }
        trace()("setup");
        TriTree tree;
        tree.build(boxesB);
        trace()("tree");
        struct PairOut {
            uint32_t ta, tb;
            XPt x[2];
        };
        std::mutex lock;
        std::vector<PairOut> pairs;
        poolFor(nt[0], [&](size_t b, size_t e) {
            std::vector<PairOut> mine;
            for (size_t t = b; t < e && !degenerate; ++t) {
                const glm::uvec3 tri = M[0]->triangles[t];
                Box q;
                for (uint32_t v : {tri.x, tri.y, tri.z}) q.add(P[0][v]);
                if (!q.overlaps(allB)) continue;
                tree.query(q, [&](uint32_t leaf) {
                    const uint32_t tb = treeTri[leaf];
                    PairOut po;
                    if (pairPoints(uint32_t(t), tb, po.x) == 2) {
                        po.ta = uint32_t(t);
                        po.tb = tb;
                        mine.push_back(po);
                    }
                });
            }
            std::lock_guard<std::mutex> g(lock);
            pairs.insert(pairs.end(), mine.begin(), mine.end());
        }, 8192);
        trace()("pairs");
        if (degenerate) return false;
        // Point ids, then each triangle's segments (sorted by triangle: deterministic whatever the threads did).
        std::sort(pairs.begin(), pairs.end(), [](const PairOut& p, const PairOut& q) {
            return p.ta != q.ta ? p.ta < q.ta : p.tb < q.tb;
        });
        std::unordered_map<XKey, uint32_t, XKeyHash> ids;
        ids.reserve(pairs.size() * 2);
        auto idOf = [&](const XPt& x) {
            auto [it, fresh] = ids.emplace(x.key, firstPoint() + uint32_t(pts.size()));
            if (fresh) pts.push_back(x);
            return it->second;
        };
        std::vector<std::pair<uint32_t, Seg>> segA, segB;
        segA.reserve(pairs.size());
        segB.reserve(pairs.size());
        for (const PairOut& po : pairs) {
            const uint32_t p = idOf(po.x[0]), q = idOf(po.x[1]);
            if (p == q) {
                degenerate = true;
                return false;
            }
            segA.push_back({po.ta, {p, q, po.tb}});
            segB.push_back({po.tb, {p, q, po.ta}});
        }
        std::vector<std::pair<uint32_t, Seg>>* segsOf[2] = {&segA, &segB};
        trace()("points");
        std::stable_sort(segB.begin(), segB.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        // Split every triangle with segments (in parallel: each on its own).
        std::vector<int8_t> vstatus[2] = {std::vector<int8_t>(nv[0], kUnknown), std::vector<int8_t>(nv[1], kUnknown)};
        std::vector<uint8_t> split[2] = {std::vector<uint8_t>(nt[0], 0), std::vector<uint8_t>(nt[1], 0)};
        std::vector<Piece> pieces;
        bool conflict = false;
        for (int s = 0; s < 2; ++s) {
            const auto& list = *segsOf[s];
            std::vector<std::pair<size_t, size_t>> ranges; // [begin, end) per split triangle
            for (size_t i = 0; i < list.size();) {
                size_t j = i;
                while (j < list.size() && list[j].first == list[i].first) ++j;
                ranges.push_back({i, j});
                split[s][list[i].first] = 1;
                i = j;
            }
            cutTriangles += ranges.size();
            std::vector<Seg> flat(list.size());
            for (size_t i = 0; i < list.size(); ++i) flat[i] = list[i].second;
            std::vector<CornerStatus> corners;
            poolFor(ranges.size(), [&](size_t b, size_t e) {
                std::vector<Piece> myPieces;
                std::vector<CornerStatus> myCorners;
                for (size_t r = b; r < e && !degenerate && !selfCross; ++r)
                    splitTriangle(s, list[ranges[r].first].first, flat.data() + ranges[r].first,
                                  ranges[r].second - ranges[r].first, myPieces, myCorners);
                std::lock_guard<std::mutex> g(lock);
                pieces.insert(pieces.end(), myPieces.begin(), myPieces.end());
                corners.insert(corners.end(), myCorners.begin(), myCorners.end());
            }, 32);
            if (degenerate || selfCross) break;
            for (const CornerStatus& c : corners) {
                int8_t& st = vstatus[s][c.vertex - base(s)];
                if (st != kUnknown && st != c.status) conflict = true;
                st = c.status;
            }
        }
        if (selfCross) {
            error = "Boolean: one of the Parts runs through itself where they meet, so its inside isn't clear";
            return false;
        }
        if (degenerate) return false;
        if (conflict) return false; // (a nudge may settle it; if not, the caller reports it)
        trace()("split");
        // The side of every other vertex: spread along the edges no segment crosses, else the winding number.
        for (int s = 0; s < 2; ++s) {
            std::unordered_set<uint64_t> cutEdges;
            std::vector<uint8_t> onCut(nv[s], 0); // (only edges between two of these can be cut: no lookup elsewhere)
            for (const XPt& p : pts)
                if (p.key.side == s) {
                    cutEdges.insert(uint64_t(p.key.u) << 32 | p.key.v);
                    onCut[p.key.u] = onCut[p.key.v] = 1;
                }
            std::vector<uint32_t> parent(nv[s]);
            std::iota(parent.begin(), parent.end(), 0u);
            auto find = [&](uint32_t x) {
                while (parent[x] != x) x = parent[x] = parent[parent[x]];
                return x;
            };
            for (const glm::uvec3& tri : M[s]->triangles)
                for (int i = 0; i < 3; ++i) {
                    const uint32_t u = std::min(tri[i], tri[(i + 1) % 3]), v = std::max(tri[i], tri[(i + 1) % 3]);
                    if (!(onCut[u] && onCut[v] && cutEdges.count(uint64_t(u) << 32 | v))) parent[find(u)] = find(v);
                }
            std::vector<int8_t> rootStatus(nv[s], kUnknown);
            for (uint32_t v = 0; v < nv[s]; ++v) {
                if (vstatus[s][v] == kUnknown) continue;
                int8_t& st = rootStatus[find(v)];
                if (st != kUnknown && st != vstatus[s][v]) return false;
                st = vstatus[s][v];
            }
            for (uint32_t v = 0; v < nv[s]; ++v) {
                if (vstatus[s][v] != kUnknown) continue;
                int8_t& st = rootStatus[find(v)];
                if (st == kUnknown) st = winding(1 - s, P[s][v]) > 0.5 ? kIn : kOut;
                vstatus[s][v] = st;
            }
        }
        trace()("sides");
        // Keep what the operation wants: a outside b (Union, Subtract) or inside (Intersect); b outside a (Union) or
        // inside (Subtract, turned inside out; Intersect).
        const int8_t keepA = op == BooleanOp::Intersect ? kIn : kOut;
        const int8_t keepB = op == BooleanOp::Union ? kOut : kIn;
        const bool flipB = op == BooleanOp::Subtract;
        auto emit = [&](std::array<uint32_t, 3> tr, int64_t parent) {
            if (flipB && parent >= int64_t(nt[0])) std::swap(tr[1], tr[2]);
            outTris.push_back(tr);
            outParent.push_back(parent);
        };
        for (int s = 0; s < 2; ++s)
            for (uint32_t t = 0; t < nt[s]; ++t) {
                if (split[s][t]) continue;
                const glm::uvec3 tri = M[s]->triangles[t];
                if (vstatus[s][tri.x] != (s == 0 ? keepA : keepB)) continue;
                emit({base(s) + tri.x, base(s) + tri.y, base(s) + tri.z}, s == 0 ? int64_t(t) : int64_t(nt[0]) + t);
            }
        std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
            return a.parent != b.parent ? a.parent < b.parent : a.tri < b.tri;
        });
        for (const Piece& p : pieces)
            if (p.status == (p.parent < int64_t(nt[0]) ? keepA : keepB)) emit(p.tri, p.parent);
        trace()("emit");
        return true;
    }
};

// Barycentric coordinates of x in triangle (a, b, c), in its plane.
dvec3 baryIn(const dvec3& x, const dvec3& a, const dvec3& b, const dvec3& c) {
    const dvec3 n = glm::cross(b - a, c - a);
    const double nn = glm::dot(n, n);
    if (!(nn > 0)) return {1.0 / 3, 1.0 / 3, 1.0 / 3};
    const double l0 = glm::dot(glm::cross(c - b, x - b), n) / nn, l1 = glm::dot(glm::cross(a - c, x - c), n) / nn;
    return {l0, l1, 1.0 - l0 - l1};
}

} // namespace

bool booleanExact(const Mesh& a, const Mesh& b, BooleanOp op, Mesh& out, std::string& error, const BooleanOptions& options,
                  BooleanInfo* info) {
    if (a.triangleCount() == 0 || b.triangleCount() == 0) {
        error = "Boolean: a Part is empty";
        return false;
    }
    trace() = Trace();
    if (!closedMesh(a) || !closedMesh(b)) {
        error = std::string("Boolean: ") + (!closedMesh(a) ? "the first" : "the second") +
                " Part isn't closed (it has open borders or edges shared by more than two faces). Close its holes or remesh it first";
        return false;
    }
    Solver s;
    s.M[0] = &a;
    s.M[1] = &b;
    s.op = op;
    for (int k = 0; k < 2; ++k) {
        const Mesh& m = k == 0 ? a : b;
        s.nv[k] = uint32_t(m.vertexCount());
        s.nt[k] = uint32_t(m.triangleCount());
        s.W[k].resize(m.vertexCount());
        for (size_t v = 0; v < m.vertexCount(); ++v) s.W[k][v] = dvec3(m.positions[v]);
    }
    s.P[0] = s.W[0];
    trace()("closed+copy");
    AABB box = a.bounds();
    box.expand(b.bounds());
    const double extent = std::max(double(glm::length(box.extent())), 1e-12);
    std::mt19937 rng(20261111);
    bool ok = false, empty = false;
    int attempt = 0;
    // The nudge: `b` grown along its normals (Union, Subtract) or shrunk (Intersect) by a hair, so a face of `b` lying on
    // a face of `a` is decided the way the operation wants: a face on a face, facing the same way, ends up once (Union,
    // Intersect) or opens (a flush cutter's Subtract), facing each other the two merge (Union) or a Subtract keeps `a`'s.
    // Then, from the second try, a random translation (and from the fourth a tiny turn) for any coincidence left.
    std::vector<dvec3> grow(b.vertexCount(), dvec3(0.0));
    for (const glm::uvec3& t : b.triangles) {
        const dvec3 n = glm::cross(s.W[1][t.y] - s.W[1][t.x], s.W[1][t.z] - s.W[1][t.x]);
        grow[t.x] += n;
        grow[t.y] += n;
        grow[t.z] += n;
    }
    const double hair = (op == BooleanOp::Intersect ? -4e-7 : 4e-7) * extent;
    for (dvec3& g : grow) {
        const double l = glm::length(g);
        g = l > 0 ? g * (hair / l) : dvec3(0.0);
    }
    for (; attempt < 8 && !ok; ++attempt) {
        s.P[1] = s.W[1];
        for (size_t v = 0; v < grow.size(); ++v) s.P[1][v] += grow[v];
        if (attempt > 0) {
            const double amp = 1e-9 * extent * std::pow(4.0, attempt);
            std::uniform_real_distribution<double> jitter(-1.0, 1.0);
            const dvec3 d(jitter(rng) * amp, jitter(rng) * amp, jitter(rng) * amp);
            const dvec3 axis = glm::normalize(dvec3(jitter(rng), jitter(rng), jitter(rng)) + dvec3(1e-9));
            const double angle = attempt >= 3 ? jitter(rng) * amp / extent : 0.0;
            const dvec3 c = dvec3(box.center());
            for (dvec3& p : s.P[1]) {
                const dvec3 r = p - c;
                p = c + r * std::cos(angle) + glm::cross(axis, r) * std::sin(angle) +
                    axis * glm::dot(axis, r) * (1.0 - std::cos(angle)) + d;
            }
        }
        s.error.clear();
        s.selfCross = false;
        if (!s.run()) {
            if (!s.error.empty()) {
                error = s.error;
                return false;
            }
            continue;
        }
        if (s.outTris.empty()) {
            empty = true;
            break;
        }
        std::vector<uint8_t> touched;
        cleanSlivers(s.outTris, s.outParent, touched, [&](uint32_t g) { return s.outPos(g); });
        trace()("slivers");
        // A closed result is the check that the run held together; else try again with another nudge.
        if (closedTriangles(s.outTris.size(), [&](size_t t) { return s.outTris[t]; })) ok = true;
        trace()("check");
    }
    if (info) {
        info->attempts = attempt;
        info->cutTriangles = s.cutTriangles;
        info->voxel = false;
    }
    if (empty) {
        error = op == BooleanOp::Intersect ? "Boolean: the Parts don't overlap, so Intersect leaves nothing"
                                           : "Boolean: that would leave nothing";
        return false;
    }
    if (!ok) {
        error = "Boolean: the Parts' surfaces meet too closely to cut cleanly (or one of them runs through itself)";
        return false;
    }

    // The mesh: every id the triangles use. Unwelded, mesh 1's triangles get their own copies of the seam points.
    const uint32_t first = s.firstPoint(), total = first + uint32_t(s.pts.size());
    std::vector<uint32_t> remap(size_t(total) * (options.weld ? 1 : 2), ~0u);
    std::vector<std::pair<uint32_t, int>> used; // (global id, the mesh whose triangle used it first)
    Mesh m;
    m.triangles.resize(s.outTris.size());
    for (size_t t = 0; t < s.outTris.size(); ++t) {
        const int side = s.outParent[t] < int64_t(s.nt[0]) ? 0 : 1;
        for (int k = 0; k < 3; ++k) {
            const uint32_t g = s.outTris[t][size_t(k)];
            const size_t slot = !options.weld && side == 1 && g >= first ? size_t(total) + g : g;
            if (remap[slot] == ~0u) {
                remap[slot] = uint32_t(used.size());
                used.push_back({g, side});
            }
            m.triangles[t][k] = remap[slot];
        }
    }
    const size_t nvOut = used.size();
    m.positions.resize(nvOut);
    for (size_t i = 0; i < nvOut; ++i) m.positions[i] = glm::vec3(s.outPos(used[i].first));
    // Vertex paint: from the mesh the vertex lies on (a seam point: along the edge it is on).
    const bool colors = a.hasColors() || b.hasColors();
    if (colors) {
        m.colors.resize(nvOut);
        for (size_t i = 0; i < nvOut; ++i) {
            const uint32_t g = used[i].first;
            if (g < first) {
                const int side = g < s.nv[0] ? 0 : 1;
                m.colors[i] = s.M[side]->colorAt(g - s.base(side));
            } else {
                const XPt& x = s.pts[g - first];
                const Mesh& src = *s.M[x.key.side];
                m.colors[i] = glm::mix(src.colorAt(x.key.u), src.colorAt(x.key.v), float(x.lam));
            }
        }
    }
    const bool groups = a.hasFaceGroups() || b.hasFaceGroups();
    const bool uvs = a.hasUvs() || b.hasUvs();
    if (groups) m.facegroups.resize(m.triangles.size());
    if (uvs) m.uvs.assign(m.triangles.size() * 3, glm::vec2(0.0f));
    for (size_t t = 0; t < s.outTris.size(); ++t) {
        const int side = s.outParent[t] < int64_t(s.nt[0]) ? 0 : 1;
        const uint32_t pt = uint32_t(side == 0 ? s.outParent[t] : s.outParent[t] - int64_t(s.nt[0]));
        const Mesh& src = *s.M[side];
        if (groups) m.facegroups[t] = src.facegroupAt(pt);
        if (!uvs || !src.hasUvs()) continue;
        const glm::uvec3 st = src.triangles[pt];
        for (int k = 0; k < 3; ++k) {
            const uint32_t g = s.outTris[t][size_t(k)];
            dvec3 w(0.0);
            int corner = -1;
            for (int j = 0; j < 3; ++j)
                if (g == s.base(side) + st[j]) corner = j;
            if (corner >= 0) {
                w[corner] = 1.0;
            } else if (g >= first && s.pts[g - first].key.side == side) {
                const XPt& x = s.pts[g - first];
                for (int j = 0; j < 3; ++j) {
                    if (st[j] == x.key.u) w[j] += 1.0 - x.lam;
                    if (st[j] == x.key.v) w[j] += x.lam;
                }
            } else if (g >= first && s.pts[g - first].key.t == pt) {
                w = s.pts[g - first].bary;
            } else { // (a corner moved by the sliver clean-up)
                w = baryIn(s.outPos(g), s.W[side][st.x], s.W[side][st.y], s.W[side][st.z]);
            }
            glm::vec2 uv(0.0f);
            for (int j = 0; j < 3; ++j) uv += src.uvs[pt * 3 + size_t(j)] * float(w[j]);
            m.uvs[t * 3 + size_t(k)] = uv;
        }
    }
    trace()("attributes");
    m.rebuildTopology();
    m.recomputeAllNormals();
    m.sanitizeAttributes();
    trace()("topology");
    out = std::move(m);
    return true;
}

bool booleanVoxel(const Mesh& a, const Mesh& b, BooleanOp op, int resolution, Mesh& out, std::string& error) {
    if (a.triangleCount() == 0 || b.triangleCount() == 0) {
        error = "Boolean: a Part is empty";
        return false;
    }
    resolution = std::clamp(resolution, kVoxelResolutionMin, kVoxelDenseMax);
    AABB box = a.bounds();
    if (op == BooleanOp::Union) box.expand(b.bounds());
    VoxelGrid grid;
    if (!makeVoxelGrid(box, resolution, 4, grid)) {
        error = "Boolean: the voxel grid would be too big";
        return false;
    }
    std::vector<float> field[2];
    for (int k = 0; k < 2; ++k) {
        // An open operand (a neck, a cut) gets its borders capped first, as Voxel Remesh does.
        Mesh capped;
        const bool open = !closedMesh(k == 0 ? a : b);
        if (open) {
            capped.positions = (k == 0 ? a : b).positions;
            capped.triangles = (k == 0 ? a : b).triangles;
            CloseHolesOptions co;
            co.refine = false;
            co.newGroup = false;
            CloseHolesStats stats;
            std::string ignored;
            closeHoles(capped, co, stats, ignored);
        }
        const Mesh& m = open ? capped : (k == 0 ? a : b);
        VoxelGrid shell = grid;
        rasterizeMeshShell(m, shell);
        std::vector<uint8_t> solid = solidifyShell(shell, 0.5f, 1, true);
        // No inside (a sheet), or holes too big to close: the outside floods in and leaves little more than the shell.
        const double cells = std::abs(meshVolume(m)) / (double(grid.cell) * grid.cell * grid.cell);
        const size_t inside = size_t(std::count(solid.begin(), solid.end(), uint8_t(1)));
        if (open && (cells <= 64.0 || double(inside) < 0.5 * std::min(cells, double(solid.size())))) {
            error = std::string("Boolean: the ") + (k == 0 ? "first" : "second") +
                    " Part is open (a sheet, or holes too big to close), so it has no inside to combine. Close its holes first";
            return false;
        }
        field[k] = insideDistanceField(m, grid, solid, 3.0f);
    }
    std::vector<float>& f = field[0];
    for (size_t i = 0; i < f.size(); ++i) {
        const float x = f[i], y = field[1][i];
        f[i] = op == BooleanOp::Union ? std::max(x, y) : op == BooleanOp::Subtract ? std::min(x, -y) : std::min(x, y);
    }
    Mesh m = meshFromField(grid, f, 0.0f, 2);
    if (m.triangleCount() == 0) {
        error = op == BooleanOp::Intersect ? "Boolean: the Parts don't overlap, so Intersect leaves nothing"
                                           : "Boolean: that would leave nothing";
        return false;
    }
    out = std::move(m);
    return true;
}

bool booleanMeshes(const Mesh& a, const Mesh& b, BooleanOp op, Mesh& out, std::string& error, const BooleanOptions& options,
                   BooleanInfo* info) {
    BooleanInfo local;
    BooleanInfo& in = info ? *info : local;
    in = {};
    std::string why;
    if (booleanExact(a, b, op, out, why, options, &in)) return true;
    // Nothing left is an answer, not a failure of the exact path.
    if (!options.voxelFallback || why.find("leave") != std::string::npos) {
        error = why;
        return false;
    }
    if (!booleanVoxel(a, b, op, options.voxelResolution, out, error)) {
        error += " (" + why + ")";
        return false;
    }
    in.voxel = true;
    in.exactError = why;
    return true;
}

} // namespace sf
