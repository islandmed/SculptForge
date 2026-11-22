// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inflate on a thin dense tube with a brush many times wider than the tube (member report on 0.13.11: the first strokes
// make a clean bulb, then a wide circle bends the tube into S-shapes and lumps). The promise: what every dab moves goes out
// along each vertex's own normal, a bulb and the thin tube beside it alike, with or without X symmetry. On 0.13.11 a dab
// that held the bulb and the thin tube took the thin tube's smaller cells for a packed groove and carried the whole dab
// along one area normal (0.73 to 0.76 of the moves along the own normals). The axis drift printed is the sphere falloff's
// (the tube's near side, closer to the dab's centre, swells more than its far side); with View falloff it is 0.
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/Sculpt.h"
#include "core/StarterMeshes.h"
#include "core/Tube.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

using namespace sf;
using glm::vec3;

namespace {
// A long thin tube along x, centred on the origin, with square cells (`round` of them round) as a remeshed Part has.
Mesh thinTube(float length, float radius, int round) {
    TubeShape s;
    s.points = {{vec3(-0.5f * length, 0, 0), radius}, {vec3(0, 0, 0), radius}, {vec3(0.5f * length, 0, 0), radius}};
    s.edge = 2.0f * 3.14159265f * radius / float(round);
    return tubeMesh(s);
}

struct Tally {
    double along = 0.0, total = 0.0;
    float share() const { return total > 0.0 ? float(along / total) : 1.0f; }
};

// One stroke of Inflate dabs from a to b, each on the surface under it seen along -z; the moves tallied against the own
// normals. With `pen`, the pressure swells from 0.3 to 1 and back along the stroke (a mouse presses fully throughout).
void inflateStroke(Document& d, vec3 a, vec3 b, float radius, float strength, bool sym, Tally& t, bool pen = false) {
    CHECK(d.sculptor.beginStroke(d.mesh, "Inflate"));
    const int n = std::max(2, int(glm::distance(a, b) / (0.15f * radius)) + 1);
    for (int i = 0; i < n; ++i) {
        const float u = float(i) / float(n - 1);
        DabParams p;
        p.center = glm::mix(a, b, u);
        RayHit hit;
        if (d.bvh.raycast(d.mesh, p.center + vec3(0, 0, 4.0f * radius), vec3(0, 0, -1), hit)) {
            p.center = hit.position;
            p.pickNormal = hit.normal;
        }
        p.viewDir = vec3(0, 0, -1);
        p.radius = radius;
        p.strength = strength;
        p.pressure = pen ? 0.3f + 0.7f * std::sin(3.14159265f * u) : 1.0f;
        const std::vector<vec3> before = d.mesh.positions, normals = d.mesh.normals;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Inflate, p, Symmetry{sym, false, false});
        for (uint32_t v = 0; v < before.size(); ++v) {
            const vec3 move = d.mesh.positions[v] - before[v];
            const float len = glm::length(move);
            if (len < 1e-6f * radius) continue;
            t.along += double(glm::dot(move / len, glm::normalize(normals[v]))) * len;
            t.total += len;
        }
    }
    d.undo.push(d.sculptor.endStroke());
}

// How far the tube's axis moved: the largest shift of a ring's centre (vertices of one start x), over the tube radius.
float axisDrift(const Mesh& start, const Mesh& m, float radius) {
    std::map<long, std::pair<glm::dvec3, glm::dvec3>> rings; // start x -> (sum before, sum after)
    std::map<long, int> count;
    for (uint32_t v = 0; v < start.vertexCount(); ++v) {
        const long key = std::lround(start.positions[v].x / radius * 1000.0f);
        rings[key].first += glm::dvec3(start.positions[v]);
        rings[key].second += glm::dvec3(m.positions[v]);
        ++count[key];
    }
    float worst = 0.0f;
    for (auto& [key, sums] : rings) {
        if (count[key] < 16) continue; // (the caps)
        const glm::dvec3 shift = (sums.second - sums.first) / double(count[key]);
        worst = std::max(worst, float(std::hypot(shift.y, shift.z)) / radius);
    }
    return worst;
}
} // namespace

