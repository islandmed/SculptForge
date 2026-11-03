// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cut: an exact boolean between a mesh and a screen polygon extruded along the view (see Cut.h).
//
// Everything happens in normalised device coordinates (doubles), where the extrusion is a straight prism: walls
// standing on the polygon's edges and two caps at constant depth. Each wall and cap is a "face" of the prism.
// Intersection points come in two kinds, keyed so both sides of every edge find the same one:
//   A - a mesh edge crossing a prism face (key: the edge's vertices and the face),
//   B - a prism edge piercing a mesh triangle (key: the prism edge and the triangle).
// The segments where a triangle meets a face join those points. Each cut triangle and each prism face is then
// split along its segments into regions (a planar graph, holes bridged, ear-clipped), and every region is kept or
// dropped by which side of the other surface it lies on, read off locally at one of its segments. A mesh vertex
// lying exactly on the prism makes the arrangement degenerate, so the prism is nudged by a tiny random amount (and
// again, a bit more, whenever a test lands too close to call).
#include "core/Cut.h"

#include "core/Commands.h"
#include "core/Masking.h"
#include "core/PaintLayer.h"
#include "core/ScreenSelect.h"
#include "core/Sculpt.h"
#include "core/Document.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <cmath>
#include <numeric>
#include <random>
#include <unordered_map>

namespace sf {

const char* cutShapeName(CutShape s) {
    switch (s) {
    case CutShape::Line: return "line";
    case CutShape::Lasso: return "lasso";
    case CutShape::Rect: return "rect";
    case CutShape::Polyline: return "polyline";
    }
    return "lasso";
}

const char* cutModeName(CutMode m) {
    switch (m) {
    case CutMode::Away: return "away";
    case CutMode::In: return "in";
    case CutMode::Add: return "add";
    }
    return "away";
}

bool parseCutShape(const std::string& name, CutShape& out) {
    for (CutShape s : {CutShape::Line, CutShape::Lasso, CutShape::Rect, CutShape::Polyline})
        if (name == cutShapeName(s)) {
            out = s;
            return true;
        }
    if (name == "rectangle") {
        out = CutShape::Rect;
        return true;
    }
    return false;
}

bool parseCutMode(const std::string& name, CutMode& out) {
    for (CutMode m : {CutMode::Away, CutMode::In, CutMode::Add})
        if (name == cutModeName(m)) {
            out = m;
            return true;
        }
    return false;
}

namespace {

using glm::dvec2;
using glm::dvec3;
using glm::dvec4;

double cross2(dvec2 a, dvec2 b) { return a.x * b.y - a.y * b.x; }
double orient2(dvec2 a, dvec2 b, dvec2 c) { return cross2(b - a, c - a); }

double signedArea(const std::vector<dvec2>& p) {
    double a = 0.0;
    for (size_t i = 0; i < p.size(); ++i) a += cross2(p[i], p[(i + 1) % p.size()]);
    return 0.5 * a;
}

// Proper crossing of segments ab and cd (not at their ends); `t` = where along ab.
bool segmentsCross(dvec2 a, dvec2 b, dvec2 c, dvec2 d, double& t) {
    const double d1 = orient2(c, d, a), d2 = orient2(c, d, b), d3 = orient2(a, b, c), d4 = orient2(a, b, d);
    if (!(((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)))) return false;
    if (d1 == 0 || d2 == 0 || d3 == 0 || d4 == 0) return false;
    t = d1 / (d1 - d2);
    return true;
}

// Removes near-duplicate points, then cuts a self-crossing outline at its first crossing and keeps the larger
// loop, until it is simple. Counter-clockwise on return.
bool simplifyOutline(std::vector<dvec2>& p) {
    std::vector<dvec2> q;
    for (const dvec2& v : p)
        if (q.empty() || glm::length(v - q.back()) > 1e-7) q.push_back(v);
    while (q.size() > 1 && glm::length(q.front() - q.back()) <= 1e-7) q.pop_back();
    p.swap(q);
    for (size_t guard = 0; guard < 4096 && p.size() >= 3; ++guard) {
        const size_t n = p.size();
        bool crossed = false;
        for (size_t i = 0; i < n && !crossed; ++i)
            for (size_t j = i + 2; j < n && !crossed; ++j) {
                if (i == 0 && j == n - 1) continue; // neighbours through the closing edge
                double t;
                if (!segmentsCross(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n], t)) continue;
                const dvec2 x = p[i] + (p[(i + 1) % n] - p[i]) * t;
                std::vector<dvec2> a{x}, b{x};
                for (size_t k = i + 1; k <= j; ++k) a.push_back(p[k]);
                for (size_t k = j + 1; k < n; ++k) b.push_back(p[k]);
                for (size_t k = 0; k <= i; ++k) b.push_back(p[k]);
                p = std::abs(signedArea(a)) >= std::abs(signedArea(b)) ? a : b;
                crossed = true;
            }
        if (!crossed) break;
    }
    if (p.size() < 3) return false;
    if (signedArea(p) < 0) std::reverse(p.begin(), p.end());
    return std::abs(signedArea(p)) > 1e-10;
}

// A simple polygon with an even-odd inside test, edges bucketed into horizontal bands.
struct Poly2 {
    std::vector<dvec2> p;
    dvec2 lo{0.0}, hi{0.0};
    double bandH = 1.0;
    std::vector<std::vector<uint32_t>> bands;

