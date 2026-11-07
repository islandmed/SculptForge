// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Tube.h"

#include "core/Commands.h"
#include "core/Parts.h"
#include "core/PathStroke.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace sf {

using glm::vec3;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTurnPerRing = 10.0f * kPi / 180.0f; // a new ring at least every 10 degrees of bend
constexpr size_t kMaxRings = 200000;
constexpr int kMaxSides = 256;
constexpr int kMaxCapRings = 64;

bool finite(const vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// The points without consecutive duplicates (which would give the spline a zero-length span).
std::vector<TubePoint> distinctPoints(const std::vector<TubePoint>& in) {
    std::vector<TubePoint> out;
    float rmax = 0.0f;
    for (const TubePoint& p : in) rmax = std::max(rmax, p.radius);
    const float eps = std::max(rmax, 1e-6f) * 1e-4f;
    for (const TubePoint& p : in)
        if (out.empty() || glm::distance(out.back().position, p.position) > eps) out.push_back(p);
    return out;
}

// Barry-Goldman evaluation of a centripetal Catmull-Rom span (p1 -> p2) at knot t in [t1, t2].
template <class V> V lerpKnots(const V& a, const V& b, float ta, float tb, float t) {
    const float d = tb - ta;
    return d > 0.0f ? a * ((tb - t) / d) + b * ((t - ta) / d) : a;
}
template <class V>
V catmullRom(const V& p0, const V& p1, const V& p2, const V& p3, float t0, float t1, float t2, float t3, float t) {
    const V a1 = lerpKnots(p0, p1, t0, t1, t), a2 = lerpKnots(p1, p2, t1, t2, t), a3 = lerpKnots(p2, p3, t2, t3, t);
    const V b1 = lerpKnots(a1, a2, t0, t2, t), b2 = lerpKnots(a2, a3, t1, t3, t);
    return lerpKnots(b1, b2, t1, t2, t);
}

// Any unit vector at right angles to `t`.
vec3 perpendicular(const vec3& t) {
    const vec3 a = std::abs(t.x) < 0.6f ? vec3(1, 0, 0) : std::abs(t.y) < 0.6f ? vec3(0, 1, 0) : vec3(0, 0, 1);
    return glm::normalize(a - t * glm::dot(a, t));
}

struct Ring {
    vec3 center, n, b;
    float radius; // 0 = a pole (one vertex)
    int sides;
    uint32_t group; // of the band from this ring to the next
};

// Builds the triangles of one or more tubes into the same arrays.
struct Builder {
    Mesh m;
    std::vector<uint32_t> groups;
    bool quads = true; // pair matching bands into quads (not for even-edge sweeps: triangles for dynamic topology)

    uint32_t addRing(const Ring& r) {
        const uint32_t first = uint32_t(m.positions.size());
        if (r.sides <= 1) {
            m.positions.push_back(r.center);
            return first;
        }
        for (int j = 0; j < r.sides; ++j) {
            const float a = 2.0f * kPi * float(j) / float(r.sides);
            m.positions.push_back(r.center + r.radius * (std::cos(a) * r.n + std::sin(a) * r.b));
        }
        return first;
    }
    void tri(uint32_t a, uint32_t b, uint32_t c, uint32_t g) {
        m.triangles.emplace_back(a, b, c);
        groups.push_back(g);
    }
    // Joins ring A (na vertices from a0) to ring B further along the curve: quads when they match, else a strip
    // walking both rings by angle. Outward winding: (A_j, A_j+1, B_j+1) faces away from the centre line.
    void stitch(uint32_t a0, int na, uint32_t b0, int nb, uint32_t g) {
        auto A = [&](int i) { return a0 + uint32_t(na <= 1 ? 0 : i % na); };
        auto B = [&](int j) { return b0 + uint32_t(nb <= 1 ? 0 : j % nb); };
        if (na == nb && na > 1) {
            for (int j = 0; j < na; ++j) {
                tri(A(j), A(j + 1), B(j + 1), g);
                tri(A(j), B(j + 1), B(j), g);
                if (quads) m.pairQuad(uint32_t(m.triangles.size() - 2), uint32_t(m.triangles.size() - 1));
            }
            return;
        }
        const int ea = na <= 1 ? 0 : na, eb = nb <= 1 ? 0 : nb; // edges along each ring
        int i = 0, j = 0;
        while (i < ea || j < eb) {
            const bool advanceA = j >= eb || (i < ea && float(i + 1) * float(eb) <= float(j + 1) * float(ea));
            if (advanceA) {
                tri(A(i), A(i + 1), B(j), g);
                ++i;
            } else {
                tri(A(i), B(j + 1), B(j), g);
                ++j;
            }
        }
    }

    void sweep(const TubeShape& shape, bool uniform, float edge, uint32_t groupOffset) {
        const std::vector<TubeSample> line = tubeCenterline(shape, edge);
        if (line.empty()) return;
        quads = !uniform;
        const bool flat = shape.flatCaps && line.size() > 1;
        float rmax = 0.0f;
        for (const TubeSample& s : line) rmax = std::max(rmax, s.radius);
        int fixedSides = std::clamp(int(std::lround(2.0f * kPi * rmax / edge)), 6, kMaxSides);
        fixedSides += fixedSides & 1;
        auto sidesFor = [&](float radius, int least) {
            if (radius <= 0.0f) return 1;
            return uniform ? std::clamp(int(std::lround(2.0f * kPi * radius / edge)), least, kMaxSides) : fixedSides;
        };

        const std::vector<vec3> normals = tubeFrames(line);

        std::vector<Ring> rings;
        auto cap = [&](const TubeSample& s, const vec3& n, bool start) {
            const vec3 out = start ? -s.tangent : s.tangent;
            const vec3 b = glm::cross(s.tangent, n);
            const uint32_t g = groupOffset + (start ? kTubeStartCapGroup : kTubeEndCapGroup);
            const int k = flat ? std::clamp(int(std::lround(s.radius / edge)), 1, kMaxCapRings)
                               : std::clamp(int(std::lround(0.5f * kPi * s.radius / edge)), 2, kMaxCapRings);
            std::vector<Ring> part; // from the pole towards the body ring
            for (int i = 0; i < k; ++i) {
                Ring r;
                r.n = n;
                r.b = b;
                r.group = g;
                if (flat) {
                    r.center = s.position;
                    r.radius = s.radius * float(i) / float(k);
                } else {
                    const float a = 0.5f * kPi * float(i) / float(k); // from the pole
                    r.center = s.position + out * (s.radius * std::cos(a));
                    r.radius = s.radius * std::sin(a);
                }
                r.sides = sidesFor(r.radius, 3);
                part.push_back(r);
            }
            if (start) rings.insert(rings.end(), part.begin(), part.end());
            else rings.insert(rings.end(), part.rbegin(), part.rend());
        };
        cap(line.front(), normals.front(), true);
        for (size_t i = 0; i < line.size(); ++i) {
            const TubeSample& s = line[i];
            rings.push_back({s.position, normals[i], glm::cross(s.tangent, normals[i]), s.radius, sidesFor(s.radius, 6),
                             groupOffset + (i + 1 < line.size() ? kTubeBodyGroup : kTubeEndCapGroup)});
        }
        cap(line.back(), normals.back(), false);

        uint32_t prev = addRing(rings[0]);
        for (size_t i = 1; i < rings.size(); ++i) {
            const uint32_t cur = addRing(rings[i]);
            stitch(prev, rings[i - 1].sides, cur, rings[i].sides, rings[i - 1].group);
            prev = cur;
        }
    }

    Mesh finish() {
        m.facegroups = std::move(groups);
        // pairQuad grows the pairs only up to the last quad; a shorter array would be dropped as stale.
        if (!m.quadPartners.empty()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad);
        m.rebuildTopology();
        m.recomputeAllNormals();
        return std::move(m);
    }
};

// The average edge length of a mesh, from up to ~3000 of its edges.
float averageEdge(const Mesh& m) {
    const size_t nt = m.triangleCount();
    if (nt == 0) return 0.0f;
    const size_t step = std::max<size_t>(1, nt / 1000);
    double sum = 0.0;
    size_t n = 0;
    for (size_t t = 0; t < nt; t += step)
        for (int k = 0; k < 3; ++k) {
            sum += glm::distance(m.positions[m.triangles[t][k]], m.positions[m.triangles[t][(k + 1) % 3]]);
            ++n;
        }
    return float(sum / double(n));
}

float averageRadius(const TubeShape& s) {
    double r = 0.0;
    for (const TubePoint& p : s.points) r += p.radius;
    return s.points.empty() ? 0.0f : float(r / double(s.points.size()));
}

std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4g", v);
    return b;
}

} // namespace