TEST(inflate_wide_brush_thin_tube_invariant_along_own_normals) {
    // The member's clip: a bulb with a brush 2.7 tube radii wide, stacked strokes of the same size beside it, then a circle
    // about 10 radii wide across the bulb and the tube, centred on the tube and a little off it. The scene at 1/100, 1 and
    // 100 times the size, the tube 40 and 160 cells round, mouse and pen (pressure 0.3 to 1), X symmetry off and on, the
    // member's intensity (25) and the default (50).
    for (const float scale : {0.01f, 1.0f, 100.0f})
        for (const int round : {40, 160}) {
            const float R = 0.1f * scale;
            const Mesh start = thinTube(40.0f * R, R, round);
            for (const bool pen : {false, true})
                for (const bool sym : {false, true})
                    for (const float off : {0.0f, 0.6f})
                        for (const float strength : {0.25f, 0.5f}) {
                            Document d = makeDoc(start);
                            Tally bulb, stacked, wide;
                            const float x = 6.0f * R;
                            for (int i = 0; i < 2; ++i)
                                inflateStroke(d, vec3(x - 0.2f * R, 0, 0), vec3(x + 0.2f * R, 0, 0), 2.7f * R, strength, sym, bulb, pen);
                            for (int i = 0; i < 3; ++i)
                                inflateStroke(d, vec3(x + 1.5f * R, 0, 0), vec3(x + 4.0f * R, 0, 0), 2.7f * R, strength, sym, stacked, pen);
                            const float driftStacked = axisDrift(start, d.mesh, R);
                            for (int i = 0; i < 3; ++i)
                                inflateStroke(d, vec3(x - 4.0f * R, off * 10.0f * R, 0), vec3(x + 6.0f * R, off * 10.0f * R, 0), 10.0f * R,
                                              strength, sym, wide, pen);
                            const float drift = axisDrift(start, d.mesh, R);
                            const size_t flipped = geom::flippedFaces(start.positions, d.mesh);
                            std::printf("  scale %g, %d round, %s, strength %.2f, X symmetry %d, wide circle %.1f off: along the own normals "
                                        "bulb %.3f, stacked %.3f, wide %.3f; axis drift %.3f R stacked, %.3f R wide; %zu flipped\n",
                                        scale, round, pen ? "pen" : "mouse", strength, int(sym), off, bulb.share(), stacked.share(),
                                        wide.share(), driftStacked, drift, flipped);
                            CHECK(bulb.share() >= 0.95f);
                            CHECK(stacked.share() >= 0.95f);
                            CHECK(wide.share() >= 0.95f);
                            CHECK(flipped == 0);
                        }
        }
}

TEST(inflate_wide_brush_stretched_cylinder_invariant_along_own_normals) {
    // The stock Cylinder pulled long and thin, a brush 5.4 and 8 tube radii wide along it (the residual inflate-cylinder left),
    // at the member's intensity (25) and the default (50).
    const StarterMesh* starter = findStarterMesh("cylinder");
    CHECK(starter != nullptr);
    for (const int levels : {1, 2}) {
        Document d0;
        std::string error;
        CHECK(loadStarterMesh(*starter, d0, levels, error));
        Mesh m = d0.mesh;
        for (vec3& p : m.positions) p *= vec3(0.47f, 1.75f, 0.47f);
        m.recomputeAllNormals();
        const AABB b = m.bounds();
        const float R = 0.5f * std::min(b.extent().x, b.extent().z), h = b.extent().y;
        // (8 radii at strength 0.5 is left out: there each vertex along its own normal turns the pulled cap's triangles over,
        // and the dab takes the one area normal instead.)
        for (const bool sym : {false, true})
            for (const auto [brush, strength] : {std::pair{5.4f, 0.25f}, std::pair{5.4f, 0.5f}, std::pair{8.0f, 0.25f}}) {
                Document d = makeDoc(m);
                Tally t;
                for (int i = 0; i < 2; ++i)
                    inflateStroke(d, b.center() + vec3(0, -0.3f * h, 0), b.center() + vec3(0, 0.3f * h, 0), brush * R, strength, sym, t);
                std::printf("  cylinder %zu tris stretched, X symmetry %d, brush %.1f R, strength %.2f: %.3f along the own normals\n",
                            m.triangleCount(), int(sym), brush, strength, t.share());
                CHECK(t.share() >= 0.95f);
            }
    }
}

