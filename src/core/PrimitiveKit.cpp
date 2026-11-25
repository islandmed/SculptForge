// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PrimitiveKit.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Primitives.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace sf {

namespace {

constexpr float kTau = 2.0f * std::numbers::pi_v<float>;
constexpr size_t kHistoryMax = 32; // specs kept per Part (undo walks back through them)

// Builds a mesh of rings around +Y: shared indices, quads as pairs, every face wound to face `outward`.
struct Lathe {
    Mesh m;

    uint32_t point(const glm::vec3& p) {
        m.positions.push_back(p);
        return uint32_t(m.positions.size() - 1);
    }
    // n vertices on the circle of `radius` at height y; returns the first index.
    uint32_t ring(int n, float radius, float y) {
        const uint32_t first = uint32_t(m.positions.size());
        for (int i = 0; i < n; ++i) {
            const float a = kTau * float(i) / float(n);
            m.positions.push_back({radius * std::cos(a), y, -radius * std::sin(a)});
        }
        return first;
    }
    void tri(uint32_t a, uint32_t b, uint32_t c, uint32_t group, const glm::vec3& outward) {
        const glm::vec3 n = glm::cross(m.positions[b] - m.positions[a], m.positions[c] - m.positions[a]);
        if (glm::dot(n, outward) < 0.0f) std::swap(b, c);
        m.triangles.push_back({a, b, c});
        m.facegroups.push_back(group);
    }
    // The quad a b c d (around its edge), split on a-c, wound to face `outward`.
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t group, const glm::vec3& outward) {
        const glm::vec3 n = glm::cross(m.positions[c] - m.positions[a], m.positions[d] - m.positions[b]);
        if (glm::dot(n, outward) < 0.0f) std::swap(b, d);
        const uint32_t t = uint32_t(m.triangles.size());
        m.triangles.push_back({a, b, c});
        m.triangles.push_back({a, c, d});
        m.facegroups.push_back(group);
        m.facegroups.push_back(group);
        m.pairQuad(t, t + 1);
    }
    glm::vec3 radial(uint32_t v) const { return glm::normalize(glm::vec3(m.positions[v].x, 0.0f, m.positions[v].z) + glm::vec3(1e-9f, 0.0f, 0.0f)); }
    // Quads between two rings of n (the side of a shape), facing out from the axis (or in, for a pipe's inside).
    void join(uint32_t ra, uint32_t rb, int n, uint32_t group, bool inward = false) {
        for (int i = 0; i < n; ++i) {
            const uint32_t i1 = uint32_t((i + 1) % n);
            const glm::vec3 out = (inward ? -1.0f : 1.0f) * radial(ra + uint32_t(i));
            quad(ra + uint32_t(i), ra + i1, rb + i1, rb + uint32_t(i), group, out);
        }
    }
    // A fan from ring `r` to the point `c`, facing `outward` (a pole, or the centre of a cap).
    void fan(uint32_t r, int n, uint32_t c, uint32_t group, const glm::vec3& outward) {
        for (int i = 0; i < n; ++i) tri(c, r + uint32_t(i), r + uint32_t((i + 1) % n), group, outward);
    }
    // A flat cap on ring `rim` (n vertices at `radius`, height y) facing `up` (+Y or -Y): `caps` rings (1 = a fan
    // only), or an all-quad grid (n a multiple of 4).
    void cap(uint32_t rim, int n, float radius, float y, int caps, bool grid, uint32_t group, const glm::vec3& up) {
        if (caps <= 0) return;
        if (grid && n % 4 == 0 && n >= 4) {
            gridCap(rim, n, radius, y, group, up);
            return;
        }
        uint32_t outer = rim;
        for (int k = caps - 1; k >= 1; --k) {
            const uint32_t inner = ring(n, radius * float(k) / float(caps), y);
            for (int i = 0; i < n; ++i) {
                const uint32_t i1 = uint32_t((i + 1) % n);
                quad(outer + uint32_t(i), outer + i1, inner + i1, inner + uint32_t(i), group, up);
            }
            outer = inner;
        }
        fan(outer, n, point({0.0f, y, 0.0f}), group, up);
    }
    // The grid cap: a (k+1)^2 grid on the square, its boundary the rim's n = 4k vertices (shared, so the cap is
    // welded to the side), every inner point on the circle of its square's "radius", at the angle of its place
    // along that square's perimeter. All quads, no pole.
    void gridCap(uint32_t rim, int n, float radius, float y, uint32_t group, const glm::vec3& up) {
        const int k = n / 4;
        // Perimeter index (0..n) of the boundary point (u, v) in grid steps, counter to the rim's order where needed
        // (the rim runs with increasing angle; the perimeter does too: side u=+1 up, v=+1 leftwards, u=-1 down, v=-1 right).
        auto perimeter = [k](float u, float v) -> float { // (u, v) on the square's boundary, by its biggest coordinate
            const float au = std::abs(u), av = std::abs(v);
            if (u >= av) return (std::clamp(v, -1.0f, 1.0f) + 1.0f) * 0.5f * float(k);
            if (v >= au) return float(k) + (1.0f - std::clamp(u, -1.0f, 1.0f)) * 0.5f * float(k);
            if (-u >= av) return 2.0f * float(k) + (1.0f - std::clamp(v, -1.0f, 1.0f)) * 0.5f * float(k);
            return 3.0f * float(k) + (std::clamp(u, -1.0f, 1.0f) + 1.0f) * 0.5f * float(k);
        };
        // The rim's vertex i sits at angle i / n of a turn (the ring's x = cos, z = -sin): the perimeter angle
        // places inner points the same way, so the boundary matches the ring exactly.
        auto at = [&](float s, float t) {
            const float a = kTau * t / float(n);
            return glm::vec3(s * radius * std::cos(a), y, -s * radius * std::sin(a));
        };
        std::vector<uint32_t> idx(size_t((k + 1) * (k + 1)));
        for (int j = 0; j <= k; ++j)
            for (int i = 0; i <= k; ++i) {
                const float u = 2.0f * float(i) / float(k) - 1.0f, v = 2.0f * float(j) / float(k) - 1.0f;
                const float s = std::max(std::abs(u), std::abs(v));
                uint32_t id;
                if (s >= 1.0f - 1e-6f) id = rim + uint32_t(std::lround(perimeter(u, v))) % uint32_t(n);
                else if (s < 1e-6f) id = point({0.0f, y, 0.0f});
                else id = point(at(s, perimeter(u / s, v / s)));
                idx[size_t(j * (k + 1) + i)] = id;
            }
        for (int j = 0; j < k; ++j)
            for (int i = 0; i < k; ++i) {
                const uint32_t a = idx[size_t(j * (k + 1) + i)], b = idx[size_t(j * (k + 1) + i + 1)];
                const uint32_t c = idx[size_t((j + 1) * (k + 1) + i + 1)], d = idx[size_t((j + 1) * (k + 1) + i)];
                quad(a, b, c, d, group, up);
            }
    }
    Mesh finish() {
        if (m.hasQuads()) m.quadPartners.resize(m.triangles.size(), Mesh::kNoQuad);
        m.rebuildTopology();
        m.recomputeAllNormals();
        return std::move(m);
    }
};

