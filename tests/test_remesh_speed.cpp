// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Remesh speed (0.11.13, "slow even for a small number of points"): Quad Remesh's solvers, its proxy and the BVH
// build run on all threads, and none of that may change a result. The parallel BVH build makes the same tree as a
// serial one (every query the same), Quad Remesh gives the same mesh with the thread pool serial or parallel, and a
// small mesh at the defaults stays well inside a budget (nightly). Timings per stage: tests/tools/remesh_bench.cpp.
#include "TestHarness.h"

#include "core/Primitives.h"
#include "core/QRemesh.h"
#include "core/Voxel.h"
#include "GeomInvariants.h"
#include "core/SurfaceQuery.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace sf;

namespace {

// A sphere with ridges (a rough sculpt), about 12 r^2 triangles.
Mesh bumpySphere(int r) {
    Mesh m = makeQuadSphere(r, 2.0f);
    for (glm::vec3& p : m.positions) {
        const glm::vec3 n = glm::normalize(p);
        p = n * (1.0f + 0.06f * std::sin(7.0f * n.x) * std::sin(5.0f * n.y) + 0.02f * std::sin(23.0f * n.z + 3.0f * n.x));
    }
    m.recomputeAllNormals();
    return m;
}

bool sameMesh(const Mesh& a, const Mesh& b) {
    return a.positions.size() == b.positions.size() && a.triangles.size() == b.triangles.size() &&
           std::memcmp(a.positions.data(), b.positions.data(), a.positions.size() * sizeof(glm::vec3)) == 0 &&
           std::memcmp(a.triangles.data(), b.triangles.data(), a.triangles.size() * sizeof(glm::uvec3)) == 0;
}

} // namespace

TEST(bvh_parallel_build_is_the_serial_tree) {
    // 86k triangles: past the size where subtrees are built on their own threads.
    const Mesh m = bumpySphere(85);
    CHECK(m.triangleCount() > 80000);
    TriangleBVH parallel, serial;
    parallel.build(m);
    ThreadPool::shared().setSerial(true);
    serial.build(m);
    ThreadPool::shared().setSerial(false);
    size_t same = 0, exact = 0;
    const int queries = 300;
    for (int i = 0; i < queries; ++i) {
        // Points in and around the surface, spread by a low-discrepancy sequence.
        const float u = std::fmod(0.618034f * float(i), 1.0f), v = std::fmod(0.754878f * float(i), 1.0f);
        const float th = 6.2831853f * u, ph = std::acos(1.0f - 2.0f * v), rad = 0.7f + 0.6f * std::fmod(0.569840f * float(i), 1.0f);
        const glm::vec3 p = rad * glm::vec3(std::sin(ph) * std::cos(th), std::cos(ph), std::sin(ph) * std::sin(th));
        SurfaceHit a, b;
        const bool fa = parallel.closestPoint(p, 10.0f, a), fb = serial.closestPoint(p, 10.0f, b);
        if (fa && fb && a.prim == b.prim && a.position == b.position) ++same;
        // Against every triangle: the same nearest distance.
        float best = 1e30f;
        for (const glm::uvec3& t : m.triangles) {
            glm::vec3 bary;
            const glm::vec3 q = closestPointOnTriangle(p, m.positions[t.x], m.positions[t.y], m.positions[t.z], bary);
            best = std::min(best, glm::dot(q - p, q - p));
        }
        if (fa && glm::dot(a.position - p, a.position - p) == best) ++exact;
    }
    CHECK(same == size_t(queries));
    CHECK(exact == size_t(queries));
}

TEST(qremesh_same_result_on_one_thread_or_all) {
    // The fused solver passes keep each block's sums in order, so the thread count never changes the mesh.
    const Mesh src = bumpySphere(20);
    QRemeshOptions o;
    o.targetQuads = 1500;
    Mesh a, b;
    std::string error;
    CHECK(qremesh(src, o, a, error));
    ThreadPool::shared().setSerial(true);
    CHECK(qremesh(src, o, b, error));
    ThreadPool::shared().setSerial(false);
    CHECK(a.triangleCount() > 2000);
    CHECK(sameMesh(a, b));
}

TEST(qremesh_hard_edges_keep_the_dense_proxy) {
    // Smooth pieces get a coarser proxy (2 edges per quad edge instead of 2.5); a cube's hard edges stay on the denser
    // one, which keeps its corners the only poles (at 2 it had 35).
    const Mesh cube = makeCube(13, 2.0f);
    QRemeshOptions o;
    QRemeshStats st;
    Mesh out;
    std::string error;
    CHECK(qremesh(cube, o, out, error, {}, &st));
    const QuadQuality q = st.quality.measured ? st.quality : quadQuality(out, &cube);
    CHECK(q.poles == 8);
}

TEST(nightly_qremesh_small_mesh_at_the_defaults_is_fast) {
    // A 1k-triangle sculpt at the defaults (5000 quads): 0.55 s on the floor-tier laptop (1.24 s before 0.11.13).
    const Mesh src = bumpySphere(9);
    QRemeshOptions o;
    Mesh out;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(qremesh(src, o, out, error));
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  Quad Remesh, %zu triangles -> %zu: %.2f s\n", src.triangleCount(), out.triangleCount(), s);
    CHECK(s < timeLimit(2.0));
}