std::string tubeValidate(const TubeShape& s) {
    if (s.points.empty()) return "a tube needs at least one point";
    if (s.points.size() > kTubeMaxPoints) return "at most " + std::to_string(kTubeMaxPoints) + " points";
    for (const TubePoint& p : s.points) {
        if (!finite(p.position)) return "point positions must be finite";
        if (!(p.radius > 0.0f) || !std::isfinite(p.radius)) return "radii must be above 0";
    }
    if (!(s.edge >= 0.0f) || !std::isfinite(s.edge)) return "edge length must be 0 (automatic) or more";
    return {};
}

float tubeEdgeLength(const TubeShape& s) {
    if (s.edge > 0.0f) return s.edge;
    return std::max(2.0f * kPi * averageRadius(s) / 16.0f, 1e-6f);
}

std::vector<TubePoint> tubeFromStroke(const std::vector<TubePoint>& samples, float smoothing, size_t maxPoints) {
    std::vector<TubePoint> pts;
    float rmax = 0.0f; // a dab is a ball as big as the firmest touch, even one dropped as too close below
    for (const TubePoint& p : samples) {
        if (!finite(p.position) || !(p.radius > 0.0f) || !std::isfinite(p.radius)) continue;
        rmax = std::max(rmax, p.radius);
        if (pts.empty() || glm::distance(pts.back().position, p.position) >= 0.15f * std::min(p.radius, pts.back().radius))
            pts.push_back(p);
    }
    if (pts.size() == 1) pts[0].radius = rmax;
    if (pts.size() < 2) return pts;
    if (glm::distance(samples.back().position, pts.back().position) > 0.0f && finite(samples.back().position) &&
        samples.back().radius > 0.0f)
        pts.back() = samples.back(); // end where the pen lifted
    // Relax (Laplacian, the ends fixed).
    const int passes = int(std::lround(std::clamp(smoothing, 0.0f, 1.0f) * 24.0f));
    std::vector<TubePoint> tmp = pts;
    for (int k = 0; k < passes; ++k) {
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            tmp[i].position = 0.5f * pts[i].position + 0.25f * (pts[i - 1].position + pts[i + 1].position);
            tmp[i].radius = 0.5f * pts[i].radius + 0.25f * (pts[i - 1].radius + pts[i + 1].radius);
        }
        std::swap(tmp, pts);
    }
    // Resample along the length: about 1.25 radii apart, at most maxPoints.
    std::vector<float> along(pts.size(), 0.0f);
    double rsum = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) along[i] = along[i - 1] + glm::distance(pts[i - 1].position, pts[i].position);
    for (const TubePoint& p : pts) rsum += p.radius;
    const float length = along.back(), rmean = float(rsum / double(pts.size()));
    if (length < rmean) { // a dab: one ball
        TubePoint p = pts[pts.size() / 2];
        p.radius = rmax;
        return {p};
    }
    maxPoints = std::max<size_t>(maxPoints, 2);
    const float spacing = std::max(1.25f * rmean, length / float(maxPoints - 1));
    const size_t count = std::clamp<size_t>(size_t(std::ceil(length / spacing)) + 1, 2, maxPoints);
    std::vector<TubePoint> out;
    out.reserve(count);
    size_t seg = 0;
    for (size_t k = 0; k < count; ++k) {
        const float s = length * float(k) / float(count - 1);
        while (seg + 2 < pts.size() && along[seg + 1] < s) ++seg;
        const float d = along[seg + 1] - along[seg];
        const float f = d > 0.0f ? std::clamp((s - along[seg]) / d, 0.0f, 1.0f) : 0.0f;
        out.push_back({glm::mix(pts[seg].position, pts[seg + 1].position, f), glm::mix(pts[seg].radius, pts[seg + 1].radius, f)});
    }
    return out;
}