const glm::vec3 kUp(0.0f, 1.0f, 0.0f), kDown(0.0f, -1.0f, 0.0f);

Mesh lathe(const PrimitiveSpec& s) {
    Lathe L;
    const int n = s.segments;
    const float r = s.radius * s.size * 0.5f, h = s.height * s.size, y0 = -h * 0.5f, y1 = h * 0.5f;
    if (s.type == "cylinder") {
        std::vector<uint32_t> rings;
        for (int k = 0; k <= s.rings; ++k) rings.push_back(L.ring(n, r, glm::mix(y0, y1, float(k) / float(s.rings))));
        for (int k = 0; k < s.rings; ++k) L.join(rings[size_t(k)], rings[size_t(k) + 1], n, 2);
        L.cap(rings.front(), n, r, y0, s.caps, s.quadCaps, 1, kDown);
        L.cap(rings.back(), n, r, y1, s.caps, s.quadCaps, 3, kUp);
    } else if (s.type == "cone") {
        const float r2 = s.radius2 * s.size * 0.5f;
        const bool point = r2 < 1e-4f * s.size;
        std::vector<uint32_t> rings;
        for (int k = 0; k < s.rings + (point ? 0 : 1); ++k) { // to a point: the last span is the fan
            const float t = float(k) / float(s.rings);
            rings.push_back(L.ring(n, glm::mix(r, r2, t), glm::mix(y0, y1, t)));
        }
        for (size_t k = 0; k + 1 < rings.size(); ++k) L.join(rings[k], rings[k + 1], n, 2);
        if (point) L.fan(rings.back(), n, L.point({0.0f, y1, 0.0f}), 2, L.radial(rings.back()) + kUp);
        L.cap(rings.front(), n, r, y0, s.caps, s.quadCaps, 1, kDown);
        if (!point) L.cap(rings.back(), n, r2, y1, s.caps, s.quadCaps, 3, kUp);
    } else if (s.type == "capsule") {
        const float rr = std::min(r, h * 0.5f);
        const float side = h - 2.0f * rr;
        const int arc = std::max(2, n / 4);
        const int sideSpans = side > 1e-5f * s.size ? s.rings : 0; // a ball: no side rings (they would be flat)
        // Bottom pole and arcs up to the side, the side, the top arcs and pole.
        const uint32_t bottom = L.point({0.0f, -h * 0.5f, 0.0f});
        std::vector<uint32_t> rings;
        for (int i = 1; i <= arc; ++i) {
            const float a = 0.5f * std::numbers::pi_v<float> * float(i) / float(arc);
            rings.push_back(L.ring(n, rr * std::sin(a), -side * 0.5f - rr * std::cos(a)));
        }
        const size_t sideStart = rings.size() - 1;
        for (int k = 1; k <= sideSpans; ++k) rings.push_back(L.ring(n, rr, glm::mix(-side * 0.5f, side * 0.5f, float(k) / float(sideSpans))));
        const size_t sideEnd = rings.size() - 1;
        for (int i = 1; i < arc; ++i) {
            const float a = 0.5f * std::numbers::pi_v<float> * float(i) / float(arc);
            rings.push_back(L.ring(n, rr * std::cos(a), side * 0.5f + rr * std::sin(a)));
        }
        const uint32_t top = L.point({0.0f, h * 0.5f, 0.0f});
        L.fan(rings.front(), n, bottom, 1, kDown);
        for (size_t k = 0; k + 1 < rings.size(); ++k) {
            const uint32_t group = k < sideStart ? 1u : k < sideEnd ? 2u : 3u;
            L.join(rings[k], rings[k + 1], n, group);
        }
        L.fan(rings.back(), n, top, 3, kUp);
    } else if (s.type == "pipe") {
        const float r2 = s.radius2 * s.size * 0.5f;
        std::vector<uint32_t> outer, inner;
        for (int k = 0; k <= s.rings; ++k) {
            const float y = glm::mix(y0, y1, float(k) / float(s.rings));
            outer.push_back(L.ring(n, r, y));
            inner.push_back(L.ring(n, r2, y));
        }
        for (int k = 0; k < s.rings; ++k) {
            L.join(outer[size_t(k)], outer[size_t(k) + 1], n, 1);
            L.join(inner[size_t(k)], inner[size_t(k) + 1], n, 2, true);
        }
        for (int i = 0; i < n; ++i) { // the ends: one ring of quads across the wall
            const uint32_t i1 = uint32_t((i + 1) % n);
            L.quad(outer.front() + uint32_t(i), outer.front() + i1, inner.front() + i1, inner.front() + uint32_t(i), 3, kDown);
            L.quad(outer.back() + uint32_t(i), outer.back() + i1, inner.back() + i1, inner.back() + uint32_t(i), 3, kUp);
        }
    }
    return L.finish();
}

