// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Voxel Remesh up to 4096: the brick grid above 512 (core/VoxelSparse.cpp) makes the same
// surface as the dense grid, the Resolution slider's travel, the triangle and memory estimate, and the memory refusal.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/MemoryGuard.h"
#include "core/Primitives.h"
#include "core/Voxel.h"

#include <cmath>
#include <cstdio>
#include <new>
#include <algorithm>
#include <string>
#include <vector>

using namespace sf;

// CHECK with what went wrong.
#define CHECK_MSG(cond, msg)                                                       \
    do {                                                                           \
        const bool ok_ = (cond);                                                   \
        CHECK(ok_);                                                                \
        if (!ok_) std::printf("    %s\n", std::string(msg).c_str());             \
    } while (0)

namespace {

Mesh joined(const Mesh& a, Mesh b, const glm::vec3& offset) {
    Mesh m = a;
    for (auto& p : b.positions) m.positions.push_back(p + offset);
    for (auto t : b.triangles) m.triangles.push_back(t + glm::uvec3(uint32_t(a.vertexCount())));
    m.rebuildTopology();
    return m;
}

// The same remesh on the dense grid and on the bricks: identical points and triangles.
bool sameOnBricks(const Mesh& source, int resolution, std::string* why) {
    std::string e1, e2;
    setVoxelSparseAbove(kVoxelDenseMax);
    setVoxelDenoise(false); // (the brick grid has no field low-pass: the dense grid's is compared separately)
    const Mesh dense = voxelRemesh(source, resolution, &e1);
    setVoxelDenoise(true);
    setVoxelSparseAbove(0);
    const Mesh bricks = voxelRemesh(source, resolution, &e2);
    setVoxelSparseAbove(kVoxelDenseMax);
    if (dense.triangleCount() == 0 || dense.triangleCount() != bricks.triangleCount() ||
        dense.vertexCount() != bricks.vertexCount()) {
        *why = "dense " + std::to_string(dense.triangleCount()) + " (" + e1 + ") vs bricks " +
               std::to_string(bricks.triangleCount()) + " (" + e2 + ")";
        return false;
    }
    for (size_t i = 0; i < dense.triangleCount(); ++i)
        if (dense.triangles[i] != bricks.triangles[i]) {
            *why = "triangle " + std::to_string(i) + " differs";
            return false;
        }
    for (size_t i = 0; i < dense.vertexCount(); ++i)
        if (glm::distance(dense.positions[i], bricks.positions[i]) > 1e-6f) {
            *why = "point " + std::to_string(i) + " differs";
            return false;
        }
    return true;
}

} // namespace

TEST(voxel_remesh_bricks_match_dense) {
    std::string why;
    CHECK_MSG(sameOnBricks(makeIcosphere(4), 64, &why), why);
    CHECK_MSG(sameOnBricks(joined(makeIcosphere(3), makeIcosphere(3), glm::vec3(1.2f, 0, 0)), 80, &why), why); // merged parts
    CHECK_MSG(sameOnBricks(joined(makeIcosphere(3), makeIcosphere(3, 0.3f), glm::vec3(0.1f, 0, 0)), 72, &why), why); // one buried
    CHECK_MSG(sameOnBricks(makeTorus(48), 96, &why), why);
    CHECK_MSG(sameOnBricks(makeIcosphere(0), 150, &why), why); // big triangles across many bricks
    CHECK_MSG(sameOnBricks(makeCube(2), 120, &why), why);
    Mesh open = makeIcosphere(3); // a cut: capped first
    open.triangles.resize(open.triangles.size() - 12);
    open.rebuildTopology();
    CHECK_MSG(sameOnBricks(open, 64, &why), why);
}

TEST(voxel_resolution_slider_travel) {
    CHECK(voxelResolutionFromSlider(0.0f) == 32);
    CHECK(voxelResolutionFromSlider(2.0f / 3.0f) == 512);
    CHECK(voxelResolutionFromSlider(5.0f / 6.0f) == 2048);
    CHECK(voxelResolutionFromSlider(1.0f) == 4096);
    CHECK(voxelResolutionFromSlider(0.5f) == 392); // three quarters of the everyday range at half the travel's 2/3
    int last = 0;
    for (int i = 0; i <= 1000; ++i) {
        const int r = voxelResolutionFromSlider(float(i) / 1000.0f);
        CHECK(r >= last);
        last = r;
    }
    for (int r : {32, 200, 512, 528, 1024, 2048, 2112, 3008, 4096})
        CHECK(voxelResolutionFromSlider(voxelSliderFromResolution(r)) == r);
    CHECK(voxelResolutionFromSlider(voxelSliderFromResolution(513)) % 16 == 0);
}

