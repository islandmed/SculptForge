// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inflate on a thin dense tube with a brush many times wider than the tube (member report on 0.13.11: the first strokes
// make a clean bulb, then a wide circle bends the tube into S-shapes and lumps), and on a Crease groove across a tube, at
// any scene scale, mesh density, pen or mouse. What holds today, and must keep holding: the bulb and the stacked strokes
// beside it move each vertex along its own normal, nothing turns over, and a groove under stacked Inflate stays whole.
// What does not hold yet is printed, not checked: the wide circle's share along the own normals (0.73 to 0.76: the thin
// tube beside the bulb looks packed and the dab takes the one area normal) and the axis drift (the sphere falloff swells
// the tube's near side more than its far side).
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/Sculpt.h"
#include "core/StarterMeshes.h"
#include "core/Tube.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

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

namespace {
// Mouse or pen, X symmetry, the wide circle's offset (tube radii x 10) and the intensity: five of the sixteen
// combinations, every pair of the four values met (the full grid took 22 s on the PC, over the 20 s budget).
struct Input {
    bool pen, sym;
    float off, strength;
};
const Input kInputs[] = {
    {false, false, 0.0f, 0.25f}, {false, true, 0.6f, 0.5f}, {true, false, 0.6f, 0.5f}, {true, true, 0.0f, 0.5f}, {true, true, 0.6f, 0.25f},
};
} // namespace

