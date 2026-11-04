// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scale-adaptive dabs (Sculptor::adaptiveDabs): a big brush's stroke comes out as the exact path's, within a
// small tolerance, for every brush that takes the adaptive path; small brushes are unchanged, bit for bit.
// SF_BOOST_TRIS=15000000 times one big Clay stroke on the standard head (the 240 px case) both ways.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BrushAlpha.h"
#include "core/Sculpt.h"
#include "core/SculptKernel.h"
#include "core/ThreadPool.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <tuple>

using namespace sf;

namespace {

std::vector<glm::vec3>* g_record = nullptr; // (diagnosis: the exact run's centres, replayed)
const std::vector<glm::vec3>* g_replay = nullptr;

struct StrokeResult {
    Mesh mesh;
    SculptTimings timings;
    double ms = 0.0, flushMs = 0.0;
};

// A stroke of `dabs` dabs across the face of `head` (0.15 radius apart, as the app spaces them), X symmetry.
StrokeResult strokeOn(const Mesh& head, BrushType type, float radius, bool adaptive, int dabs = 16, bool alpha = false,
                      bool mask = false, float strength = 0.3f, int batch = 0, bool catchUp = false) {
    Document d = makeDoc(head);
    if (mask) { // a hard-edged mask over part of the stroke (such regions take the exact path)
        d.mesh.ensureMask();
        for (size_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].y > 0.1f ? 1.0f : 0.0f;
    }
    BrushAlpha stamp;
    if (alpha) {
        stamp.width = stamp.height = 64;
        stamp.values.resize(64 * 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) stamp.values[size_t(y) * 64 + x] = 0.5f + 0.5f * std::sin(0.3f * float(x)) * std::cos(0.2f * float(y));
    }
    d.sculptor.adaptiveDabs = adaptive;
    d.sculptor.beginStroke(d.mesh, brushName(type));
    StrokeResult out;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < dabs; ++i) {
        if (batch > 0 && i % batch == 0) { // (the solver's batches: picked on the surface as each began)
            d.sculptor.endBatch(d.mesh, d.bvh);
            if (catchUp) d.sculptor.catchUpBehind(d.mesh, d.bvh, 0.35); // (as the solver does after each batch)
            d.sculptor.beginBatch();
        }
        DabParams p;
        const float t = -0.5f + float(i) * 0.15f * radius;
        p.center = glm::vec3(t, 0.1f, 1.0f);
        if (RayHit hit; d.bvh.raycast(d.mesh, glm::vec3(t, 0.1f, 5.0f), glm::vec3(0, 0, -1), hit)) {
            p.center = hit.position;
            p.pickNormal = glm::normalize(hit.normal);
        }
        if (g_record) g_record->push_back(p.center);
        if (g_replay) p.center = (*g_replay)[size_t(i)];
        p.viewDir = p.pickDir = glm::vec3(0, 0, -1);
        p.radius = radius;
        p.strength = strength;
        p.strokeDelta = glm::vec3(0.15f * radius, 0, 0);
        if (alpha) p.alpha = &stamp;
        d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{true, false, false});
    }
    d.sculptor.endBatch(d.mesh, d.bvh);
    const auto t1 = std::chrono::steady_clock::now();
    d.sculptor.flushDraft(d.mesh, d.bvh);
    const auto t2 = std::chrono::steady_clock::now();
    out.timings = d.sculptor.timings;
    out.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    out.flushMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    d.sculptor.endStroke();
    out.mesh = std::move(d.mesh);
    return out;
}

struct Difference {
    float moved = 0.0f, position = 0.0f, normalDeg = 0.0f; // the most; normalDeg: of 999 vertices in 1000
    double meanDeg = 0.0;
};
Difference compare(const Mesh& start, const Mesh& a, const Mesh& b) {
    Difference r;
    std::vector<float> deg;
    for (size_t v = 0; v < a.positions.size(); ++v) {
        const float moved = glm::length(a.positions[v] - start.positions[v]);
        r.moved = std::max(r.moved, moved);
        r.position = std::max(r.position, glm::length(a.positions[v] - b.positions[v]));
        if (moved == 0.0f) continue;
        deg.push_back(glm::degrees(std::acos(std::clamp(glm::dot(a.normals[v], b.normals[v]), -1.0f, 1.0f))));
        r.meanDeg += deg.back();
    }
    if (deg.empty()) return r;
    r.meanDeg /= double(deg.size());
    std::nth_element(deg.begin(), deg.begin() + std::ptrdiff_t(deg.size() * 999 / 1000), deg.end());
    r.normalDeg = deg[deg.size() * 999 / 1000];
    return r;
}

} // namespace

