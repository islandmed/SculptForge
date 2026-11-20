// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smooth's effect per stroke does not depend on the mesh's density (a tester's pen Shift-smooth on a dense limb barely
// changed the surface): the same brush (same size in the world, i.e. on the screen), the same strength and pressure
// remove about the same share of a bump pattern whether the mesh has 20 000 triangles or 1.3 million.
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/MeshBoolean.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace sf;

namespace {

constexpr Symmetry kNone{false, false, false};

// The bump pattern: radial, wavelength about 0.2 on the unit sphere (a brush of radius 0.3 spans about three of them).
float bumpAt(const glm::vec3& d) {
    const float k = 2.0f * 3.14159265f / 0.2f;
    return 0.01f * std::sin(k * d.x) * std::sin(k * d.y + 0.7f) * std::sin(k * d.z + 1.9f);
}

Mesh bumpy(int level) {
    Mesh m = makeIcosphere(level);
    for (glm::vec3& p : m.positions) p *= 1.0f + bumpAt(p);
    m.recomputeAllNormals();
    return m;
}

// The bump left (RMS of |p| - 1 over the vertices within `within` of `at`) after `dabs` dabs of Smooth along an arc
// (`spacing` x radius apart, the app's default 0.125 x the size), as a share of what it was.
struct Result {
    double left = 1.0;
    size_t verts = 0;
};
Result smoothed(Mesh mesh, float radius, float strength, float pressure, int dabs, float spacing = 0.125f) {
    Document d = makeDoc(std::move(mesh));
    const glm::vec3 from = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f)), toward(1.0f, 0.0f, 0.0f);
    const float step = spacing * radius;
    const float half = step * float(dabs - 1) / 2.0f;
    const glm::vec3 start = glm::normalize(std::cos(-half) * from + std::sin(-half) * toward);
    auto stroke = geom::arc(start, toward, dabs, step, radius, strength, pressure, pressure);
    const glm::vec3 mid = stroke[stroke.size() / 2].center;
    // (about the mean: Smooth also shrinks the sphere a little, which is no bump)
    auto rms = [&](const Mesh& m) {
        double sum = 0.0, sum2 = 0.0;
        size_t n = 0;
        for (const glm::vec3& p : m.positions)
            if (glm::distance(glm::normalize(p), mid) < radius * 0.6f) {
                const double h = double(glm::length(p)) - 1.0;
                sum += h, sum2 += h * h, ++n;
            }
        const double mean = sum / double(std::max<size_t>(n, 1));
        return std::make_pair(std::sqrt(std::max(0.0, sum2 / double(std::max<size_t>(n, 1)) - mean * mean)), n);
    };
    const auto before = rms(d.mesh);
    d.sculptor.beginStroke(d.mesh, "Smooth");
    for (DabParams& p : stroke) {
        // On the surface under the cursor, as the app picks it.
        RayHit hit;
        if (d.bvh.raycast(d.mesh, p.center * 3.0f, -p.center, hit)) p.center = hit.position;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, kNone);
    }
    d.undo.push(d.sculptor.endStroke());
    const auto after = rms(d.mesh);
    return {after.first / std::max(before.first, 1e-12), before.second};
}


// The share of the bump a stroke of `dabs` dabs takes away.
double removed(const Mesh& m, float strength, float pressure, int dabs) { return 1.0 - smoothed(m, 0.3f, strength, pressure, dabs).left; }

} // namespace

TEST(smooth_density_report) { // SF_SMOOTH_REPORT=1: the numbers, by density, strength 0.5
    if (!std::getenv("SF_SMOOTH_REPORT")) return;
    for (int level : {5, 6, 7, 8}) {
        const Mesh m = bumpy(level);
        std::printf("level %d (%zu tris, edge %.4f): bump left, 1 dab / 8 dabs: ", level, m.triangleCount(), geom::meanEdge(m));
        for (float pressure : {1.0f, 0.5f, 0.2f})
            std::printf("p%.1f %.3f/%.3f  ", pressure, smoothed(m, 0.3f, 0.5f, pressure, 1).left, smoothed(m, 0.3f, 0.5f, pressure, 8).left);
        std::printf("\n");
    }
    // What a dab costs (the app's backlog thins dabs that are slow): the 1.3M mesh, a brush of 0.1 and of 0.6 (a region of 50k points).
    Document d = makeDoc(bumpy(8));
    for (float radius : {0.1f, 0.3f, 0.6f}) {
        DabParams p;
        p.center = glm::vec3(0.0f, 0.0f, 1.0f);
        p.viewDir = -p.center;
        p.radius = radius;
        p.strength = 0.5f;
        d.sculptor.beginStroke(d.mesh, "Smooth");
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, kNone);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10.0;
        d.undo.push(d.sculptor.endStroke());
        std::printf("  Smooth dab, radius %.1f on 1.3M triangles: %.2f ms\n", double(radius), ms);
    }
}

