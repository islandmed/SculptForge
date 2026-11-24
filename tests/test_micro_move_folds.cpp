// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Move on a micro level over a base with forms smaller than the move (artist-qa 11-18: a voxel-remeshed lion, subdivided,
// Micro 3; a Move beside a mask left crumpled speckle and folded leaf-like lumps around the stroke). Geometric invariants
// (GeomInvariants.h), mouse and pen strength alike: no micro sample outside the brush moves, and no micro triangle inside
// it folds over (its normal turned more than 90 degrees from before the stroke).
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/Benchmark.h"
#include "core/Micro.h"

#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

const glm::vec3 kFront(0.0f, 0.0f, 1.0f);
constexpr float kRadius = 0.4f;

// A sphere with bumps in the BASE mesh a few base edges wide (the lion's whisker dots and nostril rims): its normals fan
// out over each bump, so anything that moves the surface along them by more than a bump's size folds it.
Document bumpyMicroDoc() {
    Mesh m = makeBenchSphere(80000);
    const glm::vec3 bumps[] = {{0.0f, 0.0f, 1.0f}, {0.12f, 0.08f, 1.0f}, {-0.1f, 0.12f, 1.0f}, {0.08f, -0.14f, 1.0f},
                               {-0.15f, -0.06f, 1.0f}, {0.22f, -0.02f, 1.0f}, {-0.02f, 0.25f, 1.0f}, {0.18f, 0.2f, 1.0f}};
    for (glm::vec3& p : m.positions) {
        float h = 0.0f;
        for (glm::vec3 b : bumps) {
            const float d = glm::distance(glm::normalize(p), glm::normalize(b));
            h += 0.03f * std::exp(-d * d / (0.02f * 0.02f));
        }
        p *= 1.0f + h;
    }
    m.recomputeAllNormals();
    Document doc = makeDoc(std::move(m));
    std::string err;
    CHECK(microDivide(doc.mesh, 3, err));
    return doc;
}

DabParams dabAt(const glm::vec3& c, float r, float strength = 1.0f) {
    DabParams p;
    p.center = c;
    p.radius = r;
    p.strength = strength;
    p.viewDir = glm::vec3(0, 0, -1);
    return p;
}