TEST(adaptive_dabs_match_exact_strokes) {
    const Mesh head = makeBenchHead(3'000'000); // ~1.5M vertices: a 0.6 radius holds well over the threshold
    struct Case {
        BrushType type;
        bool alpha = false, mask = false;
        float strength = 0.3f;
    };
    // (Inflate, Nudge, Clay Block, alpha brushes and masked regions take the exact path: adaptive_dabs_leave_small_brushes_alone checks it.)
    for (const Case c : {Case{BrushType::Clay}, Case{BrushType::Standard}, Case{BrushType::Flatten},
                         Case{BrushType::Pinch}, Case{BrushType::Crease},                          Case{BrushType::Trim}}) {
        const StrokeResult exact = strokeOn(head, c.type, 0.8f, false, 16, c.alpha, c.mask, c.strength);
        const StrokeResult fast = strokeOn(head, c.type, 0.8f, true, 16, c.alpha, c.mask, c.strength);
        const Difference d = compare(head, exact.mesh, fast.mesh);
        const double perDab = double(fast.timings.vertices) / double(std::max<uint64_t>(1, fast.timings.dabs));
        std::printf("  %-12s%s%s %.0fk verts/dab, moved %.4f, adaptive within %.2g (%.2f%%), normals: mean %.3f, 99.9%% within %.2f deg; %.0f vs %.0f ms\n",
                    brushName(c.type), c.alpha ? " alpha" : "", c.mask ? " mask" : "", perDab / 1e3, double(d.moved), double(d.position),
                    100.0 * double(d.position / std::max(d.moved, 1e-9f)), d.meanDeg, double(d.normalDeg), exact.ms, fast.ms);
        CHECK(d.moved > 1e-3f);                 // (the stroke did something)
        CHECK(perDab > double(Sculptor::kAdaptiveMinVertices)); // (on the adaptive path)
        CHECK(d.position <= 0.01f * d.moved);   // within 1% of how far the stroke moved the surface
        // After the stroke the exact normals of nearly the same surface (where a plane brush's crease line moved
        // by a hair, a few vertices' normals turn by some degrees).
        CHECK(d.meanDeg < 0.5);
    }
}

TEST(adaptive_dabs_fused_batches_match_exact_strokes) {
    // Batches of 2-6 dabs placed in one pass (Sculptor::beginBatch), each picked on the surface as its batch began.
    const Mesh head = makeBenchHead(3'000'000);
    for (BrushType type : {BrushType::Clay, BrushType::Standard, BrushType::Flatten, BrushType::Pinch, BrushType::Crease,
                           BrushType::SharpCrease})
        for (int batch : {2, 6}) {
            std::vector<glm::vec3> centres;
            static const bool replay = std::getenv("SF_BOOST_REPLAY") != nullptr;
            if (replay) g_record = &centres;
            const StrokeResult exact = strokeOn(head, type, 0.8f, false, 18);
            g_record = nullptr;
            if (replay) g_replay = &centres;
            const StrokeResult fused = strokeOn(head, type, 0.8f, true, 18, false, false, 0.3f, batch);
            g_replay = nullptr;
            const Difference d = compare(head, exact.mesh, fused.mesh);
            std::printf("  %-12s batches of %d: moved %.4f, within %.2g (%.2f%%), normals: mean %.3f, 99.9%% within %.2f deg; %.0f vs %.0f ms\n",
                        brushName(type), batch, double(d.moved), double(d.position),
                        100.0 * double(d.position / std::max(d.moved, 1e-9f)), d.meanDeg, double(d.normalDeg), exact.ms, fused.ms);
            CHECK(d.moved > 1e-3f);
            CHECK(d.position <= 0.025f * d.moved); // within 2.5% (the centres are picked as the batch began)
            CHECK(d.meanDeg < 0.5);
        }
}