Mesh torus(const PrimitiveSpec& s) {
    const int n = s.segments, m = std::max(s.rings, 3);
    const float tube = s.radius2 * s.size * 0.5f, R = s.radius * s.size * 0.5f;
    Lathe L;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) {
            const float a = kTau * float(i) / float(n), b = kTau * float(j) / float(m);
            const float rr = R + tube * std::cos(b);
            L.m.positions.push_back({rr * std::cos(a), tube * std::sin(b), -rr * std::sin(a)});
        }
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) {
            const uint32_t a = uint32_t(i * m + j), b = uint32_t(i * m + (j + 1) % m);
            const uint32_t c = uint32_t(((i + 1) % n) * m + (j + 1) % m), d = uint32_t(((i + 1) % n) * m + j);
            const glm::vec3 p = L.m.positions[a];
            const glm::vec3 centre = glm::normalize(glm::vec3(p.x, 0.0f, p.z)) * R; // the tube's centre line under a
            L.quad(a, b, c, d, 1, p - centre);
        }
    return L.finish();
}

} // namespace

bool PrimitiveSpec::operator==(const PrimitiveSpec& o) const {
    return type == o.type && size == o.size && segments == o.segments && rings == o.rings && caps == o.caps &&
           quadCaps == o.quadCaps && radius == o.radius && radius2 == o.radius2 && height == o.height && axis == o.axis &&
           placement == o.placement;
}

