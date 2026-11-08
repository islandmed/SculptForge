// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cloth solver (core/Cloth.h): edge lengths kept under gravity, energy, contact with a collider, rest, seams,
// determinism across thread counts, presets, bad setups.
#include "TestHarness.h"

#include "core/Cloth.h"
#include "core/Commands.h"
#include "core/Drape.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

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

// Per vertex, how far it sticks out (in y) of its ring of neighbours: {p99, max, where the max is}.
struct Roughness {
    float p99 = 0.0f, max = 0.0f;
    glm::vec3 at{0.0f};
};
Roughness roughness(const Mesh& m, const std::vector<glm::vec3>& before) {
    Roughness r;
    std::vector<float> all;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const auto ring = m.neighbors(v);
        if (ring.size() < 6) continue; // (the sheet's border)
        float mean = 0.0f;
        for (uint32_t n : ring) mean += m.positions[n].y;
        const float e = std::abs(m.positions[v].y - mean / float(ring.size()));
        all.push_back(e);
        if (e > r.max) r.max = e, r.at = before[v];
    }
    std::sort(all.begin(), all.end());
    r.p99 = all.empty() ? 0.0f : all[all.size() * 99 / 100];
    return r;
}

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
    // Smooth folds, not jitter: hardly any vertex sticks out of its ring of neighbours by more than a small part of
    // the folds' height (ripples a few edges wide did); the sharpest point is the crest under the brush.
    const Roughness rough = roughness(d.mesh, before);
    std::printf("  cloth brush: roughness p99 %.4f, max %.4f (%.2f of the lift) at (%.3f, %.3f)\n", rough.p99, rough.max,
                rough.max / std::max(lift, 1e-9f), rough.at.x, rough.at.z);
    CHECK(rough.p99 < 0.08f * lift);
    CHECK(rough.max < 0.2f * lift);
    // No step at the region's edge: the outer fifth of the reach moves little next to the folds.
    float edgeMove = 0.0f, maxMove = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const glm::vec3 a = before[v];
        const float x = std::clamp(a.x, -0.4f, -0.4f + step * float(dabs - 1));
        const float dist = glm::distance(a, glm::vec3(x, 0.0f, 0.0f));
        const float move = glm::distance(d.mesh.positions[v], a);
        maxMove = std::max(maxMove, move);
        if (dist > 0.8f * (reach - 1e-3f)) edgeMove = std::max(edgeMove, move);
    }
    std::printf("  cloth brush: the outer band moved %.4f (%.2f of the most)\n", edgeMove, edgeMove / std::max(maxMove, 1e-9f));
    CHECK(edgeMove < 0.1f * maxMove);
    std::printf("  cloth brush: lift %.4f (radius %.2f), worst stretch %.3f, region %zu, %.2f ms/dab\n", lift, radius,
                stretch, d.sculptor.clothRegion(), d.sculptor.timings.kernelMs / std::max<double>(1, double(d.sculptor.timings.dabs)));
    CHECK(lift > 0.01f * radius);
    CHECK(!outsideMoved);
    CHECK(stretch < 0.25f);
    // One undo step puts it all back.
    d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.positions == before);
}