TEST(adaptive_dabs_analytic_normals_match_finite_differences) {
    // PlaneKernel::offsetAndNormal against the moved surface's normal from offsets a hair apart, on a bumpy patch.
    for (BrushType type : {BrushType::Standard, BrushType::Clay, BrushType::Flatten, BrushType::Crease, BrushType::Pinch,
                           BrushType::SharpCrease, BrushType::Trim})
        for (float focal : {0.0f, -0.6f, 0.5f})
            for (bool invert : {false, true}) {
                PlaneKernel k;
                k.type = type;
                k.center = glm::vec3(0.05f, -0.02f, 0.0f);
                k.areaNormal = glm::normalize(glm::vec3(0.1f, 0.05f, 1.0f));
                k.areaCenter = glm::vec3(0.0f, 0.0f, -0.01f);
                k.strokeDir = glm::vec3(1, 0, 0);
                k.r = 0.5f;
                k.s = 0.6f;
                k.sign = invert ? -1.0f : 1.0f;
                k.invert = invert;
                k.focal = focal;
                const float kexp = std::exp2(2.0f * focal);
                float worst = 0.0f;
                for (int i = 0; i < 400; ++i) {
                    const float u = -0.45f + 0.9f * float(i % 20) / 19.0f, w = -0.45f + 0.9f * float(i / 20) / 19.0f;
                    const glm::vec3 x(u, w, 0.03f * std::sin(9.0f * u) * std::cos(7.0f * w));
                    const glm::vec3 n = glm::normalize(glm::vec3(-0.27f * std::cos(9.0f * u) * std::cos(7.0f * w),
                                                                 0.21f * std::sin(9.0f * u) * std::sin(7.0f * w), 1.0f));
                    glm::vec3 d;
                    const glm::vec3 a = k.offsetAndNormal(x, n, kexp, d);
                    CHECK(glm::length(d - k.offset(x)) < 1e-6f);
                    const glm::vec3 t1 = glm::normalize(glm::cross(n, glm::vec3(1, 0, 0))), t2 = glm::cross(n, t1);
                    const float h = 1e-3f;
                    const glm::vec3 p1 = t1 + (k.offset(x + t1 * h) - k.offset(x - t1 * h)) / (2.0f * h);
                    const glm::vec3 p2 = t2 + (k.offset(x + t2 * h) - k.offset(x - t2 * h)) / (2.0f * h);
                    const glm::vec3 fd = glm::normalize(glm::cross(p1, p2));
                    if (glm::dot(fd, n) <= 0.0f) continue; // (folded: both keep n)
                    // (a negative focal's falloff is infinitely steep at the rim: differences a hair apart can't follow)
                    if (k.r - glm::length(x - k.center) < 0.05f) continue;
                    // (where Clay / Trim cut in, the slope jumps: points a hair from the cut are skipped)
                    const float cut = glm::length(k.offset(x + t1 * 2e-3f) - k.offset(x - t1 * 2e-3f)) + glm::length(k.offset(x + t2 * 2e-3f) - k.offset(x - t2 * 2e-3f));
                    if ((type == BrushType::Clay || type == BrushType::Trim) && cut > 0.0f && glm::length(d) < 1e-4f) continue;
                    worst = std::max(worst, glm::degrees(std::acos(std::clamp(glm::dot(a, fd), -1.0f, 1.0f))));
                }
                if (worst >= 1.0f)
                    std::printf("  %s focal %.1f%s: analytic normals within %.3f deg\n", brushName(type), double(focal),
                                invert ? " inverted" : "", double(worst));
                CHECK(worst < 1.0f); // (Trim: its min(1, ...) bends the slope)
            }
}

TEST(adaptive_dabs_catch_up_behind_ends_exact) {
    // Catching up the exact normals behind the brush after each batch ends where flushing them all at the end does.
    const Mesh head = makeBenchHead(3'000'000);
    const StrokeResult once = strokeOn(head, BrushType::Clay, 0.8f, true, 18, false, false, 0.3f, 3, false);
    const StrokeResult spread = strokeOn(head, BrushType::Clay, 0.8f, true, 18, false, false, 0.3f, 3, true);
    // (The caught-up normals feed the later dabs' planes, which then move the surface by a hair differently.)
    const Difference d = compare(head, once.mesh, spread.mesh);
    std::printf("  spread catch-up vs one flush: within %.2g (%.3f%% of the move), normals mean %.4f deg; pen-up flush %.0f vs %.0f ms\n",
                double(d.position), 100.0 * double(d.position / std::max(d.moved, 1e-9f)), d.meanDeg, spread.flushMs, once.flushMs);
    CHECK(d.position <= 0.005f * d.moved);
    CHECK(d.meanDeg < 0.05);
}

