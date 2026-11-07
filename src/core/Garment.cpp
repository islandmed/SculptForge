// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garments: pattern panels, their edits, seams, the flat rest shape, the Part made after the simulation, the
// T-shirt template and the project JSON (core/Garment.h). Triangulation is in GarmentMesh.cpp, files in
// GarmentFiles.cpp.
#include "core/Garment.h"

#include "core/Document.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <limits>
#include <numeric>

namespace sf::garment {

namespace {

constexpr float kPi = 3.14159265358979f;

glm::vec2 bezier(glm::vec2 p0, glm::vec2 c1, glm::vec2 c2, glm::vec2 p1, float u) {
    const float v = 1.0f - u;
    return v * v * v * p0 + 3.0f * v * v * u * c1 + 3.0f * v * u * u * c2 + u * u * u * p1;
}

uint32_t count(const Panel& p) { return uint32_t(p.points.size()); }
glm::vec2 startOf(const Panel& p, uint32_t e) { return p.points[e]; }
glm::vec2 endOf(const Panel& p, uint32_t e) { return p.points[(e + 1) % count(p)]; }
const EdgeCurve& curveOf(const Panel& p, uint32_t e) {
    static const EdgeCurve straight;
    return e < p.curves.size() ? p.curves[e] : straight;
}

// The edge sampled finely with each sample's Bezier parameter and arc length (for exact t <-> parameter maps).
struct Samples {
    std::vector<glm::vec2> pts;
    std::vector<float> u, len;
};
Samples sampleEdge(const Panel& p, uint32_t e, int n) {
    Samples s;
    const EdgeCurve& c = curveOf(p, e);
    const glm::vec2 a = startOf(p, e), b = endOf(p, e);
    if (!c.curved) n = 1;
    s.pts.reserve(size_t(n) + 1);
    float total = 0.0f;
    for (int i = 0; i <= n; ++i) {
        const float u = float(i) / float(n);
        const glm::vec2 q = c.curved ? bezier(a, c.c1, c.c2, b, u) : glm::mix(a, b, u);
        if (i) total += glm::length(q - s.pts.back());
        s.pts.push_back(q);
        s.u.push_back(u);
        s.len.push_back(total);
    }
    return s;
}
int fineCount(const Panel& p, uint32_t e) {
    const EdgeCurve& c = curveOf(p, e);
    if (!c.curved) return 1;
    const float hull = glm::length(c.c1 - startOf(p, e)) + glm::length(c.c2 - c.c1) + glm::length(endOf(p, e) - c.c2);
    return std::isfinite(hull) ? int(std::clamp(std::ceil(hull), 16.0f, 2048.0f)) : 16;
}
// The Bezier parameter at arc-length fraction t.
float paramAt(const Samples& s, float t) {
    const float total = s.len.back();
    if (total <= 0.0f) return t;
    const float want = std::clamp(t, 0.0f, 1.0f) * total;
    const auto it = std::lower_bound(s.len.begin(), s.len.end(), want);
    const size_t i = size_t(std::max<ptrdiff_t>(1, it - s.len.begin()));
    if (i >= s.len.size()) return 1.0f;
    const float l0 = s.len[i - 1], l1 = s.len[i];
    const float f = l1 > l0 ? (want - l0) / (l1 - l0) : 0.0f;
    return s.u[i - 1] + (s.u[i] - s.u[i - 1]) * f;
}

float cross2(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

bool segmentsCross(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d) {
    const float d1 = cross2(b - a, c - a), d2 = cross2(b - a, d - a);
    const float d3 = cross2(d - c, a - c), d4 = cross2(d - c, b - c);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

// Reverses a panel's direction (points, curves, notches); edge j becomes edge n - 1 - j, t becomes 1 - t.
void reverseInPlace(Panel& p, std::vector<Seam>* seams, uint32_t self) {
    const uint32_t n = count(p);
    if (n < 2) return;
    std::vector<glm::vec2> pts(n);
    std::vector<EdgeCurve> cur(n);
    for (uint32_t k = 0; k < n; ++k) {
        pts[k] = p.points[(n - k) % n];
        const EdgeCurve& c = curveOf(p, n - 1 - k);
        cur[k] = {c.curved, c.c2, c.c1};
    }
    p.points = std::move(pts);
    p.curves = std::move(cur);
    for (Notch& nt : p.notches) {
        nt.edge = n - 1 - nt.edge;
        nt.t = 1.0f - nt.t;
    }
    if (seams)
        for (Seam& s : *seams)
            for (SeamSide* side : {&s.a, &s.b})
                if (side->panel == self) {
                    side->edge = n - 1 - side->edge;
                    const float t0 = 1.0f - side->t1, t1 = 1.0f - side->t0;
                    side->t0 = t0;
                    side->t1 = t1;
                }
}

void fixPanels(std::vector<Seam>& seams, uint32_t removed) {
    seams.erase(std::remove_if(seams.begin(), seams.end(),
                               [&](const Seam& s) { return s.a.panel == removed || s.b.panel == removed; }),
                seams.end());
    for (Seam& s : seams) {
        if (s.a.panel > removed) --s.a.panel;
        if (s.b.panel > removed) --s.b.panel;
    }
}

bool overlaps(const SeamSide& x, const SeamSide& y) {
    return x.panel == y.panel && x.edge == y.edge && x.t0 < y.t1 - 1e-4f && y.t0 < x.t1 - 1e-4f;
}

} // namespace

// ---------------------------------------------------------------------------
// Panel geometry
// ---------------------------------------------------------------------------

void normalizePanel(Panel& p) {
    p.curves.resize(p.points.size());
    const uint32_t n = count(p);
    p.notches.erase(std::remove_if(p.notches.begin(), p.notches.end(),
                                   [&](const Notch& nt) { return nt.edge >= n || !(nt.t >= 0.0f && nt.t <= 1.0f); }),
                    p.notches.end());
    if (n >= 3 && panelArea(p) < 0.0f) reverseInPlace(p, nullptr, 0);
}

std::vector<glm::vec2> edgePolyline(const Panel& p, uint32_t e, float tolerance) {
    const EdgeCurve& c = curveOf(p, e);
    if (!c.curved) return {startOf(p, e), endOf(p, e)};
    // Segments for a flattening error under `tolerance`: a cubic's chord error falls with the square of the count.
    const glm::vec2 a = startOf(p, e), b = endOf(p, e);
    const float bend = std::max(glm::length(a - 2.0f * c.c1 + c.c2), glm::length(c.c1 - 2.0f * c.c2 + b));
    const float want = std::ceil(std::sqrt(0.75f * bend / std::max(tolerance, 0.01f)));
    const int n = std::isfinite(want) ? int(std::clamp(want, 4.0f, 512.0f)) : 4; // non-finite: validPanel refuses it
    std::vector<glm::vec2> out;
    out.reserve(size_t(n) + 1);
    for (int i = 0; i <= n; ++i) out.push_back(bezier(a, c.c1, c.c2, b, float(i) / float(n)));
    return out;
}

float edgeLength(const Panel& p, uint32_t e) {
    if (e >= count(p)) return 0.0f;
    return sampleEdge(p, e, fineCount(p, e)).len.back();
}

glm::vec2 edgePoint(const Panel& p, uint32_t e, float t, glm::vec2* tangent) {
    if (e >= count(p)) return glm::vec2(0.0f);
    const EdgeCurve& c = curveOf(p, e);
    const glm::vec2 a = startOf(p, e), b = endOf(p, e);
    if (!c.curved) {
        if (tangent) *tangent = glm::length(b - a) > 0 ? glm::normalize(b - a) : glm::vec2(1, 0);
        return glm::mix(a, b, std::clamp(t, 0.0f, 1.0f));
    }
    const float u = paramAt(sampleEdge(p, e, fineCount(p, e)), t);
    if (tangent) {
        const float v = 1.0f - u;
        const glm::vec2 d = 3.0f * v * v * (c.c1 - a) + 6.0f * v * u * (c.c2 - c.c1) + 3.0f * u * u * (b - c.c2);
        *tangent = glm::length(d) > 0 ? glm::normalize(d) : glm::normalize(b - a + glm::vec2(1e-6f));
    }
    return bezier(a, c.c1, c.c2, b, u);
}

std::vector<glm::vec2> outline(const Panel& p, float tolerance) {
    std::vector<glm::vec2> out;
    for (uint32_t e = 0; e < count(p); ++e) {
        std::vector<glm::vec2> pl = edgePolyline(p, e, tolerance);
        out.insert(out.end(), pl.begin(), pl.end() - 1);
    }
    return out;
}

float panelArea(const Panel& p) {
    const std::vector<glm::vec2> o = outline(p, 0.5f);
    double a = 0.0;
    for (size_t i = 0; i < o.size(); ++i) a += double(cross2(o[i], o[(i + 1) % o.size()]));
    return float(a * 0.5);
}

float perimeter(const Panel& p) {
    float l = 0.0f;
    for (uint32_t e = 0; e < count(p); ++e) l += edgeLength(p, e);
    return l;
}

glm::vec2 panelMin(const Panel& p) {
    glm::vec2 m(std::numeric_limits<float>::max());
    for (glm::vec2 q : outline(p, 0.5f)) m = glm::min(m, q);
    return p.points.empty() ? glm::vec2(0.0f) : m;
}
glm::vec2 panelMax(const Panel& p) {
    glm::vec2 m(std::numeric_limits<float>::lowest());
    for (glm::vec2 q : outline(p, 0.5f)) m = glm::max(m, q);
    return p.points.empty() ? glm::vec2(0.0f) : m;
}

bool validPanel(const Panel& p, std::string& error, size_t* outlineBudget) {
    if (p.points.size() < 3) {
        error = p.name + " needs at least 3 points";
        return false;
    }
    auto out = [](glm::vec2 q) { return !std::isfinite(q.x) || !std::isfinite(q.y) || std::abs(q.x) > 1e6f || std::abs(q.y) > 1e6f; };
    for (size_t k = 0; k < p.points.size(); ++k)
        if (out(p.points[k]) || (k < p.curves.size() && p.curves[k].curved && (out(p.curves[k].c1) || out(p.curves[k].c2)))) {
            error = p.name + " has a point out of range";
            return false;
        }
    if (outlineBudget) { // curves flatten to up to 512 points each: bound the work across a whole import or load
        size_t n = 0;
        for (uint32_t e = 0; e < count(p); ++e) n += edgePolyline(p, e, 0.5f).size() - 1;
        if (n > *outlineBudget) {
            *outlineBudget = 0;
            error = p.name + ": over " + std::to_string(kOutlineBudget) + " outline points in all";
            return false;
        }
        *outlineBudget -= n;
    }
    for (uint32_t e = 0; e < count(p); ++e)
        if (edgeLength(p, e) < 1.0f) {
            error = p.name + ": edge " + std::to_string(e + 1) + " is shorter than 1 mm";
            return false;
        }
    if (std::abs(panelArea(p)) < 1.0f) {
        error = p.name + " has no area";
        return false;
    }
    const std::vector<glm::vec2> o = outline(p, 0.5f);
    const size_t n = o.size();
    if (n > 20000) {
        error = p.name + " has too many points";
        return false;
    }
    // Self-crossing check on a uniform grid (about one segment per cell): only segments sharing a cell are tested,
    // so a valid outline costs about O(n). Hostile outlines that pile segments into few cells hit the budget.
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (glm::vec2 q : o) lo = glm::min(lo, q), hi = glm::max(hi, q);
    const int g = std::clamp(int(std::ceil(std::sqrt(double(n)))), 1, 256);
    const glm::vec2 cell = glm::max((hi - lo) / float(g), glm::vec2(1e-6f));
    std::vector<std::vector<uint32_t>> grid(size_t(g) * size_t(g));
    size_t work = 0;
    const size_t budget = 4000000;
    for (size_t i = 0; i < n; ++i) {
        const glm::vec2 a = o[i], b = o[(i + 1) % n];
        // Walk the cells the segment passes through (grid traversal), testing what is already there.
        const glm::vec2 pa = (a - lo) / cell, pb = (b - lo) / cell, d = pb - pa;
        int x = std::clamp(int(pa.x), 0, g - 1), y = std::clamp(int(pa.y), 0, g - 1);
        const int xe = std::clamp(int(pb.x), 0, g - 1), ye = std::clamp(int(pb.y), 0, g - 1);
        const int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1;
        const float dtx = d.x != 0 ? std::abs(1.0f / d.x) : std::numeric_limits<float>::infinity();
        const float dty = d.y != 0 ? std::abs(1.0f / d.y) : std::numeric_limits<float>::infinity();
        float tx = d.x != 0 ? (sx > 0 ? float(x + 1) - pa.x : pa.x - float(x)) * dtx : dtx;
        float ty = d.y != 0 ? (sy > 0 ? float(y + 1) - pa.y : pa.y - float(y)) * dty : dty;
        for (int steps = 0; steps <= 2 * g + 2; ++steps) {
            std::vector<uint32_t>& c = grid[size_t(y) * size_t(g) + size_t(x)];
            work += c.size() + 1;
            if (work > budget) {
                error = p.name + "'s outline is too complex to check";
                return false;
            }
            for (uint32_t j : c) {
                if (j + 1 == i || (i + 1) % n == j) continue; // neighbours share a point
                if (segmentsCross(a, b, o[j], o[(j + 1) % n])) {
                    error = p.name + "'s outline crosses itself";
                    return false;
                }
            }
            if (c.empty() || c.back() != uint32_t(i)) c.push_back(uint32_t(i));
            if (x == xe && y == ye) break;
            // Step to the next cell; on an exact corner take both axes' neighbours so a crossing there is seen.
            if (std::abs(tx - ty) < 1e-6f) {
                if (x + sx >= 0 && x + sx < g) grid[size_t(y) * size_t(g) + size_t(x + sx)].push_back(uint32_t(i));
                if (y + sy >= 0 && y + sy < g) grid[size_t(y + sy) * size_t(g) + size_t(x)].push_back(uint32_t(i));
                x = std::clamp(x + sx, 0, g - 1), y = std::clamp(y + sy, 0, g - 1);
                tx += dtx, ty += dty;
            } else if (tx < ty) {
                x = std::clamp(x + sx, 0, g - 1);
                tx += dtx;
            } else {
                y = std::clamp(y + sy, 0, g - 1);
                ty += dty;
            }
        }
    }
    return true;
}

std::vector<glm::vec2> offsetOutline(const Panel& p, float d) {
    std::vector<glm::vec2> o = outline(p, 0.2f);
    const size_t n = o.size();
    std::vector<glm::vec2> out;
    if (n < 3) return o;
    const float sign = panelArea(p) >= 0 ? 1.0f : -1.0f;
    for (size_t i = 0; i < n; ++i) {
        const glm::vec2 a = o[(i + n - 1) % n], b = o[i], c = o[(i + 1) % n];
        auto normal = [&](glm::vec2 u, glm::vec2 v) {
            glm::vec2 t = v - u;
            const float l = glm::length(t);
            t = l > 0 ? t / l : glm::vec2(1, 0);
            return sign * glm::vec2(t.y, -t.x); // outward for a counter-clockwise outline
        };
        const glm::vec2 n1 = normal(a, b), n2 = normal(b, c);
        glm::vec2 m = n1 + n2;
        const float ml = glm::length(m);
        if (ml < 1e-4f) {
            out.push_back(b + n1 * d);
            continue;
        }
        m /= ml;
        const float cosHalf = glm::dot(m, n1);
        if (cosHalf < 0.5f) { // sharper than 120 degrees: a bevel instead of a long spike
            out.push_back(b + n1 * d);
            out.push_back(b + n2 * d);
        } else {
            out.push_back(b + m * (d / cosHalf));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

void mirrorPanel(Pattern& pat, uint32_t i) {
    if (i >= pat.panels.size()) return;
    Panel& p = pat.panels[i];
    const float cx = (panelMin(p).x + panelMax(p).x) * 0.5f;
    auto flip = [&](glm::vec2& q) { q.x = 2.0f * cx - q.x; };
    for (glm::vec2& q : p.points) flip(q);
    for (EdgeCurve& c : p.curves) {
        flip(c.c1);
        flip(c.c2);
    }
    p.grainDegrees = 180.0f - p.grainDegrees;
    reverseInPlace(p, &pat.seams, i);
}

uint32_t duplicatePanel(Pattern& pat, uint32_t i, glm::vec2 offset, const std::string& name) {
    if (i >= pat.panels.size()) return 0;
    Panel p = pat.panels[i];
    for (glm::vec2& q : p.points) q += offset;
    for (EdgeCurve& c : p.curves) {
        c.c1 += offset;
        c.c2 += offset;
    }
    p.name = name.empty() ? p.name + " 2" : name;
    pat.panels.push_back(std::move(p));
    return uint32_t(pat.panels.size() - 1);
}

uint32_t mirroredCopy(Pattern& pat, uint32_t i, const std::string& name) {
    if (i >= pat.panels.size()) return 0;
    const float w = panelMax(pat.panels[i]).x - panelMin(pat.panels[i]).x;
    const uint32_t j = duplicatePanel(pat, i, {w + 40.0f, 0.0f}, name);
    // Mirror the copy only (it has no seams yet, so the pattern's seams are untouched).
    Pattern one;
    one.panels.push_back(pat.panels[j]);
    mirrorPanel(one, 0);
    pat.panels[j] = std::move(one.panels[0]);
    return j;
}

bool unfoldPanel(Pattern& pat, uint32_t i, uint32_t e, std::string& error) {
    if (i >= pat.panels.size() || e >= count(pat.panels[i])) {
        error = "No such edge";
        return false;
    }
    Panel& p = pat.panels[i];
    if (curveOf(p, e).curved) {
        error = "The fold line must be a straight edge";
        return false;
    }
    const uint32_t n = count(p);
    const glm::vec2 a = startOf(p, e), b = endOf(p, e);
    const glm::vec2 dir = glm::normalize(b - a);
    auto reflect = [&](glm::vec2 q) {
        const glm::vec2 r = q - a;
        return a + 2.0f * glm::dot(r, dir) * dir - r;
    };
    // Old edges e+1 .. e-1 (in order) then their mirror images in reverse.
    Panel out = p;
    out.points.clear();
    out.curves.clear();
    out.notches.clear();
    std::vector<uint32_t> order;
    for (uint32_t k = 1; k < n; ++k) order.push_back((e + k) % n);
    for (uint32_t j : order) {
        out.points.push_back(startOf(p, j));
        out.curves.push_back(curveOf(p, j));
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const EdgeCurve& c = curveOf(p, *it);
        out.points.push_back(reflect(endOf(p, *it)));
        out.curves.push_back({c.curved, reflect(c.c2), reflect(c.c1)});
    }
    const uint32_t m = n - 1; // edges in each half
    for (const Notch& nt : p.notches) {
        if (nt.edge == e) continue;
        const uint32_t k = (nt.edge + n - e - 1) % n;
        out.notches.push_back({k, nt.t});
        out.notches.push_back({2 * m - 1 - k, 1.0f - nt.t});
    }
    // Seams: the fold edge's go, the others follow their edge.
    pat.seams.erase(std::remove_if(pat.seams.begin(), pat.seams.end(),
                                   [&](const Seam& s) {
                                       return (s.a.panel == i && s.a.edge == e) || (s.b.panel == i && s.b.edge == e);
                                   }),
                    pat.seams.end());
    for (Seam& s : pat.seams)
        for (SeamSide* side : {&s.a, &s.b})
            if (side->panel == i) side->edge = (side->edge + n - e - 1) % n;
    p = std::move(out);
    normalizePanel(p);
    return true;
}

void removePanel(Pattern& pat, uint32_t i) {
    if (i >= pat.panels.size()) return;
    pat.panels.erase(pat.panels.begin() + i);
    fixPanels(pat.seams, i);
}

void splitEdge(Pattern& pat, uint32_t i, uint32_t e, float t) {
    if (i >= pat.panels.size() || e >= count(pat.panels[i]) || !(t > 0.001f && t < 0.999f)) return;
    Panel& p = pat.panels[i];
    p.curves.resize(p.points.size());
    const EdgeCurve c = curveOf(p, e);
    const glm::vec2 a = startOf(p, e), b = endOf(p, e);
    glm::vec2 mid;
    EdgeCurve first, second;
    if (c.curved) {
        const float u = paramAt(sampleEdge(p, e, fineCount(p, e)), t);
        // de Casteljau at u
        const glm::vec2 p01 = glm::mix(a, c.c1, u), p12 = glm::mix(c.c1, c.c2, u), p23 = glm::mix(c.c2, b, u);
        const glm::vec2 q0 = glm::mix(p01, p12, u), q1 = glm::mix(p12, p23, u);
        mid = glm::mix(q0, q1, u);
        first = {true, p01, q0};
        second = {true, q1, p23};
    } else {
        mid = glm::mix(a, b, t);
    }
    p.points.insert(p.points.begin() + e + 1, mid);
    p.curves[e] = first;
    p.curves.insert(p.curves.begin() + e + 1, second);
    for (Notch& nt : p.notches) {
        if (nt.edge > e) ++nt.edge;
        else if (nt.edge == e) {
            if (nt.t < t) nt.t /= t;
            else {
                nt.t = (nt.t - t) / (1.0f - t);
                ++nt.edge;
            }
        }
    }
    std::vector<Seam> kept;
    for (Seam s : pat.seams) {
        bool keep = true;
        for (SeamSide* side : {&s.a, &s.b}) {
            if (side->panel != i) continue;
            if (side->edge > e) ++side->edge;
            else if (side->edge == e) {
                if (side->t1 <= t + 1e-4f) {
                    side->t0 /= t;
                    side->t1 = std::min(1.0f, side->t1 / t);
                } else if (side->t0 >= t - 1e-4f) {
                    side->t0 = std::max(0.0f, (side->t0 - t) / (1.0f - t));
                    side->t1 = (side->t1 - t) / (1.0f - t);
                    ++side->edge;
                } else {
                    keep = false; // a seam across the new point: sew the two edges again
                }
            }
        }
        if (keep) kept.push_back(s);
    }
    pat.seams = std::move(kept);
}

bool removePoint(Pattern& pat, uint32_t i, uint32_t k) {
    if (i >= pat.panels.size()) return false;
    Panel& p = pat.panels[i];
    const uint32_t n = count(p);
    if (n <= 3 || k >= n) return false;
    p.curves.resize(n);
    const uint32_t before = (k + n - 1) % n; // edge ending at k: becomes the merged straight edge
    auto gone = [&](const SeamSide& s) { return s.panel == i && (s.edge == before || s.edge == k); };
    pat.seams.erase(std::remove_if(pat.seams.begin(), pat.seams.end(),
                                   [&](const Seam& s) { return gone(s.a) || gone(s.b); }),
                    pat.seams.end());
    p.notches.erase(std::remove_if(p.notches.begin(), p.notches.end(),
                                   [&](const Notch& nt) { return nt.edge == before || nt.edge == k; }),
                    p.notches.end());
    p.curves[before] = {};
    p.points.erase(p.points.begin() + k);
    p.curves.erase(p.curves.begin() + k);
    auto shift = [&](uint32_t& edge) {
        if (k == 0) {
            if (edge > 0) --edge; // edge n-1 (now n-2) is the merged one
        } else if (edge > k) {
            --edge;
        }
    };
    for (Notch& nt : p.notches) shift(nt.edge);
    for (Seam& s : pat.seams)
        for (SeamSide* side : {&s.a, &s.b})
            if (side->panel == i) shift(side->edge);
    return true;
}

// ---------------------------------------------------------------------------
// Seams
// ---------------------------------------------------------------------------

float seamSideLength(const Pattern& pat, const SeamSide& s) {
    if (s.panel >= pat.panels.size()) return 0.0f;
    return edgeLength(pat.panels[s.panel], s.edge) * std::max(0.0f, s.t1 - s.t0);
}

std::string edgeName(const Pattern& pat, uint32_t panel, uint32_t edge) {
    if (panel >= pat.panels.size()) return "?";
    return pat.panels[panel].name + " " + std::to_string(edge + 1);
}

bool addSeam(Pattern& pat, const Seam& seam, std::string& error) {
    for (const SeamSide* s : {&seam.a, &seam.b}) {
        if (s->panel >= pat.panels.size() || s->edge >= count(pat.panels[s->panel])) {
            error = "The seam's edge is not in the pattern";
            return false;
        }
        if (!(s->t0 >= 0.0f && s->t1 <= 1.0f && s->t1 - s->t0 > 0.001f)) {
            error = "The seam's range along " + edgeName(pat, s->panel, s->edge) + " is empty";
            return false;
        }
        for (const Seam& o : pat.seams)
            if (overlaps(*s, o.a) || overlaps(*s, o.b)) {
                error = edgeName(pat, s->panel, s->edge) + " is already sewn there";
                return false;
            }
    }
    if (overlaps(seam.a, seam.b)) {
        error = "A seam can't sew an edge to itself";
        return false;
    }
    pat.seams.push_back(seam);
    return true;
}

SeamCheck checkSeam(const Pattern& pat, const Seam& seam) {
    SeamCheck c;
    c.lengthA = seamSideLength(pat, seam.a);
    c.lengthB = seamSideLength(pat, seam.b);
    const float diff = std::abs(c.lengthA - c.lengthB);
    c.lengthsMatch = diff <= std::max(3.0f, 0.02f * std::max(c.lengthA, c.lengthB));
    c.twisted = seam.flipped;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s <-> %s: %.0f / %.0f mm", edgeName(pat, seam.a.panel, seam.a.edge).c_str(),
                  edgeName(pat, seam.b.panel, seam.b.edge).c_str(), double(c.lengthA), double(c.lengthB));
    c.text = buf;
    if (!c.lengthsMatch) c.text += " (eased in)";
    if (c.twisted) c.text += " (twisted)";
    return c;
}

std::vector<std::string> patternProblems(const Pattern& pat) {
    std::vector<std::string> out;
    std::string error;
    for (const Panel& p : pat.panels)
        if (!validPanel(p, error)) out.push_back(error);
    for (size_t i = 0; i < pat.seams.size(); ++i) {
        const Seam& s = pat.seams[i];
        if (s.a.panel >= pat.panels.size() || s.b.panel >= pat.panels.size()) {
            out.push_back("Seam " + std::to_string(i + 1) + " sews a panel that is gone");
            continue;
        }
        const SeamCheck c = checkSeam(pat, s);
        if (c.twisted) out.push_back(c.text + ": the fabric turns over along it (Flip the seam)");
        else if (!c.lengthsMatch) out.push_back(c.text + ": the sides differ in length");
        for (size_t j = i + 1; j < pat.seams.size(); ++j)
            for (const SeamSide* x : {&s.a, &s.b})
                for (const SeamSide* y : {&pat.seams[j].a, &pat.seams[j].b})
                    if (overlaps(*x, *y)) out.push_back(edgeName(pat, x->panel, x->edge) + " is sewn twice");
    }
    return out;
}

// ---------------------------------------------------------------------------
// Rest shape
// ---------------------------------------------------------------------------

void flatRest(const GarmentMesh& m, std::vector<glm::vec3>& rest) {
    rest.resize(m.flat.size());
    for (size_t v = 0; v < m.flat.size(); ++v) rest[v] = glm::vec3(m.flat[v] * 0.001f, 0.0f);
}

// ---------------------------------------------------------------------------
// The Part
// ---------------------------------------------------------------------------

Mesh garmentToMesh(const GarmentMesh& gm, const std::vector<glm::vec3>& positions, float thicknessMm) {
    Mesh out;
    const size_t n = gm.flat.size();
    if (positions.size() != n || gm.triangles.empty()) return out;
    // Weld the seams: union-find over the links, the smallest index represents its set.
    std::vector<uint32_t> parent(n);
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t v) {
        while (parent[v] != v) v = parent[v] = parent[parent[v]];
        return v;
    };
    for (const SeamLink& l : gm.links) {
        if (l.a >= n || l.b >= n) continue;
        const uint32_t a = find(l.a), b = find(l.b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
    std::vector<uint32_t> remap(n, ~0u);
    std::vector<glm::dvec3> sum;
    std::vector<int> cnt;
    for (uint32_t v = 0; v < n; ++v) {
        const uint32_t r = find(v);
        if (remap[r] == ~0u) {
            remap[r] = uint32_t(sum.size());
            sum.emplace_back(0.0);
            cnt.push_back(0);
        }
        remap[v] = remap[r];
        sum[remap[v]] += glm::dvec3(positions[v]);
        ++cnt[remap[v]];
    }
    out.positions.resize(sum.size());
    for (size_t i = 0; i < sum.size(); ++i) out.positions[i] = glm::vec3(sum[i] / double(cnt[i]));
    // UVs: the whole sheet in 0..1 (the longer side), so texel density is the same on every panel.
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (glm::vec2 q : gm.flat) {
        lo = glm::min(lo, q);
        hi = glm::max(hi, q);
    }
    const float span = std::max(1e-3f, std::max(hi.x - lo.x, hi.y - lo.y)) * 1.02f;
    const glm::vec2 base = lo - glm::vec2(0.01f * span);
    for (size_t t = 0; t < gm.triangles.size(); ++t) {
        const glm::uvec3 tri = gm.triangles[t];
        const glm::uvec3 w(remap[tri.x], remap[tri.y], remap[tri.z]);
        if (w.x == w.y || w.y == w.z || w.x == w.z) continue;
        out.triangles.push_back(w);
        for (int k = 0; k < 3; ++k) out.uvs.push_back((gm.flat[tri[k]] - base) / span);
        out.facegroups.push_back(gm.panelOf[tri.x] + 1);
    }
    out.rebuildTopology();
    out.recomputeAllNormals();
    const float t = thicknessMm * 0.001f;
    if (!(t > 0.0f)) return out;
    // Thicken outward (the inner side stays where the simulation left it, off the body): the outer surface is the
    // inner one moved along its normals; the inner one is wound the other way; a rim of quads joins their borders.
    const size_t nv = out.positions.size(), nt = out.triangles.size();
    Mesh s;
    s.positions.resize(2 * nv);
    for (size_t v = 0; v < nv; ++v) {
        s.positions[v] = out.positions[v] + out.normals[v] * t;
        s.positions[nv + v] = out.positions[v];
    }
    s.triangles = out.triangles;
    s.uvs = out.uvs;
    s.facegroups = out.facegroups;
    for (size_t i = 0; i < nt; ++i) {
        const glm::uvec3 tri = out.triangles[i];
        s.triangles.push_back(glm::uvec3(tri.x, tri.z, tri.y) + glm::uvec3(uint32_t(nv)));
        s.uvs.push_back(out.uvs[3 * i]);
        s.uvs.push_back(out.uvs[3 * i + 2]);
        s.uvs.push_back(out.uvs[3 * i + 1]);
        s.facegroups.push_back(0x8000u + out.facegroups[i]);
    }
    // Border edges: used by one triangle, in that triangle's direction.
    std::vector<std::pair<uint64_t, size_t>> edges; // (key, triangle * 3 + corner)
    edges.reserve(nt * 3);
    for (size_t i = 0; i < nt; ++i)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = out.triangles[i][k], b = out.triangles[i][(k + 1) % 3];
            edges.push_back({uint64_t(std::min(a, b)) << 32 | std::max(a, b), i * 3 + size_t(k)});
        }
    std::sort(edges.begin(), edges.end());
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].first == edges[i].first) ++j;
        if (j - i == 1) {
            const size_t tri = edges[i].second / 3;
            const int k = int(edges[i].second % 3);
            const uint32_t a = out.triangles[tri][k], b = out.triangles[tri][(k + 1) % 3];
            const glm::vec2 ua = out.uvs[3 * tri + size_t(k)], ub = out.uvs[3 * tri + size_t((k + 1) % 3)];
            const uint32_t fg = 0x8000u + out.facegroups[tri];
            // Outer a->b is wound one way: the rim quad (b, a, a', b') faces out of the border.
            const uint32_t ia = a + uint32_t(nv), ib = b + uint32_t(nv);
            s.triangles.push_back({b, a, ia});
            s.triangles.push_back({b, ia, ib});
            for (glm::vec2 uv : {ub, ua, ua, ub, ua, ub}) s.uvs.push_back(uv);
            s.facegroups.push_back(fg);
            s.facegroups.push_back(fg);
        }
        i = j;
    }
    s.rebuildTopology();
    s.recomputeAllNormals();
    return s;
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

Panel rectanglePanel(const std::string& name, float w, float h, glm::vec2 at) {
    Panel p;
    p.name = name;
    p.points = {at, at + glm::vec2(w, 0), at + glm::vec2(w, h), at + glm::vec2(0, h)};
    p.curves.resize(4);
    return p;
}

namespace {

// A body panel (front or back) centred on x = cx, hem at y = 0: hem, side, armhole, shoulder, neckline, shoulder,
// armhole, side (edges 0-7).
Panel bodyPanel(const std::string& name, float cx, float half, float length, float neckDepth) {
    const float shoulderHalf = half - 30.0f, neckHalf = 85.0f, drop = 45.0f, armholeY = length - 270.0f;
    Panel p;
    p.name = name;
    auto P = [&](float x, float y) { return glm::vec2(cx + x, y); };
    p.points = {P(-half, 0), P(half, 0), P(half, armholeY), P(shoulderHalf, length - drop),
                P(neckHalf, length), P(-neckHalf, length), P(-shoulderHalf, length - drop), P(-half, armholeY)};
    p.curves.resize(8);
    const float in = half - 45.0f;
    p.curves[2] = {true, P(in, armholeY), P(in, armholeY + 130.0f)};
    p.curves[6] = {true, P(-in, armholeY + 130.0f), P(-in, armholeY)};
    const float cy = length - 8.0f * neckDepth / 6.0f;
    p.curves[4] = {true, P(neckHalf - 5.0f, cy), P(-neckHalf + 5.0f, cy)};
    p.notches = {{1, 0.5f}, {7, 0.5f}};
    return p;
}

// A short sleeve centred on x = cx: hem (0), underarm (1), cap (2, from the +x underarm over the top), underarm (3).
Panel sleevePanel(const std::string& name, float cx, float capLength) {
    const float half = 190.0f, hemHalf = 170.0f, underarmY = 200.0f;
    Panel p;
    p.name = name;
    p.points = {{cx - hemHalf, 0}, {cx + hemHalf, 0}, {cx + half, underarmY}, {cx - half, underarmY}};
    p.curves.resize(4);
    // Cap height chosen so the cap is `capLength` long (bisection on the control points' height).
    float lo = 0.0f, hi = 400.0f;
    for (int it = 0; it < 40; ++it) {
        const float m = 0.5f * (lo + hi);
        p.curves[2] = {true, {cx + 0.55f * half, underarmY + m}, {cx - 0.55f * half, underarmY + m}};
        (edgeLength(p, 2) < capLength ? lo : hi) = m;
    }
    p.notches = {{2, 0.5f}};
    return p;
}

} // namespace

Pattern tshirtPattern(float chestMm, float lengthMm) {
    Pattern pat;
    const float half = std::clamp((chestMm + 80.0f) * 0.25f, 120.0f, 1000.0f); // 8 cm ease
    const float length = std::clamp(lengthMm, 300.0f, 1500.0f);
    pat.panels.push_back(bodyPanel("Front", 0.0f, half, length, 90.0f));
    pat.panels.push_back(bodyPanel("Back", 2.0f * half + 60.0f, half, length, 20.0f));
    // Each armhole (front + back) is the cap's length, plus 2% ease.
    const float armhole = edgeLength(pat.panels[0], 2) + edgeLength(pat.panels[1], 6);
    const float sx = 3.0f * half + 120.0f + 190.0f; // 60 mm right of the back
    pat.panels.push_back(sleevePanel("Left sleeve", sx, armhole * 1.02f));
    pat.panels.push_back(sleevePanel("Right sleeve", sx + 440.0f, armhole * 1.02f));
    std::string error;
    auto sew = [&](uint32_t pa, uint32_t ea, float a0, float a1, uint32_t pb, uint32_t eb, float b0, float b1) {
        addSeam(pat, {{pa, ea, a0, a1}, {pb, eb, b0, b1}, false}, error);
    };
    // Sides and shoulders: front +x (the wearer's left) meets the back's -x edge.
    sew(0, 1, 0, 1, 1, 7, 0, 1);
    sew(0, 7, 0, 1, 1, 1, 0, 1);
    sew(0, 3, 0, 1, 1, 5, 0, 1);
    sew(0, 5, 0, 1, 1, 3, 0, 1);
    // Left sleeve (on +X): its +x half of the cap faces back. Right sleeve: its +x half faces front.
    sew(0, 2, 0, 1, 2, 2, 0.5f, 1);
    sew(1, 6, 0, 1, 2, 2, 0, 0.5f);
    sew(0, 6, 0, 1, 3, 2, 0, 0.5f);
    sew(1, 2, 0, 1, 3, 2, 0.5f, 1);
    // Underarm seams close each sleeve into a tube.
    sew(2, 1, 0, 1, 2, 3, 0, 1);
    sew(3, 1, 0, 1, 3, 3, 0, 1);
    return pat;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

namespace {

Json num(float v) { return Json(double(std::round(double(v) * 1000.0) / 1000.0)); }
float fnum(const Json& j, float fallback) {
    const double d = j.asNumber(fallback);
    return std::isfinite(d) && std::abs(d) < 1e7 ? float(d) : fallback;
}
Json sideJson(const SeamSide& s) {
    Json j = Json::array();
    j.push(int(s.panel));
    j.push(int(s.edge));
    j.push(num(s.t0));
    j.push(num(s.t1));
    return j;
}
bool sideFromJson(const Json& j, SeamSide& s) {
    if (!j.isArray() || j.size() != 4) return false;
    const double p = j[0].asNumber(-1), e = j[1].asNumber(-1);
    if (!(p >= 0 && p < 1e6 && e >= 0 && e < 1e6)) return false;
    s = {uint32_t(p), uint32_t(e), fnum(j[2], 0.0f), fnum(j[3], 1.0f)};
    return true;
}

} // namespace

Json patternToJson(const Pattern& pat) {
    Json j = Json::object();
    j.set("version", 1);
    j.set("particle_mm", num(pat.particleDistance));
    j.set("fabric", pat.fabric);
    j.set("thickness_mm", num(pat.thickness));
    Json panels = Json::array();
    for (const Panel& p : pat.panels) {
        Json pj = Json::object();
        pj.set("name", p.name);
        Json pts = Json::array();
        for (glm::vec2 q : p.points) {
            pts.push(num(q.x));
            pts.push(num(q.y));
        }
        pj.set("points", std::move(pts));
        Json curves = Json::array();
        for (size_t e = 0; e < p.curves.size(); ++e) {
            const EdgeCurve& c = p.curves[e];
            if (!c.curved) continue;
            Json cj = Json::array();
            cj.push(int(e));
            for (float v : {c.c1.x, c.c1.y, c.c2.x, c.c2.y}) cj.push(num(v));
            curves.push(std::move(cj));
        }
        pj.set("curves", std::move(curves));
        Json notches = Json::array();
        for (const Notch& nt : p.notches) {
            Json nj = Json::array();
            nj.push(int(nt.edge));
            nj.push(num(nt.t));
            notches.push(std::move(nj));
        }
        pj.set("notches", std::move(notches));
        pj.set("grain", num(p.grainDegrees));
        pj.set("allowance_mm", num(p.seamAllowance));
        panels.push(std::move(pj));
    }
    j.set("panels", std::move(panels));
    Json seams = Json::array();
    for (const Seam& s : pat.seams) {
        Json sj = Json::object();
        sj.set("a", sideJson(s.a));
        sj.set("b", sideJson(s.b));
        if (s.flipped) sj.set("flipped", true);
        seams.push(std::move(sj));
    }
    j.set("seams", std::move(seams));
    return j;
}

bool patternFromJson(const Json& j, Pattern& out, std::string& error) {
    if (!j.isObject() || !j["panels"].isArray()) {
        error = "Not a garment pattern";
        return false;
    }
    if (j["version"].asNumber(1) > 1) {
        error = "The garment pattern was saved by a newer SculptForge";
        return false;
    }
    Pattern pat;
    pat.particleDistance = std::clamp(fnum(j["particle_mm"], 15.0f), 2.0f, 200.0f);
    if (j["fabric"].isString()) pat.fabric = j["fabric"].asString().substr(0, 32);
    pat.thickness = std::clamp(fnum(j["thickness_mm"], 2.0f), 0.0f, 100.0f);
    std::vector<int> index; // old panel index -> new (-1: left out)
    size_t totalPoints = 0, outlineBudget = kOutlineBudget;
    for (const Json& pj : j["panels"].items()) {
        index.push_back(-1);
        if (pat.panels.size() >= 512) break;
        Panel p;
        if (pj["name"].isString()) p.name = pj["name"].asString().substr(0, 128);
        const Json& pts = pj["points"];
        if (!pts.isArray() || pts.size() % 2 || pts.size() < 6 || totalPoints + pts.size() / 2 > 20000) {
            error += (error.empty() ? "" : "; ") + p.name + " left out (bad points)";
            continue;
        }
        for (size_t k = 0; k + 1 < pts.size(); k += 2) p.points.push_back({fnum(pts[k], 0.0f), fnum(pts[k + 1], 0.0f)});
        totalPoints += p.points.size();
        p.curves.resize(p.points.size());
        for (const Json& cj : pj["curves"].items()) {
            if (!cj.isArray() || cj.size() != 5) continue;
            const double e = cj[0].asNumber(-1);
            if (!(e >= 0 && e < double(p.points.size()))) continue;
            p.curves[size_t(e)] = {true, {fnum(cj[1], 0), fnum(cj[2], 0)}, {fnum(cj[3], 0), fnum(cj[4], 0)}};
        }
        for (const Json& nj : pj["notches"].items())
            if (nj.isArray() && nj.size() == 2 && nj[0].asNumber(-1) >= 0 && nj[0].asNumber(-1) < 1e6)
                p.notches.push_back({uint32_t(nj[0].asNumber(0)), fnum(nj[1], 0.5f)});
        p.grainDegrees = fnum(pj["grain"], 90.0f);
        p.seamAllowance = std::clamp(fnum(pj["allowance_mm"], 10.0f), 0.0f, 200.0f);
        normalizePanel(p);
        std::string why;
        if (!validPanel(p, why, &outlineBudget)) {
            error += (error.empty() ? "" : "; ") + why + " (left out)";
            continue;
        }
        index.back() = int(pat.panels.size());
        pat.panels.push_back(std::move(p));
    }
    for (const Json& sj : j["seams"].items()) {
        Seam s;
        if (!sideFromJson(sj["a"], s.a) || !sideFromJson(sj["b"], s.b)) continue;
        if (s.a.panel >= index.size() || s.b.panel >= index.size() || index[s.a.panel] < 0 || index[s.b.panel] < 0)
            continue;
        s.a.panel = uint32_t(index[s.a.panel]);
        s.b.panel = uint32_t(index[s.b.panel]);
        s.flipped = sj["flipped"].asBool(false);
        std::string why;
        addSeam(pat, s, why); // (unusable seams are dropped)
    }
    out = std::move(pat);
    return true;
}

bool documentPattern(const Document& doc, Pattern& out) {
    if (!doc.projectSettings.isObject() || !doc.projectSettings.has("garment")) return false;
    std::string error;
    return patternFromJson(doc.projectSettings["garment"], out, error);
}

void setDocumentPattern(Document& doc, const Pattern& pat) {
    if (!doc.projectSettings.isObject()) doc.projectSettings = Json::object();
    if (pat.empty()) {
        if (!doc.projectSettings.has("garment")) return;
        Json copy = Json::object();
        for (const auto& [k, v] : doc.projectSettings.members())
            if (k != "garment") copy.set(k, v);
        doc.projectSettings = std::move(copy);
    } else {
        doc.projectSettings.set("garment", patternToJson(pat));
    }
    doc.modified = true;
}

} // namespace sf::garment