    explicit Poly2(std::vector<dvec2> pts) : p(std::move(pts)) {
        lo = hi = p[0];
        for (const dvec2& v : p) {
            lo = glm::min(lo, v);
            hi = glm::max(hi, v);
        }
        const size_t nb = std::clamp<size_t>(p.size(), 1, 1024);
        bandH = std::max((hi.y - lo.y) / double(nb), 1e-300);
        bands.resize(nb);
        for (uint32_t i = 0; i < p.size(); ++i) {
            const dvec2 a = p[i], b = p[(i + 1) % p.size()];
            const size_t b0 = band(std::min(a.y, b.y)), b1 = band(std::max(a.y, b.y));
            for (size_t k = b0; k <= b1; ++k) bands[k].push_back(i);
        }
    }
    size_t band(double y) const {
        return size_t(std::clamp((y - lo.y) / bandH, 0.0, double(bands.size() - 1)));
    }
    bool contains(dvec2 q) const {
        if (q.x < lo.x || q.x > hi.x || q.y < lo.y || q.y > hi.y) return false;
        bool in = false;
        for (uint32_t i : bands[band(q.y)]) {
            const dvec2 a = p[i], b = p[(i + 1) % p.size()];
            if ((a.y > q.y) != (b.y > q.y) && q.x < a.x + (q.y - a.y) / (b.y - a.y) * (b.x - a.x)) in = !in;
        }
        return in;
    }
};

// --- Planar regions: a polygon split by segments, holes bridged to it --------------------------------------

struct PEdge {
    uint32_t a, b;
    int32_t tag; // what made the edge (the other surface), -1 = the polygon's own outline or a bridge
};

struct PRegion {
    std::vector<uint32_t> cycle; // counter-clockwise, interior on the left
    std::vector<int32_t> tags;   // tag of edge cycle[i] -> cycle[i + 1]
};

bool planarRegions(const std::vector<dvec2>& p, std::vector<PEdge> edges, std::vector<PRegion>& out) {
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
void earClip(const std::vector<dvec2>& p, const std::vector<uint32_t>& cyc, std::vector<std::array<uint32_t, 3>>& out) {
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

// --- The boolean -------------------------------------------------------------------------------------------

enum : uint8_t { kVertex, kA, kB, kCorner };

struct Pt {
    dvec3 ndc{0.0};
    uint8_t kind = kVertex;
    uint32_t u = 0, v = 0; // A: the edge (u < v); B: v = triangle; corner: u = prism corner id
    uint32_t e = 0;        // B: the prism edge
    double lam = 0.0;      // A: world-space position along u -> v
    dvec3 bary{0.0};       // B: world-space barycentric coordinates in triangle v
};

struct Seg {
    uint32_t p, q; // global point ids
    uint32_t other; // the prism face (on a triangle) or the triangle (on a face)
};

struct AKeyHash {
    size_t operator()(const std::pair<uint64_t, uint32_t>& k) const {
        return std::hash<uint64_t>()(k.first * 0x9E3779B97F4A7C15ull ^ k.second);
    }
};

struct Cutter {
    // Inputs.
    const Mesh& mesh;
    glm::dmat4 vp, inv;
    double s = 1.0; // orientation of the world -> NDC map
    std::vector<dvec2> r;
    double z0 = 0.0, z1 = 0.0;
    bool unionMode = false, caps = true;

    // State.
    uint32_t nv = 0, n = 0;
    std::vector<dvec3> N;  // NDC of each vertex
    std::vector<double> W; // clip w of each vertex
    std::vector<Pt> pts;   // the points that aren't mesh vertices (id nv + i)
    std::unordered_map<std::pair<uint64_t, uint32_t>, int64_t, AKeyHash> amap; // A key -> point id or -1
    std::unordered_map<uint32_t, std::vector<Seg>> triSegs;
    std::vector<std::vector<Seg>> faceSegs;
    bool degenerate = false;
    double eps = 1e-12, baryEps = 1e-11;
    // Wall grid.
    int G = 1;
    dvec2 glo{0.0}, ghi{0.0};
    std::vector<std::vector<uint32_t>> cells;
    std::unique_ptr<Poly2> poly;

    // Output: triangles as global ids, with the mesh triangle they came from (-1 = prism).
    std::vector<std::array<uint32_t, 3>> outTris;
    std::vector<int64_t> outParent;
    std::vector<uint8_t> splitParent; // per mesh triangle: it was cut
    size_t removedTriangles = 0, prismTriangles = 0;

    explicit Cutter(const Mesh& m) : mesh(m) {}

    uint32_t faceCount() const { return n + 2; }
    dvec3 ndcOf(uint32_t g) const { return g < nv ? N[g] : pts[g - nv].ndc; }
    dvec3 outward(uint32_t f) const {
        if (f == n) return {0, 0, -1};
        if (f == n + 1) return {0, 0, 1};
        const dvec2 e = glm::normalize(r[(f + 1) % n] - r[f]);
        return {e.y, -e.x, 0.0};
    }
    // Signed distance to face f's plane, positive towards the prism's inside.
    double dist(uint32_t f, const dvec3& x) const {
        if (f == n) return x.z - z0;
        if (f == n + 1) return z1 - x.z;
        const dvec2 a = r[f], e = r[(f + 1) % n] - a;
        return cross2(e, dvec2(x) - a) / glm::length(e);
    }
    // Whether a point on face f's plane lies on the face.
    bool onFace(uint32_t f, const dvec3& x) {
        if (f >= n) return poly->contains(dvec2(x));
        const dvec2 a = r[f], e = r[(f + 1) % n] - a;
        const double t = glm::dot(dvec2(x) - a, e) / glm::dot(e, e);
        const double te = baryEps;
        if (std::abs(t) < te || std::abs(t - 1) < te || std::abs(x.z - z0) < eps || std::abs(x.z - z1) < eps) degenerate = true;
        return t > 0 && t < 1 && x.z > z0 && x.z < z1;
    }
    bool insidePrism(const dvec3& x) const { return x.z > z0 && x.z < z1 && poly->contains(dvec2(x)); }
    // Prism edges: vertical k at corner k, bottom n + k and top 2n + k along wall k. Corners: bottom k, top n + k.
    uint32_t cornerId(uint32_t k, bool top) const { return nv + (top ? n : 0) + k % n; }
    std::pair<uint32_t, uint32_t> edgeEnds(uint32_t e) const {
        if (e < n) return {cornerId(e, false), cornerId(e, true)};
        if (e < 2 * n) return {cornerId(e - n, false), cornerId(e - n + 1, false)};
        return {cornerId(e - 2 * n, true), cornerId(e - 2 * n + 1, true)};
    }
    dvec3 unproject(const dvec3& ndc) const {
        const dvec4 w = inv * dvec4(ndc, 1.0);
        return dvec3(w) / w.w;
    }
    uint32_t addPoint(const Pt& p) {
        pts.push_back(p);
        return nv + uint32_t(pts.size() - 1);
    }

    int cell(double x, double lo, double hi) const {
        return std::clamp(int((x - lo) / std::max(hi - lo, 1e-300) * G), 0, G - 1);
    }

    // The A point where mesh edge (a, b) crosses face f's plane: its id, or -1 when the crossing is off the face.
    int64_t aPoint(uint32_t a, uint32_t b, uint32_t f, dvec3& pos) {
        const uint32_t u = std::min(a, b), v = std::max(a, b);
        const double du = dist(f, N[u]), dv = dist(f, N[v]);
        const double t = du / (du - dv);
        pos = N[u] + (N[v] - N[u]) * t;
        const auto key = std::make_pair((uint64_t(u) << 32) | v, f);
        if (auto it = amap.find(key); it != amap.end()) return it->second;
        int64_t id = -1;
        if (onFace(f, pos)) {
            Pt p;
            p.ndc = pos;
            p.kind = kA;
            p.u = u;
            p.v = v;
            p.lam = t * W[u] / ((1 - t) * W[v] + t * W[u]); // perspective-correct
            id = addPoint(p);
        }
        amap.emplace(key, id);
        return id;
    }

    void processTriangle(uint32_t t) {
        const glm::uvec3 tri = mesh.triangles[t];
        const dvec3 a = N[tri.x], b = N[tri.y], c = N[tri.z];
        const dvec2 lo = glm::min(glm::min(dvec2(a), dvec2(b)), dvec2(c)), hi = glm::max(glm::max(dvec2(a), dvec2(b)), dvec2(c));
        const double zmin = std::min({a.z, b.z, c.z}), zmax = std::max({a.z, b.z, c.z});
        if (zmax < z0 || zmin > z1 || hi.x < poly->lo.x || lo.x > poly->hi.x || hi.y < poly->lo.y || lo.y > poly->hi.y) return;
        // Candidate walls from the grid, then the caps the triangle straddles.
        std::vector<uint32_t> faces;
        const int x0 = cell(lo.x, glo.x, ghi.x), x1 = cell(hi.x, glo.x, ghi.x);
        const int y0 = cell(lo.y, glo.y, ghi.y), y1 = cell(hi.y, glo.y, ghi.y);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (uint32_t k : cells[size_t(y) * G + x]) faces.push_back(k);
        std::sort(faces.begin(), faces.end());
        faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
        const size_t walls = faces.size();
        if (zmin < z0 && zmax > z0) faces.push_back(n);
        if (zmin < z1 && zmax > z1) faces.push_back(n + 1);
        if (faces.empty()) return;
        const dvec3 rn = glm::cross(b - a, c - a);
        const double rn2 = glm::dot(rn, rn);
        if (rn2 <= 0) return;
        // B points: the prism edges of the candidate walls that pierce the triangle.
        std::vector<std::pair<uint32_t, uint32_t>> pierced; // (prism edge, point id)
        std::vector<uint32_t> tried;
        auto pierce = [&](uint32_t e) {
            if (std::find(tried.begin(), tried.end(), e) != tried.end()) return;
            tried.push_back(e);
            const auto [i0, i1] = edgeEnds(e);
            const dvec3 p0 = pts[i0 - nv].ndc, p1 = pts[i1 - nv].ndc;
            const double d0 = glm::dot(rn, p0 - a), d1 = glm::dot(rn, p1 - a);
            const double scale = std::sqrt(rn2) * eps;
            if (std::abs(d0) < scale || std::abs(d1) < scale) {
                // A prism corner on the triangle's plane: only a problem when it is on the triangle.
                const dvec3 x = std::abs(d0) < scale ? p0 : p1;
                const double l0 = glm::dot(glm::cross(b - x, c - x), rn) / rn2, l1 = glm::dot(glm::cross(c - x, a - x), rn) / rn2;
                if (l0 > -baryEps && l1 > -baryEps && 1 - l0 - l1 > -baryEps) degenerate = true;
                return;
            }
            if ((d0 > 0) == (d1 > 0)) return;
            const dvec3 x = p0 + (p1 - p0) * (d0 / (d0 - d1));
            const double l0 = glm::dot(glm::cross(b - x, c - x), rn) / rn2;
            const double l1 = glm::dot(glm::cross(c - x, a - x), rn) / rn2;
            const double l2 = 1.0 - l0 - l1;
            if (std::abs(l0) < baryEps || std::abs(l1) < baryEps || std::abs(l2) < baryEps) degenerate = true;
            if (l0 <= 0 || l1 <= 0 || l2 <= 0) return;
            Pt p;
            p.ndc = x;
            p.kind = kB;
            p.e = e;
            p.v = t;
            const dvec3 wl(l0 / W[tri.x], l1 / W[tri.y], l2 / W[tri.z]);
            p.bary = wl / (wl.x + wl.y + wl.z);
            pierced.push_back({e, addPoint(p)});
        };
        for (size_t i = 0; i < walls; ++i) {
            const uint32_t k = faces[i];
            for (uint32_t e : {k, (k + 1) % n, n + k, 2 * n + k}) pierce(e);
        }
        const uint32_t vi[3] = {tri.x, tri.y, tri.z};
        for (uint32_t f : faces) {
            double d[3];
            for (int i = 0; i < 3; ++i) {
                d[i] = dist(f, N[vi[i]]);
                if (std::abs(d[i]) < eps) degenerate = true;
            }
            if ((d[0] > 0) == (d[1] > 0) && (d[1] > 0) == (d[2] > 0)) continue;
            // The two triangle edges the plane crosses.
            struct X {
                dvec3 pos;
                int64_t id;
            } xs[2];
            int nx = 0;
            for (int i = 0; i < 3 && nx < 2; ++i) {
                const int j = (i + 1) % 3;
                if ((d[i] > 0) == (d[j] > 0)) continue;
                xs[nx].id = aPoint(vi[i], vi[j], f, xs[nx].pos);
                ++nx;
            }
            if (nx != 2) {
                degenerate = true;
                return;
            }
            // The face's own edges through the triangle.
            std::vector<std::pair<double, uint32_t>> bs;
            const dvec3 dir = xs[1].pos - xs[0].pos;
            const double dd = glm::dot(dir, dir);
            for (const auto& [e, id] : pierced) {
                const bool mine = f < n ? (e == f || e == (f + 1) % n || e == n + f || e == 2 * n + f)
                                        : (f == n ? (e >= n && e < 2 * n) : e >= 2 * n);
                if (mine) bs.push_back({dd > 0 ? glm::dot(pts[id - nv].ndc - xs[0].pos, dir) / dd : 0.0, id});
            }
            if (xs[0].id < 0 && xs[1].id < 0 && bs.empty()) continue;
            std::sort(bs.begin(), bs.end());
            bool in = xs[0].id >= 0;
            uint32_t cur = in ? uint32_t(xs[0].id) : 0;
            auto seg = [&](uint32_t p, uint32_t q) {
                triSegs[t].push_back({p, q, f});
                faceSegs[f].push_back({p, q, t});
            };
            for (const auto& [param, id] : bs) {
                if (in) seg(cur, id);
                else cur = id;
                in = !in;
            }
            if (in != (xs[1].id >= 0)) {
                degenerate = true;
                return;
            }
            if (in) seg(cur, uint32_t(xs[1].id));
        }
    }

    // Splits one mesh triangle along its segments; keeps the regions outside the prism.
    void splitTriangle(uint32_t t, const std::vector<Seg>& segs) {
        const glm::uvec3 tri = mesh.triangles[t];
        const uint32_t vi[3] = {tri.x, tri.y, tri.z};
        const dvec3 rn = glm::cross(N[tri.y] - N[tri.x], N[tri.z] - N[tri.x]);
        // 2D frame: drop the normal's largest axis, keeping counter-clockwise = rn.
        int axis = 0;
        if (std::abs(rn.y) > std::abs(rn[axis])) axis = 1;
        if (std::abs(rn.z) > std::abs(rn[axis])) axis = 2;
        int ia = (axis + 1) % 3, ib = (axis + 2) % 3;
        if (rn[axis] < 0) std::swap(ia, ib);
        std::vector<uint32_t> ids;
        std::unordered_map<uint32_t, uint32_t> local;
        auto loc = [&](uint32_t g) {
            auto [it, fresh] = local.emplace(g, uint32_t(ids.size()));
            if (fresh) ids.push_back(g);
            return it->second;
        };
        for (uint32_t v : vi) loc(v);
        std::vector<PEdge> edges;
        // Outline: each side with the A points on it, in order.
        for (int i = 0; i < 3; ++i) {
            const uint32_t u = vi[i], w = vi[(i + 1) % 3];
            std::vector<std::pair<double, uint32_t>> on;
            for (const Seg& s : segs)
                for (uint32_t g : {s.p, s.q})
                    if (g >= nv && pts[g - nv].kind == kA && pts[g - nv].u == std::min(u, w) && pts[g - nv].v == std::max(u, w))
                        on.push_back({glm::dot(pts[g - nv].ndc - N[u], N[w] - N[u]), g});
            std::sort(on.begin(), on.end());
            on.erase(std::unique(on.begin(), on.end()), on.end());
            uint32_t prev = loc(u);
            for (const auto& [param, g] : on) {
                const uint32_t l = loc(g);
                edges.push_back({prev, l, -1});
                prev = l;
            }
            edges.push_back({prev, loc(w), -1});
        }
        for (const Seg& s : segs) edges.push_back({loc(s.p), loc(s.q), int32_t(s.other)});
        std::vector<dvec2> p2(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const dvec3 x = ndcOf(ids[i]);
            p2[i] = {x[ia], x[ib]};
        }
        std::vector<PRegion> regions;
        if (!planarRegions(p2, edges, regions)) {
            degenerate = true;
            return;
        }
        std::vector<std::array<uint32_t, 3>> tris;
        for (const PRegion& reg : regions) {
            int which = -1;
            for (size_t k = 0; k < reg.tags.size() && which < 0; ++k)
                if (reg.tags[k] >= 0) which = int(k);
            if (which < 0) {
                degenerate = true;
                return;
            }
            const dvec3 P = ndcOf(ids[reg.cycle[which]]), Q = ndcOf(ids[reg.cycle[(which + 1) % reg.cycle.size()]]);
            const bool inside = glm::dot(glm::cross(rn, Q - P), outward(uint32_t(reg.tags[which]))) < 0;
            if (inside) continue;
            tris.clear();
            earClip(p2, reg.cycle, tris);
            for (const auto& tr : tris) {
                outTris.push_back({ids[tr[0]], ids[tr[1]], ids[tr[2]]});
                outParent.push_back(t);
            }
        }
    }

    // Face f's outline: its prism edges in order (counter-clockwise around its outward normal), with direction.
    std::vector<std::pair<uint32_t, bool>> faceEdges(uint32_t f) const {
        std::vector<std::pair<uint32_t, bool>> out; // (edge, forward)
        if (f < n) {
            out = {{n + f, true}, {(f + 1) % n, true}, {2 * n + f, false}, {f, false}};
        } else if (f == n + 1) {
            for (uint32_t k = 0; k < n; ++k) out.push_back({2 * n + k, true});
        } else {
            for (uint32_t k = n; k-- > 0;) out.push_back({n + k, false});
        }
        return out;
    }
    dvec2 face2d(uint32_t f, const dvec3& x) const {
        if (f == n + 1) return {x.x, x.y};
        if (f == n) return {x.x, -x.y};
        const dvec2 a = r[f], e = glm::normalize(r[(f + 1) % n] - a);
        return {glm::dot(dvec2(x) - a, e), x.z};
    }

    // Splits face f along its segments. Returns, per prism edge on its outline with no B points, the status of the
    // region along it (1 inside the mesh, 0 outside) in `edgeStatus`. Keeps the regions the mode wants.
    void splitFace(uint32_t f, std::vector<int8_t>& edgeStatus) {
        const std::vector<Seg>& segs = faceSegs[f];
        std::vector<uint32_t> ids;
        std::unordered_map<uint32_t, uint32_t> local;
        auto loc = [&](uint32_t g) {
            auto [it, fresh] = local.emplace(g, uint32_t(ids.size()));
            if (fresh) ids.push_back(g);
            return it->second;
        };
        std::vector<PEdge> edges;
        std::vector<std::pair<uint64_t, uint32_t>> plainEdges; // (local a, local b) -> prism edge, for edgeStatus
        const auto outline = faceEdges(f);
        loc(outline.empty() ? 0 : (outline[0].second ? edgeEnds(outline[0].first).first : edgeEnds(outline[0].first).second));
        for (const auto& [e, fwd] : outline) {
            const auto [e0, e1] = edgeEnds(e);
            const uint32_t from = fwd ? e0 : e1, to = fwd ? e1 : e0;
            const dvec3 A = pts[from - nv].ndc, B = pts[to - nv].ndc;
            std::vector<std::pair<double, uint32_t>> on;
            for (const Seg& s : segs)
                for (uint32_t g : {s.p, s.q})
                    if (g >= nv && pts[g - nv].kind == kB && pts[g - nv].e == e) on.push_back({glm::dot(pts[g - nv].ndc - A, B - A), g});
            std::sort(on.begin(), on.end());
            on.erase(std::unique(on.begin(), on.end()), on.end());
            uint32_t prev = loc(from);
            for (const auto& [param, g] : on) {
                const uint32_t l = loc(g);
                edges.push_back({prev, l, -1});
                prev = l;
            }
            edges.push_back({prev, loc(to), -1});
            if (on.empty()) plainEdges.push_back({(uint64_t(loc(from)) << 32) | loc(to), e});
        }
        for (const Seg& s : segs) edges.push_back({loc(s.p), loc(s.q), int32_t(s.other)});
        std::vector<dvec2> p2(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) p2[i] = face2d(f, ndcOf(ids[i]));
        std::vector<PRegion> regions;
        if (!planarRegions(p2, edges, regions)) {
            degenerate = true;
            return;
        }
        const dvec3 out = outward(f);
        std::vector<std::array<uint32_t, 3>> tris;
        for (const PRegion& reg : regions) {
            int which = -1;
            for (size_t k = 0; k < reg.tags.size() && which < 0; ++k)
                if (reg.tags[k] >= 0) which = int(k);
            if (which < 0) {
                degenerate = true;
                return;
            }
            const uint32_t t = uint32_t(reg.tags[which]);
            const glm::uvec3 tri = mesh.triangles[t];
            const dvec3 nt = s * glm::cross(N[tri.y] - N[tri.x], N[tri.z] - N[tri.x]);
            const dvec3 P = ndcOf(ids[reg.cycle[which]]), Q = ndcOf(ids[reg.cycle[(which + 1) % reg.cycle.size()]]);
            const bool insideMesh = glm::dot(glm::cross(out, Q - P), nt) < 0;
            for (size_t k = 0; k < reg.cycle.size(); ++k) {
                const uint64_t key = (uint64_t(reg.cycle[k]) << 32) | reg.cycle[(k + 1) % reg.cycle.size()];
                for (const auto& [pk, e] : plainEdges)
                    if (pk == key) edgeStatus[e] = insideMesh ? 1 : 0;
            }
            if (insideMesh == unionMode) continue;
            tris.clear();
            earClip(p2, reg.cycle, tris);
            emitPrism(ids, tris);
        }
    }

    void emitPrism(const std::vector<uint32_t>& ids, const std::vector<std::array<uint32_t, 3>>& tris) {
        // Counter-clockwise around the outward normal in NDC; the world winding flips with s, and a difference
        // keeps the prism's inside-out surface.
        const bool flip = unionMode ? s < 0 : s > 0;
        for (const auto& tr : tris) {
            if (flip) outTris.push_back({ids[tr[0]], ids[tr[2]], ids[tr[1]]});
            else outTris.push_back({ids[tr[0]], ids[tr[1]], ids[tr[2]]});
            outParent.push_back(-1);
            ++prismTriangles;
        }
    }

    // Winding number of the mesh around an NDC point (1 inside a closed mesh, 0 outside).
    double winding(const dvec3& x) const {
        double sum = 0.0;
        for (const glm::uvec3& tri : mesh.triangles) {
            const dvec3 A = N[tri.x] - x, B = N[tri.y] - x, C = N[tri.z] - x;
            const double la = glm::length(A), lb = glm::length(B), lc = glm::length(C);
            const double num = glm::dot(A, glm::cross(B, C));
            const double den = la * lb * lc + glm::dot(A, B) * lc + glm::dot(B, C) * la + glm::dot(C, A) * lb;
            sum += 2.0 * std::atan2(num, den);
        }
        return s * sum / (4.0 * 3.14159265358979323846);
    }

    // One attempt; false when degenerate (try again with another nudge).
    bool run() {
        pts.clear();
        amap.clear();
        triSegs.clear();
        outTris.clear();
        outParent.clear();
        degenerate = false;
        removedTriangles = prismTriangles = 0;
        n = uint32_t(r.size());
        poly = std::make_unique<Poly2>(r);
        faceSegs.assign(faceCount(), {});
        for (uint32_t k = 0; k < 2 * n; ++k) {
            Pt p;
            p.kind = kCorner;
            p.u = k;
            p.ndc = dvec3(r[k % n], k < n ? z0 : z1);
            addPoint(p);
        }
        // Wall grid over the shape's box, clipped to the mesh's.
        dvec2 mlo(1e300), mhi(-1e300);
        for (const dvec3& x : N) {
            mlo = glm::min(mlo, dvec2(x));
            mhi = glm::max(mhi, dvec2(x));
        }
        glo = glm::max(poly->lo, mlo);
        ghi = glm::min(poly->hi, mhi);
        G = std::clamp(int(std::sqrt(double(n)) * 2.0), 4, 256);
        cells.assign(size_t(G) * G, {});
        for (uint32_t k = 0; k < n; ++k) {
            const dvec2 a = r[k], b = r[(k + 1) % n];
            const int x0 = cell(std::min(a.x, b.x), glo.x, ghi.x), x1 = cell(std::max(a.x, b.x), glo.x, ghi.x);
            const int y0 = cell(std::min(a.y, b.y), glo.y, ghi.y), y1 = cell(std::max(a.y, b.y), glo.y, ghi.y);
            if (std::max(a.x, b.x) < glo.x || std::min(a.x, b.x) > ghi.x || std::max(a.y, b.y) < glo.y || std::min(a.y, b.y) > ghi.y)
                continue;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) cells[size_t(y) * G + x].push_back(k);
        }
        const uint32_t nt = uint32_t(mesh.triangleCount());
        for (uint32_t t = 0; t < nt && !degenerate; ++t) processTriangle(t);
        if (degenerate) return false;

        splitParent.assign(nt, 0);
        for (auto& [t, segs] : triSegs) {
            splitParent[t] = 1;
            ++removedTriangles; // counted again below if a piece survives; only used to tell "nothing happened"
            splitTriangle(t, segs);
            if (degenerate) return false;
        }
        const bool anySegments = !triSegs.empty();
        // Untouched triangles: entirely on one side.
        for (uint32_t t = 0; t < nt; ++t) {
            if (splitParent[t]) continue;
            const glm::uvec3 tri = mesh.triangles[t];
            if (insidePrism((N[tri.x] + N[tri.y] + N[tri.z]) / 3.0)) {
                ++removedTriangles;
                continue;
            }
            outTris.push_back({tri.x, tri.y, tri.z});
            outParent.push_back(t);
        }
        if (!caps) return true;
        // Prism faces: split the cut ones; the rest take the status of a neighbour across an uncut edge.
        std::vector<int8_t> edgeStatus(3 * n, -1);
        std::vector<int8_t> faceStatus(faceCount(), -1);
        for (uint32_t f = 0; f < faceCount(); ++f)
            if (!faceSegs[f].empty()) {
                splitFace(f, edgeStatus);
                if (degenerate) return false;
                faceStatus[f] = 2; // done
            }
        if (!anySegments) {
            const int8_t st = winding(pts[0].ndc) > 0.5 ? 1 : 0;
            std::fill(faceStatus.begin(), faceStatus.end(), st);
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (uint32_t f = 0; f < faceCount(); ++f) {
                if (faceStatus[f] != -1) continue;
                for (const auto& [e, fwd] : faceEdges(f))
                    if (edgeStatus[e] >= 0) {
                        faceStatus[f] = edgeStatus[e];
                        changed = true;
                        break;
                    }
                if (faceStatus[f] >= 0)
                    for (const auto& [e, fwd] : faceEdges(f)) edgeStatus[e] = faceStatus[f];
            }
        }
        for (uint32_t f = 0; f < faceCount(); ++f) {
            if (faceStatus[f] == 2) continue;
            if (faceStatus[f] < 0) return false;
            if ((faceStatus[f] == 1) == unionMode) continue;
            std::vector<uint32_t> ids, cyc;
            std::vector<dvec2> p2;
            for (const auto& [e, fwd] : faceEdges(f)) {
                const auto [e0, e1] = edgeEnds(e);
                ids.push_back(fwd ? e0 : e1);
                cyc.push_back(uint32_t(cyc.size()));
                p2.push_back(face2d(f, pts[ids.back() - nv].ndc));
            }
            std::vector<std::array<uint32_t, 3>> tris;
            earClip(p2, cyc, tris);
            emitPrism(ids, tris);
        }
        return true;
    }
};

bool closedMesh(const Mesh& mesh) {
    std::unordered_map<uint64_t, int> count;
    count.reserve(mesh.triangleCount() * 3);
    for (const glm::uvec3& t : mesh.triangles)
        for (int i = 0; i < 3; ++i) {
            const uint32_t a = t[i], b = t[(i + 1) % 3];
            count[(uint64_t(a) << 32) | b] += 1;
        }
    for (const auto& [key, c] : count) {
        const uint64_t twin = (key << 32) | (key >> 32);
        auto it = count.find(twin);
        if (c != 1 || it == count.end() || it->second != 1) return false;
    }
    return !mesh.triangles.empty();
}

} // namespace

bool isWatertight(const Mesh& mesh) { return closedMesh(mesh); }

bool cutRegion(CutShape shape, const std::vector<glm::vec2>& points, bool flip, bool closed,
               std::vector<glm::dvec2>& region, std::string& error) {
    region.clear();
    constexpr double kFar = 1000.0; // NDC: far beyond any visible model
    auto side = [&](dvec2 d) { return flip ? dvec2(d.y, -d.x) : dvec2(-d.y, d.x); };
    if (shape == CutShape::Line || (shape == CutShape::Polyline && !closed)) {
        std::vector<dvec2> p;
        for (const glm::vec2& v : points)
            if (p.empty() || glm::length(dvec2(v) - p.back()) > 1e-6) p.push_back(dvec2(v));
        if (shape == CutShape::Line && p.size() > 2) p = {p.front(), p.back()};
        if (p.size() < 2) {
            error = "Cut: draw a line (two different points)";
            return false;
        }
        const dvec2 d0 = glm::normalize(p[1] - p[0]), d1 = glm::normalize(p.back() - p[p.size() - 2]);
        const dvec2 start = p.front() - d0 * kFar, end = p.back() + d1 * kFar;
        region.push_back(start);
        if (shape == CutShape::Polyline) region.insert(region.end(), p.begin(), p.end()); // (a line's are on the way)
        region.push_back(end);
        region.push_back(end + side(d1) * kFar);
        region.push_back(start + side(d0) * kFar);
    } else {
        for (const glm::vec2& v : points) region.push_back(dvec2(v));
        if (shape == CutShape::Rect && region.size() == 2) {
            const dvec2 a = region[0], b = region[1];
            region = {a, {b.x, a.y}, b, {a.x, b.y}};
        }
        if (region.size() < 3) {
            error = "Cut: the shape needs at least three points";
            return false;
        }
    }
    if (!simplifyOutline(region)) {
        error = "Cut: the shape encloses no area";
        return false;
    }
    return true;
}

bool cutMesh(const Mesh& mesh, const glm::mat4& viewProj, const std::vector<glm::dvec2>& region, CutMode mode,
             float depth, Mesh& out, std::string& error) {
    if (mesh.triangleCount() == 0) {
        error = "Cut: the Part is empty";
        return false;
    }
    if (region.size() < 3) {
        error = "Cut: the shape needs at least three points";
        return false;
    }
    Cutter c(mesh);
    c.vp = glm::dmat4(viewProj);
    c.inv = glm::inverse(c.vp);
    c.s = glm::determinant(c.vp) < 0 ? -1.0 : 1.0;
    c.nv = uint32_t(mesh.vertexCount());
    c.N.resize(c.nv);
    c.W.resize(c.nv);
    double zmin = 1e300, zmax = -1e300;
    dvec2 lo(1e300), hi(-1e300);
    for (uint32_t v = 0; v < c.nv; ++v) {
        const dvec4 clip = c.vp * dvec4(dvec3(mesh.positions[v]), 1.0);
        if (clip.w <= 1e-12) {
            error = "Cut: part of the Part is behind the camera; step back and try again";
            return false;
        }
        c.W[v] = clip.w;
        c.N[v] = dvec3(clip) / clip.w;
        zmin = std::min(zmin, c.N[v].z);
        zmax = std::max(zmax, c.N[v].z);
        lo = glm::min(lo, dvec2(c.N[v]));
        hi = glm::max(hi, dvec2(c.N[v]));
    }
    const double extent = std::max({hi.x - lo.x, hi.y - lo.y, 1e-9});
    c.eps = 1e-12 * std::max(1.0, extent);
    const bool closed = closedMesh(mesh);
    c.unionMode = mode == CutMode::Add;
    c.caps = closed;
    if (c.unionMode && !closed) {
        error = "Cut Add needs a closed (watertight) Part; remesh it first";
        return false;
    }
    // Depth range of the extrusion.
    const Poly2 shape(region);
    double zFront = 1e300, zBack = -1e300;
    dvec3 frontHit(0.0), backHit(0.0); // world points of the nearest and farthest surface under the shape
    for (uint32_t v = 0; v < c.nv; ++v)
        if (shape.contains(dvec2(c.N[v]))) {
            if (c.N[v].z < zFront) {
                zFront = c.N[v].z;
                frontHit = dvec3(mesh.positions[v]);
            }
            if (c.N[v].z > zBack) {
                zBack = c.N[v].z;
                backHit = dvec3(mesh.positions[v]);
            }
        }
    // A shape inside one big triangle covers no vertex: also sample the surface under the shape's corners and
    // centre (front and back hits, interpolated in the triangles there).
    {
        std::vector<dvec2> samples;
        const size_t stride = std::max<size_t>(1, region.size() / 48);
        dvec2 centre(0.0);
        for (size_t i = 0; i < region.size(); ++i) {
            centre += region[i];
            if (i % stride == 0 && shape.contains(region[i] * 0.999 + region[(i + 1) % region.size()] * 0.0005 +
                                                  region[(i + region.size() - 1) % region.size()] * 0.0005))
                samples.push_back(region[i]);
        }
        centre /= double(region.size());
        if (shape.contains(centre)) samples.push_back(centre);
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
            const glm::uvec3 tri = mesh.triangles[t];
            const dvec3 a = c.N[tri.x], b = c.N[tri.y], cc = c.N[tri.z];
            const double area = orient2(dvec2(a), dvec2(b), dvec2(cc));
            if (area == 0.0) continue;
            const dvec2 tlo = glm::min(glm::min(dvec2(a), dvec2(b)), dvec2(cc)), thi = glm::max(glm::max(dvec2(a), dvec2(b)), dvec2(cc));
            for (const dvec2& q : samples) {
                if (q.x < tlo.x || q.x > thi.x || q.y < tlo.y || q.y > thi.y) continue;
                const double l0 = orient2(dvec2(b), dvec2(cc), q) / area, l1 = orient2(dvec2(cc), dvec2(a), q) / area;
                const double l2 = 1.0 - l0 - l1;
                if (l0 < 0 || l1 < 0 || l2 < 0) continue;
                const double z = l0 * a.z + l1 * b.z + l2 * cc.z;
                if (z < zFront) {
                    zFront = z;
                    frontHit = c.unproject(dvec3(q, z));
                }
                if (z > zBack) {
                    zBack = z;
                    backHit = c.unproject(dvec3(q, z));
                }
            }
        }
    }
    const double margin = 1e-3 * std::max(zmax - zmin, 1e-6);
    double z0 = zmin - margin, z1 = zmax + margin;
    if (mode != CutMode::Away) {
        AABB box;
        for (const glm::vec3& p : mesh.positions) box.expand(p);
        const double size = double(std::max({box.extent().x, box.extent().y, box.extent().z}));
        const double d = std::clamp(double(depth), 1e-4, 4.0) * size;
        const dvec3 forward = glm::normalize(c.unproject({0, 0, 0.5}) - c.unproject({0, 0, -0.5}));
        auto ndcZ = [&](const dvec3& p, bool& ok) {
            const dvec4 clip = c.vp * dvec4(p, 1.0);
            ok = clip.w > 1e-12;
            return clip.z / clip.w;
        };
        bool ok = true;
        if (zFront > 1e299) {
            if (mode == CutMode::In) {
                error = "Cut: the shape misses the Part";
                return false;
            }
            const dvec3 mid = dvec3(box.center());
            z0 = ndcZ(mid - forward * (d * 0.5), ok);
            z1 = ndcZ(mid + forward * (d * 0.5), ok);
        } else if (mode == CutMode::In) {
            z1 = ndcZ(frontHit + forward * d, ok);
        } else {
            const dvec3 pf = frontHit, pb = backHit;
            z0 = ndcZ(pf - forward * d, ok);
            z1 = std::max(ndcZ((pf + pb) * 0.5, ok), ndcZ(pf + forward * (d * 0.25), ok));
        }
        if (!ok || !(z1 > z0)) {
            error = "Cut: the depth reaches past the camera; lower Depth or step back";
            return false;
        }
    }
    std::mt19937 rng(12345);
    for (int attempt = 0; attempt < 8; ++attempt) {
        const double amp = 1e-7 * extent * std::pow(3.0, attempt); // at most ~2e-4 of the view
        std::uniform_real_distribution<double> jitter(-amp, amp);
        c.r = region;
        for (dvec2& p : c.r) p += dvec2(jitter(rng), jitter(rng));
        c.z0 = z0 + jitter(rng) * 1e-3;
        c.z1 = z1 + jitter(rng) * 1e-3;
        if (!c.run()) continue;
        // Output vertices: every id the triangles use, in order.
        std::vector<uint32_t> remap(c.nv + c.pts.size(), ~0u), used;
        for (const auto& t : c.outTris)
            for (uint32_t g : t)
                if (remap[g] == ~0u) {
                    remap[g] = uint32_t(used.size());
                    used.push_back(g);
                }
        const bool changed = c.prismTriangles > 0 || c.outTris.size() != mesh.triangleCount() || used.size() != mesh.vertexCount();
        if (!changed) {
            error = "Cut: the shape misses the Part";
            return false;
        }
        if (c.outTris.empty()) {
            error = "Cut: that would remove the whole Part";
            return false;
        }
        Mesh m;
        const size_t nvOut = used.size(), ntOut = c.outTris.size();
        m.positions.resize(nvOut);
        const bool hasLayers = !mesh.layers.empty();
        if (hasLayers) {
            m.base.resize(nvOut);
            m.layers = mesh.layers;
            for (SculptLayer& l : m.layers) l.delta.assign(nvOut, glm::vec3(0.0f));
        }
        if (mesh.hasMask()) m.mask.assign(nvOut, 0.0f);
        glm::vec3 capColor(1.0f);
        if (mesh.hasColors()) {
            m.colors.assign(nvOut, glm::vec3(1.0f));
            glm::dvec3 sum(0.0);
            size_t k = 0;
            for (const Pt& p : c.pts)
                if (p.kind == kA) {
                    sum += dvec3(glm::mix(mesh.colors[p.u], mesh.colors[p.v], float(p.lam)));
                    ++k;
                }
            if (k) capColor = glm::vec3(sum / double(k));
        }
        std::vector<uint32_t> kept(nvOut, 0);
        // Weights of the source vertices for each output vertex.
        auto weights = [&](uint32_t g, std::array<std::pair<uint32_t, float>, 3>& w) {
            if (g < c.nv) {
                w = {{{g, 1.0f}, {g, 0.0f}, {g, 0.0f}}};
                return true;
            }
            const Pt& p = c.pts[g - c.nv];
            if (p.kind == kA) {
                w = {{{p.u, float(1 - p.lam)}, {p.v, float(p.lam)}, {p.v, 0.0f}}};
                return true;
            }
            if (p.kind == kB) {
                const glm::uvec3 tri = mesh.triangles[p.v];
                w = {{{tri.x, float(p.bary.x)}, {tri.y, float(p.bary.y)}, {tri.z, float(p.bary.z)}}};
                return true;
            }
            return false;
        };
        const std::vector<glm::vec3>& base = mesh.basePositions();
        for (size_t i = 0; i < nvOut; ++i) {
            const uint32_t g = used[i];
            std::array<std::pair<uint32_t, float>, 3> w;
            if (!weights(g, w)) { // a prism corner
                m.positions[i] = glm::vec3(c.unproject(c.pts[g - c.nv].ndc));
                if (hasLayers) m.base[i] = m.positions[i];
                if (mesh.hasColors()) m.colors[i] = capColor;
                continue;
            }
            glm::dvec3 pos(0.0), bp(0.0);
            float best = -1.0f;
            for (const auto& [v, wt] : w) {
                pos += dvec3(mesh.positions[v]) * double(wt);
                if (hasLayers) bp += dvec3(base[v]) * double(wt);
                if (wt > best) {
                    best = wt;
                    kept[i] = v;
                }
            }
            m.positions[i] = glm::vec3(pos);
            if (hasLayers) {
                m.base[i] = glm::vec3(bp);
                for (size_t l = 0; l < m.layers.size(); ++l) {
                    glm::vec3 d(0.0f);
                    for (const auto& [v, wt] : w) d += mesh.layers[l].delta[v] * wt;
                    m.layers[l].delta[i] = d;
                }
            }
            if (mesh.hasMask()) {
                float mk = 0.0f;
                for (const auto& [v, wt] : w) mk += mesh.mask[v] * wt;
                m.mask[i] = mk;
            }
            if (mesh.hasColors()) {
                glm::vec3 col(0.0f);
                for (const auto& [v, wt] : w) col += mesh.colors[v] * wt;
                m.colors[i] = col;
            }
        }
        m.triangles.resize(ntOut);
        uint32_t capGroup = 0;
        if (c.prismTriangles > 0) {
            for (uint32_t gid : mesh.polygroups) capGroup = std::max(capGroup, gid);
            capGroup += 1;
            m.ensurePolygroups(0);
        } else if (mesh.hasPolygroups()) {
            m.polygroups.resize(ntOut);
        }
        if (mesh.hasHidden()) m.hidden.assign(ntOut, 0);
        if (mesh.hasUvs()) m.uvs.assign(ntOut * 3, glm::vec2(0.0f));
        if (mesh.hasUvFlags()) m.uvFlags.assign(ntOut * 3, 0);
        std::vector<uint32_t> newIndex(mesh.triangleCount(), Mesh::kNoQuad);
        for (size_t t = 0; t < ntOut; ++t) {
            const auto& tr = c.outTris[t];
            m.triangles[t] = glm::uvec3(remap[tr[0]], remap[tr[1]], remap[tr[2]]);
            const int64_t parent = c.outParent[t];
            if (parent < 0) {
                if (!m.polygroups.empty()) m.polygroups[t] = capGroup;
                continue;
            }
            const uint32_t pt = uint32_t(parent);
            if (!m.polygroups.empty()) m.polygroups[t] = mesh.polygroupAt(pt);
            if (mesh.hasHidden()) m.hidden[t] = mesh.hidden[pt];
            if (!c.splitParent[pt]) {
                newIndex[pt] = uint32_t(t);
                if (mesh.hasUvs())
                    for (int k = 0; k < 3; ++k) m.uvs[t * 3 + k] = mesh.uvs[pt * 3 + k];
                if (mesh.hasUvFlags())
                    for (int k = 0; k < 3; ++k) m.uvFlags[t * 3 + k] = mesh.uvFlags[pt * 3 + k];
                continue;
            }
            if (mesh.hasUvs()) {
                const glm::uvec3 src = mesh.triangles[pt];
                for (int k = 0; k < 3; ++k) {
                    std::array<std::pair<uint32_t, float>, 3> w;
                    if (!weights(tr[k], w)) continue;
                    glm::vec2 uv(0.0f);
                    for (const auto& [v, wt] : w)
                        for (int j = 0; j < 3; ++j)
                            if (src[j] == v) uv += mesh.uvs[pt * 3 + j] * wt;
                    m.uvs[t * 3 + k] = uv;
                }
            }
        }
        if (mesh.hasQuads()) remapQuads(mesh, newIndex, m);
        m.texture = mesh.texture;
        m.paintToTexture = mesh.paintToTexture;
        m.selectionSets = mesh.selectionSets;
        if (!mesh.paintLayers.empty()) copyPaintLayers(mesh, m, kept);
        m.rebuildTopology();
        m.recomputeAllNormals();
        m.sanitizeAttributes();
        if (closed && !closedMesh(m)) continue; // a crack: try again with another nudge
        out = std::move(m);
        return true;
    }
    error = "Cut: the shape runs too close to the Part's vertices; draw it again slightly differently";
    return false;
}