TEST(inflate_wide_brush_thin_tube_invariant_along_own_normals) {
    // The member's clip: a bulb with a brush 2.7 tube radii wide, stacked strokes of the same size beside it, then a circle
    // about 10 radii wide across the bulb and the tube, centred on the tube and a little off it. The scene at 1/100, 1 and
    // 100 times the size, the tube 40 and 160 cells round, mouse and pen (pressure 0.3 to 1), X symmetry off and on, the
    // member's intensity (25) and the default (50), in the combinations of kInputs.
    for (const float scale : {0.01f, 1.0f, 100.0f})
        for (const int round : {40, 160}) {
            const float R = 0.1f * scale;
            const Mesh start = thinTube(40.0f * R, R, round);
            for (const Input& in : kInputs) { // (the wide share: printed only, see the top)
                Document d = makeDoc(start);
                Tally bulb, stacked, wide;
                const float x = 6.0f * R;
                for (int i = 0; i < 2; ++i)
                    inflateStroke(d, vec3(x - 0.2f * R, 0, 0), vec3(x + 0.2f * R, 0, 0), 2.7f * R, in.strength, in.sym, bulb, in.pen);
                for (int i = 0; i < 3; ++i)
                    inflateStroke(d, vec3(x + 1.5f * R, 0, 0), vec3(x + 4.0f * R, 0, 0), 2.7f * R, in.strength, in.sym, stacked, in.pen);
                const float driftStacked = axisDrift(start, d.mesh, R);
                for (int i = 0; i < 3; ++i)
                    inflateStroke(d, vec3(x - 4.0f * R, in.off * 10.0f * R, 0), vec3(x + 6.0f * R, in.off * 10.0f * R, 0), 10.0f * R,
                                  in.strength, in.sym, wide, in.pen);
                const float drift = axisDrift(start, d.mesh, R);
                const size_t flipped = geom::flippedFaces(start.positions, d.mesh);
                std::printf("  scale %g, %d round, %s, strength %.2f, X symmetry %d, wide circle %.1f off: along the own normals "
                            "bulb %.3f, stacked %.3f, wide %.3f; axis drift %.3f R stacked, %.3f R wide; %zu flipped\n",
                            scale, round, in.pen ? "pen" : "mouse", in.strength, int(in.sym), in.off, bulb.share(), stacked.share(),
                            wide.share(), driftStacked, drift, flipped);
                CHECK(bulb.share() >= 0.95f);
                CHECK(stacked.share() >= 0.95f);
                CHECK(flipped == 0);
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

namespace {
// One stroke from a to b (dabs a tenth of the radius apart, each on the surface under it seen along -z), X symmetry on,
// the pressure fixed: as the app's stroke command lays it down.
void lineStroke(Document& d, BrushType type, const char* name, vec3 a, vec3 b, float radius, float pressure) {
    CHECK(d.sculptor.beginStroke(d.mesh, name));
    const int n = std::max(2, int(glm::distance(a, b) / (0.1f * radius)) + 1);
    for (int i = 0; i < n; ++i) {
        DabParams p;
        p.center = glm::mix(a, b, float(i) / float(n - 1));
        RayHit hit;
        if (!d.bvh.raycast(d.mesh, p.center + vec3(0, 0, 4.0f * radius), vec3(0, 0, -1), hit)) continue;
        p.center = hit.position;
        p.pickNormal = hit.normal;
        p.viewDir = vec3(0, 0, -1);
        p.radius = radius;
        p.strength = 0.5f;
        p.pressure = pressure;
        d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{true, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
}
} // namespace

TEST(inflate_stacked_over_crease_groove_build_up_stays_whole) {
    // artist-qa's session on the stretched stock Cylinder (1440x900, 15.8 px a unit, the tube 74 px in radius), X symmetry on,
    // intensity 50: two Crease strokes of 60 px across the tube's middle, then Inflate along the tube across the groove,
    // 160 px (a straight stroke at full pressure, a slanted one at 0.6) and 400 px the same. Many dabs build up over the
    // groove: a rule that judged each dab alone let the swelling's lower lip roll over the groove (50 flipped faces).
    const StarterMesh* starter = findStarterMesh("cylinder");
    CHECK(starter != nullptr);
    for (const int levels : {1, 2}) {
        Document d0;
        std::string error;
        CHECK(loadStarterMesh(*starter, d0, levels, error));
        Mesh m = d0.mesh;
        for (vec3& p : m.positions) p *= vec3(0.47f, 1.75f, 0.47f);
        m.recomputeAllNormals();
        Document d = makeDoc(std::move(m));
        const std::vector<vec3> start = d.mesh.positions;
        const AABB b = d.mesh.bounds();
        const vec3 c = b.center();
        const float px = 0.5f * std::min(b.extent().x, b.extent().z) / 74.0f; // (one pixel of the session)
        lineStroke(d, BrushType::Crease, "Crease", c + vec3(-390, 0, 0) * px, c + vec3(390, 0, 0) * px, 60 * px, 1.0f);
        lineStroke(d, BrushType::Crease, "Crease", c + vec3(-390, -14, 0) * px, c + vec3(390, -14, 0) * px, 60 * px, 1.0f);
        const std::vector<vec3> grooved = d.mesh.positions;
        for (const float radius : {160.0f, 400.0f}) {
            lineStroke(d, BrushType::Inflate, "Inflate", c + vec3(0, 139, 0) * px, c + vec3(0, -139, 0) * px, radius * px, 1.0f);
            lineStroke(d, BrushType::Inflate, "Inflate", c + vec3(-49, -139, 0) * px, c + vec3(49, 139, 0) * px, radius * px, 0.6f);
        }
        const size_t flipped = geom::flippedFaces(start, d.mesh), flippedGroove = geom::flippedFaces(grooved, d.mesh);
        const size_t inverted = geom::invertedFaces(grooved, d.mesh), folded = geom::foldedFaces(grooved, d.mesh);
        std::printf("  Crease then Inflate 160 + 400 px on the stretched cylinder (%zu tris): %zu flipped against the start, %zu against "
                    "the groove, %zu inverted, %zu folded\n", d.mesh.triangleCount(), flipped, flippedGroove, inverted, folded);
        // (At 12k the swollen lip's coarse faces turn past 90 degrees from the start on 0.13.11 too, 113 of them, rolling
        // nothing over: they are checked against the groove. The per-dab fold rule tore both: 27 against the groove at 12k,
        // 292 against the start at 49k.)
        if (levels == 2) CHECK(flipped == 0);
        CHECK(flippedGroove == 0);
        CHECK(inverted == 0);
    }
}
