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
#include "core/StarterMeshes.h"

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
    float deepMoved = 0.0f; // ... of one more than 3 radii behind every dab's centre along the view (the additive brushes' reach)
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
    std::vector<glm::vec3> centres;
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
        centres.push_back(hit.position);
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
        bool deep = true;
        for (const glm::vec3& c : centres) deep = deep && std::abs(start[v].z - c.z) > 3.0f * kRadius;
        if (deep) out.deepMoved = std::max(out.deepMoved, glm::distance(start[v], q));
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
    case BrushType::Crease: return "Crease";
    case BrushType::Pinch: return "Pinch";
    case BrushType::SharpCrease: return "SharpCrease";
    case BrushType::Move: return "Move";
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
                // (the plane brushes leave what is a radius behind; Standard what is 3 radii behind: at the sphere's rim the
                // back is within that, a thin part by design)
                CHECK((t == BrushType::Standard ? r.deepMoved : r.farMoved) == 0.0f);
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

TEST(view_falloff_invariant_additive_brushes_leave_the_back_side) {
    // gh#229 follow-up (0.13.10): Standard with View falloff dented the far side of the model, seen after orbiting. The
    // brushes that add or cut along the normal leave what lies more than 3 brush radii behind the surface under the cursor
    // (the back of the sphere); the back stays, with or without Front faces only. Move / Grab are the control: reaching everything inside the circle is View falloff's purpose.
    for (BrushType t : {BrushType::Standard, BrushType::Inflate, BrushType::Crease, BrushType::Pinch, BrushType::SharpCrease})
        for (bool ffo : {false, true}) {
            Document d = makeDoc(makeIcosphere(6, 1.0f));
            const Result r = strokeDown(d, 0.5f, true, 0.6f, [&](Document& doc, const DabParams& q) {
                DabParams p = q;
                p.frontFacesOnly = ffo;
                doc.sculptor.dab(doc.mesh, doc.bvh, t, p, kX);
            });
            std::printf("  %-10s View falloff, front faces only %d: dab move %.4f, back side moved %.5f\n", nameOf(t), int(ffo),
                        double(r.maxDab), double(r.farMoved));
            CHECK(r.finite);
            CHECK(r.maxDab > 0.0f); // (it still sculpts the front)
            CHECK(r.farMoved == 0.0f);
        }
    // Negative control: Move under View falloff does carry the back side along (that is what View falloff is for), so a
    // zero above is the brush's gate, not a stroke that missed.
    Document d = makeDoc(makeIcosphere(6, 1.0f));
    const std::vector<glm::vec3> start = d.mesh.positions;
    d.sculptor.beginStroke(d.mesh, "Test");
    DabParams gp;
    gp.center = glm::vec3(0.0f, 0.0f, 1.0f);
    gp.viewDir = glm::vec3(0.0f, 0.0f, -1.0f);
    gp.radius = kRadius;
    gp.strength = 0.6f;
    gp.viewFalloff = true;
    d.sculptor.beginGrab(d.mesh, d.bvh, gp, Symmetry{false, false, false}); // (on the mirror axis, X symmetry would cancel the move)
    d.sculptor.grab(d.mesh, d.bvh, glm::vec3(0.1f, 0.0f, 0.0f));
    d.undo.push(d.sculptor.endStroke());
    Result g;
    float frontMoved = 0.0f;
    for (size_t v = 0; v < start.size(); ++v) {
        const float moved = glm::distance(start[v], d.mesh.positions[v]);
        if (start[v].z < -0.3f) g.farMoved = std::max(g.farMoved, moved);
        if (start[v].z > 0.3f) frontMoved = std::max(frontMoved, moved);
    }
    std::printf("  Move       View falloff: front side moved %.5g, back side moved %.5g\n", double(frontMoved), double(g.farMoved));
    CHECK(frontMoved > 0.01f);
    CHECK(g.farMoved > 0.01f);
}

