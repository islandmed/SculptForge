// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inflate on a thin tube, the member's own sequence (0.13.11 clip) as the app lays it down (its dabs logged): the stock
// Cylinder at level 2 scaled 0.15 x 2.5 x 0.15 (a tube 1.5 in radius, R), X symmetry, intensity 50, the mouse; a bulb with
// two strokes 2.6 R in radius, three stacked strokes of the same size below it, then three circles 9.9 R in radius up and
// down the tube. Today most of the big dabs take Inflate's one area normal (the tube beside the swollen bulb looks packed),
// which keeps the swollen neck whole but carries the tube toward the viewer into an S-bend. Moving those dabs along the
// own normals stops the drift but folds the neck (626 flipped faces); sub-steps and the Nudge fold limiter don't stop it.
// What holds today, and must keep holding: nothing turns over; the drift toward the viewer gets no worse.
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/Sculpt.h"
#include "core/StarterMeshes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

using namespace sf;
using glm::vec3;

namespace {
struct Tally {
    double along = 0.0, total = 0.0;
    float share() const { return total > 0.0 ? float(along / total) : 1.0f; }
};

// One stroke of Inflate dabs from a to b, 0.15 radii apart (the app's spacing), each on the surface under it seen along -z,
// X symmetry on, full pressure; the moves tallied against the own normals.
void inflateStroke(Document& d, vec3 a, vec3 b, float radius, Tally& t) {
    CHECK(d.sculptor.beginStroke(d.mesh, "Inflate"));
    const int n = std::max(2, int(glm::distance(a, b) / (0.15f * radius)) + 1);
    for (int i = 0; i < n; ++i) {
        DabParams p;
        p.center = glm::mix(a, b, float(i) / float(n - 1));
        RayHit hit;
        if (d.bvh.raycast(d.mesh, p.center + vec3(0, 0, 4.0f * radius), vec3(0, 0, -1), hit)) {
            p.center = hit.position;
            p.pickNormal = hit.normal;
        }
        p.viewDir = vec3(0, 0, -1);
        p.radius = radius;
        p.strength = 0.5f;
        const std::vector<vec3> before = d.mesh.positions, normals = d.mesh.normals;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Inflate, p, Symmetry{true, false, false});
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

// How far a tube along y moved toward the viewer (+z): the largest shift of a ring's centre (the vertices of one start y,
// rings `step` apart), over the tube radius.
float depthDrift(const Mesh& start, const Mesh& m, float radius, float step) {
    std::map<long, std::pair<double, int>> rings;
    for (uint32_t v = 0; v < start.vertexCount(); ++v) {
        auto& [sum, n] = rings[std::lround(start.positions[v].y / step)];
        sum += double(m.positions[v].z - start.positions[v].z);
        ++n;
    }
    float worst = 0.0f;
    for (const auto& [key, ring] : rings)
        if (ring.second >= 16) worst = std::max(worst, float(std::abs(ring.first / ring.second)) / radius);
    return worst;
}
} // namespace

TEST(inflate_member_sequence_on_the_stretched_stock_cylinder_invariant) {
    const StarterMesh* starter = findStarterMesh("cylinder");
    CHECK(starter != nullptr);
    Document d0;
    std::string error;
    CHECK(loadStarterMesh(*starter, d0, 2, error));
    Mesh m = d0.mesh;
    for (vec3& p : m.positions) p *= vec3(0.15f, 2.5f, 0.15f);
    m.recomputeAllNormals();
    const Mesh start = m;
    const AABB b = start.bounds();
    const float R = 0.5f * std::min(b.extent().x, b.extent().z);
    Document d = makeDoc(start);
    Tally small, wide;
    const auto stroke = [&](float x0, float y0, float x1, float y1, float radius, Tally& t) {
        inflateStroke(d, vec3(x0 * R, y0 * R, 0), vec3(x1 * R, y1 * R, 0), radius * R, t);
    };
    for (int i = 0; i < 2; ++i) stroke(-0.19f, 11.33f, -0.19f, 10.15f, 2.6f, small);
    for (int i = 0; i < 3; ++i) stroke(-0.23f, 8.0f, -0.23f, 2.8f, 2.6f, small);
    const float driftSmall = depthDrift(start, d.mesh, R, 0.25f * R);
    stroke(-0.19f, 15.3f, -0.28f, -1.9f, 9.9f, wide);
    stroke(-0.28f, -0.43f, -0.44f, 15.5f, 9.9f, wide);
    stroke(-0.44f, 14.0f, -0.42f, -0.6f, 9.9f, wide);
    const float driftWide = depthDrift(start, d.mesh, R, 0.25f * R);
    const size_t flipped = geom::flippedFaces(start.positions, d.mesh), folded = geom::foldedFaces(start.positions, d.mesh);
    const size_t inverted = geom::invertedFaces(start.positions, d.mesh);
    std::printf("  along the own normals %.3f bulb + stacked, %.3f wide; toward the viewer %.3f R, then %.3f R; %zu flipped, %zu folded, "
                "%zu inverted\n", small.share(), wide.share(), driftSmall, driftWide, flipped, folded, inverted);
    CHECK(flipped == 0);
    CHECK(folded == 0);
    CHECK(inverted == 0);
    // (today 0.45 R and 5.5 R: a fix brings these down, nothing may raise them)
    CHECK(driftSmall <= 0.5f);
    CHECK(driftWide <= 6.0f);
}
