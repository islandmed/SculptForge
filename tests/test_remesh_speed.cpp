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

TEST(nightly_qremesh_small_mesh_at_the_defaults_is_fast) {
    // A 1k-triangle sculpt at the defaults (5000 quads): 0.76 s on the floor-tier laptop (1.24 s before 0.11.13).
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