TEST(adaptive_dabs_leave_small_brushes_alone) {
    // Small brushes, and the brushes that always take the exact path, come out bit for bit as before.
    const Mesh head = makeBenchHead(1'500'000);
    for (const auto& [type, radius, alpha] : {std::tuple{BrushType::Clay, 0.08f, false}, std::tuple{BrushType::Smooth, 0.8f, false},
                                              std::tuple{BrushType::Nudge, 0.8f, false}, std::tuple{BrushType::Inflate, 0.8f, false}, std::tuple{BrushType::ClayBlock, 0.8f, false}, std::tuple{BrushType::Standard, 0.8f, true}}) {
        const StrokeResult exact = strokeOn(head, type, radius, false, 8, alpha), fast = strokeOn(head, type, radius, true, 8, alpha);
        CHECK(exact.mesh.positions == fast.mesh.positions);
        CHECK(exact.mesh.normals == fast.mesh.normals);
    }
    const StrokeResult exact = strokeOn(head, BrushType::Clay, 0.8f, false, 8, false, true);
    const StrokeResult fast = strokeOn(head, BrushType::Clay, 0.8f, true, 8, false, true);
    CHECK(exact.mesh.positions == fast.mesh.positions); // (a masked region)
}

TEST(adaptive_dabs_normals_during_the_stroke) {
    // Mid-stroke (before the exact normals catch up) the adaptive ones are already close to the exact ones.
    const Mesh head = makeBenchHead(1'500'000);
    auto run = [&](bool adaptive) {
        Document d = makeDoc(head);
        d.sculptor.adaptiveDabs = adaptive;
        d.sculptor.beginStroke(d.mesh, "Clay");
        for (int i = 0; i < 8; ++i) {
            DabParams p;
            p.center = glm::vec3(-0.3f + 0.09f * float(i), 0.1f, 0.0f);
            if (RayHit hit; d.bvh.raycast(d.mesh, glm::vec3(p.center.x, 0.1f, 5.0f), glm::vec3(0, 0, -1), hit))
                p.center = hit.position;
            p.radius = 0.6f;
            p.strength = 1.0f;
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{});
        }
        Mesh m = d.mesh; // (before endStroke's exact normals)
        d.sculptor.endStroke();
        return m;
    };
    const Mesh exact = run(false), fast = run(true);
    double sum = 0.0;
    float worst = 0.0f;
    for (size_t v = 0; v < exact.normals.size(); ++v) {
        const float a = glm::degrees(std::acos(std::clamp(glm::dot(exact.normals[v], fast.normals[v]), -1.0f, 1.0f)));
        sum += a;
        worst = std::max(worst, a);
    }
    const double mean = sum / double(exact.normals.size());
    std::printf("  mid-stroke normals: mean %.4f deg, worst %.2f deg from the exact ones\n", mean, double(worst));
    CHECK(mean < 0.05);
    CHECK(worst < 5.0f);
}

TEST(adaptive_dabs_timing) {
    const char* env = std::getenv("SF_BOOST_TRIS");
    if (!env) return; // (a measurement, not a check: SF_BOOST_TRIS=15000000)
    if (const char* t = std::getenv("SF_BOOST_THREADS")) ThreadPool::setSharedThreadCount(std::atoi(t)); // (before first use)
    const Mesh head = makeBenchHead(size_t(std::strtod(env, nullptr)));
    const float radius = std::getenv("SF_BOOST_RADIUS") ? float(std::strtod(std::getenv("SF_BOOST_RADIUS"), nullptr)) : 0.7f;
    const int batch = std::getenv("SF_BOOST_BATCH") ? std::atoi(std::getenv("SF_BOOST_BATCH")) : 4;
    for (bool adaptive : {false, true}) {
        const StrokeResult r = strokeOn(head, BrushType::Clay, radius, adaptive, 16, false, false, 0.3f, adaptive ? batch : 0);
        const SculptTimings& t = r.timings;
        std::printf("  %s: %zu vertices, %.1f k verts/dab, %.1f ms/dab (kernel %.1f, normals %.1f, BVH %.1f), end %.0f ms\n",
                    adaptive ? "adaptive" : "exact   ", head.vertexCount(), double(t.vertices) / double(std::max<uint64_t>(1, t.dabs)) / 1e3,
                    r.ms / 16.0, t.kernelMs / 16.0, t.normalsMs / 16.0, t.bvhMs / 16.0, r.flushMs);
    }
}
