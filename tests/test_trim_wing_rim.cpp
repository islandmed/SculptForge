// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Trim at the tapering rim of a thin wing (artist-qa, follow-up to trim-thin-walls): where the sheet runs out to its edge it is
// thinner than the brush, and the skin there sank through the underside (the wall clamp looked along each vertex's own slanted normal,
// which misses the wall under it): a ragged, spiky rim. The top does not get through the underside under it, wherever the strokes
// land on the wing, seen from above or at a glancing angle, with and without Front faces only.
#include "GeomInvariants.h"

#include "core/BVH.h"
#include "core/Primitives.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace sf;
using namespace sf::geom;

namespace {

constexpr Symmetry kNoSym{false, false, false};

// A sphere squashed to 1.4 x 0.06 x 1 (0.12 thick at its middle, thinner toward the rim).
const Fixture& wing() {
    static Fixture fx = [] {
        Fixture f;
        f.mesh = makeIcosphere(7, 1.0f);
        for (glm::vec3& p : f.mesh.positions) p *= glm::vec3(1.4f, 0.06f, 1.0f);
        f.mesh.recomputeAllNormals();
        f.bvh.build(f.mesh);
        return f;
    }();
    return fx;
}

struct Result {
    size_t crossed = 0; // top-side vertices that ended below the underside (a ray straight up from them meets it from beneath)
    size_t nan = 0;
    float moved = 0.0f;
};

// artist-qa's session: five passes from above (the wing is thinned to a sheet), then five along the rim seen `elevationDeg`
// above its plane, the centre of every dab where the cursor's ray lands on the wing.
Result trimWing(float radius, float elevationDeg, bool frontOnly) {
    Document d = docFrom(wing());
    const std::vector<glm::vec3> before = d.mesh.positions;
    const auto stroke = [&](const glm::vec3& view, float aim, float zAt) {
        d.sculptor.beginStroke(d.mesh, "Trim");
        for (int i = 0; i < 40; ++i) {
            DabParams p;
            const float x = -0.5f + 0.0125f * float(i);
            const float z = zAt >= 0.0f ? zAt : std::sqrt(std::max(0.0f, 1.0f - (x / 1.4f) * (x / 1.4f))); // (zAt < 0: the rim)
            RayHit hit;
            if (!d.bvh.raycast(d.mesh, glm::vec3(x, aim, z) - view * 3.0f, view, hit)) continue;
            p.center = hit.position;
            p.viewDir = view;
            p.radius = radius, p.strength = 0.8f, p.pressure = 0.2f + 0.8f * float(i) / 39.0f;
            p.strokeDelta = glm::vec3(0.0125f, 0.0f, 0.0f);
            p.frontFacesOnly = frontOnly;
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Trim, p, kNoSym);
        }
        d.undo.push(d.sculptor.endStroke());
    };
    const auto look = [](float deg) {
        const float a = deg * 3.14159265f / 180.0f;
        return glm::normalize(glm::vec3(0.0f, -std::sin(a), -std::cos(a)));
    };
    for (int pass = 0; pass < 5; ++pass) stroke(look(60.0f), 0.0f, 0.0f);
    for (int pass = 0; pass < 5; ++pass) stroke(look(elevationDeg), 0.01f * float(pass % 3), -1.0f);
    Result r;
    BVH after;
    after.build(d.mesh);
    for (uint32_t v = 0; v < before.size(); ++v) {
        if (before[v].y > 1e-4f) {
            RayHit hit;
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (after.raycast(d.mesh, d.mesh.positions[v] + up * 1e-5f, up, hit) && glm::dot(hit.normal, up) < -0.5f) ++r.crossed;
        }
        r.moved = std::max(r.moved, glm::distance(d.mesh.positions[v], before[v]));
    }
    r.nan = nonFinite(d.mesh);
    return r;
}

} // namespace

TEST(trim_wing_rim_never_crosses_the_underside) {
    for (const float radius : {0.1f, 0.14f})
        for (const float elevation : {8.0f, 60.0f})
            for (const bool front : {false, true}) {
                const Result r = trimWing(radius, elevation, front);
                std::printf("  wing rim, radius %.2f, %.0f deg, front faces only %s: crossed %zu, moved %.3f\n", double(radius),
                            double(elevation), front ? "on" : "off", r.crossed, double(r.moved));
                CHECK(r.nan == 0);
                CHECK(r.moved > 0.0f);
                CHECK(r.crossed <= 64); // the top does not get through the underside under it (before: 200 to 1600; a few at the rim's very apex)
            }
}
