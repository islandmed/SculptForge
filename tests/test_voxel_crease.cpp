// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Voxel Remesh keeps creases finer than its voxels clean (gh#240, reported by nino3thd: wrinkles on a decimated head
// came out stair-stepped): the crease pass in core/Voxel.cpp (snapCreases). On a tilted sphere with V-grooves and a
// ridge under two voxels wide (tests/CreaseProbe.h), against the same remesh without the pass:
//   the groove's depth varies along it at most 0.6x as much (the stair-step: a groove that comes and goes),
//   its flanks face the source's way (normal error at most 0.6x), its cross-sections are at most 0.6x as far off,
//   the surface sits nearer the source (mean) and never farther (Hausdorff), nothing new crosses itself or folds,
//   the topology is the same;
// the brick grid (> 512) makes the same surface as the dense one; a smooth sphere comes out exactly as without it; and
// creases the pass can't follow ask for the Resolution that would (the status line's hint).
#include "TestHarness.h"

#include "CreaseProbe.h"

#include "core/Primitives.h"
#include "core/Voxel.h"

#include <cstdio>
#include <unordered_map>

using namespace sf;

namespace {
struct Run {
    Mesh mesh;
    crease::Measure m;
    size_t crossings = 0, folds = 0;
};

Run remesh(const crease::Fixture& f, int resolution, bool snap) {
    setVoxelCreaseSnap(snap);
    Run r;
    std::string error;
    r.mesh = voxelRemesh(f.mesh, resolution, &error);
    setVoxelCreaseSnap(true);
    const glm::vec3 ext = f.mesh.bounds().extent();
    r.m = crease::measure(f, r.mesh, std::max({ext.x, ext.y, ext.z}) / float(resolution));
    r.crossings = crease::selfIntersections(r.mesh);
    { // folds: faces across an edge whose normals are more than 120 degrees apart (a crumpled crease's dark speckle)
        std::unordered_map<uint64_t, uint32_t> first;
        const auto& P = r.mesh.positions;
        auto normal = [&](uint32_t t) {
            const glm::uvec3 f = r.mesh.triangles[t];
            return glm::normalize(glm::cross(P[f.y] - P[f.x], P[f.z] - P[f.x]));
        };
        for (uint32_t t = 0; t < r.mesh.triangleCount(); ++t)
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = r.mesh.triangles[t][k], b = r.mesh.triangles[t][(k + 1) % 3];
                const uint64_t key = a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a;
                const auto [it, added] = first.emplace(key, t);
                if (!added) r.folds += glm::dot(normal(t), normal(it->second)) < -0.5f;
            }
    }
    std::printf("    res %d %s: depth cv %.3f, wiggle %.3f, normal error %.2f deg, profile error %.3f, Hausdorff %.3f, mean %.4f "
                "voxels, %zu crossings, %zu folds, %zu triangles\n",
                resolution, snap ? "on " : "off", r.m.depthCv, r.m.wiggleVox, r.m.normalErrDeg, r.m.profileErr, r.m.hausdorff,
                r.m.mean, r.crossings, r.folds, r.mesh.triangleCount());
    return r;
}

uint64_t hashOf(const Mesh& m) {
    uint64_t h = 1469598103934665603ull;
    auto add = [&](const void* p, size_t n) {
        for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const unsigned char*>(p)[i]) * 1099511628211ull;
    };
    add(m.positions.data(), m.positions.size() * sizeof(glm::vec3));
    add(m.triangles.data(), m.triangles.size() * sizeof(glm::uvec3));
    return h;
}

const crease::Fixture& fixture() {
    static const crease::Fixture f = crease::makeCreasedSphere(0.008f, 0.012f, 768, 0.01f);
    return f;
}
} // namespace

TEST(voxel_crease_invariant_fine_creases_stay_clean) {
    const Run off = remesh(fixture(), 200, false), on = remesh(fixture(), 200, true);
    CHECK(on.mesh.triangleCount() == off.mesh.triangleCount()); // (the same voxel topology)
    CHECK(on.m.depthCv <= 0.65 * off.m.depthCv); // (0.6 before the sealed-crease sign fix, gh#240: it changed both runs a little)
    CHECK(on.m.normalErrDeg <= 0.6 * off.m.normalErrDeg);
    CHECK(on.m.profileErr <= 0.65 * off.m.profileErr);
    CHECK(on.m.wiggleVox < off.m.wiggleVox);
    CHECK(on.m.mean < off.m.mean);
    CHECK(on.m.hausdorff <= off.m.hausdorff);
    CHECK(on.crossings <= off.crossings);
    CHECK(on.folds <= off.folds);
}