// The same brush (size in the world, Intensity, pressure) takes about the same share of a bump pattern off at 82k, 330k and
// 1.3M triangles (before: 48%, 9% and 2% of it after 8 dabs: a pen's Shift-smooth on a dense limb barely changed it).
TEST(smooth_effect_does_not_fall_with_density) {
    double share[3];
    int k = 0;
    for (int level : {6, 7, 8}) {
        const Mesh m = bumpy(level);
        share[k] = removed(m, 0.5f, 1.0f, 8);
        std::printf("  Smooth, 8 dabs at Intensity 0.5 on %zu triangles: %.1f%% of the bump gone\n", m.triangleCount(), 100.0 * share[k]);
        CHECK(share[k] > 0.3);
        ++k;
    }
    const double lo = std::min({share[0], share[1], share[2]}), hi = std::max({share[0], share[1], share[2]});
    CHECK(hi < 1.25 * lo);
}

// Pressure scales the strength once (a dab's effect is about proportional to it, not to its square).
TEST(smooth_pressure_is_applied_once) {
    const Mesh m = bumpy(8);
    const double full = removed(m, 0.5f, 1.0f, 1), half = removed(m, 0.5f, 0.5f, 1), fifth = removed(m, 0.5f, 0.2f, 1);
    std::printf("  one Smooth dab on %zu triangles removes %.2f%% at pressure 1, %.2f%% at 0.5, %.2f%% at 0.2\n", m.triangleCount(),
                100.0 * full, 100.0 * half, 100.0 * fifth);
    CHECK(full > 0.04);
    CHECK(half > 0.4 * full && half < 0.65 * full);
    CHECK(fifth > 0.12 * full && fifth < 0.3 * full);
}

// Smooth on a dense curved mesh: nothing shoots out at the falloff edge, nothing turns inside out, and a mirrored dab keeps
// the mirror (the steps run per mirror copy).
TEST(smooth_dense_sphere_invariants) {
    Document d = geom::denseSphere();
    const std::vector<glm::vec3> before = d.mesh.positions;
    const float edge = geom::meanEdge(d.mesh);
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f));
    p.viewDir = -p.center;
    p.radius = 0.3f;
    p.strength = 1.0f;
    d.sculptor.beginStroke(d.mesh, "Smooth");
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, Symmetry{true, false, false});
    d.undo.push(d.sculptor.endStroke());
    const glm::vec3 mirrored(-p.center.x, p.center.y, p.center.z);
    const geom::EdgeReport e = geom::edgeOutliers(d.mesh, before, {p.center, mirrored}, p.radius, edge);
    const float sym = geom::symmetryError(d.mesh, geom::mirrorMapX(geom::denseSphereFixture().mesh));
    std::printf("  Smooth across the mirror on %zu triangles: moved %.5f, mirror %.2g, edge umbrella %.3f (%d spikes)\n",
                d.mesh.triangleCount(), double(geom::maxMove(before, d.mesh)), double(sym), double(e.worst), e.spikes);
    CHECK(geom::nonFinite(d.mesh) == 0);
    CHECK(geom::invertedFaces(before, d.mesh) == 0);
    CHECK(e.spikes == 0);
    CHECK(sym < 1e-4f);
}

// A masked point does not move, however many steps the dab runs; the free ones next to it do.
TEST(smooth_steps_keep_the_mask) {
    Mesh m = bumpy(7);
    m.mask.assign(m.vertexCount(), 0.0f);
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (m.positions[v].y > 0.0f) m.mask[v] = 1.0f;
    const std::vector<glm::vec3> before = m.positions;
    Document d = makeDoc(std::move(m));
    DabParams p;
    p.center = glm::vec3(0.0f, 0.0f, 1.0f);
    p.viewDir = -p.center;
    p.radius = 0.3f;
    p.strength = 1.0f;
    d.sculptor.beginStroke(d.mesh, "Smooth");
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, kNone);
    d.undo.push(d.sculptor.endStroke());
    size_t maskedMoved = 0, freeMoved = 0;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const bool moved = d.mesh.positions[v] != before[v];
        (d.mesh.mask[v] >= 1.0f ? maskedMoved : freeMoved) += moved;
    }
    std::printf("  Smooth over a mask: %zu masked points moved, %zu free\n", maskedMoved, freeMoved);
    CHECK(maskedMoved == 0);
    CHECK(freeMoved > 100);
}

// Keep volume: the Taubin pair, repeated, still keeps the form (a plain smooth of the same strokes shrinks it more).
TEST(smooth_keep_volume_on_a_dense_mesh) {
    auto shrink = [](bool keep) {
        Document d = makeDoc(makeIcosphere(7));
        const double before = meshVolume(d.mesh);
        d.sculptor.beginStroke(d.mesh, "Smooth");
        for (const DabParams& dab : geom::arc(glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), 12, 0.04f, 0.3f, 1.0f, 1.0f, 1.0f)) {
            DabParams q = dab;
            q.keepVolume = keep;
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, q, kNone);
        }
        d.undo.push(d.sculptor.endStroke());
        return geom::volumeChange(before, meshVolume(d.mesh));
    };
    const double plain = shrink(false), keep = shrink(true);
    std::printf("  Smooth on 328k triangles, 12 dabs: volume %.3f%% plain, %.3f%% keeping volume\n", 100.0 * plain, 100.0 * keep);
    CHECK(std::abs(keep) < 0.5 * std::abs(plain));
}
