// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plane brushes under View falloff, and brushes fed a non-finite value (gh#229). View falloff reaches through the
// model as a cylinder; Clay and Clay Block took their plane from the whole cylinder (the mean of the front and the far
// side, inside the model) and pulled the far side through to it: one stroke on a new Sphere tore two bands of shards.
// The promise, on the reporter's case (the default Sphere, a 250 mm brush on the 2 m sphere, X symmetry): no dab moves
// a vertex farther than the brush radius, the far side stays, nothing turns NaN; on a thin part the brush still reaches
// through. A NaN pressure or normal drops the dab: the mesh stays as it was.

#include "TestHarness.h"

#include "core/BrushRecipe.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace sf;

namespace {

constexpr Symmetry kX{true, false, false};
constexpr float kRadius = 0.25f; // (250 mm on the 2 m Sphere)

struct Result {
    float maxDab = 0.0f;  // the largest move of a vertex in one dab
    float farMoved = 0.0f; // the largest move of a vertex on the far side (z < -0.3, the back of the sphere)
    bool finite = true;
    size_t dropped = 0;
};

// A stroke the way the reporter drew it: down the side of the sphere seen from the front (+z), near its outline,
// X symmetry mirroring it to the other side; each dab picked on the surface the dabs before it left.
template <class Dab>
Result strokeDown(Document& d, float x, bool viewFalloff, float strength, Dab&& dab) {
    Result out;
    const std::vector<glm::vec3> start = d.mesh.positions;
    d.sculptor.beginStroke(d.mesh, "Test");
    glm::vec3 last(0.0f);
    for (int i = 0; i <= 16; ++i) {
        const float y = -0.5f + float(i) / 16.0f;
        const glm::vec3 from(x, y, 3.0f), dir(0.0f, 0.0f, -1.0f);
        RayHit hit;
        if (!d.bvh.raycast(d.mesh, from, dir, hit)) continue;
        DabParams p;
        p.center = hit.position;
        p.pickNormal = glm::normalize(hit.normal);
        p.viewDir = dir;
        p.radius = kRadius;
        p.strength = strength;
        p.viewFalloff = viewFalloff;
        p.strokeDelta = i ? hit.position - last : glm::vec3(0.0f);
        last = hit.position;
        const std::vector<glm::vec3> before = d.mesh.positions;
        dab(d, p);
        for (size_t v = 0; v < before.size(); ++v)
            out.maxDab = std::max(out.maxDab, glm::distance(before[v], d.mesh.positions[v]));
    }
    out.dropped = d.sculptor.nonFiniteDabs;
    d.undo.push(d.sculptor.endStroke());
    for (size_t v = 0; v < start.size(); ++v) {
        const glm::vec3& q = d.mesh.positions[v];
        out.finite = out.finite && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
        if (start[v].z < -0.3f) out.farMoved = std::max(out.farMoved, glm::distance(start[v], q));
    }
    return out;
}

Result brushStroke(Document& d, BrushType type, float x, bool viewFalloff, float strength = 0.35f) {
    return strokeDown(d, x, viewFalloff, strength,
                      [&](Document& doc, const DabParams& p) { doc.sculptor.dab(doc.mesh, doc.bvh, type, p, kX); });
}

const char* nameOf(BrushType t) {
    switch (t) {
    case BrushType::Clay: return "Clay";
    case BrushType::ClayBlock: return "Clay Block";
    case BrushType::Flatten: return "Flatten";
    case BrushType::Trim: return "Trim";
    case BrushType::Inflate: return "Inflate";
    default: return "Standard";
    }
}

} // namespace

TEST(view_falloff_invariant_clay_block_reporter_case_tears_no_shards) {
    // gh#229 as filed: new Sphere (81,920 triangles), Clay Block, Intensity 35, X symmetry, View falloff on.
    Document d = makeDoc(makeIcosphere(6, 1.0f));
    CHECK(d.mesh.triangleCount() == 81920);
    const Result r = brushStroke(d, BrushType::ClayBlock, 0.7f, true);
    std::printf("  Clay Block, View falloff: largest dab move %.4f (radius %.2f), far side moved %.4f\n", double(r.maxDab),
                double(kRadius), double(r.farMoved));
    CHECK(r.finite);
    CHECK(r.maxDab > 0.0f);   // (it sculpts)
    CHECK(r.maxDab < kRadius); // (no vertex flung across the model)
    CHECK(r.farMoved == 0.0f); // (the back of the sphere is a radius and more behind the plane)
}