std::vector<TubeSample> tubeCenterline(const TubeShape& shape, float edge) {
    const std::vector<TubePoint> p = distinctPoints(shape.points);
    if (p.empty()) return {};
    if (p.size() == 1) return {{p[0].position, p[0].radius, vec3(0, 0, 1)}};
    edge = std::max(edge, 1e-6f);
    double length = 0.0;
    for (size_t i = 1; i < p.size(); ++i) length += glm::distance(p[i - 1].position, p[i].position);
    if (length / edge > double(kMaxRings)) edge = float(length / double(kMaxRings));

    // Dense samples along each span (centripetal knots; phantom ends mirror the neighbours).
    const size_t n = p.size();
    auto P = [&](long i) -> TubePoint {
        if (i < 0) return {2.0f * p[0].position - p[1].position, p[0].radius};
        if (i >= long(n)) return {2.0f * p[n - 1].position - p[n - 2].position, p[n - 1].radius};
        return p[size_t(i)];
    };
    std::vector<vec3> dp;
    std::vector<float> dr;
    for (size_t i = 0; i + 1 < n; ++i) {
        const TubePoint a = P(long(i) - 1), b = P(long(i)), c = P(long(i) + 1), d = P(long(i) + 2);
        const float t0 = 0.0f;
        const float t1 = t0 + std::max(std::sqrt(glm::distance(a.position, b.position)), 1e-6f);
        const float t2 = t1 + std::max(std::sqrt(glm::distance(b.position, c.position)), 1e-6f);
        const float t3 = t2 + std::max(std::sqrt(glm::distance(c.position, d.position)), 1e-6f);
        const float chord = glm::distance(b.position, c.position);
        const int m = std::clamp(int(std::ceil(chord / (0.25f * edge))), 2, 512);
        const float lo = std::min(b.radius, c.radius), hi = std::max(b.radius, c.radius);
        for (int k = 0; k < m; ++k) {
            const float t = t1 + (t2 - t1) * float(k) / float(m);
            dp.push_back(catmullRom(a.position, b.position, c.position, d.position, t0, t1, t2, t3, t));
            dr.push_back(std::clamp(catmullRom(a.radius, b.radius, c.radius, d.radius, t0, t1, t2, t3, t), lo, hi));
        }
    }
    dp.push_back(p.back().position);
    dr.push_back(p.back().radius);

    std::vector<vec3> dt(dp.size());
    for (size_t i = 0; i < dp.size(); ++i) {
        const vec3 d = dp[std::min(i + 1, dp.size() - 1)] - dp[i > 0 ? i - 1 : 0];
        dt[i] = glm::length(d) > 1e-12f ? glm::normalize(d) : (i > 0 ? dt[i - 1] : vec3(0, 0, 1));
    }
    // Rings: an edge length apart, closer where the curve turns.
    std::vector<TubeSample> out{{dp[0], dr[0], dt[0]}};
    float acc = 0.0f;
    vec3 last = dt[0];
    const float cosTurn = std::cos(kTurnPerRing);
    for (size_t i = 1; i + 1 < dp.size(); ++i) {
        acc += glm::distance(dp[i - 1], dp[i]);
        if (acc >= edge || glm::dot(dt[i], last) < cosTurn) {
            out.push_back({dp[i], dr[i], dt[i]});
            acc = 0.0f;
            last = dt[i];
        }
    }
    acc += glm::distance(dp[dp.size() - 2], dp.back());
    if (out.size() > 1 && acc < 0.3f * edge) out.back() = {dp.back(), dr.back(), dt.back()};
    else out.push_back({dp.back(), dr.back(), dt.back()});
    return out;
}

