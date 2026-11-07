// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Even triangulation of pattern panels (core/Garment.h). Per panel:
//  1. The outline is sampled at the particle distance h, edge by edge, split at every seam end; both sides of a
//     seam get the same number of points, so the seam's particles pair one to one.
//  2. The inside is filled with a triangular lattice of spacing h along the grain line, keeping points that are
//     at least about h/2 from the outline and outside every outline segment's diametral circle (so the outline
//     segments are Delaunay edges almost always).
//  3. Bowyer-Watson Delaunay triangulation of all the points; outline segments that are still missing are
//     recovered by edge flips; triangles outside the outline are dropped.
//  4. Laplacian smoothing of the inside points (never folding a triangle) and constrained Lawson flips back to
//     a Delaunay triangulation.
#include "core/Garment.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace sf::garment {

namespace {

using dvec2 = glm::dvec2;

double orient(dvec2 a, dvec2 b, dvec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

// > 0 when d is inside the circle through counter-clockwise a, b, c.
double inCircle(dvec2 a, dvec2 b, dvec2 c, dvec2 d) {
    const double adx = a.x - d.x, ady = a.y - d.y, bdx = b.x - d.x, bdy = b.y - d.y, cdx = c.x - d.x, cdy = c.y - d.y;
    const double ad = adx * adx + ady * ady, bd = bdx * bdx + bdy * bdy, cd = cdx * cdx + cdy * cdy;
    return adx * (bdy * cd - bd * cdy) - ady * (bdx * cd - bd * cdx) + ad * (bdx * cdy - bdy * cdx);
}

bool insidePolygon(const std::vector<dvec2>& poly, dvec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const dvec2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

double segDist2(dvec2 p, dvec2 a, dvec2 b) {
    const dvec2 d = b - a;
    const double l = glm::dot(d, d);
    const double t = l > 0 ? std::clamp(glm::dot(p - a, d) / l, 0.0, 1.0) : 0.0;
    const dvec2 q = a + d * t - p;
    return glm::dot(q, q);
}

uint64_t key(uint32_t a, uint32_t b) { return uint64_t(std::min(a, b)) << 32 | std::max(a, b); }

// --- Bowyer-Watson ---

struct Delaunay {
    struct Tri {
        int v[3];
        int n[3]; // across the edge opposite v[i] (v[i+1] -> v[i+2]); -1 = none
        bool alive;
    };
    std::vector<dvec2> P;
    std::vector<Tri> T;
    std::vector<int> stamp;
    int mark = 0, last = 0;

    void init(dvec2 lo, dvec2 hi) {
        const dvec2 c = (lo + hi) * 0.5;
        const double r = std::max(hi.x - lo.x, hi.y - lo.y) * 20.0 + 1.0;
        P = {c + dvec2(-r, -r), c + dvec2(r, -r), c + dvec2(0, r)};
        T = {{{0, 1, 2}, {-1, -1, -1}, true}};
        stamp = {0};
    }

    int locate(dvec2 p) {
        int t = (last >= 0 && last < int(T.size()) && T[size_t(last)].alive) ? last : -1;
        if (t < 0)
            for (size_t i = 0; i < T.size(); ++i)
                if (T[i].alive) {
                    t = int(i);
                    break;
                }
        for (size_t steps = 0; steps < T.size() + 16; ++steps) {
            const Tri& tr = T[size_t(t)];
            int next = -1;
            for (int i = 0; i < 3; ++i) {
                const dvec2 a = P[size_t(tr.v[(i + 1) % 3])], b = P[size_t(tr.v[(i + 2) % 3])];
                if (orient(a, b, p) < 0 && tr.n[i] >= 0) {
                    next = tr.n[i];
                    break;
                }
            }
            if (next < 0) return t;
            t = next;
        }
        for (size_t i = 0; i < T.size(); ++i) { // (a walk that cycles: scan)
            const Tri& tr = T[i];
            if (tr.alive && orient(P[size_t(tr.v[0])], P[size_t(tr.v[1])], p) >= 0 &&
                orient(P[size_t(tr.v[1])], P[size_t(tr.v[2])], p) >= 0 && orient(P[size_t(tr.v[2])], P[size_t(tr.v[0])], p) >= 0)
                return int(i);
        }
        return t;
    }

    bool bad(int t, dvec2 p) const {
        const Tri& tr = T[size_t(t)];
        return inCircle(P[size_t(tr.v[0])], P[size_t(tr.v[1])], P[size_t(tr.v[2])], p) > 0;
    }

    void insert(dvec2 p) {
        const int pi = int(P.size());
        P.push_back(p);
        const int t0 = locate(p);
        ++mark;
        std::vector<int> cavity{t0}, stack{t0};
        stamp[size_t(t0)] = mark;
        struct Boundary {
            int a, b, outside;
        };
        std::vector<Boundary> border;
        while (!stack.empty()) {
            const int t = stack.back();
            stack.pop_back();
            for (int i = 0; i < 3; ++i) {
                const int nb = T[size_t(t)].n[i];
                const int a = T[size_t(t)].v[(i + 1) % 3], b = T[size_t(t)].v[(i + 2) % 3];
                if (nb >= 0 && stamp[size_t(nb)] == mark) continue;
                if (nb >= 0 && bad(nb, p)) {
                    stamp[size_t(nb)] = mark;
                    cavity.push_back(nb);
                    stack.push_back(nb);
                } else {
                    border.push_back({a, b, nb});
                }
            }
        }
        // Edges between two cavity triangles were skipped; border edges whose outside joined the cavity later
        // are interior after all.
        border.erase(std::remove_if(border.begin(), border.end(),
                                    [&](const Boundary& e) { return e.outside >= 0 && stamp[size_t(e.outside)] == mark; }),
                     border.end());
        for (int t : cavity) T[size_t(t)].alive = false;
        std::vector<int> made;
        made.reserve(border.size());
        size_t reuse = 0;
        for (const Boundary& e : border) {
            int id;
            if (reuse < cavity.size()) id = cavity[reuse++];
            else {
                id = int(T.size());
                T.push_back({});
                stamp.push_back(0);
            }
            T[size_t(id)] = {{e.a, e.b, pi}, {-1, -1, e.outside}, true};
            if (e.outside >= 0) {
                Tri& o = T[size_t(e.outside)];
                for (int j = 0; j < 3; ++j)
                    if (o.v[(j + 1) % 3] == e.b && o.v[(j + 2) % 3] == e.a) o.n[j] = id;
            }
            made.push_back(id);
        }
        for (int id : made) {
            Tri& t = T[size_t(id)];
            for (int other : made) {
                if (other == id) continue;
                const Tri& o = T[size_t(other)];
                if (o.v[0] == t.v[1]) t.n[0] = other; // edge (b, p) is the other's (p, a') with a' = b
                if (o.v[1] == t.v[0]) t.n[1] = other; // edge (p, a)
            }
        }
        last = made.empty() ? 0 : made.front();
    }
};

// --- Triangle soup with an edge map, for flips ---

struct Soup {
    std::vector<dvec2>* P = nullptr;
    std::vector<glm::uvec3> tris;
    std::unordered_map<uint64_t, std::array<int, 2>> edges;

    void build() {
        edges.clear();
        edges.reserve(tris.size() * 2);
        for (size_t t = 0; t < tris.size(); ++t)
            for (int k = 0; k < 3; ++k) {
                auto [it, fresh] = edges.try_emplace(key(tris[t][k], tris[t][(k + 1) % 3]), std::array<int, 2>{-1, -1});
                (it->second[0] < 0 ? it->second[0] : it->second[1]) = int(t);
            }
    }
    static uint32_t opposite(const glm::uvec3& t, uint32_t u, uint32_t v) {
        for (int k = 0; k < 3; ++k)
            if (t[k] != u && t[k] != v) return t[k];
        return t[0];
    }
    void replace(uint64_t e, int from, int to) {
        auto it = edges.find(e);
        if (it == edges.end()) return;
        for (int& x : it->second)
            if (x == from) {
                x = to;
                return;
            }
    }
    // Flips edge (u, v) shared by two triangles when their quad is strictly convex.
    bool flip(uint32_t u, uint32_t v) {
        auto it = edges.find(key(u, v));
        if (it == edges.end() || it->second[0] < 0 || it->second[1] < 0) return false;
        int t1 = it->second[0], t2 = it->second[1];
        // t1 holds u -> v in its winding.
        auto hasDirected = [&](int t, uint32_t a, uint32_t b) {
            for (int k = 0; k < 3; ++k)
                if (tris[size_t(t)][k] == a && tris[size_t(t)][(k + 1) % 3] == b) return true;
            return false;
        };
        if (!hasDirected(t1, u, v)) std::swap(t1, t2);
        if (!hasDirected(t1, u, v)) return false;
        const uint32_t x = opposite(tris[size_t(t1)], u, v), y = opposite(tris[size_t(t2)], u, v);
        const std::vector<dvec2>& p = *P;
        if (!(orient(p[x], p[u], p[y]) > 0 && orient(p[y], p[v], p[x]) > 0)) return false;
        tris[size_t(t1)] = {x, u, y};
        tris[size_t(t2)] = {y, v, x};
        edges.erase(it);
        edges[key(x, y)] = {t1, t2};
        replace(key(u, y), t2, t1);
        replace(key(v, x), t1, t2);
        return true;
    }
    bool has(uint32_t u, uint32_t v) const { return edges.count(key(u, v)) != 0; }
};

bool properCross(dvec2 a, dvec2 b, dvec2 c, dvec2 d) {
    const double d1 = orient(a, b, c), d2 = orient(a, b, d), d3 = orient(c, d, a), d4 = orient(c, d, b);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

// Makes segment (a, b) an edge by flipping the edges crossing it (Sloan 1993). False when it can't.
bool recover(Soup& s, uint32_t a, uint32_t b) {
    const std::vector<dvec2>& P = *s.P;
    for (int round = 0; round < 400 && !s.has(a, b); ++round) {
        bool flipped = false;
        std::vector<uint64_t> crossing;
        for (const auto& [k, ts] : s.edges) {
            const uint32_t u = uint32_t(k >> 32), v = uint32_t(k & 0xffffffffu);
            if (u == a || u == b || v == a || v == b) continue;
            if (properCross(P[a], P[b], P[u], P[v])) crossing.push_back(k);
        }
        std::sort(crossing.begin(), crossing.end());
        for (uint64_t k : crossing)
            if (s.flip(uint32_t(k >> 32), uint32_t(k & 0xffffffffu))) flipped = true;
        if (!flipped) return false;
    }
    return s.has(a, b);
}

void lawson(Soup& s, const std::unordered_map<uint64_t, bool>& fixed) {
    const std::vector<dvec2>& P = *s.P;
    std::vector<uint64_t> queue;
    queue.reserve(s.edges.size());
    for (const auto& [k, ts] : s.edges) queue.push_back(k);
    std::sort(queue.begin(), queue.end());
    size_t budget = queue.size() * 20 + 100;
    while (!queue.empty() && budget--) {
        const uint64_t k = queue.back();
        queue.pop_back();
        if (fixed.count(k)) continue;
        auto it = s.edges.find(k);
        if (it == s.edges.end() || it->second[0] < 0 || it->second[1] < 0) continue;
        const uint32_t u = uint32_t(k >> 32), v = uint32_t(k & 0xffffffffu);
        const glm::uvec3 t1 = s.tris[size_t(it->second[0])], t2 = s.tris[size_t(it->second[1])];
        const uint32_t y = Soup::opposite(t2, u, v);
        if (!(inCircle(P[t1.x], P[t1.y], P[t1.z], P[y]) > 1e-9)) continue;
        const uint32_t x = Soup::opposite(t1, u, v);
        if (s.flip(u, v)) {
            for (uint64_t e : {key(x, u), key(u, y), key(y, v), key(v, x)}) queue.push_back(e);
        }
    }
}

// One panel's outline samples.
struct EdgeSamples {
    std::vector<float> t;        // along the edge, from 0 (its start); its end is the next edge's first sample
    std::vector<uint32_t> index; // vertex (panel-local)
};

} // namespace

bool triangulatePattern(const Pattern& pat, GarmentMesh& out, std::string& error) {
    out = {};
    const double h = std::clamp(double(pat.particleDistance), 1.0, 500.0);
    if (pat.panels.empty()) {
        error = "The pattern has no panels";
        return false;
    }
    for (const Panel& p : pat.panels) {
        if (!validPanel(p, error)) return false;
        if (panelArea(p) < 0.0f) {
            error = p.name + " is drawn clockwise";
            return false;
        }
    }
    // Points per seam: the longer side at h, both sides the same.
    std::vector<int> seamCount(pat.seams.size());
    for (size_t s = 0; s < pat.seams.size(); ++s) {
        const Seam& sm = pat.seams[s];
        if (sm.a.panel >= pat.panels.size() || sm.b.panel >= pat.panels.size()) {
            error = "A seam sews a panel that is gone";
            return false;
        }
        const double l = std::max(seamSideLength(pat, sm.a), seamSideLength(pat, sm.b));
        seamCount[s] = std::max(1, int(std::lround(l / h)));
    }
    // Each seam side's samples (global vertex ids), from t0 to t1.
    std::vector<std::array<std::vector<uint32_t>, 2>> seamVerts(pat.seams.size());
    // Vertex count guard: a 20 mm pattern stays well under this; tiny particle distances on big panels don't.
    double estimate = 0.0;
    for (const Panel& p : pat.panels) estimate += double(panelArea(p)) / (0.866 * h * h) + double(perimeter(p)) / h;
    if (estimate > 2.0e6) {
        error = "The particle distance is too small for panels this big (over 2 million particles)";
        return false;
    }

    for (uint32_t pi = 0; pi < pat.panels.size(); ++pi) {
        const Panel& p = pat.panels[pi];
        const uint32_t ne = uint32_t(p.points.size());
        const uint32_t base = uint32_t(out.flat.size());
        out.panelFirstVertex.push_back(base);
        out.panelFirstTriangle.push_back(uint32_t(out.triangles.size()));
        std::vector<dvec2> P;          // panel-local vertices (outline first)
        std::vector<EdgeSamples> samples(ne);
        // Seam ends on each edge split it into pieces; a piece that is a seam side takes the seam's count.
        for (uint32_t e = 0; e < ne; ++e) {
            std::vector<float> cuts{0.0f, 1.0f};
            for (const Seam& sm : pat.seams)
                for (const SeamSide* sd : {&sm.a, &sm.b})
                    if (sd->panel == pi && sd->edge == e) {
                        cuts.push_back(sd->t0);
                        cuts.push_back(sd->t1);
                    }
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end(), [](float a, float b) { return std::abs(a - b) < 1e-4f; }),
                       cuts.end());
            const float len = edgeLength(p, e);
            for (size_t c = 0; c + 1 < cuts.size(); ++c) {
                const float a = cuts[c], b = cuts[c + 1];
                int n = std::max(1, int(std::lround(double(len) * double(b - a) / h)));
                for (size_t s = 0; s < pat.seams.size(); ++s)
                    for (const SeamSide* sd : {&pat.seams[s].a, &pat.seams[s].b})
                        if (sd->panel == pi && sd->edge == e && std::abs(sd->t0 - a) < 1e-4f && std::abs(sd->t1 - b) < 1e-4f)
                            n = seamCount[s];
                for (int k = 0; k < n; ++k) {
                    const float t = a + (b - a) * float(k) / float(n);
                    samples[e].t.push_back(t);
                    samples[e].index.push_back(uint32_t(P.size()));
                    P.push_back(dvec2(edgePoint(p, e, t)));
                }
            }
        }
        const uint32_t nb = uint32_t(P.size()); // outline vertices: 0 .. nb-1, in order around the panel
        std::vector<dvec2> poly(P.begin(), P.end());
        // Fill: a triangular lattice along the grain.
        const double g = double(p.grainDegrees) * 3.14159265358979 / 180.0;
        const dvec2 e1 = h * dvec2(std::cos(g), std::sin(g)), e2 = h * dvec2(std::cos(g + 1.0471975512), std::sin(g + 1.0471975512));
        dvec2 lo(1e30), hi(-1e30);
        for (dvec2 q : poly) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
        // Lattice coordinates (i, j) covering the bounding box: solve q = o + i e1 + j e2 at the corners.
        const dvec2 o = lo;
        const double det = e1.x * e2.y - e1.y * e2.x;
        double imin = 1e30, imax = -1e30, jmin = 1e30, jmax = -1e30;
        for (dvec2 c : {lo, hi, dvec2(lo.x, hi.y), dvec2(hi.x, lo.y)}) {
            const dvec2 d = c - o;
            const double i = (d.x * e2.y - d.y * e2.x) / det, j = (e1.x * d.y - e1.y * d.x) / det;
            imin = std::min(imin, i);
            imax = std::max(imax, i);
            jmin = std::min(jmin, j);
            jmax = std::max(jmax, j);
        }
        // A grid over the outline's segments to find the near ones quickly.
        const double cell = 2.0 * h;
        const int gx = std::max(1, int(std::ceil((hi.x - lo.x) / cell)) + 1), gy = std::max(1, int(std::ceil((hi.y - lo.y) / cell)) + 1);
        std::vector<std::vector<uint32_t>> grid(size_t(gx) * size_t(gy));
        for (uint32_t s = 0; s < nb; ++s) {
            const dvec2 a = poly[s], b = poly[(s + 1) % nb];
            const int x0 = std::clamp(int((std::min(a.x, b.x) - lo.x) / cell), 0, gx - 1), x1 = std::clamp(int((std::max(a.x, b.x) - lo.x) / cell), 0, gx - 1);
            const int y0 = std::clamp(int((std::min(a.y, b.y) - lo.y) / cell), 0, gy - 1), y1 = std::clamp(int((std::max(a.y, b.y) - lo.y) / cell), 0, gy - 1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) grid[size_t(y) * size_t(gx) + size_t(x)].push_back(s);
        }
        const double keep2 = 0.55 * 0.55 * h * h;
        for (long j = long(std::floor(jmin)); j <= long(std::ceil(jmax)); ++j)
            for (long i = long(std::floor(imin)); i <= long(std::ceil(imax)); ++i) {
                const dvec2 q = o + double(i) * e1 + double(j) * e2;
                if (q.x < lo.x || q.x > hi.x || q.y < lo.y || q.y > hi.y || !insidePolygon(poly, q)) continue;
                const int cx = std::clamp(int((q.x - lo.x) / cell), 0, gx - 1), cy = std::clamp(int((q.y - lo.y) / cell), 0, gy - 1);
                bool ok = true;
                for (int y = std::max(0, cy - 1); y <= std::min(gy - 1, cy + 1) && ok; ++y)
                    for (int x = std::max(0, cx - 1); x <= std::min(gx - 1, cx + 1) && ok; ++x)
                        for (uint32_t s : grid[size_t(y) * size_t(gx) + size_t(x)]) {
                            const dvec2 a = poly[s], b = poly[(s + 1) % nb];
                            const dvec2 m = 0.5 * (a + b);
                            const double r2 = 0.25 * glm::dot(b - a, b - a) * 1.1;
                            if (segDist2(q, a, b) < keep2 || glm::dot(q - m, q - m) < r2) {
                                ok = false;
                                break;
                            }
                        }
                if (ok) P.push_back(q);
            }
        // Delaunay, inserting in a snake order over coarse cells (short walks).
        Delaunay dt;
        dt.init(lo, hi);
        std::vector<uint32_t> order(P.size());
        for (uint32_t k = 0; k < order.size(); ++k) order[k] = k;
        const double oc = 8.0 * h;
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            const long ra = long((P[a].y - lo.y) / oc), rb = long((P[b].y - lo.y) / oc);
            if (ra != rb) return ra < rb;
            const double xa = (ra & 1) ? -P[a].x : P[a].x, xb = (rb & 1) ? -P[b].x : P[b].x;
            return xa < xb;
        });
        std::vector<int> dtIndex(P.size());
        for (uint32_t k : order) {
            dtIndex[k] = int(dt.P.size());
            dt.insert(P[k]);
        }
        std::vector<uint32_t> back(dt.P.size(), ~0u);
        for (uint32_t k = 0; k < P.size(); ++k) back[size_t(dtIndex[k])] = k;
        Soup soup;
        soup.P = &P;
        for (const auto& t : dt.T) {
            if (!t.alive || t.v[0] < 3 || t.v[1] < 3 || t.v[2] < 3) continue;
            soup.tris.push_back({back[size_t(t.v[0])], back[size_t(t.v[1])], back[size_t(t.v[2])]});
        }
        soup.build();
        std::unordered_map<uint64_t, bool> fixedEdges;
        for (uint32_t s = 0; s < nb; ++s) {
            const uint32_t a = s, b = (s + 1) % nb;
            fixedEdges[key(a, b)] = true;
            if (!soup.has(a, b) && !recover(soup, a, b)) {
                error = p.name + ": could not triangulate the outline (try a larger particle distance)";
                return false;
            }
        }
        // Drop what's outside the outline.
        std::vector<glm::uvec3> inside;
        for (const glm::uvec3& t : soup.tris)
            if (insidePolygon(poly, (P[t.x] + P[t.y] + P[t.z]) / 3.0)) inside.push_back(t);
        soup.tris = std::move(inside);
        soup.build();
        lawson(soup, fixedEdges);
        // Smooth the inside points (never folding a triangle), then flip back to Delaunay.
        std::vector<std::vector<uint32_t>> vt(P.size());
        for (uint32_t t = 0; t < soup.tris.size(); ++t)
            for (int k = 0; k < 3; ++k) vt[soup.tris[t][k]].push_back(t);
        for (int pass = 0; pass < 4; ++pass) {
            for (uint32_t v = nb; v < P.size(); ++v) {
                if (vt[v].empty()) continue;
                dvec2 sum(0.0);
                int n = 0;
                for (uint32_t t : vt[v])
                    for (int k = 0; k < 3; ++k)
                        if (soup.tris[t][k] != v) {
                            sum += P[soup.tris[t][k]];
                            ++n;
                        }
                const dvec2 old = P[v];
                P[v] = glm::mix(old, sum / double(n), 0.6);
                for (uint32_t t : vt[v]) {
                    const glm::uvec3 tr = soup.tris[t];
                    if (orient(P[tr.x], P[tr.y], P[tr.z]) < 0.05 * h * h) {
                        P[v] = old;
                        break;
                    }
                }
            }
            lawson(soup, fixedEdges);
            for (auto& l : vt) l.clear();
            for (uint32_t t = 0; t < soup.tris.size(); ++t)
                for (int k = 0; k < 3; ++k) vt[soup.tris[t][k]].push_back(t);
        }
        // Points the triangulation didn't use (none in practice) are kept so indices stay simple.
        for (uint32_t v = 0; v < P.size(); ++v) {
            out.flat.push_back(glm::vec2(P[v]));
            out.panelOf.push_back(pi);
            out.border.push_back(v < nb ? 1 : 0);
        }
        for (const glm::uvec3& t : soup.tris) out.triangles.push_back(t + glm::uvec3(base));
        // This panel's seam sides.
        for (size_t s = 0; s < pat.seams.size(); ++s)
            for (int side = 0; side < 2; ++side) {
                const SeamSide& sd = side == 0 ? pat.seams[s].a : pat.seams[s].b;
                if (sd.panel != pi) continue;
                std::vector<uint32_t>& list = seamVerts[s][size_t(side)];
                const EdgeSamples& es = samples[sd.edge];
                for (size_t k = 0; k < es.t.size(); ++k)
                    if (es.t[k] >= sd.t0 - 1e-4f && es.t[k] <= sd.t1 + 1e-4f) list.push_back(base + es.index[k]);
                if (sd.t1 > 1.0f - 1e-4f) list.push_back(base + samples[(sd.edge + 1) % ne].index.front());
            }
    }
    out.panelFirstVertex.push_back(uint32_t(out.flat.size()));
    out.panelFirstTriangle.push_back(uint32_t(out.triangles.size()));
    for (size_t s = 0; s < pat.seams.size(); ++s) {
        const std::vector<uint32_t>& a = seamVerts[s][0];
        const std::vector<uint32_t>& b = seamVerts[s][1];
        if (a.size() != b.size() || a.empty()) {
            error = "Seam " + std::to_string(s + 1) + " could not be matched";
            return false;
        }
        const size_t n = a.size();
        for (size_t k = 0; k < n; ++k) out.links.push_back({a[k], pat.seams[s].flipped ? b[k] : b[n - 1 - k]});
    }
    return true;
}

MeshQuality meshQuality(const GarmentMesh& m, float h) {
    MeshQuality q{1e30f, 0.0f, 180.0f};
    if (m.triangles.empty()) return {};
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k) {
            const glm::vec2 a = m.flat[t[k]], b = m.flat[t[(k + 1) % 3]], c = m.flat[t[(k + 2) % 3]];
            const float l = glm::length(b - a) / h;
            q.minEdge = std::min(q.minEdge, l);
            q.maxEdge = std::max(q.maxEdge, l);
            const glm::vec2 u = b - a, v = c - a;
            const float cosA = glm::dot(u, v) / std::max(1e-12f, glm::length(u) * glm::length(v));
            q.minAngle = std::min(q.minAngle, std::acos(std::clamp(cosA, -1.0f, 1.0f)) * 57.2957795f);
        }
    return q;
}

} // namespace sf::garment