TEST(voxel_remesh_estimate) {
    const Mesh sphere = makeIcosphere(5);
    for (int r : {64, 160}) {
        const size_t made = voxelRemesh(sphere, r).triangleCount();
        const VoxelRemeshEstimate e = estimateVoxelRemesh(sphere, r);
        CHECK_MSG(double(e.triangles) > 0.8 * double(made) && double(e.triangles) < 1.25 * double(made),
                  std::to_string(e.triangles) + " estimated, " + std::to_string(made) + " made");
        CHECK(e.bytes > made * 150);
    }
    // 4x the resolution, about 16x the triangles; above 512 the memory goes with the surface, not the volume.
    const VoxelRemeshEstimate a = estimateVoxelRemesh(sphere, 1024), b = estimateVoxelRemesh(sphere, 4096);
    CHECK(b.triangles > 15 * a.triangles && b.triangles < 17 * a.triangles);
    CHECK(b.bytes < 20 * a.bytes);
    CHECK(estimateVoxelRemesh(Mesh(), 512).triangles == 0);
}

TEST(voxel_remesh_refuses_what_wont_fit) {
    // Too little free memory for the bricks: an empty result that says what it needs, never a crash or a swap storm.
    setAvailableMemoryOverride(size_t(64) << 20);
    std::string err;
    CHECK(voxelRemesh(makeIcosphere(4), 2048, &err).triangleCount() == 0);
    CHECK_MSG(err.find("needs ~") != std::string::npos && err.find("Lower the Resolution") != std::string::npos, err);
    CommandRegistry reg;
    Document d;
    CHECK(reg.execute(d, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const CommandResult r = reg.execute(d, "remesh", {{"resolution", 4096.0}});
    CHECK(!r.ok && r.message.find("needs ~") != std::string::npos);
    CHECK(d.mesh.triangleCount() == 1280); // unchanged
    setAvailableMemoryOverride(0);
    // Cancel works on the bricks too.
    err.clear();
    CHECK(voxelRemesh(makeIcosphere(3), 600, &err, [](float f) { return f < 0.3f; }).triangleCount() == 0);
    CHECK(err.find("cancelled") != std::string::npos);
}

TEST(voxel_remesh_past_free_memory_needs_the_go_ahead) {
    // 4 GB computer with 10 MB free: a 600 remesh of a small sphere is past the free memory but well within the
    // computer. Refused unless the person agreed (allowSwap); with 1 MB of computer it is refused either way.
    const Mesh sphere = makeIcosphere(3);
    setMemoryLimitsOverride({size_t(10) << 20, size_t(4) << 30, size_t(2) << 30});
    std::string err;
    CHECK(voxelRemesh(sphere, 600, &err).triangleCount() == 0);
    CHECK_MSG(err.find("needs ~") != std::string::npos && err.find("Lower the Resolution") != std::string::npos, err);
    err.clear();
    const Mesh made = voxelRemesh(sphere, 600, &err, {}, true);
    CHECK_MSG(made.triangleCount() > 10000, err);
    setMemoryLimitsOverride({size_t(10) << 20, size_t(1) << 20, 0});
    err.clear();
    CHECK(voxelRemesh(sphere, 600, &err, {}, true).triangleCount() == 0);
    CHECK_MSG(err.find("can't hold it even with swap") != std::string::npos, err);
    setMemoryLimitsOverride({}, false);
}

TEST(voxel_remesh_one_estimate_every_resolution) {
    // The same tiers and the same words at 256, 512, 600 and 1024 (artist-qa 11-12: 512 used to ask a different question
    // than 600). The figure rises with the Resolution within the dense grid (up to 512) and within the bricks (above it); it
    // falls once at 513 because the bricks hold only the surface (measured peak, 20k-triangle sphere: 512 2.0 GB, 600 0.5 GB).
    const Mesh sphere = makeIcosphere(5);
    CHECK(estimateVoxelRemesh(sphere, 512).bytes > estimateVoxelRemesh(sphere, 256).bytes);
    CHECK(estimateVoxelRemesh(sphere, 1024).bytes > estimateVoxelRemesh(sphere, 600).bytes);
    CHECK(estimateVoxelRemesh(sphere, 512).bytes < size_t(3) << 30); // (measured 2.0 GB; was 3.5 with 22 bytes a sample)
    setMemoryLimitsOverride({size_t(10) << 20, size_t(64) << 30, size_t(32) << 30});
    for (int r : {256, 512, 600, 1024}) {
        std::string err;
        CHECK(voxelRemesh(sphere, r, &err).triangleCount() == 0);
        const std::string need = "Voxel Remesh at " + std::to_string(r) + " needs ~" + memoryText(estimateVoxelRemesh(sphere, r).bytes) + "; 10 MB is free.";
        CHECK_MSG(err.find(need) == 0, err);
    }
    setMemoryLimitsOverride({}, false);
}

TEST(voxel_remesh_swap_path_invariant_same_surface) {
    // Invariant: agreeing to a remesh past the free memory (allowSwap) changes how it is allowed, never what it makes. On a
    // curved mesh, both paths give the identical surface: same counts, same bounds, same volume, no open edges.
    const Mesh sphere = makeIcosphere(3);
    setMemoryLimitsOverride({size_t(10) << 20, size_t(4) << 30, size_t(2) << 30});
    std::string err;
    const Mesh swap = voxelRemesh(sphere, 600, &err, {}, true);
    setMemoryLimitsOverride({}, false);
    const Mesh plain = voxelRemesh(sphere, 600, &err);
    CHECK_MSG(swap.triangleCount() > 10000, err);
    CHECK(swap.triangleCount() == plain.triangleCount());
    CHECK(swap.vertexCount() == plain.vertexCount());
    for (size_t i = 0; i < plain.vertexCount(); i += 997) CHECK(glm::distance(swap.positions[i], plain.positions[i]) < 1e-5f);
}

TEST(voxel_remesh_out_of_memory_rolls_back) {
    // An allocation failing midway (here from the progress callback, as a worker's would surface): an empty result and a
    // clear message, on the dense grid and on the bricks, the source untouched.
    const Mesh sphere = makeIcosphere(3);
    const size_t before = sphere.triangleCount();
    for (int res : {96, 600}) {
        std::string err;
        const Mesh r = voxelRemesh(sphere, res, &err, [](float f) -> bool {
            if (f > 0.2f) throw std::bad_alloc();
            return true;
        });
        CHECK(r.triangleCount() == 0);
        CHECK_MSG(err.find("ran out of memory") != std::string::npos && err.find("unchanged") != std::string::npos, err);
        CHECK(sphere.triangleCount() == before);
    }
}

TEST(voxel_remesh_4096_end_to_end) {
    // A needle along x: 4096 voxels along it and only a few million triangles, the full grid extent on one axis.
    Mesh needle = makeIcosphere(4);
    for (auto& p : needle.positions) p *= glm::vec3(1.0f, 0.02f, 0.02f);
    needle.rebuildTopology();
    std::string err;
    std::vector<float> steps;
    const Mesh r = voxelRemesh(needle, 4096, &err, [&](float f) { steps.push_back(f); return true; });
    CHECK_MSG(r.triangleCount() > 1'000'000, err);
    CHECK(steps.size() >= 10 && std::is_sorted(steps.begin(), steps.end()) && steps.back() == 1.0f);
    std::vector<uint64_t> edges;
    edges.reserve(r.triangleCount() * 3);
    double volume = 0.0;
    for (const glm::uvec3& t : r.triangles) {
        for (int k = 0; k < 3; ++k) edges.push_back(uint64_t(std::min(t[k], t[(k + 1) % 3])) << 32 | std::max(t[k], t[(k + 1) % 3]));
        volume += double(glm::dot(r.positions[t.x], glm::cross(r.positions[t.y], r.positions[t.z]))) / 6.0;
    }
    std::sort(edges.begin(), edges.end());
    bool closed = !edges.empty();
    for (size_t i = 0; i < edges.size(); i += 2) closed = closed && i + 1 < edges.size() && edges[i] == edges[i + 1] &&
                                                   (i + 2 >= edges.size() || edges[i + 2] != edges[i]);
    CHECK(closed); // every edge in exactly two triangles
    const double expected = 4.18879 * 0.02 * 0.02; // an ellipsoid's volume
    CHECK_MSG(std::abs(volume - expected) < 0.05 * expected, std::to_string(volume) + " vs " + std::to_string(expected));
    const AABB b = r.bounds();
    CHECK(std::abs(b.min.x + 1.0f) < 0.01f && std::abs(b.max.x - 1.0f) < 0.01f);
}