TEST(view_falloff_invariant_standard_edge_of_the_gate_is_smooth) {
    // No step where the brush stops reaching: neighbouring vertices of the stroke move by about the same (a step shows as
    // a ridge).
    Document d = makeDoc(makeIcosphere(6, 1.0f));
    const std::vector<glm::vec3> start = d.mesh.positions;
    strokeDown(d, 0.5f, true, 0.6f, [&](Document& doc, const DabParams& p) {
        doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, p, kX);
    });
    float worst = 0.0f;
    for (const glm::uvec3& t : d.mesh.triangles)
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = t[e], b = t[(e + 1) % 3];
            worst = std::max(worst, std::abs(glm::distance(start[a], d.mesh.positions[a]) - glm::distance(start[b], d.mesh.positions[b])));
        }
    std::printf("  Standard, View falloff: largest move step between neighbours %.5f\n", double(worst));
    CHECK(worst < 0.01f);
}

TEST(view_falloff_invariant_additive_brushes_tear_nothing_on_a_head) {
    // A big brush (a fifth of the head) with View falloff on the side of a head: its underhangs (nose, lips, ears, jaw
    // over the neck) face away from the view but lie right behind the facing surface. Gating them apart from it tore the
    // nose and lips off (artist-qa 11-21): neighbouring vertices must move alike, no edge stretching to a fraction of
    // the dab's own move.
    const StarterMesh* head = findStarterMesh("human-head");
    CHECK(head != nullptr);
    if (!head) return;
    for (BrushType t : {BrushType::Standard, BrushType::Crease, BrushType::Pinch, BrushType::SharpCrease}) {
        Document d;
        std::string error;
        CHECK(loadStarterMesh(*head, d, 2, error));
        glm::vec3 lo(1e9f), hi(-1e9f);
        for (const glm::vec3& q : d.mesh.positions) lo = glm::min(lo, q), hi = glm::max(hi, q);
        const float height = hi.y - lo.y;
        const std::vector<glm::vec3> start = d.mesh.positions;
        d.sculptor.beginStroke(d.mesh, "Test");
        float moved = 0.0f;
        for (int i = 0; i <= 8; ++i) { // across the face from the side (+x looking -x), at nose and lip height
            const glm::vec3 from(hi.x + 1.0f, lo.y + 0.62f * height - 0.05f * height * float(i) / 8.0f,
                                 hi.z - 0.12f * height - 0.1f * height * float(i) / 8.0f);
            RayHit hit;
            if (!d.bvh.raycast(d.mesh, from, glm::vec3(-1.0f, 0.0f, 0.0f), hit)) continue;
            DabParams p;
            p.center = hit.position;
            p.pickNormal = glm::normalize(hit.normal);
            p.viewDir = glm::vec3(-1.0f, 0.0f, 0.0f);
            p.radius = 0.2f * height;
            p.strength = 0.6f;
            p.viewFalloff = true;
            d.sculptor.dab(d.mesh, d.bvh, t, p, Symmetry{false, false, false});
        }
        d.undo.push(d.sculptor.endStroke());
        for (size_t v = 0; v < start.size(); ++v) moved = std::max(moved, glm::distance(start[v], d.mesh.positions[v]));
        float worst = 0.0f;
        for (const glm::uvec3& tri : d.mesh.triangles)
            for (int e = 0; e < 3; ++e) {
                const uint32_t a = tri[e], b = tri[(e + 1) % 3];
                worst = std::max(worst, glm::length((d.mesh.positions[a] - start[a]) - (d.mesh.positions[b] - start[b])));
            }
        std::printf("  %-11s head, View falloff: head height %.2f, largest move %.4f, largest difference between neighbours %.4f\n", nameOf(t),
                    double(height), double(moved), double(worst));
        CHECK(moved > 0.0f);
        CHECK(worst < 0.25f * moved);
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

TEST(view_falloff_invariant_additive_recipe_leaves_the_back_side) {
    // A Brush Lab recipe that builds along the normal (no plane) is an additive brush too.
    BrushRecipe recipe;
    recipe.name = "Build";
    recipe.depth = 0.8f;
    Document d = makeDoc(makeIcosphere(6, 1.0f));
    const Result r = strokeDown(d, 0.5f, true, 0.6f, [&](Document& doc, const DabParams& p) {
        doc.sculptor.dabRecipe(doc.mesh, doc.bvh, recipe, p, kX);
    });
    std::printf("  recipe Build, View falloff: dab move %.4f, back side %.5f\n", double(r.maxDab), double(r.farMoved));
    CHECK(r.finite);
    CHECK(r.maxDab > 0.0f);
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