// A masked band beside the move (the artist's Ctrl-mask), off the brush's middle.
void maskBand(Document& doc) {
    Symmetry sym;
    sym.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Mask"));
    for (int pass = 0; pass < 3; ++pass) // (to a fully masked core)
        for (int k = 0; k < 5; ++k)
            doc.sculptor.dabMask(doc.mesh, doc.bvh, dabAt(glm::normalize(glm::vec3(-0.3f + 0.15f * float(k), -0.32f, 1.0f)), 0.1f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(doc.mesh.micro->hasMask());
}

// A Move across the bumps, sideways (the view's plane) by about 0.17 in six steps (less than a fold of the falloff itself: a Move dragged near its radius squeezes any surface over), at the pen's `strength`.
void move(Document& doc, float strength) {
    Symmetry sym;
    sym.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Move"));
    doc.sculptor.beginGrab(doc.mesh, doc.bvh, dabAt(glm::normalize(glm::vec3(0.05f, 0.05f, 1.0f)), kRadius, strength), sym);
    for (int i = 0; i < 6; ++i) doc.sculptor.grab(doc.mesh, doc.bvh, glm::vec3(0.025f, 0.012f, 0.0f));
    doc.undo.push(doc.sculptor.endStroke());
}

struct Report {
    size_t folded = 0, inside = 0, outsideMoved = 0;
    float worstOutside = 0.0f, mostInside = 0.0f, microEdge = 0.0f;
};

Report measure(const Mesh& m, const geom::MicroSnapshot& before, const geom::MicroSnapshot& after) {
    const MicroMesh& mm = *m.micro;
    const float reach = kRadius + 3.0f * mm.baseEdge;
    const uint32_t n = mm.n;
    const glm::vec3 c = glm::normalize(glm::vec3(0.05f, 0.05f, 1.0f)) * 1.035f;
    Report r;
    double edges = 0.0;
    for (size_t k = 0; k < before.tris.size(); ++k) {
        const glm::vec3* b = before.points.data() + k * mm.samples;
        const glm::vec3* a = after.points.data() + k * mm.samples;
        edges += glm::distance(b[MicroMesh::index(0, 0, n)], b[MicroMesh::index(1, 0, n)]);
        for (uint32_t s = 0; s < mm.samples; ++s) {
            const float d = glm::distance(a[s], b[s]), from = glm::distance(b[s], c);
            if (from < kRadius) r.mostInside = std::max(r.mostInside, d);
            if (from > reach) { // (the base cage's triangles reach a little past the falloff, and its normals one ring more)
                r.worstOutside = std::max(r.worstOutside, d);
                r.outsideMoved += d > 0.0f;
            }
        }
        auto tri = [&](uint32_t i0, uint32_t j0, uint32_t i1, uint32_t j1, uint32_t i2, uint32_t j2) {
            const uint32_t x = MicroMesh::index(i0, j0, n), y = MicroMesh::index(i1, j1, n), z = MicroMesh::index(i2, j2, n);
            if (glm::distance((b[x] + b[y] + b[z]) / 3.0f, c) > kRadius) return;
            ++r.inside;
            const glm::vec3 nb = glm::cross(b[y] - b[x], b[z] - b[x]), na = glm::cross(a[y] - a[x], a[z] - a[x]);
            if (glm::dot(nb, na) <= 0.0f) ++r.folded;
        };
        for (uint32_t j = 0; j < n; ++j)
            for (uint32_t i = 0; i + j < n; ++i) {
                tri(i, j, i + 1, j, i, j + 1);
                if (i + j + 1 < n) tri(i + 1, j, i + 1, j + 1, i, j + 1);
            }
    }
    r.microEdge = before.tris.empty() ? 1.0f : float(edges / double(before.tris.size()));
    return r;
}

void check(const char* what, bool masked, float strength) {
    Document doc = bumpyMicroDoc();
    if (masked) maskBand(doc);
    const geom::MicroSnapshot before = geom::microSnapshot(doc.mesh, kFront, 0.5f);
    move(doc, strength);
    const geom::MicroSnapshot after = geom::microSnapshot(doc.mesh, kFront, 0.5f);
    const Report r = measure(doc.mesh, before, after);
    size_t full = 0;
    const size_t held = geom::maskedMoved(doc.mesh, before, after, &full); // (and the mask still holds)
    if (masked) std::printf("  fully masked samples %zu, moved %zu\n", full, held);
    CHECK(!masked || (full > 0 && held == 0));
    std::printf("  %-22s strength %.1f: moved up to %.3f, folded %zu of %zu micro triangles in the brush; outside it %zu samples moved, worst %.2e "
                "(%.1f%% of a micro edge)\n",
                what, strength, r.mostInside, r.folded, r.inside, r.outsideMoved, r.worstOutside, 100.0f * r.worstOutside / r.microEdge);
    CHECK(r.inside > 0);
    CHECK(r.mostInside > 0.05f); // (a stroke that does nothing folds nothing either)
    CHECK(r.folded == 0);
    // Outside the brush: still to well under a hundredth of a micro edge (the grid's 16-bit rounding, never a crumple).
    CHECK(r.worstOutside < 0.01f * r.microEdge);
}

} // namespace

TEST(micro_move_beside_a_mask_folds_nothing) {
    for (float strength : {1.0f, 0.4f}) check("Move beside a mask", true, strength); // mouse (full) and a light pen
}

TEST(micro_move_free_folds_nothing) {
    for (float strength : {1.0f, 0.4f}) check("Move (no mask)", false, strength);
}
