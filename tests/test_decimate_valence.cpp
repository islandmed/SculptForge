// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// decimate()'s valence cap (core/Decimate.h, kDecimateMaxValence): a flat area no longer fans out into one hub
// vertex (slivers, and minutes and gigabytes on a 1M-triangle Part: docs/design/pose-all.md), and the A/B
// nightly_decimate_valence_cap measures time and quality with and without it.
#include "TestHarness.h"

#include "core/Decimate.h"
#include "core/Primitives.h"
#include "core/SurfaceQuery.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace sf;

namespace {

size_t maxValence(const Mesh& m) {
    std::vector<std::vector<uint32_t>> nbr(m.vertexCount());
    for (const glm::uvec3& t : m.triangles)
        for (int c = 0; c < 3; ++c) {
            nbr[t[c]].push_back(t[(c + 1) % 3]);
            nbr[t[c]].push_back(t[(c + 2) % 3]);
        }
    size_t most = 0;
    for (auto& n : nbr) {
        std::sort(n.begin(), n.end());
        most = std::max(most, size_t(std::unique(n.begin(), n.end()) - n.begin()));
    }
    return most;
}

// Share of triangles whose smallest angle is under 10 degrees.
double sliverShare(const Mesh& m) {
    size_t slivers = 0;
    for (const glm::uvec3& t : m.triangles) {
        const glm::vec3 p[3] = {m.positions[t.x], m.positions[t.y], m.positions[t.z]};
        float smallest = 180.0f;
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 a = p[(c + 1) % 3] - p[c], b = p[(c + 2) % 3] - p[c];
            const float la = glm::length(a), lb = glm::length(b);
            if (la <= 0.0f || lb <= 0.0f) {
                smallest = 0.0f;
                break;
            }
            smallest = std::min(smallest, glm::degrees(std::acos(std::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f))));
        }
        if (smallest < 10.0f) ++slivers;
    }
    return m.triangles.empty() ? 0.0 : double(slivers) / double(m.triangles.size());
}

// Largest distance from a source vertex to the decimated surface (one side of the Hausdorff distance).
float deviation(const Mesh& src, const Mesh& out) {
    TriangleBVH bvh;
    bvh.build(out);
    float worst = 0.0f;
    for (const glm::vec3& p : src.positions) {
        SurfaceHit hit;
        if (bvh.closestPoint(p, 1e30f, hit)) worst = std::max(worst, hit.distance);
    }
    return worst;
}

// A sculpt-like surface: an icosphere with bumps of a few sizes.
Mesh bumpySphere(int subdivisions) {
    Mesh m = makeIcosphere(subdivisions, 1.0f);
    for (glm::vec3& p : m.positions) {
        const float h = 0.06f * std::sin(7.0f * p.x) * std::sin(5.0f * p.y) + 0.02f * std::sin(23.0f * p.z + 3.0f * p.x);
        p *= 1.0f + h;
    }
    m.recomputeAllNormals();
    return m;
}

} // namespace

TEST(decimate_valence_cap_keeps_flat_areas_even) {
    const Mesh cube = makeCube(40, 2.0f); // flat faces: every collapse there costs nothing
    const Mesh capped = decimate(cube, cube.triangleCount() / 20);
    CHECK(capped.triangleCount() <= cube.triangleCount() / 20 + 2);
    CHECK(maxValence(capped) <= kDecimateMaxValence + 2);
    const Mesh uncapped = decimate(cube, cube.triangleCount() / 20, false, 0);
    CHECK(maxValence(uncapped) > maxValence(capped)); // (what the cap prevents)
    CHECK(sliverShare(capped) <= sliverShare(uncapped));
    std::printf("  valence %zu vs %zu uncapped; slivers %.1f %% vs %.1f %%; deviation %.5f vs %.5f\n", maxValence(capped),
                maxValence(uncapped), sliverShare(capped) * 100.0, sliverShare(uncapped) * 100.0, deviation(cube, capped),
                deviation(cube, uncapped));
    CHECK(deviation(cube, capped) < 0.01f); // the cube's size is 2
}

// SF_DECIMATE_AB=1 (ctest entry decimate_valence_ab, labels bench;nightly): time, valence, slivers and deviation with
// the cap and without, on flat and on sculpt-like meshes, to the Pose Mesh share (1.5 %) and a typical Decimate (10 %).
TEST(nightly_decimate_valence_cap) {
    if (!std::getenv("SF_DECIMATE_AB")) return;
    const char* outPath = std::getenv("SF_DECIMATE_AB_OUT");
    if (outPath) std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());
    std::FILE* out = outPath ? std::fopen(outPath, "w") : nullptr;
    auto row = [&](const char* text) {
        std::printf("%s\n", text);
        if (out) std::fprintf(out, "%s\n", text);
    };
    row("| mesh | tris | target | cap | s | out tris | max valence | slivers < 10 deg | max deviation |");
    row("|---|---|---|---|---|---|---|---|---|");
    struct Case {
        const char* name;
        Mesh mesh;
    };
    std::vector<Case> cases;
    cases.push_back({"flat cube", makeCube(160, 2.0f)});        // 307k
    cases.push_back({"bumpy sphere", bumpySphere(7)});          // 328k
    cases.push_back({"bumpy sphere", bumpySphere(8)});          // 1.3M
    for (const Case& c : cases)
        for (const double share : {0.015, 0.10})
            for (const size_t cap : {kDecimateMaxValence, size_t(0)}) {
                const size_t target = size_t(double(c.mesh.triangleCount()) * share);
                const auto t0 = std::chrono::steady_clock::now();
                const Mesh d = decimate(c.mesh, target, false, cap);
                const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                char line[512];
                std::snprintf(line, sizeof line, "| %s | %zu | %.1f %% | %s | %.2f | %zu | %zu | %.1f %% | %.4f |",
                              c.name, c.mesh.triangleCount(), share * 100.0, cap ? "16" : "off", s, d.triangleCount(),
                              maxValence(d), sliverShare(d) * 100.0, deviation(c.mesh, d));
                row(line);
            }
    if (out) std::fclose(out);
}