TEST(view_falloff_invariant_plane_brushes_matrix_on_the_default_sphere) {
    // Every brush, View falloff off and on, both sides of the sphere (the stroke and its mirror): bounded, finite.
    for (BrushType t : {BrushType::Clay, BrushType::ClayBlock, BrushType::Flatten, BrushType::Trim, BrushType::Standard})
        for (bool vf : {false, true})
            for (float x : {0.4f, 0.7f, 0.85f}) {
                Document d = makeDoc(makeIcosphere(6, 1.0f));
                const Result r = brushStroke(d, t, x, vf);
                std::printf("  %-10s view falloff %d, x %.2f: dab move %.4f, far side %.4f\n", nameOf(t), int(vf), double(x),
                            double(r.maxDab), double(r.farMoved));
                CHECK(r.finite);
                CHECK(r.maxDab < kRadius);
                if (t != BrushType::Standard) CHECK(r.farMoved == 0.0f); // (Standard pushes both sides along the view: its promise)
                CHECK(r.dropped == 0);
            }
}

TEST(view_falloff_invariant_still_reaches_through_a_thin_part) {
    // A thin part (an ear, a horn: a plate 0.12 thick under a 0.25 brush): the far side is within the radius, so View
    // falloff still sculpts it, as artists use it for.
    Mesh m = makeIcosphere(6, 1.0f);
    for (glm::vec3& q : m.positions) q.z *= 0.06f;
    m.recomputeAllNormals();
    for (BrushType t : {BrushType::Clay, BrushType::ClayBlock, BrushType::Flatten, BrushType::Standard}) {
        Document d = makeDoc(m);
        const std::vector<glm::vec3> before = d.mesh.positions;
        const Result r = brushStroke(d, t, 0.3f, true, 0.6f);
        float back = 0.0f;
        for (size_t v = 0; v < before.size(); ++v)
            if (before[v].z < -0.02f) back = std::max(back, glm::distance(before[v], d.mesh.positions[v]));
        std::printf("  %-10s thin part: back side moved %.4f\n", nameOf(t), double(back));
        CHECK(r.finite);
        CHECK(back > 1e-4f);
    }
}

TEST(view_falloff_invariant_plane_recipe_tears_no_shards) {
    // A Brush Lab recipe with a Clay plane takes its plane the same way.
    BrushRecipe recipe;
    recipe.name = "Clay plane";
    recipe.plane = BrushRecipe::Plane::Clay;
    recipe.planeAmount = 1.0f;
    recipe.depth = 0.0f;
    Document d = makeDoc(makeIcosphere(6, 1.0f));
    const Result r = strokeDown(d, 0.7f, true, 0.35f, [&](Document& doc, const DabParams& p) {
        doc.sculptor.dabRecipe(doc.mesh, doc.bvh, recipe, p, kX);
    });
    std::printf("  recipe Clay plane, View falloff: dab move %.4f, far side %.4f\n", double(r.maxDab), double(r.farMoved));
    CHECK(r.finite);
    CHECK(r.maxDab > 0.0f);
    CHECK(r.maxDab < kRadius);
    CHECK(r.farMoved == 0.0f);
}

TEST(non_finite_invariant_pressure_or_normal_drops_the_dab) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (BrushType t : {BrushType::Standard, BrushType::Clay, BrushType::ClayBlock, BrushType::Inflate}) {
        // A NaN pressure: nothing moves, nothing turns NaN.
        Document d = makeDoc(makeIcosphere(6, 1.0f));
        const std::vector<glm::vec3> before = d.mesh.positions;
        d.sculptor.beginStroke(d.mesh, "Test");
        DabParams p;
        p.center = glm::vec3(0.0f, 0.0f, 1.0f);
        p.viewDir = glm::vec3(0.0f, 0.0f, -1.0f);
        p.radius = kRadius;
        p.strength = 0.35f;
        p.pressure = nan;
        d.sculptor.dab(d.mesh, d.bvh, t, p, kX);
        d.undo.push(d.sculptor.endStroke());
        CHECK(d.mesh.positions == before);

        // A NaN normal under the brush: the dab is dropped (and counted, for the log), the mesh is as it was.
        Document e = makeDoc(makeIcosphere(6, 1.0f));
        RayHit hit;
        CHECK(e.bvh.raycast(e.mesh, glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, -1.0f), hit));
        const uint32_t under = e.mesh.triangles[hit.triangle].x;
        e.mesh.normals[under] = glm::vec3(nan);
        const std::vector<glm::vec3> start = e.mesh.positions;
        e.sculptor.beginStroke(e.mesh, "Test");
        p.pressure = 1.0f;
        p.center = hit.position;
        e.sculptor.dab(e.mesh, e.bvh, t, p, kX);
        const size_t dropped = e.sculptor.nonFiniteDabs;
        e.undo.push(e.sculptor.endStroke());
        std::printf("  %-10s NaN normal: %zu dabs dropped\n", nameOf(t), dropped);
        CHECK(dropped >= 1);
        CHECK(e.mesh.positions == start);
    }
}