std::vector<vec3> tubeFrames(const std::vector<TubeSample>& line) {
    // Rotation-minimizing frames by double reflection (Wang, Juttler, Zheng and Liu 2008).
    std::vector<vec3> normals(line.size());
    if (line.empty()) return normals;
    normals[0] = perpendicular(line[0].tangent);
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        const vec3 v1 = line[i + 1].position - line[i].position;
        const float c1 = glm::dot(v1, v1);
        vec3 r = normals[i];
        if (c1 > 1e-20f) {
            const vec3 rl = r - (2.0f / c1) * glm::dot(v1, r) * v1;
            const vec3 tl = line[i].tangent - (2.0f / c1) * glm::dot(v1, line[i].tangent) * v1;
            const vec3 v2 = line[i + 1].tangent - tl;
            const float c2 = glm::dot(v2, v2);
            r = c2 > 1e-20f ? rl - (2.0f / c2) * glm::dot(v2, rl) * v2 : rl;
        }
        const vec3 t = line[i + 1].tangent;
        r -= t * glm::dot(r, t);
        normals[i + 1] = glm::length(r) > 1e-12f ? glm::normalize(r) : perpendicular(t);
    }
    return normals;
}

TubeShape tubeMirrored(const TubeShape& shape) {
    TubeShape m = shape;
    for (TubePoint& p : m.points) p.position.x = -p.position.x;
    return m;
}

