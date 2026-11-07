// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cloth solver (core/Cloth.h): edge lengths kept under gravity, energy, contact with a collider, rest, seams,
// determinism across thread counts, presets, bad setups.
#include "TestHarness.h"

#include "core/Cloth.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace sf;

namespace {

// A square sheet of `res` x `res` cells, `size` metres, horizontal at height y.
ClothSetup sheet(int res, float size, float y) {
    const Mesh m = makePlane(res, size);
    ClothSetup s;
    s.positions = m.positions;
    for (glm::vec3& p : s.positions) p.y += y;
    s.triangles = m.triangles;
    return s;
}

} // namespace

TEST(cloth_presets_and_names) {
    FabricPreset f;
    CHECK(fabricFromName("denim", f) && f == FabricPreset::Denim);
    CHECK(fabricFromName("Silk", f) && f == FabricPreset::Silk);
    CHECK(!fabricFromName("nylon", f));
    // Stiffer fabrics bend less and weigh more, in order.
    for (int i = 0; i + 1 < int(FabricPreset::Count); ++i) {
        CHECK(fabricPreset(FabricPreset(i)).bendCompliance > fabricPreset(FabricPreset(i + 1)).bendCompliance);
        CHECK(fabricPreset(FabricPreset(i)).density < fabricPreset(FabricPreset(i + 1)).density);
    }
}

TEST(cloth_rejects_bad_setups) {
    ClothSim sim;
    ClothSetup s;
    CHECK(!sim.init(s, fabricPreset(FabricPreset::Cotton)));
    s = sheet(2, 1.0f, 0.0f);
    s.triangles.push_back({0, 1, 999});
    CHECK(!sim.init(s, fabricPreset(FabricPreset::Cotton)));
    s = sheet(2, 1.0f, 0.0f);
    s.pin.assign(3, 1.0f); // (wrong size)
    CHECK(!sim.init(s, fabricPreset(FabricPreset::Cotton)));
    s.pin.clear();
    s.seams.push_back({0, 1000});
    CHECK(!sim.init(s, fabricPreset(FabricPreset::Cotton)));
    s.seams.clear();
    CHECK(sim.init(s, fabricPreset(FabricPreset::Cotton)));
}

TEST(cloth_hanging_sheet_keeps_its_length) {
    // A 1 m sheet held along one edge falls and swings down; its edges stay within a couple of percent of rest
    // and it ends up hanging below the held edge.
    const int res = 20;
    ClothSetup s = sheet(res, 1.0f, 0.0f);
    s.pin.assign(s.positions.size(), 0.0f);
    for (int i = 0; i <= res; ++i) s.pin[i] = 1.0f; // (row j = 0)
    ClothSim sim;
    CHECK(sim.init(s, fabricPreset(FabricPreset::Cotton)));
    double peak = 0.0;
    float worstStrain = 0.0f;
    for (int f = 0; f < 240; ++f) {
        sim.step();
        peak = std::max(peak, sim.kineticEnergy());
        worstStrain = std::max(worstStrain, sim.maxStrain());
    }
    std::printf("  hanging: worst strain %.4f, final strain %.4f, energy %.4g (peak %.4g)\n", worstStrain,
                sim.maxStrain(), sim.kineticEnergy(), peak);
    CHECK(sim.maxStrain() < 0.03f);
    CHECK(sim.kineticEnergy() < peak * 0.5); // (damped: the swing dies down)
    const glm::vec3 farCorner = sim.positions()[size_t(res) * (res + 1) + res];
    CHECK(farCorner.y < -0.8f);              // hanging nearly straight down
    for (int i = 0; i <= res; ++i) CHECK(sim.positions()[i] == s.positions[i]); // held edge never moved
}