namespace {

// The highest fold on each side of x = 0 after a 20-dab drag toward +x starting at `x0` (Symmetry X or none).
std::pair<float, float> dragFolds(float x0, bool symmetric, Roughness* rough = nullptr) {
    Document d = makeDoc(makePlane(100, 2.0f));
    const std::vector<glm::vec3> before = d.mesh.positions;
    d.sculptor.beginStroke(d.mesh, "Cloth");
    for (int i = 0; i < 20; ++i) {
        DabParams p;
        p.center = glm::vec3(x0 + 0.02f * float(i), 0.0f, 0.0f);
        p.viewDir = glm::vec3(0, -1, 0);
        p.radius = 0.2f;
        p.strength = 0.6f;
        p.strokeDelta = i == 0 ? glm::vec3(0.0f) : glm::vec3(0.02f, 0.0f, 0.0f);
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Cloth, p, Symmetry{symmetric, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
    float left = 0.0f, right = 0.0f;
    for (const glm::vec3& q : d.mesh.positions) (q.x < 0.0f ? left : right) = std::max(q.x < 0.0f ? left : right, std::abs(q.y));
    if (rough) *rough = roughness(d.mesh, before);
    return {left, right};
}

} // namespace

TEST(cloth_brush_symmetric_drag_across_the_mirror_plane) {
    // Symmetry X: the copies' moves add up per vertex (as every brush's do). A stroke on one side folds each side as
    // much as the same stroke alone. A stroke across x = 0 has the copies meet and then pull apart, which draws the
    // cloth taut between them (as two hands would): it must stay smooth and the same on both sides, not crumple.
    const auto [aloneL, aloneR] = dragFolds(0.25f, false);
    const auto [sideL, sideR] = dragFolds(0.25f, true);
    Roughness rough;
    const auto [crossL, crossR] = dragFolds(-0.2f, true, &rough);
    const float alone = std::max(aloneL, aloneR);
    std::printf("  folds: alone %.4f; symmetric on one side %.4f / %.4f; across the plane %.4f / %.4f; roughness p99 %.4f "
                "max %.4f\n", alone, sideL, sideR, crossL, crossR, rough.p99, rough.max);
    CHECK(std::abs(sideL - alone) < 0.25f * alone && std::abs(sideR - alone) < 0.25f * alone);
    const float lift = std::max(crossL, crossR);
    CHECK(std::abs(crossL - crossR) < 0.25f * lift);
    CHECK(rough.p99 < 0.08f * std::max(lift, 1e-6f));
    CHECK(rough.max < 0.25f * std::max(lift, 1e-6f));
}

TEST(cloth_brush_symmetric_on_a_sphere_folds_as_much) {
    // A 1 m ball seen from the front, a drag along +x on one side (x 0.2 -> 0.55) with Symmetry X, against the same
    // drag alone: each side moves the cloth off the ball at least as much as the drag alone (more: the cloth between
    // the two grabs, pulled both ways, rises into a taut ridge), never the weak smudge splitting the grabs gave.
    auto run = [](bool symmetric) {
        Document d = makeDoc(makeIcosphere(6, 1.0f));
        d.sculptor.beginStroke(d.mesh, "Cloth");
        for (int i = 0; i < 24; ++i) {
            const float x = 0.2f + 0.015f * float(i);
            DabParams p;
            p.center = glm::vec3(x, 0.0f, std::sqrt(1.0f - x * x));
            p.viewDir = glm::vec3(0, 0, -1);
            p.radius = 0.15f;
            p.strength = 0.5f;
            p.strokeDelta = i == 0 ? glm::vec3(0.0f) : glm::vec3(0.015f, 0.0f, 0.0f);
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Cloth, p, Symmetry{symmetric, false, false});
        }
        d.undo.push(d.sculptor.endStroke());
        float left = 0.0f, right = 0.0f;
        for (const glm::vec3& q : d.mesh.positions) {
            const float out = std::abs(glm::length(q) - 1.0f);
            (q.x < 0.0f ? left : right) = std::max(q.x < 0.0f ? left : right, out);
        }
        return std::pair{left, right};
    };
    const auto [aL, aR] = run(false);
    const auto [sL, sR] = run(true);
    std::printf("  sphere folds: alone %.4f / %.4f, symmetric %.4f / %.4f\n", aL, aR, sL, sR);
    CHECK(std::min(sL, sR) > 0.75f * aR);
    CHECK(std::max(sL, sR) < 2.5f * aR);
    CHECK(std::abs(sL - sR) < 0.1f * std::max(sL, sR));
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

TEST(cloth_self_collision_keeps_layers_apart) {
    // Two sheets in one cloth: the lower one held, the upper one falls onto it. With self-collision it rests on top
    // (half a cell's width keeps a particle from slipping between the lower one's); without, it falls through.
    auto run = [](float selfDistance, size_t* pairs) {
        ClothSetup a = sheet(20, 1.0f, 0.0f), b = sheet(20, 1.0f, 0.3f);
        const uint32_t n = uint32_t(a.positions.size());
        a.pin.assign(n, 1.0f);
        a.positions.insert(a.positions.end(), b.positions.begin(), b.positions.end());
        for (glm::uvec3 t : b.triangles) a.triangles.push_back(t + glm::uvec3(n));
        a.pin.resize(a.positions.size(), 0.0f);
        ClothSettings set;
        set.selfDistance = selfDistance;
        ClothSim sim;
        CHECK(sim.init(a, fabricPreset(FabricPreset::Cotton), set));
        size_t most = 0;
        for (int f = 0; f < 60; ++f) sim.step(), most = std::max(most, sim.selfPairs());
        if (pairs) *pairs = most;
        float lowest = 1.0f;
        for (size_t i = n; i < sim.positions().size(); ++i) lowest = std::min(lowest, sim.positions()[i].y);
        return lowest;
    };
    size_t pairs = 0;
    const float with = run(0.04f, &pairs), without = run(0.0f, nullptr);
    std::printf("  self-collision: upper sheet's lowest %.4f with (%zu pairs), %.4f without\n", with, pairs, without);
    CHECK(pairs > 0);
    CHECK(with > 0.02f);
    CHECK(without < -0.05f);
}

TEST(cloth_self_collision_same_on_any_thread_count) {
    ClothSetup s = sheet(40, 1.0f, 0.0f);
    s.pin.assign(s.positions.size(), 0.0f);
    for (size_t i = 0; i < 41; ++i) s.pin[i] = 1.0f; // (one edge held: the sheet swings down and folds onto itself)
    ClothSettings set;
    set.selfDistance = 0.012f;
    auto run = [&]() {
        ClothSim sim;
        sim.init(s, fabricPreset(FabricPreset::Silk), set);
        for (int f = 0; f < 40; ++f) sim.step();
        return sim.positions();
    };
    const auto parallel = run();
    ThreadPool::shared().setSerial(true);
    const auto serial = run();
    ThreadPool::shared().setSerial(false);
    CHECK(std::memcmp(parallel.data(), serial.data(), serial.size() * sizeof(glm::vec3)) == 0);
}

TEST(cloth_rest_scale_grows_edges) {
    ClothSetup s = sheet(10, 1.0f, 0.0f);
    ClothSettings set;
    set.gravity = glm::vec3(0.0f);
    ClothSim sim;
    CHECK(sim.init(s, fabricPreset(FabricPreset::Cotton), set));
    for (uint32_t i = 0; i < sim.particles(); ++i) sim.setRestScale(i, 1.3f);
    CHECK(sim.restScale(0) == 1.3f);
    for (int f = 0; f < 60; ++f) sim.step();
    double before = 0.0, after = 0.0;
    for (const ClothSim::Edge& e : sim.stretchEdges())
        before += e.rest, after += glm::distance(sim.positions()[e.a], sim.positions()[e.b]);
    std::printf("  rest scale 1.3: edges %.3f of their start length\n", after / before);
    CHECK(after > 1.15 * before);
}

namespace {

// A stroke of `dabs` dabs on the flat sheet in `mode`, the brush moving `step` along +x from `x0`; returns the doc.
Document clothStroke(ClothBrushMode mode, float x0, int dabs, float step, float softBody = 0.0f, bool invert = false) {
    Document d = makeDoc(makePlane(100, 2.0f));
    d.sculptor.cloth.mode = mode;
    d.sculptor.cloth.softBody = softBody;
    d.sculptor.beginStroke(d.mesh, "Cloth");
    for (int i = 0; i < dabs; ++i) {
        DabParams p;
        p.center = glm::vec3(x0 + step * float(i), 0.0f, 0.0f);
        p.viewDir = glm::vec3(0, -1, 0);
        p.radius = 0.2f;
        p.strength = 0.6f;
        p.invert = invert;
        p.strokeDelta = i == 0 ? glm::vec3(0.0f) : glm::vec3(step, 0.0f, 0.0f);
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Cloth, p, Symmetry{false, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
    return d;
}

} // namespace

TEST(cloth_brush_modes_by_name) {
    ClothBrushMode m;
    for (int i = 0; i < int(ClothBrushMode::Count); ++i) {
        CHECK(clothModeFromName(clothModeName(ClothBrushMode(i)), m) && m == ClothBrushMode(i));
        CHECK(clothModeFromName(clothModeLabel(ClothBrushMode(i)), m) && m == ClothBrushMode(i));
    }
    CHECK(clothModeFromName("Inflate", m) && m == ClothBrushMode::Inflate);
    CHECK(!clothModeFromName("twist", m));
}

TEST(cloth_brush_grab_holds_what_it_took) {
    // Grab at the origin and move 0.2 along +x: the cloth that was under the brush's centre goes most of the way.
    const Mesh plane = makePlane(100, 2.0f);
    uint32_t centre = 0;
    for (uint32_t v = 0; v < plane.vertexCount(); ++v)
        if (glm::length(plane.positions[v]) < glm::length(plane.positions[centre])) centre = v;
    Document d = clothStroke(ClothBrushMode::Grab, 0.0f, 11, 0.02f);
    const glm::vec3 moved = d.mesh.positions[centre] - plane.positions[centre];
    std::printf("  grab: the centre moved (%.4f, %.4f, %.4f) for a travel of 0.2\n", moved.x, moved.y, moved.z);
    CHECK(moved.x > 0.12f);
    CHECK(moved.x < 0.22f);
}

TEST(cloth_brush_expand_pinch_inflate_soft) {
    // Dabs in place at the origin. Pinch gathers the cloth under the brush toward the centre and it folds; Expand
    // over those folds spreads them out again (Alt swaps the two); Inflate lifts it and leaves it longer (more
    // fabric); Soft body springs a drag back toward the start.
    const Mesh plane = makePlane(100, 2.0f);
    auto radial = [&](const Mesh& m) {
        double sum = 0.0;
        int count = 0;
        for (uint32_t v = 0; v < plane.vertexCount(); ++v) {
            const glm::vec3 a = plane.positions[v];
            const float r = glm::length(a);
            if (r < 0.05f || r > 0.15f) continue;
            glm::vec3 d = m.positions[v] - a;
            sum += glm::dot(glm::vec3(d.x, 0.0f, d.z), a / r);
            ++count;
        }
        return sum / std::max(1, count);
    };
    auto lift = [](const Mesh& m) {
        float l = 0.0f;
        for (const glm::vec3& q : m.positions) l = std::max(l, std::abs(q.y));
        return l;
    };
    auto dabs = [](Document& d, ClothBrushMode mode, int count, bool invert) {
        d.sculptor.cloth.mode = mode;
        d.sculptor.beginStroke(d.mesh, "Cloth");
        for (int i = 0; i < count; ++i) {
            DabParams p;
            p.viewDir = glm::vec3(0, -1, 0);
            p.radius = 0.2f;
            p.strength = 0.6f;
            p.invert = invert;
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Cloth, p, Symmetry{false, false, false});
        }
        d.undo.push(d.sculptor.endStroke());
    };
    Document pinch = makeDoc(plane), swapped = makeDoc(plane);
    dabs(pinch, ClothBrushMode::Pinch, 10, false);
    dabs(swapped, ClothBrushMode::Expand, 10, true);
    const float gathered = lift(pinch.mesh), pinchRadial = float(radial(pinch.mesh));
    dabs(pinch, ClothBrushMode::Expand, 10, false);
    std::printf("  pinch: radial %.5f, folds %.4f; expand + Alt: radial %.5f; then Expand: radial %.5f, folds %.4f\n",
                pinchRadial, gathered, radial(swapped.mesh), radial(pinch.mesh), lift(pinch.mesh));
    CHECK(pinchRadial < -0.001f);
    CHECK(gathered > 0.005f);
    CHECK(radial(swapped.mesh) < -0.001);
    CHECK(radial(pinch.mesh) > pinchRadial + 0.001f);
    CHECK(lift(pinch.mesh) < 0.85f * gathered);

    Document inflate = makeDoc(plane);
    dabs(inflate, ClothBrushMode::Inflate, 15, false);
    double before = 0.0, after = 0.0;
    for (const glm::uvec3& t : plane.triangles)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = t[k], b = t[(k + 1) % 3];
            if (glm::length(plane.positions[a]) > 0.1f) continue;
            before += glm::distance(plane.positions[a], plane.positions[b]);
            after += glm::distance(inflate.mesh.positions[a], inflate.mesh.positions[b]);
        }
    std::printf("  inflate: lift %.4f, fabric under the brush %.3f of its length\n", lift(inflate.mesh), after / before);
    CHECK(lift(inflate.mesh) > 0.01f);
    CHECK(after > 1.03 * before);

    auto mean = [&](const Document& d) {
        double m = 0.0;
        for (uint32_t v = 0; v < plane.vertexCount(); ++v) m += glm::distance(d.mesh.positions[v], plane.positions[v]);
        return m / double(plane.vertexCount());
    };
    const double plastic = mean(clothStroke(ClothBrushMode::Drag, -0.4f, 20, 0.02f));
    const double soft = mean(clothStroke(ClothBrushMode::Drag, -0.4f, 20, 0.02f, 1.0f));
    std::printf("  drag: mean move %.6f plastic, %.6f soft body\n", plastic, soft);
    CHECK(soft < 0.7 * plastic);
}

// --- Drape ---

namespace {

// A ball Part (radius 0.3 x scale) and, active, a sheet Part (1 m x scale) a little above it.
Document drapeDoc(float scale = 1.0f) {
    Mesh ball = makeIcosphere(4, 0.3f * scale);
    Document d = makeDoc(std::move(ball));
    Mesh sheet = makePlane(30, 1.0f * scale);
    for (glm::vec3& q : sheet.positions) q.y += 0.33f * scale;
    sheet.recomputeAllNormals();
    d.addPart(std::move(sheet), "Sheet");
    return d;
}

CommandResult runCommand(Document& d, const std::string& name, const Args& args = {}) {
    static CommandRegistry registry;
    return registry.execute(d, name, args);
}

} // namespace

TEST(cloth_drape_command_rests_on_the_other_parts) {
    Document d = drapeDoc();
    const std::vector<glm::vec3> before = d.mesh.positions;
    const CommandResult r = runCommand(d, "cloth_drape", {{"fabric", std::string("cotton")}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok);
    CHECK(r.message.find("at rest") != std::string::npos);
    float closest = 1e9f, lowest = 1e9f;
    for (const glm::vec3& q : d.mesh.positions) closest = std::min(closest, glm::length(q)), lowest = std::min(lowest, q.y);
    CHECK(closest > 0.297f); // on the ball, not through it
    CHECK(lowest < 0.0f);    // the corners hang down past its middle
    // One undo step puts the sheet back.
    d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.positions == before);
    // Unknown fabric, and a fully masked sheet that can't move.
    CHECK(!runCommand(d, "cloth_drape", {{"fabric", std::string("tweed")}}).ok);
    d.mesh.ensureMask();
    std::fill(d.mesh.mask.begin(), d.mesh.mask.end(), 1.0f);
    CHECK(!runCommand(d, "cloth_drape").ok);
    CHECK(d.mesh.positions == before);
}

TEST(cloth_drape_scale_is_real_size) {
    // The same scene modelled twice as big drapes the same with scale=2 (units per metre).
    Document a = drapeDoc(1.0f), b = drapeDoc(2.0f);
    CHECK(runCommand(a, "cloth_drape", {{"frames", 120.0}}).ok);
    CHECK(runCommand(b, "cloth_drape", {{"frames", 120.0}, {"scale", 2.0}}).ok);
    float worst = 0.0f;
    for (size_t v = 0; v < a.mesh.vertexCount(); ++v)
        worst = std::max(worst, glm::length(b.mesh.positions[v] * 0.5f - a.mesh.positions[v]));
    std::printf("  scale: largest difference %.6f m\n", worst);
    CHECK(worst < 1e-3f);
}

TEST(cloth_drape_collides_where_a_moved_part_is_shown) {
    // A Part's transform places it (a moved linked copy): the cloth meets it there, not at its mesh positions.
    Document d = drapeDoc();
    d.parts[0].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
    DrapeScene scene;
    std::string error;
    CHECK(drapeScene(d, DrapeRequest{}, scene, error));
    glm::vec3 point, normal;
    CHECK(scene.colliders.closest(glm::vec3(2.0f, 0.35f, 0.0f), 0.1f, point, normal));
    CHECK(std::abs(point.y - 0.3f) < 0.01f);
    CHECK(!scene.colliders.closest(glm::vec3(0.0f, 0.35f, 0.0f), 0.1f, point, normal));
    // The brush's colliders take the same placement.
    ClothColliders brush;
    brush.add(d.parts[0].data->mesh, d.parts[0].transform);
    brush.build();
    CHECK(brush.closest(glm::vec3(2.0f, 0.35f, 0.0f), 0.1f, point, normal));
}

TEST(cloth_ab_strokes_on_icospheres) {
    // Stroke timings per mode: icospheres of subdivision 6, 8 and 9
    // (41K, 655K, 2.6M vertices, radius 1), seen from the front, one 24-dab stroke along +x (x 0.2 -> 0.545), brush
    // radius 0.15, per mode: the stroke's time, ms per dab and the undo's time. SF_CLOTH_AB=1 (SF_CLOTH_AB_SUBS=6,8).
    const char* on = std::getenv("SF_CLOTH_AB");
    if (!on) return;
    const char* subsEnv = std::getenv("SF_CLOTH_AB_SUBS");
    std::vector<int> subs;
    for (const char* c = subsEnv ? subsEnv : "6,8,9"; *c; ++c)
        if (*c >= '0' && *c <= '9') subs.push_back(*c - '0');
    const ClothBrushMode modes[] = {ClothBrushMode::Drag, ClothBrushMode::Push, ClothBrushMode::Grab,
                                    ClothBrushMode::Expand, ClothBrushMode::Pinch, ClothBrushMode::Inflate};
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::time_point a, clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    for (int sub : subs) {
        Document d = makeDoc(makeIcosphere(sub, 1.0f));
        for (int variant = 0; variant < 7; ++variant) {
            // (the six modes, then Drag without self-collision)
            const ClothBrushMode mode = variant < 6 ? modes[variant] : ClothBrushMode::Drag;
            d.sculptor.cloth.mode = mode;
            d.sculptor.cloth.selfCollide = variant < 6;
            d.sculptor.timings = {};
            const auto t0 = clock::now();
            d.sculptor.beginStroke(d.mesh, "Cloth");
            double worst = 0.0;
            for (int i = 0; i < 24; ++i) {
                const float x = 0.2f + 0.015f * float(i);
                DabParams p;
                p.center = glm::vec3(x, 0.0f, std::sqrt(1.0f - x * x));
                p.viewDir = glm::vec3(0, 0, -1);
                p.radius = 0.15f;
                p.strength = 0.5f;
                p.strokeDelta = i == 0 ? glm::vec3(0.0f) : glm::vec3(0.015f, 0.0f, 0.0f);
                const auto a = clock::now();
                d.sculptor.dab(d.mesh, d.bvh, BrushType::Cloth, p, Symmetry{false, false, false});
                worst = std::max(worst, ms(a, clock::now()));
            }
            d.undo.push(d.sculptor.endStroke());
            const double stroke = ms(t0, clock::now());
            float out = 0.0f;
            for (const glm::vec3& q : d.mesh.positions) out = std::max(out, std::abs(glm::length(q) - 1.0f));
            const auto u = clock::now();
            d.undo.undo(d.mesh, d.bvh);
            const double undo = ms(u, clock::now());
            const SculptTimings& t = d.sculptor.timings;
            std::printf("AB sub %d verts %u mode %s stroke_ms %.1f ms_per_dab %.2f slowest_dab_ms %.2f region %zu "
                        "moved_out %.4f undo_ms %.1f | per dab: build %.2f solve %.2f other kernel %.2f writeback %.2f\n",
                        sub, d.mesh.vertexCount(), variant < 6 ? clothModeName(mode) : "drag-noself", stroke, stroke / 24.0, worst,
                        d.sculptor.clothRegion(), out, undo, t.clothBuildMs / 24.0, t.clothSolveMs / 24.0,
                        (t.kernelMs - t.clothSolveMs) / 24.0, t.normalsMs / 24.0);
        }
    }
}

// --- Bench (SF_CLOTH_BENCH=1; limits with SF_BENCH_LIMITS=1): the bars in docs/design/cloth.md on this machine ---

TEST(cloth_bench_drape_and_brush_on_a_dense_body) {
    if (!std::getenv("SF_CLOTH_BENCH")) return;
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::time_point a, clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    // A ~2M-triangle body (a quad sphere, 0.6 m across) and a 20K-vertex sheet (1 m) just above it.
    const Mesh body = makeQuadSphere(408, 0.6f);
    ClothSetup s = sheet(140, 1.0f, 0.32f);
    std::printf("  bench: body %zu triangles, sheet %zu vertices\n", body.triangleCount(), s.positions.size());
    auto t0 = clock::now();
    ClothColliders colliders;
    colliders.add(body);
    colliders.build();
    auto t1 = clock::now();
    std::vector<double> frames;
    auto last = clock::now();
    DrapeOptions opt;
    opt.maxFrames = 900;
    opt.progress = [&](int, float) {
        const auto now = clock::now();
        frames.push_back(ms(last, now));
        last = now;
        return true;
    };
    const DrapeResult r = drapeToRest(s, colliders, fabricPreset(FabricPreset::Cotton), opt);
    auto t2 = clock::now();
    std::sort(frames.begin(), frames.end());
    const double total = ms(t0, t2) / 1000.0;
    std::printf("  bench drape: colliders %.0f ms, %d frames (rested %d), total %.2f s, frame p50 %.1f ms, slowest %.1f ms\n",
                ms(t0, t1), r.frames, int(r.rested), total, frames[frames.size() / 2], frames.back());
    // The Cloth brush on the dense body: a 40-dab drag, 5 cm radius.
    Document d = makeDoc(Mesh(body));
    std::vector<double> dabs;
    d.sculptor.beginStroke(d.mesh, "Cloth");
    for (int i = 0; i < 40; ++i) {
        DabParams p;
        p.center = glm::vec3(-0.1f + 0.005f * float(i), 0.0f, 0.3f);
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = 0.05f;
        p.strength = 0.6f;
        p.strokeDelta = i == 0 ? glm::vec3(0.0f) : glm::vec3(0.005f, 0.0f, 0.0f);
        const auto a = clock::now();
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Cloth, p, Symmetry{false, false, false});
        dabs.push_back(ms(a, clock::now()));
    }
    d.undo.push(d.sculptor.endStroke());
    std::sort(dabs.begin(), dabs.end());
    std::printf("  bench brush: region %zu vertices, dab p50 %.1f ms, slowest %.1f ms\n", d.sculptor.clothRegion(),
                dabs[dabs.size() / 2], dabs.back());
    if (std::getenv("SF_BENCH_LIMITS")) CHECK(r.rested && total < 10.0);
}
