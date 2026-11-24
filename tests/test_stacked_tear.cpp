// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stacked Clay, Crease and Inflate strokes on a divided mesh (#194): the build-up stays clean (no folds, no collapsed
// triangles) at the 5.2M triangles of three Divides. Crease packs the grid across its line, and Inflate along each
// vertex's own normal then carried the groove's walls through one another.
#include "TestHarness.h"

#include "GeomInvariants.h"
#include "core/Benchmark.h"
#include "core/Multires.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>

using namespace sf;

namespace {

constexpr Symmetry kNone{false, false, false};

// One stroke: dabs 15% of the brush radius apart along a->b, each on the surface under it, seen straight on.
void strokeOn(Document& d, BrushType type, glm::vec3 a, glm::vec3 b, float radius) {
    d.sculptor.beginStroke(d.mesh, brushName(type));
    const float spacing = radius * 0.15f;
    const int n = std::max(2, int(glm::distance(a, b) / spacing) + 1);
    for (int i = 0; i < n; ++i) {
        DabParams p;
        p.center = glm::mix(a, b, float(i) / float(n - 1));
        RayHit hit;
        if (d.bvh.raycast(d.mesh, p.center + glm::vec3(0, 0, 5), glm::vec3(0, 0, -1), hit)) {
            p.center = hit.position;
            p.pickNormal = hit.normal;
        }
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = radius;
        p.strength = 0.5f;
        d.sculptor.dab(d.mesh, d.bvh, type, p, kNone);
    }
    d.undo.push(d.sculptor.endStroke());
}

// The smallest triangle within `reach` of `spot`, over the mean one there (a collapsed sliver is near 0).
float smallestTriangle(const Mesh& m, const glm::vec3& spot, float reach) {
    double sum = 0.0;
    float smallest = 1e30f;
    size_t n = 0;
    for (const glm::uvec3& t : m.triangles) {
        if (glm::distance(m.positions[t.x], spot) > reach) continue;
        const float area = 0.5f * glm::length(glm::cross(m.positions[t.y] - m.positions[t.x], m.positions[t.z] - m.positions[t.x]));
        sum += area, smallest = std::min(smallest, area), ++n;
    }
    return n ? smallest / float(sum / double(n)) : 1.0f;
}

} // namespace

TEST(stacked_crease_inflate_invariant_5m) {
    // The artist pass's session (#194): a sphere of ~5.2M triangles (Divide x3), 12 Clay strokes in one spot, then 4 Crease
    // (15 px) and 4 Inflate (25 px) each crossing the others. The sphere is ~370 px across, so 1 px = 0.0054.
    Mesh m = makeBenchSphere(81000);
    std::string err;
    for (int i = 0; i < 3; ++i) CHECK(subdivDivide(m, true, err));
    CHECK(m.triangleCount() > 5000000);
    Document d = makeDoc(std::move(m));
    const glm::vec3 spot(0.0f, 0.0f, 1.0f);
    const float px = 0.0054f;
    const glm::vec3 lines[4][2] = {{{-0.20f, -0.04f, 0}, {0.20f, 0.08f, 0}}, {{-0.16f, 0.08f, 0}, {0.16f, -0.08f, 0}},
                                   {{-0.20f, 0.0f, 0}, {0.20f, 0.0f, 0}}, {{-0.12f, -0.12f, 0}, {0.12f, 0.12f, 0}}};
    for (int i = 0; i < 12; ++i) {
        const float size = i < 4 ? 20.0f : i < 8 ? 35.0f : 50.0f;
        strokeOn(d, BrushType::Clay, spot + lines[i % 4][0], spot + lines[i % 4][1], size * 0.5f * px);
    }
    const std::vector<glm::vec3> clayed = d.mesh.positions;
    for (int i = 0; i < 4; ++i) strokeOn(d, BrushType::Crease, spot + lines[i][0], spot + lines[i][1], 15.0f * 0.5f * px);
    const std::vector<glm::vec3> beforeInflate = d.mesh.positions;
    const size_t invertedByCrease = geom::invertedFaces(clayed, d.mesh);
    for (int i = 0; i < 4; ++i) strokeOn(d, BrushType::Inflate, spot + lines[i][0], spot + lines[i][1], 25.0f * 0.5f * px);

    const size_t bad = geom::nonFinite(d.mesh), inverted = geom::invertedFaces(beforeInflate, d.mesh);
    const float smallest = smallestTriangle(d.mesh, spot, 0.5f);
    std::printf("  stacked Crease + Inflate at %zu triangles: %zu non-finite, %zu inverted faces after Crease, %zu after Inflate, "
                "smallest triangle %.3f of the mean\n", d.mesh.triangleCount(), bad, invertedByCrease, inverted, smallest);
    CHECK(bad == 0);
    CHECK(inverted <= invertedByCrease); // (Inflate folds nothing: before the fix 20 faces turned inside out, the dark specks and needles)
    CHECK(smallest > 0.02f); // (before: 0.001, the grid collapsed across the groove)
}
