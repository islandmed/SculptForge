// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Garments: pattern panels, their edits, seams, the body arrangement, the Part made after the simulation, the
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
    return std::clamp(int(std::ceil(hull / 1.0f)), 16, 2048);
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

Slot mirroredSlot(Slot s) {
    switch (s) {
    case Slot::Left: return Slot::Right;
    case Slot::Right: return Slot::Left;
    case Slot::LeftArm: return Slot::RightArm;
    case Slot::RightArm: return Slot::LeftArm;
    default: return s;
    }
}

bool overlaps(const SeamSide& x, const SeamSide& y) {
    return x.panel == y.panel && x.edge == y.edge && x.t0 < y.t1 - 1e-4f && y.t0 < x.t1 - 1e-4f;
}

} // namespace

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

const char* slotId(Slot s) {
    static const char* ids[] = {"front", "back", "left", "right", "left_arm", "right_arm", "free"};
    return s < Slot::Count ? ids[int(s)] : "free";
}
const char* slotLabel(Slot s) {
    static const char* labels[] = {"Front", "Back", "Left side", "Right side", "Left arm", "Right arm", "Free"};
    return s < Slot::Count ? labels[int(s)] : "Free";
}
bool slotFromId(const std::string& id, Slot& out) {
    for (int i = 0; i < int(Slot::Count); ++i)
        if (id == slotId(Slot(i))) {
            out = Slot(i);
            return true;
        }
    return false;
}

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
    const int n = std::clamp(int(std::ceil(std::sqrt(0.75f * bend / std::max(tolerance, 0.01f)))), 4, 512);
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