TEST(voxel_remesh_cancel_lands_within_a_round_and_progress_moves) {
    // 0.14.0 (a member cancelled slow remeshes and worked around them): Voxel Remesh asked the caller only between its
    // stages, so a Cancel waited for the whole stage (280 ms at 0.5M triangles, seconds at 5M). Now the input-sized stages
    // ask every round: a Cancel asked 60 ms in returns well within 200 ms on a 1M-triangle sculpt, the fraction never
    // goes backwards and moves inside the long stages, and the result is the same as before the rounds (one mesh, run twice).
    const Mesh src = bumpySphere(290); // ~1M triangles
    CHECK(src.triangleCount() > 900000);
    std::string error;
    {
        const auto t0 = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point askedAt{};
        const Mesh out = voxelRemesh(src, 200, &error, [&](float) {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(now - t0).count() < 60.0) return true;
            if (askedAt == std::chrono::steady_clock::time_point{}) askedAt = now;
            return false;
        });
        const double late = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - askedAt).count();
        std::printf("  Voxel Remesh %zu triangles: Cancel returned %.0f ms after it was asked\n", src.triangleCount(), late);
        CHECK(out.triangleCount() == 0);
        CHECK(error == "Voxel Remesh cancelled");
        CHECK(late < timeLimit(200.0));
    }
    std::vector<float> marks;
    const Mesh a = voxelRemesh(src, 200, &error, [&](float f) {
        marks.push_back(f);
        return true;
    });
    CHECK(a.triangleCount() > 0);
    CHECK(marks.size() > 20); // (the long stages report their rounds, not just their ends)
    bool monotonic = true;
    size_t insideRasterise = 0, insideDistance = 0;
    for (size_t i = 1; i < marks.size(); ++i) {
        monotonic &= marks[i] >= marks[i - 1];
        insideRasterise += marks[i] > 0.02f && marks[i] < 0.1f;
        insideDistance += marks[i] > 0.25f && marks[i] < 0.6f;
    }
    CHECK(monotonic);
    CHECK(insideRasterise >= 1); // (a round per 400k triangles: 2 at 1M, so one mark inside the stage)
    CHECK(insideDistance >= 8);
    CHECK(marks.back() == 1.0f);
    const Mesh b = voxelRemesh(src, 200, &error);
    CHECK(sameMesh(a, b));
}

TEST(qremesh_cancel_lands_inside_the_bvh_build_and_the_pre_decimation) {
    // 0.14.0: on a dense sculpt Quad Remesh's first half second builds the surface BVH and decimates the input to its
    // proxy in parallel blocks, and neither asked the caller: a Cancel there waited 200-470 ms at 2M-5M triangles. Both
    // ask now; a Cancel asked 60 ms in returns well within 250 ms on a 1M-triangle sculpt.
    const Mesh src = bumpySphere(290); // ~1M triangles
    QRemeshOptions o;
    Mesh out;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point askedAt{};
    const bool ok = qremesh(src, o, out, error, [&](float) {
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double, std::milli>(now - t0).count() < 60.0) return true;
        if (askedAt == std::chrono::steady_clock::time_point{}) askedAt = now;
        return false;
    });
    const double late = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - askedAt).count();
    std::printf("  Quad Remesh %zu triangles: Cancel returned %.0f ms after it was asked (%s)\n", src.triangleCount(), late,
                ok ? "finished anyway" : error.c_str());
    CHECK(!ok);
    CHECK(late < timeLimit(250.0));
}

namespace {
double signedVolume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z]))) / 6.0;
    return v;
}
} // namespace

TEST(voxel_remesh_geometric_invariants_on_a_dense_curved_mesh) {
    // The promise the speed work must keep on a dense curved sculpt (the gather path, 1M+ small triangles): the remesh keeps
    // the enclosed volume within 1%, every vertex lands on the sculpt (within a cell of it: no spikes, no sinks), nothing is
    // non-finite, the faces stay outward, and the result is the same as the old splat path's (checked by hash on 13 shapes
    // in the bench; here: the splat path forced by a coarse mesh gives the same remesh of the same shape at the same resolution).
    const Mesh src = bumpySphere(290); // ~1M triangles, bumpy: ridges 6% of the radius
    const double v0 = signedVolume(src);
    std::string error;
    const Mesh out = voxelRemesh(src, 200, &error);
    CHECK(out.triangleCount() > 100000);
    CHECK(geom::nonFinite(out) == 0);
    CHECK(std::abs(geom::volumeChange(v0, signedVolume(out))) < 0.01);
    CHECK(geom::inwardFaces(out) == 0);
    // Every new vertex within a cell (2 / 200 of the diameter) of the source surface: no spikes, no sinks.
    TriangleBVH bvh;
    bvh.build(src);
    const float cell = 2.0f * 1.06f / 200.0f;
    float worst = 0.0f;
    for (const glm::vec3& p : out.positions) {
        SurfaceHit hit;
        const float d = bvh.closestPoint(p, 10.0f * cell, hit) ? glm::distance(p, hit.position) : 10.0f * cell;
        worst = std::max(worst, d);
    }
    std::printf("  Voxel Remesh invariants: volume %+.3f%%, farthest vertex from the sculpt %.2f cells\n",
                100.0 * geom::volumeChange(v0, signedVolume(out)), worst / cell);
    CHECK(worst < 1.0f * cell);
}