namespace {
// A stroke of dabs along the horizontal line across the tube's front at height 0 (x from -0.8 R to 0.8 R), each on the surface under it.
void acrossTube(Document& d, BrushType type, const char* name, float R, float radius, float strength, bool pen) {
    CHECK(d.sculptor.beginStroke(d.mesh, name));
    const float step = std::max(0.15f * radius, 0.02f * R);
    for (float x = -0.8f * R; x <= 0.8f * R; x += step) {
        DabParams p;
        p.center = vec3(x, 0.0f, 2.0f * R);
        RayHit hit;
        if (d.bvh.raycast(d.mesh, p.center, vec3(0, 0, -1), hit)) {
            p.center = hit.position;
            p.pickNormal = hit.normal;
        }
        p.viewDir = vec3(0, 0, -1);
        p.radius = radius;
        p.strength = strength;
        p.pressure = pen ? 0.3f + 0.7f * std::sin(3.14159265f * (x / R + 0.8f) / 1.6f) : 1.0f;
        d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{false, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
}
} // namespace

TEST(inflate_wide_brush_groove_stays_whole_at_any_scale) {
    // The #194 groove on a tube (artist-qa's case): Crease across the stretched stock Cylinder, then Inflate 5 radii and a
    // stacked 2-then-5-radii pass across it, with the scene at 1/100 and 100 times the size, mouse and pen: the fold test is
    // a ratio of lengths, so the groove takes the one area normal alike and stays whole.
    const StarterMesh* starter = findStarterMesh("cylinder");
    CHECK(starter != nullptr);
    for (const float scale : {0.01f, 100.0f})
        for (const int levels : {1, 2})
            for (const bool pen : {false, true}) {
                Document d0;
                std::string error;
                CHECK(loadStarterMesh(*starter, d0, levels, error));
                Mesh m = d0.mesh;
                for (vec3& p : m.positions) p *= vec3(0.47f, 1.75f, 0.47f) * scale;
                m.recomputeAllNormals();
                Document d = makeDoc(std::move(m));
                const AABB b = d.mesh.bounds();
                const float R = 0.5f * std::min(b.extent().x, b.extent().z);
                for (int i = 0; i < 4; ++i) acrossTube(d, BrushType::Crease, "Crease", R, 0.15f * R, 0.5f, pen);
                const std::vector<vec3> grooved = d.mesh.positions;
                for (int i = 0; i < 3; ++i) acrossTube(d, BrushType::Inflate, "Inflate", R, 5.0f * R, 0.5f, pen);
                for (int i = 0; i < 2; ++i) acrossTube(d, BrushType::Inflate, "Inflate", R, (i ? 5.0f : 2.0f) * R, i ? 1.0f : 0.5f, pen);
                const size_t flipped = geom::flippedFaces(grooved, d.mesh), inverted = geom::invertedFaces(grooved, d.mesh);
                const size_t folded = geom::foldedFaces(grooved, d.mesh);
                std::printf("  groove, scale %g, %zu tris, %s: %zu flipped, %zu inverted, %zu folded\n", scale, d.mesh.triangleCount(),
                            pen ? "pen" : "mouse", flipped, inverted, folded);
                CHECK(flipped == 0);
                CHECK(inverted == 0);
                CHECK(folded == 0);
            }
}