const std::vector<std::string>& primitiveTypes() {
    static const std::vector<std::string> types{"sphere",  "cube",    "plane",   "cylinder", "cone",
                                                "torus",   "capsule", "pipe",    "icosphere", "quadsphere"};
    return types;
}

bool isPrimitiveType(const std::string& type) {
    const auto& t = primitiveTypes();
    return std::find(t.begin(), t.end(), type) != t.end();
}

const char* primitiveLabel(const std::string& type) {
    if (type == "sphere") return "Sphere";
    if (type == "cube") return "Cube";
    if (type == "plane") return "Plane";
    if (type == "cylinder") return "Cylinder";
    if (type == "cone") return "Cone";
    if (type == "torus") return "Torus";
    if (type == "capsule") return "Capsule";
    if (type == "pipe") return "Pipe";
    if (type == "icosphere") return "Icosphere";
    if (type == "quadsphere") return "Quad Sphere";
    return "Primitive";
}

PrimitiveSpec primitiveDefaults(const std::string& type, bool lowPoly) {
    PrimitiveSpec s;
    s.type = type;
    // Classic: exactly the meshes Append has made since #31 (segments, cap rings and side rings as before).
    if (type == "sphere") s.segments = lowPoly ? 2 : 5;
    else if (type == "icosphere") s.segments = lowPoly ? 1 : 3;
    else if (type == "quadsphere") s.segments = lowPoly ? 4 : 32;
    else if (type == "cube") s.segments = lowPoly ? 1 : 48;
    else if (type == "plane") s.segments = lowPoly ? 4 : 128;
    else if (type == "cylinder") { // low-poly: quad caps (a fan's pole pinches under subdivision and stops edge loops)
        s.segments = lowPoly ? 12 : 48;
        s.rings = lowPoly ? 1 : 24;
        s.caps = lowPoly ? 1 : 4;
        s.quadCaps = lowPoly;
        s.radius = 0.6f;
    } else if (type == "cone") {
        s.segments = lowPoly ? 12 : 48;
        s.rings = lowPoly ? 1 : 24;
        s.caps = lowPoly ? 1 : 4;
        s.quadCaps = lowPoly;
        s.radius = 0.8f;
        s.radius2 = 0.0f;
    } else if (type == "torus") {
        s.segments = lowPoly ? 16 : 64;
        s.rings = lowPoly ? 8 : 32;
        s.radius = 0.7f;
        s.radius2 = 0.3f;
    } else if (type == "capsule") {
        s.segments = lowPoly ? 12 : 48;
        s.rings = lowPoly ? 1 : 16;
        s.radius = 0.5f;
    } else if (type == "pipe") {
        s.segments = lowPoly ? 12 : 48;
        s.rings = lowPoly ? 1 : 24;
        s.radius = 0.6f;
        s.radius2 = 0.4f;
    }
    return s;
}