Mesh tubeSweep(const TubeShape& shape, bool uniform, float edge) {
    if (!tubeValidate(shape).empty()) return {};
    if (!(edge > 0.0f)) edge = tubeEdgeLength(shape);
    Builder b;
    b.sweep(shape, uniform, edge, 0);
    if (shape.mirrorX) {
        float xmax = 0.0f, rmax = 0.0f;
        for (const TubePoint& p : shape.points) {
            xmax = std::max(xmax, std::abs(p.position.x));
            rmax = std::max(rmax, p.radius);
        }
        if (xmax > 1e-4f * rmax) b.sweep(tubeMirrored(shape), uniform, edge, kTubeEndCapGroup); // not on the plane
    }
    return b.finish();
}

Mesh tubeMesh(const TubeShape& shape, size_t maxTriangles) {
    float edge = tubeEdgeLength(shape);
    if (maxTriangles > 0 && !shape.points.empty()) { // estimate: 2 triangles per side per ring
        float length = 0.0f, rmax = 0.0f;
        for (size_t i = 0; i < shape.points.size(); ++i) {
            rmax = std::max(rmax, shape.points[i].radius);
            if (i) length += glm::distance(shape.points[i - 1].position, shape.points[i].position);
        }
        const double sides = std::clamp(2.0 * kPi * rmax / edge, 6.0, double(kMaxSides));
        const double est = 2.0 * sides * (length / edge + 2.0 * kPi * rmax / edge) * (shape.mirrorX ? 2.0 : 1.0);
        if (est > double(maxTriangles)) edge *= float(std::sqrt(est / double(maxTriangles)));
    }
    Mesh m = tubeSweep(shape, false, edge);
    if (m.triangleCount() > 0) m.tube = shape;
    return m;
}

CommandResult tubeAdd(Document& doc, const TubeShape& shape, bool merge, const std::string& name, int* edited) {
    if (const std::string err = tubeValidate(shape); !err.empty()) return {false, "Tube: " + err};
    if (merge && (doc.mesh.multires || doc.mesh.micro))
        return {false, "Tube: the active Part has subdivision levels or a micro level; draw the tube as a new Part"};
    if (merge && doc.mesh.triangleCount() > 0) {
        const float r = averageRadius(shape);
        const float own = averageEdge(doc.mesh);
        const float edge = shape.edge > 0.0f ? shape.edge
                                             : std::clamp(own > 0.0f ? own : tubeEdgeLength(shape), 2.0f * kPi * r / 48.0f,
                                                          2.0f * kPi * r / 8.0f);
        const Mesh tube = tubeSweep(shape, true, edge);
        if (tube.triangleCount() == 0) return {false, "Tube: nothing to sweep"};
        const bool wasTube = !doc.mesh.tube.empty();
        Mesh merged = mergeMeshes({&doc.mesh, &tube});
        merged.tube = {};
        const size_t added = tube.triangleCount();
        doc.replaceMeshUndoable(std::move(merged), "Tube");
        if (edited) *edited = doc.activePart;
        return {true, "Tube merged into \"" + doc.activePartInfo().name + "\": " + std::to_string(added) + " triangles" +
                          (wasTube ? " (that Part's own tube is baked now)" : "")};
    }
    Mesh m = tubeMesh(shape);
    if (m.triangleCount() == 0) return {false, "Tube: nothing to sweep"};
    const size_t faces = m.triangleCount() - m.quadCount();
    const std::string partName = doc.uniquePartName(name.empty() ? "Tube" : name);
    const int at = doc.addPart(std::move(m), partName, false, doc.activePart + 1);
    UndoRecord r; // undo takes the new Part out again
    r.label = "Tube";
    r.part = std::make_shared<PartStep>();
    r.part->id = doc.parts[size_t(at)].id;
    r.part->at = at;
    doc.undo.push(std::move(r));
    doc.modified = true;
    if (edited) *edited = at;
    return {true, "Added Tube Part \"" + partName + "\": " + std::to_string(shape.points.size()) + " points, " +
                      std::to_string(faces) + " faces"};
}