// --- Command -----------------------------------------------------------------------------------------------

void registerCutCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    registry.add(
        {"cut",
         "Cut the active Part with a shape drawn on the screen (Line, Lasso, Rectangle or Polyline), extruded along "
         "the view. Modes: away (remove what the shape covers, through the whole Part, and close it with a flat cap), "
         "in (cut a flat-bottomed pocket `depth` deep), add (add the shape extruded `depth` out of the surface). The "
         "result stays watertight. Coordinates are normalised (-1..1, y up) in the given view; without a view the "
         "shape is drawn on the Part seen from the front (+Z).",
         {{"shape", T::String, "line, lasso, rect or polyline. Default lasso.", false},
          {"points", T::String, "The shape's points \"x0 y0 x1 y1 ...\": two for a line (or a rect's corners), three or "
                                "more for a lasso, two or more for a polyline.", true},
          {"mode", T::String, "away, in or add. Default away.", false},
          {"depth", T::Number, "In and add: how deep or tall, as a fraction of the Part's size (0.001-4). Default 0.1.", false},
          {"flip", T::Bool, "Line and open polyline: cut the right side instead of the left. Default false.", false},
          {"closed", T::Bool, "Polyline: a closed polygon (cuts its inside) instead of a bent line. Default false.", false},
          {"view_proj", T::String, "Optional 16 numbers: column-major view-projection matrix.", false},
          {"symmetry", T::String, "Mirror axes, e.g. \"x\": the mirror image of the shape cuts too (default none).", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.mesh.triangleCount() == 0) return {false, "The Part is empty"};
             if (!doc.mesh.zspheres.empty()) return {false, "Cut: make Forge Spheres into a mesh first"};
             if (doc.mesh.micro) return {false, "Cut: apply or remove the micro level first"};
             CutShape shape = CutShape::Lasso;
             CutMode mode = CutMode::Away;
             if (!parseCutShape(argString(a, "shape", "lasso"), shape)) return {false, "Cut: shape is line, lasso, rect or polyline"};
             if (!parseCutMode(argString(a, "mode", "away"), mode)) return {false, "Cut: mode is away, in or add"};
             std::vector<glm::vec2> points;
             {
                 const std::string text = argString(a, "points");
                 std::vector<float> nums;
                 const char* s = text.c_str();
                 char* end = nullptr;
                 for (float v = std::strtof(s, &end); end != s; v = std::strtof(s, &end)) {
                     nums.push_back(v);
                     s = end;
                 }
                 for (size_t i = 0; i + 1 < nums.size(); i += 2) points.push_back({nums[i], nums[i + 1]});
                 if (nums.size() % 2) return {false, "Cut: points are x y pairs"};
             }
             std::string error;
             glm::mat4 vp;
             if (!viewProjArg(doc.mesh, a, vp, error)) return {false, error};
             std::vector<SymCopy> copies;
             if (!symmetryCopiesArg(a, doc.mesh, copies, error)) return {false, error};
             if (copies.empty()) copies.push_back(SymCopy{});
             std::vector<glm::dvec2> region;
             if (!cutRegion(shape, points, argBool(a, "flip", false), argBool(a, "closed", false), region, error))
                 return {false, error};
             const float depth = float(argNumber(a, "depth", 0.1));
             // A mirror image of the shape that lands on the shape itself (a rectangle across the mirror plane) would
             // cut the same prism twice: skip it.
             auto sameCutter = [&](const glm::mat4& a, const glm::mat4& b) {
                 const glm::dmat4 A(a), invB = glm::inverse(glm::dmat4(b));
                 for (const glm::dvec2& r : region)
                     for (double z : {-0.5, 0.5}) {
                         const glm::dvec4 w = invB * glm::dvec4(r, z, 1.0);
                         const glm::dvec4 q = A * (w / w.w);
                         const glm::dvec3 n = glm::dvec3(q) / q.w;
                         bool found = false;
                         for (const glm::dvec2& s : region)
                             found = found || (glm::length(glm::dvec2(n) - s) < 1e-5 * (1.0 + glm::length(s)) &&
                                               std::abs(n.z - z) < 1e-5);
                         if (!found) return false;
                     }
                 return true;
             };
             Mesh current;
             const Mesh* src = &doc.mesh;
             size_t applied = 0;
             std::string lastError;
             std::vector<glm::mat4> done;
             for (const SymCopy& copy : copies) {
                 // The copy of the shape: points p with copy^-1(p) inside the original.
                 glm::mat4 inv(glm::transpose(copy.m));
                 inv[3] = glm::vec4(copy.pivot - glm::transpose(copy.m) * copy.pivot, 1.0f);
                 const glm::mat4 view = vp * inv;
                 if (std::any_of(done.begin(), done.end(), [&](const glm::mat4& d) { return sameCutter(d, view); })) continue;
                 done.push_back(view);
                 Mesh next;
                 if (!cutMesh(*src, view, region, mode, depth, next, lastError)) continue;
                 current = std::move(next);
                 src = &current;
                 ++applied;
             }
             if (applied == 0) {
                 if (lastError.find("misses") != std::string::npos) return {true, lastError};
                 return {false, lastError};
             }
             const size_t before = doc.mesh.triangleCount();
             doc.replaceMeshUndoable(std::move(current), "Cut");
             const char* verb = mode == CutMode::Add ? "Cut Add" : mode == CutMode::In ? "Cut In" : "Cut Away";
             return {true, std::string(verb) + ": " + std::to_string(before) + " -> " +
                               std::to_string(doc.mesh.triangleCount()) + " triangles"};
         }});
}

} // namespace sf