void clampPrimitiveSpec(PrimitiveSpec& s) {
    if (!isPrimitiveType(s.type)) s.type = "cylinder";
    if (!std::isfinite(s.size) || s.size <= 0.0f) s.size = 1.0f;
    const bool sphere = s.type == "sphere" || s.type == "icosphere";
    const bool cells = s.type == "cube" || s.type == "plane" || s.type == "quadsphere";
    s.segments = sphere ? std::clamp(s.segments, 0, 6) : cells ? std::clamp(s.segments, 1, 512) : std::clamp(s.segments, 3, 256);
    if (s.quadCaps && !sphere && !cells) s.segments = std::max(4, (s.segments + 2) / 4 * 4);
    s.rings = std::clamp(s.rings, s.type == "torus" ? 3 : 1, 256);
    s.caps = std::clamp(s.caps, 0, 8);
    auto fin = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
    s.radius = std::clamp(fin(s.radius, 0.6f), 0.02f, 1.0f);
    s.radius2 = std::clamp(fin(s.radius2, 0.0f), 0.0f, 1.0f);
    s.height = std::clamp(fin(s.height, 1.0f), 0.02f, 1.0f);
    s.axis = std::clamp(s.axis, 0, 2);
    if (s.type == "cone") s.radius2 = std::min(s.radius2, s.radius);
    if (s.type == "pipe") s.radius2 = std::clamp(s.radius2, 0.02f, std::max(0.02f, s.radius - 0.02f));
    if (s.type == "torus") {
        s.radius2 = std::clamp(s.radius2, 0.02f, 1.0f);
        s.radius = std::clamp(s.radius, s.radius2 + 0.02f, 1.0f - s.radius2); // the tube inside the box, around a hole
        if (s.radius < s.radius2 + 0.02f) { // too fat a tube: thin it to fit
            s.radius2 = std::min(s.radius2, 0.49f);
            s.radius = 1.0f - s.radius2;
        }
    }
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(s.placement[c][r])) s.placement = glm::mat4(1.0f);
}

PrimitiveControls primitiveControls(const std::string& type) {
    PrimitiveControls c;
    if (type == "sphere" || type == "icosphere") c.segmentsLabel = "Subdivisions %d";
    else if (type == "cube" || type == "plane" || type == "quadsphere") c.segmentsLabel = "Cells %d";
    if (type == "cylinder" || type == "cone" || type == "pipe") {
        c.rings = c.radius = c.height = c.axis = true;
        c.caps = type != "pipe";
        c.radius2 = type != "cylinder";
        c.radius2Label = type == "pipe" ? "Inner radius %.2f" : "Top radius %.2f";
    } else if (type == "capsule") {
        c.rings = c.radius = c.height = c.axis = true;
    } else if (type == "torus") {
        c.rings = c.radius = c.radius2 = c.axis = true;
        c.radius2Label = "Tube radius %.2f";
    } else if (type == "plane") {
        c.axis = true;
    }
    return c;
}

Mesh makePrimitive(const PrimitiveSpec& given) {
    PrimitiveSpec s = given;
    clampPrimitiveSpec(s);
    Mesh m;
    if (s.type == "sphere" || s.type == "icosphere") m = makeIcosphere(s.segments, s.size * 0.5f);
    else if (s.type == "quadsphere") m = makeQuadSphere(s.segments, s.size);
    else if (s.type == "cube") m = makeCube(s.segments, s.size);
    else if (s.type == "plane") m = makePlane(s.segments, s.size);
    else if (s.type == "torus") m = torus(s);
    else m = lathe(s);
    // The axis: +Y turned onto +X or +Z (a quarter turn about Z or X), then the placement.
    glm::mat4 xf = s.placement;
    if (s.axis == 0) xf = xf * glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    else if (s.axis == 2) xf = xf * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    if (xf != glm::mat4(1.0f)) {
        for (auto& p : m.positions) p = glm::vec3(xf * glm::vec4(p, 1.0f));
        m.recomputeAllNormals();
    }
    return m;
}

Json primitiveSpecJson(const PrimitiveSpec& s) {
    Json j = Json::object();
    j.set("type", s.type);
    j.set("size", double(s.size));
    j.set("segments", double(s.segments));
    j.set("rings", double(s.rings));
    j.set("caps", double(s.caps));
    j.set("quad_caps", s.quadCaps);
    j.set("radius", double(s.radius));
    j.set("radius2", double(s.radius2));
    j.set("height", double(s.height));
    if (s.axis != 1) j.set("axis", std::string(s.axis == 0 ? "x" : "z"));
    if (s.placement != glm::mat4(1.0f)) {
        Json p = Json::array();
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) p.push(double(s.placement[c][r]));
        j.set("placement", std::move(p));
    }
    return j;
}

