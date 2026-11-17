// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A heavy stroke is a pure function of the mesh and its dabs (a region over most of a big mesh takes the adaptive path:
// area plane from a sample, normals carried by the offset's derivative, the exact ones after): the same dab list twice
// from the same mesh gives the same positions and normals, bit for bit, and undo gives the mesh back exactly, normals
// included (a vertex whose faces have no area keeps no normal the stroke gave it). Found on a 28M-vertex scan: two replays
// of five 240 px dabs differed in millions of positions by an ulp, from one such vertex's stale normal.
#include "TestHarness.h"

#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/Sha256.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace sf;

namespace {

constexpr Symmetry kNoSymmetry{false, false, false};

// positions + normals as one digest
std::string meshDigest(const Mesh& m) {
    std::vector<uint8_t> bytes(m.positions.size() * sizeof(glm::vec3) + m.normals.size() * sizeof(glm::vec3));
    std::memcpy(bytes.data(), m.positions.data(), m.positions.size() * sizeof(glm::vec3));
    std::memcpy(bytes.data() + m.positions.size() * sizeof(glm::vec3), m.normals.data(), m.normals.size() * sizeof(glm::vec3));
    return sha256Hex(bytes.data(), bytes.size());
}

// Dabs whose regions cover most of the sphere (radius 1): each one's sphere of 1.9 takes in far more than the
// adaptive threshold (Sculptor::kAdaptiveMinVertices) on the meshes below.
std::vector<DabParams> bigDabs() {
    std::vector<DabParams> dabs;
    for (int i = 0; i < 5; ++i) {
        const glm::vec3 c = glm::normalize(glm::vec3(0.2f * float(i) - 0.4f, 1.0f, 0.3f - 0.1f * float(i)));
        DabParams p;
        p.center = c;
        p.viewDir = -c;
        p.radius = 1.9f;
        p.strength = 0.5f;
        dabs.push_back(p);
    }
    return dabs;
}

void stroke(Document& d, const std::vector<DabParams>& dabs) {
    d.sculptor.beginStroke(d.mesh, "Standard");
    for (const DabParams& p : dabs) d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, kNoSymmetry);
    d.undo.push(d.sculptor.endStroke());
}

size_t differ(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return std::max(a.size(), b.size());
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

// The checks at one size: replay determinism, exact undo, and normals that are a function of the positions only.
void checkSphere(Mesh sphere, bool withLoose) {
    if (withLoose) { // a vertex whose only face has no area (a scan's collapsed sliver), inside every dab's region: its normal sum is zero
        const uint32_t sliver = uint32_t(sphere.positions.size());
        sphere.positions.push_back(glm::normalize(glm::vec3(0.1f, 1.0f, 0.1f)));
        sphere.normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
        sphere.triangles.push_back(glm::uvec3(sliver, 0, 0));
        sphere.rebuildTopology(); // (the icosphere's adjacency is implicit in its subdivision levels; this mesh is a scan's kind)
    }
    const std::vector<DabParams> dabs = bigDabs();
    Document d = makeDoc(std::move(sphere));
    d.mesh.recomputeAllNormals(); // (as a loaded scan has them: from its faces; the icosphere's own are radial)
    CHECK(d.mesh.normals.size() == d.mesh.positions.size());
    const std::vector<glm::vec3> positions0 = d.mesh.positions, normals0 = d.mesh.normals;
    const std::string digest0 = meshDigest(d.mesh);
    size_t region = 0;
    for (const glm::vec3& x : d.mesh.positions) region += glm::distance(x, dabs[0].center) < dabs[0].radius;
    CHECK(region >= Sculptor::kAdaptiveMinVertices);

    stroke(d, dabs);
    const std::string first = meshDigest(d.mesh);
    CHECK(first != digest0); // (the stroke did something)
    const std::vector<glm::vec3> positions1 = d.mesh.positions, normals1 = d.mesh.normals;

    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(differ(d.mesh.positions, positions0) == 0);
    CHECK(differ(d.mesh.normals, normals0) == 0);
    CHECK(meshDigest(d.mesh) == digest0);


    stroke(d, dabs); // the same dabs, from the mesh undo gave back
    const std::string second = meshDigest(d.mesh);
    std::printf("  %zu vertices, region %zu: positions differ %zu, normals differ %zu\n", d.mesh.vertexCount(), region,
                differ(d.mesh.positions, positions1), differ(d.mesh.normals, normals1));
    CHECK(second == first);

    // The normals are what the positions say (no stale one anywhere, the sliver vertex's included)
    Mesh fresh = d.mesh;
    fresh.recomputeAllNormals();
    CHECK(differ(d.mesh.normals, fresh.normals) == 0);

    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(meshDigest(d.mesh) == digest0);
}

// Everything a stroke could touch, as one digest: positions, normals, the base and every layer's offsets, the mask, the colours.
std::string fullDigest(const Mesh& m) {
    std::string all = meshDigest(m);
    auto add = [&](const void* data, size_t bytes) { all += sha256Hex(data, bytes); };
    add(m.base.data(), m.base.size() * sizeof(glm::vec3));
    for (const SculptLayer& l : m.layers) add(l.delta.data(), l.delta.size() * sizeof(glm::vec3));
    add(m.mask.data(), m.mask.size() * sizeof(float));
    add(m.colors.data(), m.colors.size() * sizeof(glm::vec3));
    return sha256Hex(all.data(), all.size());
}

} // namespace