bool tubeMatchesMesh(const Mesh& mesh) {
    if (mesh.tube.empty()) return false;
    const Mesh s = tubeSweep(mesh.tube);
    const size_t n = s.vertexCount();
    if (n != mesh.vertexCount() || s.triangleCount() != mesh.triangleCount() || n == 0) return false;
    float scale = 0.0f;
    for (const TubePoint& p : mesh.tube.points) scale = std::max(scale, p.radius);
    const float eps = 1e-4f * scale;
    const size_t step = std::max<size_t>(1, n / 64);
    for (size_t v = 0; v < n; v += step)
        if (glm::distance(s.positions[v], mesh.positions[v]) > eps) return false;
    return glm::distance(s.positions[n - 1], mesh.positions[n - 1]) <= eps;
}

CommandResult tubeEdit(Document& doc, TubeShape shape, const std::string& label) {
    if (doc.mesh.tube.empty()) return {false, "The active Part is not a Tube Part (tube_new makes one)"};
    if (!tubeMatchesMesh(doc.mesh))
        return {false, "Tube: this Part was moved or changed since the tube was drawn, so its points no longer fit; "
                       "Bake keeps it as it is (or undo the change to edit the points)"};
    if (const std::string err = tubeValidate(shape); !err.empty()) return {false, "Tube: " + err};
    doc.replaceMeshUndoable(tubeMesh(shape), label);
    return {true, label + "; " + std::to_string(shape.points.size()) + " points"};
}

Json tubeToJson(const TubeShape& s) {
    Json pts = Json::array();
    for (const TubePoint& p : s.points) {
        for (int k = 0; k < 3; ++k) pts.push(double(p.position[k]));
        pts.push(double(p.radius));
    }
    Json out = Json::object();
    out.set("points", std::move(pts));
    if (s.flatCaps) out.set("flat_caps", true);
    if (s.mirrorX) out.set("mirror_x", true);
    if (s.edge > 0.0f) out.set("edge", double(s.edge));
    return out;
}

TubeShape tubeFromJson(const Json& json) {
    TubeShape s;
    const Json& pts = json["points"];
    if (!pts.isArray() || pts.size() % 4 != 0) return {};
    for (size_t i = 0; i < pts.size(); i += 4)
        s.points.push_back({vec3(float(pts[i].asNumber(NAN)), float(pts[i + 1].asNumber(NAN)), float(pts[i + 2].asNumber(NAN))),
                            float(pts[i + 3].asNumber(NAN))});
    s.flatCaps = json["flat_caps"].asBool(false);
    s.mirrorX = json["mirror_x"].asBool(false);
    s.edge = float(json["edge"].asNumber(0.0));
    if (!tubeValidate(s).empty()) return {};
    return s;
}

