// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Trim never tears a thin wall (members session 2026-11, tester phoenix): a Trim stroke on a shell or a thin wing cuts the skin
// the viewer sees down to its plane, and stops where the far wall is: the two walls never meet or cross, the far wall (the
// inner face, the underside) does not move, and nothing turns over. With and without Front faces only, at several sizes.
#include "GeomInvariants.h"

#include "core/Primitives.h"

#include <cmath>
#include <cstdio>
#include <map>

using namespace sf;
using namespace sf::geom;

namespace {

constexpr Symmetry kNoSym{false, false, false};

// Two layers on one mesh: vertex i of the skin the viewer sees, vertex layer + i of the far wall (same layout, so the
// pairs are known), the far wall's triangles turned over so both face out of the material.
Mesh twoLayers(const Mesh& skin, const Mesh& farWall) {
    Mesh m = skin;
    const uint32_t n = uint32_t(skin.positions.size());
    m.positions.insert(m.positions.end(), farWall.positions.begin(), farWall.positions.end());
    for (const glm::uvec3& t : farWall.triangles) m.triangles.push_back(glm::uvec3(t.x + n, t.z + n, t.y + n));
    m.recomputeAllNormals();
    return m;
}

struct Thin {
    Fixture fx;
    uint32_t layer = 0; // vertices in the skin
};

// A hollow sphere: radius 1, a wall `wall` thick.
const Thin& shell(float wall) {
    static std::map<int, Thin> cache;
    const int key = int(wall * 100000.0f);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Thin t;
    const Mesh outer = makeIcosphere(6, 1.0f), inner = makeIcosphere(6, 1.0f - wall);
    t.layer = uint32_t(outer.positions.size());
    t.fx.mesh = twoLayers(outer, inner);
    t.fx.bvh.build(t.fx.mesh);
    return cache.emplace(key, std::move(t)).first->second;
}

// A curved wing: a sheet over 2 x 2 bending as y = 0.08 sin(2.5 x), `wall` thick.
const Thin& wing(float wall) {
    static std::map<int, Thin> cache;
    const int key = int(wall * 100000.0f);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Thin t;
    Mesh top = makePlane(240, 2.0f), under = top;
    for (glm::vec3& p : top.positions) p.y = 0.08f * std::sin(2.5f * p.x);
    for (glm::vec3& p : under.positions) p.y = 0.08f * std::sin(2.5f * p.x) - wall;
    t.layer = uint32_t(top.positions.size());
    t.fx.mesh = twoLayers(top, under);
    t.fx.bvh.build(t.fx.mesh);
    return cache.emplace(key, std::move(t)).first->second;
}

struct Result {
    float minWall = 1e9f;   // the thinnest the wall got (outer minus inner, along the skin's own direction)
    float farMoved = 0.0f;  // the largest move of any far-wall vertex
    float skinMoved = 0.0f; // the largest move of any skin vertex
    size_t nan = 0, flips = 0;
};

Result measure(const Thin& t, const Document& d, const std::vector<glm::vec3>& before, const std::vector<glm::vec3>& dirs) {
    Result r;
    for (uint32_t i = 0; i < t.layer; ++i) {
        const glm::vec3 gap = d.mesh.positions[i] - d.mesh.positions[t.layer + i];
        r.minWall = std::min(r.minWall, glm::dot(gap, dirs[i]));
        r.skinMoved = std::max(r.skinMoved, glm::distance(d.mesh.positions[i], before[i]));
        r.farMoved = std::max(r.farMoved, glm::distance(d.mesh.positions[t.layer + i], before[t.layer + i]));
    }
    r.nan = nonFinite(d.mesh);
    r.flips = flippedFaces(before, d.mesh);
    return r;
}

// Three rounds of two overlapping pen strokes across the shell's top: what drills through a thin wall.
Result trimShell(float wall, float radius, float strength, bool frontOnly) {
    const Thin& t = shell(wall);
    Document d = docFrom(t.fx);
    const std::vector<glm::vec3> before = d.mesh.positions;
    std::vector<glm::vec3> dirs(t.layer);
    for (uint32_t i = 0; i < t.layer; ++i) dirs[i] = glm::normalize(before[i]);
    const glm::vec3 at = glm::normalize(glm::vec3(0.45f, 0.6f, 0.66f));
    for (int round = 0; round < 3; ++round) {
        for (const auto& s : overlapStrokes(at, radius, strength)) {
            d.sculptor.beginStroke(d.mesh, "Trim");
            for (DabParams p : s) {
                p.frontFacesOnly = frontOnly;
                d.sculptor.dab(d.mesh, d.bvh, BrushType::Trim, p, kNoSym);
            }
            d.undo.push(d.sculptor.endStroke());
        }
    }
    return measure(t, d, before, dirs);
}

// The same on the wing: strokes along x seen from above, over its bend.
Result trimWing(float wall, float radius, float strength, bool frontOnly) {
    const Thin& t = wing(wall);
    Document d = docFrom(t.fx);
    const std::vector<glm::vec3> before = d.mesh.positions;
    const std::vector<glm::vec3> dirs(t.layer, glm::vec3(0.0f, 1.0f, 0.0f));
    for (int round = 0; round < 3; ++round) {
        for (const float z : {0.1f, 0.1f + 0.4f * radius}) {
            d.sculptor.beginStroke(d.mesh, "Trim");
            for (int i = 0; i < 40; ++i) {
                DabParams p;
                const float x = -0.5f + 0.025f * float(i);
                p.center = glm::vec3(x, 0.08f * std::sin(2.5f * x), z);
                p.viewDir = glm::vec3(0.0f, -1.0f, 0.0f);
                p.radius = radius, p.strength = strength, p.pressure = 0.2f + 0.8f * float(i) / 39.0f;
                p.strokeDelta = glm::vec3(0.025f, 0.0f, 0.0f);
                p.frontFacesOnly = frontOnly;
                d.sculptor.dab(d.mesh, d.bvh, BrushType::Trim, p, kNoSym);
            }
            d.undo.push(d.sculptor.endStroke());
        }
    }
    return measure(t, d, before, dirs);
}

void check(const char* what, const Result& r, float wall, float radius) {
    const float keep = 0.02f * radius; // Trim leaves at least this much wall
    std::printf("  %s: thinnest wall %.5f of %.5f, far wall moved %.2g, skin moved %.4f, flips %zu\n", what, double(r.minWall),
                double(wall), double(r.farMoved), double(r.skinMoved), r.flips);
    CHECK(r.nan == 0);
    CHECK(r.flips == 0);
    CHECK(r.minWall >= std::min(wall, 0.5f * keep) - 1e-6f); // the two walls never meet or cross: no hole
    CHECK(r.farMoved < 1e-6f);                       // the wall behind the skin is not Trim's to cut
    if (wall > 2.0f * keep) CHECK(r.skinMoved > 0.0f); // and the skin is still trimmed where there is wall to spare
}

} // namespace

TEST(trim_never_tears_a_thin_shell) {
    for (const float wall : {0.004f, 0.012f})
        for (const float radius : {0.05f, 0.15f, 0.3f})
            for (const bool front : {false, true}) {
                char what[96];
                std::snprintf(what, sizeof what, "shell wall %.3f, radius %.2f, front faces only %s", double(wall), double(radius),
                              front ? "on" : "off");
                check(what, trimShell(wall, radius, 0.8f, front), wall, radius);
            }
}

TEST(trim_never_tears_a_thin_wing) {
    for (const float wall : {0.004f, 0.012f})
        for (const float radius : {0.08f, 0.2f})
            for (const bool front : {false, true}) {
                char what[96];
                std::snprintf(what, sizeof what, "wing wall %.3f, radius %.2f, front faces only %s", double(wall), double(radius),
                              front ? "on" : "off");
                check(what, trimWing(wall, radius, 0.8f, front), wall, radius);
            }
}