TEST(cloth_drapes_over_a_sphere_and_rests) {
    // A sheet dropped onto a ball rests on it: nothing inside the ball (thickness kept, within a little), the
    // middle on top, the corners hanging down past the ball's equator.
    const float radius = 0.3f;
    const Mesh ball = makeIcosphere(4, radius);
    ClothColliders colliders;
    colliders.add(ball);
    colliders.build();
    CHECK(colliders.triangles() == ball.triangles.size());
    ClothSetup s = sheet(30, 1.0f, radius + 0.05f);
    const Fabric& fabric = fabricPreset(FabricPreset::Cotton);
    DrapeOptions opt;
    opt.maxFrames = 900;
    const DrapeResult r = drapeToRest(s, colliders, fabric, opt);
    std::printf("  sphere: %d frames, rested %d, %.0f ms\n", r.frames, int(r.rested), r.milliseconds);
    CHECK(r.rested);
    CHECK(r.positions.size() == s.positions.size());
    float closest = 1e9f;
    for (const glm::vec3& p : r.positions) closest = std::min(closest, glm::length(p));
    std::printf("  sphere: closest particle %.4f m from the centre (ball %.3f + thickness %.4f)\n", closest, radius,
                fabric.thickness);
    // (the icosphere's flat faces sit up to ~0.3% inside the true radius)
    CHECK(closest > radius * 0.99f);
    const glm::vec3 middle = r.positions[s.positions.size() / 2];
    CHECK(middle.y > radius * 0.98f && middle.y < radius + 0.02f);
    CHECK(r.positions[0].y < 0.0f); // a corner hangs below the equator
}

TEST(cloth_same_result_on_any_thread_count) {
    const Mesh ball = makeIcosphere(3, 0.3f);
    ClothColliders colliders;
    colliders.add(ball);
    colliders.build();
    ClothSetup s = sheet(40, 1.0f, 0.35f); // (big enough that the pool splits every pass)
    DrapeOptions opt;
    opt.maxFrames = 60;
    const DrapeResult parallel = drapeToRest(s, colliders, fabricPreset(FabricPreset::Silk), opt);
    ThreadPool::shared().setSerial(true);
    const DrapeResult serial = drapeToRest(s, colliders, fabricPreset(FabricPreset::Silk), opt);
    ThreadPool::shared().setSerial(false);
    CHECK(parallel.positions.size() == serial.positions.size());
    CHECK(std::memcmp(parallel.positions.data(), serial.positions.data(), serial.positions.size() * sizeof(glm::vec3)) == 0);
}

TEST(cloth_seams_close) {
    // Two flat panels side by side with a 10 cm gap, sewn along their facing edges, no gravity: the seam closes.
    const int res = 10;
    ClothSetup a = sheet(res, 0.5f, 0.0f);
    ClothSetup s;
    const uint32_t n = uint32_t(a.positions.size());
    for (glm::vec3 p : a.positions) s.positions.push_back(p - glm::vec3(0.3f, 0.0f, 0.0f));
    for (glm::vec3 p : a.positions) s.positions.push_back(p + glm::vec3(0.3f, 0.0f, 0.0f));
    s.triangles = a.triangles;
    for (glm::uvec3 t : a.triangles) s.triangles.push_back(t + glm::uvec3(n));
    for (int j = 0; j <= res; ++j) s.seams.push_back({uint32_t(j * (res + 1) + res), n + uint32_t(j * (res + 1))});
    ClothSettings settings;
    settings.gravity = glm::vec3(0.0f);
    ClothSim sim;
    CHECK(sim.init(s, fabricPreset(FabricPreset::Cotton), settings));
    for (int f = 0; f < 120; ++f) sim.step();
    float gap = 0.0f;
    for (const ClothSeam& seam : s.seams) gap = std::max(gap, glm::length(sim.positions()[seam.a] - sim.positions()[seam.b]));
    std::printf("  seams: widest gap %.5f m, strain %.4f\n", gap, sim.maxStrain());
    CHECK(gap < 0.002f);
    CHECK(sim.maxStrain() < 0.05f); // (the panels moved together, not stretched)
}

// --- Cloth brush ---

namespace {

// A drag stroke across a flat sheet toward +x: `dabs` dabs, each moving `step` along the stroke.
void clothDrag(Document& d, float radius, int dabs, float step, Symmetry sym = Symmetry{false, false, false}) {
    d.sculptor.beginStroke(d.mesh, "Cloth");
    for (int i = 0; i < dabs; ++i) {
        DabParams p;
        p.center = glm::vec3(-0.4f + step * float(i), 0.0f, 0.0f);
        p.viewDir = glm::vec3(0, -1, 0);
        p.radius = radius;
        p.strength = 0.6f;
        p.strokeDelta = i == 0 ? glm::vec3(0.0f) : glm::vec3(step, 0.0f, 0.0f);
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Cloth, p, sym);
    }
    d.undo.push(d.sculptor.endStroke());
}

} // namespace