bool parseTubePoints(const std::string& text, float radius, std::vector<TubePoint>& out, std::string& error) {
    out.clear();
    std::string t = text;
    for (char& c : t)
        if (c == ';' || c == '|') c = ' ';
    std::istringstream in(t);
    std::string tok;
    while (in >> tok) {
        std::vector<float> v;
        size_t start = 0;
        while (true) {
            const size_t end = tok.find(',', start);
            const std::string part = tok.substr(start, end == std::string::npos ? std::string::npos : end - start);
            char* stop = nullptr;
            const float x = std::strtof(part.c_str(), &stop);
            if (part.empty() || stop != part.c_str() + part.size()) v.clear(), v.resize(9);
            else v.push_back(x);
            if (end == std::string::npos || v.size() > 4) break;
            start = end + 1;
        }
        if (v.size() < 3 || v.size() > 4) {
            error = "bad point \"" + tok + "\": use x,y,z or x,y,z,radius";
            return false;
        }
        out.push_back({vec3(v[0], v[1], v[2]), v.size() == 4 ? v[3] : radius});
    }
    if (out.empty()) {
        error = "no points";
        return false;
    }
    return true;
}

namespace {

constexpr const char* kNotTube = "The active Part is not a Tube Part (tube_new makes one; a baked tube is a plain mesh)";

bool validPoint(const TubeShape& s, int i) { return i >= 0 && size_t(i) < s.points.size(); }

void applyOptions(TubeShape& s, const Args& a) {
    if (a.contains("caps")) s.flatCaps = argString(a, "caps") == "flat";
    if (a.contains("symmetry")) {
        const std::string sym = argString(a, "symmetry");
        s.mirrorX = sym.find('x') != std::string::npos || sym.find('X') != std::string::npos;
    }
    if (a.contains("edge")) s.edge = float(argNumber(a, "edge", 0.0));
}

} // namespace

void registerTubeCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    const ParamSpec capsParam{"caps", T::String, "Ends: \"round\" (default) or \"flat\".", false};
    const ParamSpec symParam{"symmetry", T::String, "\"x\" adds a mirror copy across x = 0; \"none\" removes it.", false};
    const ParamSpec edgeParam{"edge", T::Number, "Target edge length (0 = automatic, from the radius).", false};
    reg.add({"tube_new",
             "Tube: sweep a clay tube along a smooth curve through 3D points (like drawing with a 3D pen). By default "
             "a new Tube Part whose points stay editable (tube_set_point, tube_insert_point, tube_delete_point) "
             "until tube_bake; with merge=true it goes into the active Part with even edge lengths (good for dynamic "
             "topology). One undo step either way.",
             {{"points", T::String, "Points \"x,y,z[,radius] x,y,z[,radius] ...\" (spaces, ';' or '|' between them).", true},
              {"radius", T::Number, "Radius for points given without one (default 0.05).", false},
              {"smooth", T::Number,
               "0 (default): the points are the curve's control points. Above 0 (to 1): they are raw stroke samples, "
               "smoothed by this much and simplified to at most 500 control points.",
               false},
              capsParam, symParam, edgeParam,
              {"merge", T::Bool, "Merge into the active Part instead of adding a Tube Part (default false).", false},
              {"name", T::String, "Part name (default \"Tube\").", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const float radius = float(argNumber(a, "radius", 0.05));
                 TubeShape s;
                 std::string err;
                 if (!parseTubePoints(argString(a, "points"), radius, s.points, err)) return {false, "Tube: " + err};
                 if (const float sm = float(argNumber(a, "smooth", 0.0)); sm > 0.0f) {
                     if (const std::string e = tubeValidate(s); !e.empty()) return {false, "Tube: " + e};
                     s.points = tubeFromStroke(s.points, sm);
                 }
                 applyOptions(s, a);
                 return tubeAdd(doc, s, argBool(a, "merge", false), argString(a, "name", "Tube"));
             }});
    reg.add({"tube_list", "List the active Tube Part's control points (index, position, radius) and options.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 const TubeShape& s = doc.mesh.tube;
                 if (s.empty()) return {false, kNotTube};
                 std::string out = std::to_string(s.points.size()) + " points, " + (s.flatCaps ? "flat" : "round") +
                                   " caps" + (s.mirrorX ? ", mirrored in X" : "") +
                                   (s.edge > 0.0f ? ", edge " + num(s.edge) : ", edge automatic");
                 for (size_t i = 0; i < s.points.size(); ++i) {
                     const TubePoint& p = s.points[i];
                     out += "\n" + std::to_string(i) + ": (" + num(p.position.x) + ", " + num(p.position.y) + ", " +
                            num(p.position.z) + ") radius " + num(p.radius);
                 }
                 return {true, out};
             }});
    reg.add({"tube_set_point", "Move and/or resize a control point of the active Tube Part.",
             {{"index", T::Number, "Point index (0 = the start).", true},
              {"position", T::String, "New position \"x,y,z\" (default: unchanged).", false},
              {"radius", T::Number, "New radius (default: unchanged).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 TubeShape s = doc.mesh.tube;
                 if (s.empty()) return {false, kNotTube};
                 const int i = int(argNumber(a, "index", -1));
                 if (!validPoint(s, i)) return {false, "index must be from 0 to " + std::to_string(s.points.size() - 1)};
                 TubePoint& p = s.points[size_t(i)];
                 if (a.contains("position") && !parseVec3(argString(a, "position"), p.position))
                     return {false, "position must be \"x,y,z\""};
                 p.radius = float(argNumber(a, "radius", p.radius));
                 return tubeEdit(doc, std::move(s), "Tube point");
             }});
    reg.add({"tube_insert_point",
             "Insert a control point on the active Tube Part halfway between point `index` and the next one.",
             {{"index", T::Number, "The point before the new one.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 TubeShape s = doc.mesh.tube;
                 if (s.empty()) return {false, kNotTube};
                 const int i = int(argNumber(a, "index", -1));
                 if (i < 0 || size_t(i) + 1 >= s.points.size())
                     return {false, s.points.size() < 2 ? "Tube: a one-point tube has no span to insert into"
                                                        : "index must be from 0 to " + std::to_string(s.points.size() - 2)};
                 const TubePoint& p = s.points[size_t(i)];
                 const TubePoint& q = s.points[size_t(i) + 1];
                 s.points.insert(s.points.begin() + i + 1, TubePoint{0.5f * (p.position + q.position), 0.5f * (p.radius + q.radius)});
                 return tubeEdit(doc, std::move(s), "Tube insert point");
             }});
    reg.add({"tube_delete_point", "Delete a control point of the active Tube Part (one point always stays).",
             {{"index", T::Number, "Point index.", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 TubeShape s = doc.mesh.tube;
                 if (s.empty()) return {false, kNotTube};
                 const int i = int(argNumber(a, "index", -1));
                 if (!validPoint(s, i)) return {false, "index must be from 0 to " + std::to_string(s.points.size() - 1)};
                 if (s.points.size() == 1) return {false, "Tube: the last point stays (delete the Part instead)"};
                 s.points.erase(s.points.begin() + i);
                 return tubeEdit(doc, std::move(s), "Tube delete point");
             }});
    reg.add({"tube_set", "Change the active Tube Part's ends, mirror copy, density or thickness.",
             {capsParam, symParam, edgeParam,
              {"radius_scale", T::Number, "Multiply every point's radius by this (default 1).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 TubeShape s = doc.mesh.tube;
                 if (s.empty()) return {false, kNotTube};
                 applyOptions(s, a);
                 const float k = float(argNumber(a, "radius_scale", 1.0));
                 if (!(k > 0.0f) || !std::isfinite(k)) return {false, "radius_scale must be above 0"};
                 for (TubePoint& p : s.points) p.radius *= k;
                 return tubeEdit(doc, std::move(s), "Tube settings");
             }});
    reg.add({"tube_bake",
             "Bake the active Tube Part into a plain, sculptable mesh (its points are no longer editable). "
             "uniform=true re-sweeps it with even edge lengths (triangles) for dynamic topology; else it keeps its quads.",
             {{"uniform", T::Bool, "Even edge lengths (default false).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.tube.empty()) return {false, kNotTube};
                 Mesh m = argBool(a, "uniform", false) ? tubeSweep(doc.mesh.tube, true) : doc.mesh;
                 m.tube = {};
                 const size_t tris = m.triangleCount();
                 doc.replaceMeshUndoable(std::move(m), "Tube bake");
                 return {true, "Baked the tube: " + std::to_string(tris) + " triangles, ready to sculpt"};
             }});
}

} // namespace sf