bool validPanel(const Panel& p, std::string& error) {
    if (p.points.size() < 3) {
        error = p.name + " needs at least 3 points";
        return false;
    }
    for (glm::vec2 q : p.points)
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || std::abs(q.x) > 1e6f || std::abs(q.y) > 1e6f) {
            error = p.name + " has a point out of range";
            return false;
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
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            if (segmentsCross(o[i], o[(i + 1) % n], o[j], o[(j + 1) % n])) {
                error = p.name + "'s outline crosses itself";
                return false;
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
    pat.panels[j].place.slot = mirroredSlot(pat.panels[j].place.slot);
    pat.panels[j].place.offset.x = -pat.panels[j].place.offset.x;
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
// Arrangement
// ---------------------------------------------------------------------------

BodyFrame defaultBody() {
    BodyFrame b;
    b.bounds.min = {-0.85f, 0.0f, -0.13f};
    b.bounds.max = {0.85f, 1.75f, 0.13f};
    b.shoulderY = 1.45f;
    b.torsoHalfWidth = 0.17f;
    b.torsoFrontZ = 0.11f;
    b.torsoBackZ = -0.11f;
    b.arms = true;
    b.leftShoulder = {0.17f, 1.40f, 0.0f};
    b.rightShoulder = {-0.17f, 1.40f, 0.0f};
    b.armRadius = 0.05f;
    return b;
}

BodyFrame analyzeBody(const Mesh& body) {
    BodyFrame f;
    if (body.positions.empty()) return defaultBody();
    for (const glm::vec3& p : body.positions) f.bounds.expand(p);
    const float h = f.bounds.extent().y;
    const float y0 = f.bounds.min.y;
    f.centerX = f.bounds.center().x;
    f.centerZ = f.bounds.center().z;
    if (!(h > 1e-6f)) return defaultBody();
    // Torso width: x coverage of the chest band by triangles (not vertices: a coarse mesh has gaps between
    // them), from the middle out to the first empty gap (arms held away).
    const float binW = h / 175.0f; // 1 cm on a 1.75 m figure
    const int bins = 400;
    std::vector<int> occ(bins, 0);
    const float bandLo = y0 + 0.58f * h, bandHi = y0 + 0.70f * h;
    auto cover = [&](float xa, float xb) {
        // Both sides of the middle fold onto |x - centre|.
        const float lo = xa - f.centerX, hi = xb - f.centerX;
        const float a = (lo <= 0.0f && hi >= 0.0f) ? 0.0f : std::min(std::abs(lo), std::abs(hi));
        const float b = std::max(std::abs(lo), std::abs(hi));
        for (int k = int(a / binW); k <= int(b / binW) && k < bins; ++k) ++occ[size_t(k)];
    };
    for (const glm::uvec3& t : body.triangles) {
        if (t.x >= body.positions.size() || t.y >= body.positions.size() || t.z >= body.positions.size()) continue;
        const glm::vec3 a = body.positions[t.x], b = body.positions[t.y], c = body.positions[t.z];
        const float ylo = std::min({a.y, b.y, c.y}), yhi = std::max({a.y, b.y, c.y});
        if (yhi < bandLo || ylo > bandHi) continue;
        const float xlo = std::min({a.x, b.x, c.x}), xhi = std::max({a.x, b.x, c.x});
        if (xlo < f.centerX && xhi > f.centerX) {
            cover(xlo, f.centerX);
            cover(f.centerX, xhi);
        } else {
            cover(xlo, xhi);
        }
    }
    if (body.triangles.empty())
        for (const glm::vec3& p : body.positions)
            if (p.y >= bandLo && p.y <= bandHi) cover(p.x, p.x);
    int edge = 0;
    for (int k = 0; k < bins; ++k) {
        if (occ[size_t(k)]) edge = k;
        else if (edge > 0 || k > 2) break;
    }
    f.torsoHalfWidth = std::max(binW, (float(edge) + 1.0f) * binW);
    // Depth: the front and back of the chest band near the middle.
    float front = f.centerZ, back = f.centerZ;
    bool any = false;
    for (const glm::vec3& p : body.positions)
        if (p.y >= bandLo && p.y <= bandHi && std::abs(p.x - f.centerX) < 0.5f * f.torsoHalfWidth) {
            front = any ? std::max(front, p.z) : p.z;
            back = any ? std::min(back, p.z) : p.z;
            any = true;
        }
    f.torsoFrontZ = front;
    f.torsoBackZ = back;
    if (any) f.centerZ = 0.5f * (front + back);
    // Shoulders: the highest point over the outer half of the torso's width (the neck and head are narrower,
    // arms held out are further out), below the top of the head.
    f.shoulderY = y0 + 0.83f * h;
    float top = -std::numeric_limits<float>::max();
    for (const glm::vec3& p : body.positions) {
        const float dx = std::abs(p.x - f.centerX);
        if (dx >= 0.5f * f.torsoHalfWidth && dx <= f.torsoHalfWidth && p.y > bandLo && p.y < y0 + 0.95f * h) top = std::max(top, p.y);
    }
    if (top > bandLo) f.shoulderY = top;
    // Arms: vertices well outside the torso above the waist, one slab near the shoulder and one further out.
    f.armRadius = 0.03f * h;
    for (int side = 0; side < 2; ++side) {
        const float s = side == 0 ? 1.0f : -1.0f;
        const float x1 = 1.4f * f.torsoHalfWidth, x2 = x1 + 0.15f * h, slab = 0.08f * h;
        glm::dvec3 m1(0.0), m2(0.0);
        size_t n1 = 0, n2 = 0;
        for (const glm::vec3& p : body.positions) {
            if (p.y < y0 + 0.5f * h) continue;
            const float dx = s * (p.x - f.centerX);
            if (dx >= x1 && dx < x1 + slab) {
                m1 += glm::dvec3(p);
                ++n1;
            } else if (dx >= x2 && dx < x2 + slab) {
                m2 += glm::dvec3(p);
                ++n2;
            }
        }
        if (n1 < 8) continue;
        const glm::vec3 p1 = glm::vec3(m1 / double(n1));
        glm::vec3 dir(s, 0.0f, 0.0f);
        if (n2 >= 8) dir = glm::normalize(glm::vec3(m2 / double(n2)) - p1);
        if (dir.x * s < 0.3f) continue; // not an arm held out
        // Radius: the mean distance of the near slab's vertices from the arm's line.
        double r = 0.0;
        for (const glm::vec3& p : body.positions) {
            if (p.y < y0 + 0.5f * h) continue;
            const float dx = s * (p.x - f.centerX);
            if (dx >= x1 && dx < x1 + slab) {
                const glm::vec3 d = p - p1;
                r += double(glm::length(d - glm::dot(d, dir) * dir));
            }
        }
        const float radius = float(r / double(n1));
        // The shoulder: back along the arm to the torso's side.
        const float back = (s * (p1.x - f.centerX) - f.torsoHalfWidth) / std::max(0.3f, std::abs(dir.x));
        const glm::vec3 shoulder = p1 - dir * back;
        f.arms = true;
        f.armRadius = side == 0 ? radius : 0.5f * (f.armRadius + radius);
        (side == 0 ? f.leftShoulder : f.rightShoulder) = shoulder;
        (side == 0 ? f.leftArmDir : f.rightArmDir) = dir;
    }
    if (!f.arms) { // arms down or none: sleeves start at the shoulders, pointing out and down
        f.leftShoulder = {f.centerX + f.torsoHalfWidth, f.shoulderY - 0.03f * h, f.centerZ};
        f.rightShoulder = {f.centerX - f.torsoHalfWidth, f.shoulderY - 0.03f * h, f.centerZ};
        f.leftArmDir = glm::normalize(glm::vec3(0.3f, -1.0f, 0.0f));
        f.rightArmDir = glm::normalize(glm::vec3(-0.3f, -1.0f, 0.0f));
    }
    return f;
}

void autoSlots(Pattern& pat) {
    auto has = [](std::string s, const char* w) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s.find(w) != std::string::npos;
    };
    int sleeves = 0;
    for (Panel& p : pat.panels) {
        if (p.place.slot != Slot::Front) continue; // chosen already
        if (has(p.name, "back")) p.place.slot = Slot::Back;
        else if (has(p.name, "sleeve") || has(p.name, "arm")) {
            const bool right = has(p.name, "right") || (!has(p.name, "left") && sleeves % 2 == 1);
            p.place.slot = right ? Slot::RightArm : Slot::LeftArm;
            ++sleeves;
        } else if (has(p.name, "side")) {
            p.place.slot = has(p.name, "right") ? Slot::Right : Slot::Left;
        }
    }
}

namespace {

// Arc length of an ellipse quarter from angle 0, tabulated, so a panel's x (arc length) maps to an angle.
struct EllipseArc {
    float a, b; // semi-axes along S and R0
    std::vector<float> len; // at angles i / N * pi
    static constexpr int N = 128;
    EllipseArc(float aa, float bb) : a(aa), b(bb), len(N + 1) {
        float l = 0.0f;
        glm::vec2 prev(0.0f, b);
        for (int i = 0; i <= N; ++i) {
            const float th = float(i) / float(N) * kPi;
            const glm::vec2 q(a * std::sin(th), b * std::cos(th));
            if (i) l += glm::length(q - prev);
            prev = q;
            len[size_t(i)] = l;
        }
    }
    // (along S, along R0) at arc length x from the front point (x < 0 goes the other way; past half: straight on).
    glm::vec2 at(float x) const {
        const float s = x < 0 ? -1.0f : 1.0f;
        const float want = std::abs(x);
        if (want >= len.back()) return {s * 0.0f, -b - (want - len.back())}; // (never this far in practice)
        const auto it = std::lower_bound(len.begin(), len.end(), want);
        const size_t i = size_t(std::max<ptrdiff_t>(1, it - len.begin()));
        const float f = len[i] > len[i - 1] ? (want - len[i - 1]) / (len[i] - len[i - 1]) : 0.0f;
        const float th = (float(i - 1) + f) / float(N) * kPi;
        return {s * a * std::sin(th), b * std::cos(th)};
    }
};

struct SlotFrame {
    glm::vec3 c, axis, out, side; // panel top centre on the axis; +y of the panel; facing; +x of the panel
    float across = 0.1f, depth = 0.1f; // ellipse semi-axes (side, out)
};

SlotFrame slotFrame(const Panel& p, const BodyFrame& b, float gap) {
    SlotFrame f;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float frontD = std::max(0.02f, b.torsoFrontZ - b.centerZ), backD = std::max(0.02f, b.centerZ - b.torsoBackZ);
    const glm::vec3 torso(b.centerX, b.shoulderY, b.centerZ);
    switch (p.place.slot) {
    case Slot::Front: f = {torso, up, {0, 0, 1}, {}, b.torsoHalfWidth, frontD}; break;
    case Slot::Back: f = {torso, up, {0, 0, -1}, {}, b.torsoHalfWidth, backD}; break;
    case Slot::Left: f = {torso, up, {1, 0, 0}, {}, std::max(frontD, backD), b.torsoHalfWidth}; break;
    case Slot::Right: f = {torso, up, {-1, 0, 0}, {}, std::max(frontD, backD), b.torsoHalfWidth}; break;
    case Slot::LeftArm:
    case Slot::RightArm: {
        const bool left = p.place.slot == Slot::LeftArm;
        const glm::vec3 dir = glm::normalize(left ? b.leftArmDir : b.rightArmDir);
        f.c = left ? b.leftShoulder : b.rightShoulder;
        f.axis = -dir; // the sleeve's top (cap) points back to the shoulder
        glm::vec3 o = up - glm::dot(up, f.axis) * f.axis;
        if (glm::length(o) < 1e-3f) o = glm::vec3(0, 0, 1) - glm::dot(glm::vec3(0, 0, 1), f.axis) * f.axis;
        f.out = glm::normalize(o);
        f.across = f.depth = b.armRadius;
        break;
    }
    default: f = {torso + glm::vec3(0, 0, b.torsoFrontZ - b.centerZ + 0.25f), up, {0, 0, 1}, {}, 0, 0}; break;
    }
    f.side = glm::cross(f.axis, f.out); // so that (side x axis) faces out: counter-clockwise panels face away
    f.across += gap + p.place.offset.z;
    f.depth += gap + p.place.offset.z;
    return f;
}

} // namespace

glm::vec3 placePoint(const Panel& p, glm::vec2 flat, const BodyFrame& body, float gap) {
    const glm::vec2 lo = panelMin(p), hi = panelMax(p);
    const float x = (flat.x - 0.5f * (lo.x + hi.x)) * 0.001f + p.place.offset.x;
    const float y = (flat.y - hi.y) * 0.001f + p.place.offset.y;
    const SlotFrame f = slotFrame(p, body, gap);
    if (!p.place.wrap || p.place.slot == Slot::Free) return f.c + f.axis * y + f.out * f.depth + f.side * x;
    const EllipseArc arc(f.across, f.depth);
    const glm::vec2 q = arc.at(x);
    return f.c + f.axis * y + f.side * q.x + f.out * q.y;
}

void placePattern(const Pattern& pat, const GarmentMesh& m, const BodyFrame& body, std::vector<glm::vec3>& placed,
                  std::vector<glm::vec3>& rest, float gap) {
    placed.resize(m.flat.size());
    rest.resize(m.flat.size());
    // One frame and arc table per panel (placePoint would rebuild them per vertex).
    for (size_t pi = 0; pi + 1 < m.panelFirstVertex.size() && pi < pat.panels.size(); ++pi) {
        const Panel& p = pat.panels[pi];
        const glm::vec2 lo = panelMin(p), hi = panelMax(p);
        const SlotFrame f = slotFrame(p, body, gap);
        const EllipseArc arc(f.across, f.depth);
        const bool wrap = p.place.wrap && p.place.slot != Slot::Free;
        for (uint32_t v = m.panelFirstVertex[pi]; v < m.panelFirstVertex[pi + 1]; ++v) {
            const glm::vec2 q = m.flat[v];
            const float x = (q.x - 0.5f * (lo.x + hi.x)) * 0.001f + p.place.offset.x;
            const float y = (q.y - hi.y) * 0.001f + p.place.offset.y;
            if (wrap) {
                const glm::vec2 e = arc.at(x);
                placed[v] = f.c + f.axis * y + f.side * e.x + f.out * e.y;
            } else {
                placed[v] = f.c + f.axis * y + f.out * f.depth + f.side * x;
            }
            rest[v] = glm::vec3(q * 0.001f, 0.0f);
        }
    }
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
    pat.panels[1].place.slot = Slot::Back;
    // Each armhole (front + back) is the cap's length, plus 2% ease.
    const float armhole = edgeLength(pat.panels[0], 2) + edgeLength(pat.panels[1], 6);
    const float sx = 3.0f * half + 120.0f + 190.0f; // 60 mm right of the back
    pat.panels.push_back(sleevePanel("Left sleeve", sx, armhole * 1.02f));
    pat.panels.push_back(sleevePanel("Right sleeve", sx + 440.0f, armhole * 1.02f));
    pat.panels[2].place.slot = Slot::LeftArm;
    pat.panels[3].place.slot = Slot::RightArm;
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
        pj.set("slot", slotId(p.place.slot));
        Json off = Json::array();
        for (int k = 0; k < 3; ++k) off.push(num(p.place.offset[k]));
        pj.set("offset", std::move(off));
        pj.set("wrap", p.place.wrap);
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
    size_t totalPoints = 0;
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
        Slot s;
        if (pj["slot"].isString() && slotFromId(pj["slot"].asString(), s)) p.place.slot = s;
        if (pj["offset"].isArray() && pj["offset"].size() == 3)
            for (int k = 0; k < 3; ++k) p.place.offset[k] = std::clamp(fnum(pj["offset"][size_t(k)], 0.0f), -10.0f, 10.0f);
        p.place.wrap = pj["wrap"].asBool(true);
        normalizePanel(p);
        std::string why;
        if (!validPanel(p, why)) {
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