TEST(cloth_brush_drag_folds_without_stretching) {
    Document d = makeDoc(makePlane(100, 2.0f));
    const std::vector<glm::vec3> before = d.mesh.positions;
    const float radius = 0.2f, step = 0.02f;
    const int dabs = 20;
    clothDrag(d, radius, dabs, step);
    CHECK(std::string(brushName(BrushType::Cloth)) == "Cloth");
    BrushType t;
    CHECK(brushFromName("cloth", t) && t == BrushType::Cloth);
    // Out of the plane: folds (the flat sheet only moved along it, the cloth buckled up or down).
    float lift = 0.0f;
    for (const glm::vec3& q : d.mesh.positions) lift = std::max(lift, std::abs(q.y));
    // Nothing beyond the region (reach x radius around the stroke) moved.
    const float reach = d.sculptor.cloth.reach * radius + 1e-3f;
    bool outsideMoved = false;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const glm::vec3 a = before[v];
        const float x = std::clamp(a.x, -0.4f, -0.4f + step * float(dabs - 1));
        if (glm::distance(a, glm::vec3(x, 0.0f, 0.0f)) > reach && d.mesh.positions[v] != a) outsideMoved = true;
    }
    // Edges stay about their length: none much longer than it was (compression allowed: that is the folds).
    float stretch = 0.0f;
    glm::vec3 worstAt{0.0f};
    int over = 0, edges = 0;
    for (const glm::uvec3& tri : d.mesh.triangles)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = tri[k], b = tri[(k + 1) % 3];
            const float rest = glm::distance(before[a], before[b]);
            const float e = glm::distance(d.mesh.positions[a], d.mesh.positions[b]) / rest - 1.0f;
            ++edges, over += e > 0.1f;
            if (e > stretch) stretch = e, worstAt = before[a];
        }
    std::printf("  cloth brush: worst stretch at (%.3f, %.3f), %d of %d edges over 10%%\n", worstAt.x, worstAt.z, over, edges);
    std::printf("  cloth brush: lift %.4f (radius %.2f), worst stretch %.3f, region %zu, %.2f ms/dab\n", lift, radius,
                stretch, d.sculptor.clothRegion(), d.sculptor.timings.kernelMs / std::max<double>(1, double(d.sculptor.timings.dabs)));
    CHECK(lift > 0.01f * radius);
    CHECK(!outsideMoved);
    CHECK(stretch < 0.25f);
    // One undo step puts it all back.
    d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.positions == before);
}

TEST(cloth_brush_mask_holds) {
    Document d = makeDoc(makePlane(80, 2.0f));
    d.mesh.ensureMask();
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].z > 0.05f ? 1.0f : 0.0f;
    const std::vector<glm::vec3> before = d.mesh.positions;
    clothDrag(d, 0.25f, 15, 0.03f);
    bool maskedMoved = false, freeMoved = false;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (d.mesh.mask[v] >= 1.0f && d.mesh.positions[v] != before[v]) maskedMoved = true;
        if (d.mesh.mask[v] <= 0.0f && d.mesh.positions[v] != before[v]) freeMoved = true;
    }
    CHECK(!maskedMoved);
    CHECK(freeMoved);
}

TEST(cloth_brush_settle_drapes_down_and_collides) {
    // Settle over a ball: the sheet sags under gravity but stays outside the collider Part.
    Document d = makeDoc(makePlane(60, 1.0f));
    for (glm::vec3& q : d.mesh.positions) q.y += 0.32f;
    d.mesh.recomputeAllNormals();
    d.bvh.build(d.mesh);
    const Mesh ball = makeIcosphere(4, 0.3f);
    ClothColliders colliders;
    colliders.add(ball);
    colliders.build();
    d.sculptor.clothColliders = &colliders;
    d.sculptor.cloth.mode = ClothBrushMode::Settle;
    d.sculptor.beginStroke(d.mesh, "Cloth");
    for (int i = 0; i < 30; ++i) {
        DabParams p;
        p.center = glm::vec3(0.25f, 0.32f, 0.0f);
        p.viewDir = glm::vec3(0, -1, 0);
        p.radius = 0.15f;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Cloth, p, Symmetry{false, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
    float lowest = 1.0f, closest = 1.0f;
    for (const glm::vec3& q : d.mesh.positions) lowest = std::min(lowest, q.y), closest = std::min(closest, glm::length(q));
    std::printf("  settle: lowest %.4f, closest to the ball's centre %.4f\n", lowest, closest);
    CHECK(lowest < 0.30f);    // sagged
    CHECK(closest > 0.297f);  // never inside the ball
}