TEST(stroke_determinism_undo_restores_layers_mask_and_colours_exactly) {
    Mesh sphere = makeIcosphere(7); // 164k vertices
    Document d = makeDoc(std::move(sphere));
    d.mesh.ensureColors(glm::vec3(0.8f, 0.4f, 0.2f));
    d.mesh.addLayer("Layer");
    d.activeLayer = 0;
    std::vector<DabParams> dabs = bigDabs();
    for (DabParams& p : dabs) p.radius = 0.9f;
    // a mask first (a stroke of its own), so the sculpt strokes meet a real one, and the colours vary
    d.sculptor.beginStroke(d.mesh, "Mask", d.activeLayer);
    d.mesh.ensureMask();
    d.sculptor.dabMask(d.mesh, d.bvh, dabs[0], kNoSymmetry);
    d.undo.push(d.sculptor.endStroke());
    for (size_t i = 0; i < d.mesh.colors.size(); i += 7) d.mesh.colors[i] = glm::vec3(0.1f * float(i % 10), 0.5f, 0.9f);
    const std::string before = fullDigest(d.mesh);
    for (int round = 0; round < 3; ++round) {
        d.sculptor.beginStroke(d.mesh, "Standard", d.activeLayer);
        for (const DabParams& p : dabs) d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, kNoSymmetry);
        d.undo.push(d.sculptor.endStroke());
        CHECK(fullDigest(d.mesh) != before);
        CHECK(d.undo.undo(d.mesh, d.bvh));
        CHECK(fullDigest(d.mesh) == before);
    }
    // the mask stroke itself undoes to a mesh without one
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(std::all_of(d.mesh.mask.begin(), d.mesh.mask.end(), [](float m) { return m == 0.0f; }));
}

// The app's solver catches the exact normals up behind the brush on the clock (a share per batch, all of them at a pause): where
// it has got by the next dab must not show in the stroke.
TEST(stroke_determinism_normal_catch_up_between_dabs_does_not_change_the_stroke) {
    const Mesh sphere = makeIcosphere(8);
    std::vector<DabParams> dabs = bigDabs();
    for (int i = 0; i < 5; ++i) dabs.push_back(dabs[size_t(i)]), dabs.back().center = glm::normalize(dabs.back().center + glm::vec3(0.15f, 0.0f, 0.1f)), dabs.back().viewDir = -dabs.back().center;
    const auto run = [&](int mode) {
        Document d = makeDoc(Mesh(sphere));
        d.sculptor.beginStroke(d.mesh, "Standard");
        d.sculptor.beginBatch();
        for (size_t i = 0; i < dabs.size(); ++i) {
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, dabs[i], kNoSymmetry);
            d.sculptor.endBatch(d.mesh, d.bvh);
            if (mode == 1) d.sculptor.catchUpNormals(d.mesh, d.bvh, 30000 * (i + 1)); // a part, more each time
            if (mode == 2) d.sculptor.catchUpBehind(d.mesh, d.bvh, 0.35);
            if (mode == 3 && i % 2) d.sculptor.catchUpNormals(d.mesh, d.bvh, SIZE_MAX); // a pause now and then
            d.sculptor.beginBatch();
        }
        d.sculptor.endBatch(d.mesh, d.bvh);
        d.undo.push(d.sculptor.endStroke());
        return d.mesh;
    };
    const Mesh plain = run(0);
    for (int mode = 1; mode <= 3; ++mode) {
        const Mesh caught = run(mode);
        std::printf("  catch-up mode %d: positions differ %zu, normals differ %zu\n", mode, differ(caught.positions, plain.positions), differ(caught.normals, plain.normals));
        CHECK(differ(caught.positions, plain.positions) == 0);
        CHECK(differ(caught.normals, plain.normals) == 0);
    }
}

TEST(stroke_determinism_heavy_stroke_replays_and_undoes_exactly) {
    checkSphere(makeIcosphere(8), false); // 655k vertices
}

TEST(stroke_determinism_vertex_with_zero_area_faces_keeps_no_stale_normal) {
    checkSphere(makeIcosphere(8), true);
}

// The scan-sized case (10.5M vertices, 21M triangles, regions over most of it). Nightly: minutes and several GB.
TEST(nightly_stroke_determinism_10m_vertices) {
    checkSphere(makeIcosphere(10), true);
}