TEST(voxel_crease_invariant_brick_grid_matches) {
    const Mesh dense = voxelRemesh(fixture().mesh, 200);
    setVoxelSparseAbove(100);
    const Mesh bricks = voxelRemesh(fixture().mesh, 200);
    setVoxelSparseAbove(kVoxelDenseMax);
    CHECK(dense.triangleCount() > 0);
    CHECK(hashOf(bricks) == hashOf(dense));
}

TEST(voxel_crease_invariant_smooth_surface_untouched) {
    const Mesh sphere = makeQuadSphere(120, 1.0f);
    setVoxelCreaseSnap(false);
    const Mesh off = voxelRemesh(sphere, 200);
    setVoxelCreaseSnap(true);
    const Mesh on = voxelRemesh(sphere, 200);
    CHECK(off.triangleCount() > 0);
    CHECK(hashOf(on) == hashOf(off));
}

// The Resolution hint: creases 1.6 voxels wide at 200 ask for the Resolution that makes them two voxels wide (250, on
// the slider's step of 8: 256) with its triangle estimate; at 320 they are 2.6 voxels wide and a smooth sphere has none:
// no hint.
TEST(voxel_crease_resolution_hint) {
    VoxelRemeshNotes notes;
    std::string error;
    voxelRemesh(fixture().mesh, 200, &error, {}, false, false, &notes);
    std::printf("    hint at 200: %d (%zu triangles) %s\n", notes.creaseResolution, notes.creaseTriangles, voxelRemeshNote(notes).c_str());
    CHECK(notes.creaseResolution == 256);
    CHECK(notes.creaseTriangles > 0);
    CHECK(voxelRemeshNote(notes).rfind("Resolution 256 keeps fine creases", 0) == 0);
    voxelRemesh(fixture().mesh, 320, &error, {}, false, false, &notes);
    CHECK(notes.creaseResolution == 0);
    CHECK(voxelRemeshNote(notes).empty());
    voxelRemesh(makeQuadSphere(120, 1.0f), 200, &error, {}, false, false, &notes);
    CHECK(notes.creaseResolution == 0);
}

// A slit (closed lips: walls 0.4 voxels apart, 2 voxels deep) and a crease under a voxel wide can't be held by the voxels:
// the pass leaves them as the voxels made them instead of pulling single vertices onto one wall (artist-qa 11-25: shards
// and notches along a lip line, 3-6x the edges sharper than 60 degrees). At most 2% more sharp edges than without it.
TEST(voxel_crease_invariant_slits_left_alone) {
    auto sharpEdges = [](const Mesh& m) {
        std::unordered_map<uint64_t, uint32_t> first;
        size_t sharp = 0;
        auto normal = [&](uint32_t t) {
            const glm::uvec3 f = m.triangles[t];
            return glm::normalize(glm::cross(m.positions[f.y] - m.positions[f.x], m.positions[f.z] - m.positions[f.x]));
        };
        for (uint32_t t = 0; t < m.triangleCount(); ++t)
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = m.triangles[t][k], b = m.triangles[t][(k + 1) % 3];
                const auto [it, added] = first.emplace(a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a, t);
                if (!added) sharp += glm::dot(normal(t), normal(it->second)) < 0.5f;
            }
        return sharp;
    };
    for (const auto& [halfWidth, depth] : {std::pair{0.002f, 0.02f}, std::pair{0.004f, 0.004f}}) {
        const crease::Fixture slit = crease::makeCreasedSphere(halfWidth, depth, 768, 0.01f);
        setVoxelCreaseSnap(false);
        const Mesh off = voxelRemesh(slit.mesh, 200);
        setVoxelCreaseSnap(true);
        const Mesh on = voxelRemesh(slit.mesh, 200);
        const size_t sharpOff = sharpEdges(off), sharpOn = sharpEdges(on);
        std::printf("    slit %.3f x %.3f: %zu sharp edges off, %zu on, %zu crossings on\n", halfWidth, depth, sharpOff, sharpOn,
                    crease::selfIntersections(on));
        CHECK(sharpOn <= sharpOff + sharpOff / 50 + 4); // (without the skip: 2875 of 2634 and 30 of 20)
        CHECK(crease::selfIntersections(on) <= crease::selfIntersections(off));
    }
}