bool primitiveSpecFromJson(const Json& j, PrimitiveSpec& out) {
    if (!j.isObject() || !j["type"].isString() || !isPrimitiveType(j["type"].asString())) return false;
    PrimitiveSpec s;
    s.type = j["type"].asString();
    s.size = float(j["size"].asNumber(1.0));
    s.segments = int(j["segments"].asNumber(s.segments));
    s.rings = int(j["rings"].asNumber(s.rings));
    s.caps = int(j["caps"].asNumber(s.caps));
    s.quadCaps = j["quad_caps"].asBool(false);
    s.radius = float(j["radius"].asNumber(s.radius));
    s.radius2 = float(j["radius2"].asNumber(s.radius2));
    s.height = float(j["height"].asNumber(s.height));
    if (j["axis"].isString()) s.axis = j["axis"].asString() == "x" ? 0 : j["axis"].asString() == "z" ? 2 : 1;
    if (const Json& p = j["placement"]; p.isArray() && p.size() == 16) {
        size_t k = 0;
        for (const Json& v : p.items()) {
            s.placement[int(k / 4)][int(k % 4)] = float(v.asNumber(k % 5 == 0 ? 1.0 : 0.0));
            ++k;
        }
    }
    clampPrimitiveSpec(s);
    out = s;
    return true;
}

bool primitiveMatches(const Mesh& mesh, const PrimitiveSpec& spec) {
    const Mesh gen = makePrimitive(spec);
    if (gen.vertexCount() != mesh.vertexCount() || gen.triangleCount() != mesh.triangleCount()) return false;
    if (gen.triangles != mesh.triangles) return false;
    for (size_t t = 0; t < gen.triangles.size(); ++t)
        if (gen.facegroupAt(uint32_t(t)) != mesh.facegroupAt(uint32_t(t))) return false;
    const float tol = 1e-4f * std::max(spec.size, 1e-6f);
    for (size_t v = 0; v < gen.positions.size(); ++v)
        if (glm::distance(gen.positions[v], mesh.positions[v]) > tol) return false;
    return true;
}

void setPrimitiveSpec(PartData& data, PrimitiveSpec spec, uint64_t revision) {
    clampPrimitiveSpec(spec);
    PrimitiveState& st = data.primitive;
    st.history.erase(std::remove(st.history.begin(), st.history.end(), spec), st.history.end());
    st.history.push_back(std::move(spec));
    if (st.history.size() > kHistoryMax) st.history.erase(st.history.begin());
    st.checkedRevision = revision;
    st.fresh = true;
}

bool primitiveFresh(const Document& doc, int index) {
    if (index < 0 || index >= doc.partCount()) return false;
    PrimitiveState& st = doc.parts[size_t(index)].data->primitive;
    if (!st.any()) return false;
    const uint64_t revision = doc.partUndo(index).revision();
    if (st.checkedRevision == revision) return st.fresh;
    const Mesh& mesh = doc.partMesh(index);
    st.fresh = false;
    for (size_t k = st.history.size(); k-- > 0;)
        if (primitiveMatches(mesh, st.history[k])) {
            if (k + 1 != st.history.size()) std::rotate(st.history.begin() + long(k), st.history.begin() + long(k) + 1, st.history.end());
            st.fresh = true;
            break;
        }
    st.checkedRevision = revision;
    return st.fresh;
}

void registerPrimitiveCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"part_primitive",
             "Change the settings of the active Part's primitive and build it again (one undo step): only while the Part "
             "is still exactly the primitive Append made (no sculpting, remeshing or face-group edits since; moving it "
             "whole with the Gizmo is fine). info=true only reports the settings.",
             {{"segments", T::Number, "Around the axis (3-256); cube, plane, quad sphere: cells per side; spheres: subdivisions (0-6).", false},
              {"rings", T::Number, "Along the side (1-256); torus: around the tube (3-256).", false},
              {"caps", T::Number, "Cylinder, cone: 0 open, 1 a fan, 2-8 that many rings of quads.", false},
              {"quad_caps", T::Bool, "Caps as an all-quad grid, no pole (segments become a multiple of 4).", false},
              {"radius", T::Number, "As a fraction of half the size (0.02-1).", false},
              {"radius2", T::Number, "Cone: top radius (0 = a point); torus: tube radius; pipe: inner radius.", false},
              {"height", T::Number, "As a fraction of the size (0.02-1).", false},
              {"axis", T::String, "The shape's axis: y (up, the default), x or z.", false},
              {"preset", T::String, "'lowpoly' (the Polygon-mode cage) or 'dense' (the Classic sculpting mesh): every setting at once.", false},
              {"info", T::Bool, "Only report the settings (and whether they still apply).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const int i = doc.activePart;
                 PrimitiveState& st = doc.activePartInfo().data->primitive;
                 if (!st.any()) return {false, "The active Part is not a primitive from Append"};
                 auto text = [&](const PrimitiveSpec& s) {
                     std::string out = std::string(primitiveLabel(s.type)) + ": segments " + std::to_string(s.segments) +
                                       ", rings " + std::to_string(s.rings);
                     const PrimitiveControls c = primitiveControls(s.type);
                     if (c.caps) out += ", caps " + std::to_string(s.caps) + (s.quadCaps ? " (quad grid)" : "");
                     char buf[96];
                     if (c.radius) {
                         std::snprintf(buf, sizeof buf, ", radius %.2f", double(s.radius));
                         out += buf;
                     }
                     if (c.radius2) {
                         std::snprintf(buf, sizeof buf, ", radius2 %.2f", double(s.radius2));
                         out += buf;
                     }
                     if (c.height) {
                         std::snprintf(buf, sizeof buf, ", height %.2f", double(s.height));
                         out += buf;
                     }
                     if (c.axis && s.axis != 1) out += std::string(", axis ") + (s.axis == 0 ? "x" : "z");
                     return out;
                 };
                 const bool fresh = primitiveFresh(doc, i);
                 if (argBool(a, "info", false))
                     return {true, text(st.spec()) + (fresh ? "" : " (edited since: the settings no longer apply)")};
                 if (!fresh)
                     return {false, "The Part was edited since it was made: its primitive settings no longer apply (undo the "
                                    "edits to get them back)"};
                 PrimitiveSpec s = st.spec();
                 if (a.contains("preset")) {
                     const std::string p = argString(a, "preset");
                     if (p != "lowpoly" && p != "dense") return {false, "preset must be lowpoly or dense"};
                     PrimitiveSpec d = primitiveDefaults(s.type, p == "lowpoly");
                     d.size = s.size;
                     d.placement = s.placement;
                     s = d;
                 }
                 if (a.contains("segments")) s.segments = int(std::lround(argNumber(a, "segments", s.segments)));
                 if (a.contains("rings")) s.rings = int(std::lround(argNumber(a, "rings", s.rings)));
                 if (a.contains("caps")) s.caps = int(std::lround(argNumber(a, "caps", s.caps)));
                 if (a.contains("quad_caps")) s.quadCaps = argBool(a, "quad_caps", s.quadCaps);
                 if (a.contains("radius")) s.radius = float(argNumber(a, "radius", s.radius));
                 if (a.contains("radius2")) s.radius2 = float(argNumber(a, "radius2", s.radius2));
                 if (a.contains("height")) s.height = float(argNumber(a, "height", s.height));
                 if (a.contains("axis")) {
                     const std::string ax = argString(a, "axis", "y");
                     if (ax != "x" && ax != "y" && ax != "z") return {false, "axis must be x, y or z"};
                     s.axis = ax == "x" ? 0 : ax == "y" ? 1 : 2;
                 }
                 clampPrimitiveSpec(s);
                 if (s == st.spec()) return {true, "No change: " + text(s)};
                 Mesh m = makePrimitive(s);
                 if (doc.mesh.hasColors()) m.ensureColors(doc.mesh.colors.front()); // a flat paint survives a rebuild
                 const size_t tris = m.triangleCount();
                 doc.replaceMeshUndoable(std::move(m), "Primitive settings");
                 setPrimitiveSpec(*doc.activePartInfo().data, s, doc.undo.revision());
                 return {true, text(s) + " (" + std::to_string(tris) + " triangles)"};
             }});
}

} // namespace sf
