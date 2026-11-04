// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brush engine fixes for 0.10.9: Clay follows Intensity and pressure (#83), alphas on micro levels (#92), strokes
// over a silhouette (#84), Smooth along face group borders (#81) and the colour tinting the material.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BrushAlpha.h"
#include "core/Micro.h"
#include "core/Polypaint.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/Stroke.h"

#include <algorithm>
#include <cmath>

using namespace sf;

namespace {

constexpr Symmetry kNone{false, false, false};

// The highest point of the plane (its normal is +Y) after one stroke of `dabs` dabs along X.
float clayStrokeHeight(BrushType type, float strength, float pressure, int dabs = 24) {
    Document d = makeDoc(makePlane(64, 2.0f));
    d.sculptor.beginStroke(d.mesh, brushName(type));
    for (int i = 0; i < dabs; ++i) {
        DabParams p;
        // On the surface under the cursor, as the app picks it (the slab rises under the stroke).
        p.center = glm::vec3(-0.3f + 0.6f * float(i) / float(dabs - 1), 0.0f, 0.0f);
        RayHit hit;
        if (d.bvh.raycast(d.mesh, p.center + glm::vec3(0, 5, 0), glm::vec3(0, -1, 0), hit)) p.center = hit.position;
        p.viewDir = glm::vec3(0, -1, 0);
        p.radius = 0.25f;
        p.strength = strength;
        p.pressure = pressure;
        for (int k = 0; k < 3; ++k) d.sculptor.dab(d.mesh, d.bvh, type, p, kNone); // (a slow stroke: dabs overlap)
    }
    d.undo.push(d.sculptor.endStroke());
    float top = 0.0f;
    for (const glm::vec3& x : d.mesh.positions) top = std::max(top, x.y);
    return top;
}

float microFront(const Document& doc, float x, float y) {
    RayHit hit;
    CHECK(microRaycast(doc.mesh, doc.bvh, glm::vec3(x, y, 5.0f), glm::vec3(0, 0, -1), hit));
    return glm::length(hit.position);
}

} // namespace

TEST(clay_follows_intensity_and_pressure) {
    for (BrushType type : {BrushType::Clay, BrushType::ClayBuildup}) {
        const float i10 = clayStrokeHeight(type, 0.1f, 1.0f), i50 = clayStrokeHeight(type, 0.5f, 1.0f),
                    i100 = clayStrokeHeight(type, 1.0f, 1.0f);
        CHECK(i10 > 0.0f);
        CHECK(i10 < 0.5f * i50); // (before #83 a light stroke planed nearly as high as a full one)
        CHECK(i50 < 0.8f * i100);
        const float light = clayStrokeHeight(type, 0.5f, 0.2f), full = clayStrokeHeight(type, 0.5f, 1.0f);
        CHECK(light > 0.0f);
        CHECK(light < 0.5f * full);
    }
    // The pull follows the strength below the default Intensity and is whole from it up.
    CHECK(clayPull(0.0f) == 0.0f && clayPull(0.25f) == 0.5f && clayPull(0.5f) == 1.0f && clayPull(1.0f) == 1.0f);
}

TEST(clay_follows_intensity_on_micro_levels) {
    float heights[3];
    const float strengths[3] = {0.25f, 0.5f, 1.0f}; // (on a sphere a weak Clay fills the sides first)
    for (int k = 0; k < 3; ++k) {
        Document doc = makeDoc(makeBenchSphere(20000));
        std::string err;
        CHECK(microDivide(doc.mesh, 3, err));
        const float h0 = microFront(doc, 0.001f, 0.002f);
        DabParams p;
        p.center = glm::vec3(0, 0, 1);
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = 0.3f;
        p.strength = strengths[k];
        CHECK(doc.sculptor.beginStroke(doc.mesh, "Clay"));
        for (int i = 0; i < 20; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Clay, p, kNone);
        doc.undo.push(doc.sculptor.endStroke());
        heights[k] = microFront(doc, 0.001f, 0.002f) - h0;
    }
    CHECK(heights[0] < 0.5f * heights[1]);
    CHECK(heights[1] < heights[2]);
}

// #92: a stamp (Drag Stamp or any stroke) keeps its alpha on a micro level instead of falling back to round.
TEST(micro_dabs_use_the_alpha) {
    BrushAlpha square = BrushAlpha::square();
    float corner[2], centre[2];
    for (int k = 0; k < 2; ++k) {
        Document doc = makeDoc(makeBenchSphere(20000));
        std::string err;
        CHECK(microDivide(doc.mesh, 3, err));
        const float c0 = microFront(doc, 0.25f, 0.25f), m0 = microFront(doc, 0.001f, 0.002f);
        DabParams p;
        p.center = glm::vec3(0, 0, 1);
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = 0.3f;
        p.strength = 0.5f;
        p.focal = -1.0f; // (Focal Shift -100: the square shows hard to its corners)
        p.alpha = k == 1 ? &square : nullptr;
        CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
        for (int i = 0; i < 8; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, p, kNone);
        doc.undo.push(doc.sculptor.endStroke());
        corner[k] = microFront(doc, 0.25f, 0.25f) - c0;
        centre[k] = microFront(doc, 0.001f, 0.002f) - m0;
    }
    // Round leaves the square's corner (beyond the radius) alone; the square raises it about as much as the centre.
    CHECK(centre[0] > 0.005f && centre[1] > 0.005f);
    CHECK(std::abs(corner[0]) < 0.1f * centre[0]);
    CHECK(corner[1] > 0.5f * centre[1]);
}

// #84: a stroke sliding over the edge of a form seen in profile keeps sculpting while the brush still covers it.
TEST(strokes_catch_the_surface_at_a_silhouette) {
    Document doc = makeDoc(makeBenchSphere(20000)); // (radius 1 around the origin)
    // A screen of 100 px per unit, looking down -Z.
    int casts = 0;
    auto cast = [&](float x, float y, RayHit& hit) {
        ++casts;
        return doc.bvh.raycast(doc.mesh, glm::vec3(x / 100.0f, y / 100.0f, 5.0f), glm::vec3(0, 0, -1), hit);
    };
    RayHit hit;
    CHECK(pickNearCursor(50.0f, 0.0f, 20.0f, cast, hit)); // on the surface: the plain ray, once
    CHECK(casts == 1 && std::abs(hit.position.x - 0.5f) < 1e-4f);
    // Just off the edge, the brush still over it: the dab lands on the silhouette.
    CHECK(pickNearCursor(108.0f, 3.0f, 20.0f, cast, hit));
    CHECK(glm::length(glm::vec2(hit.position)) > 0.97f);
    CHECK(glm::distance(glm::vec2(hit.position), glm::vec2(1.08f, 0.03f)) < 0.12f);
    // Further out than the brush reaches: nothing (the stroke pauses, as before).
    CHECK(!pickNearCursor(125.0f, 0.0f, 20.0f, cast, hit));

    // A stroke across the profile from inside to outside: every dab within a radius of the edge lands.
    int plain = 0, caught = 0;
    for (int i = 0; i <= 20; ++i) {
        const float x = 90.0f + float(i); // 0.90 .. 1.10
        RayHit h;
        if (cast(x, 0.0f, h)) ++plain;
        if (pickNearCursor(x, 0.0f, 15.0f, cast, h)) ++caught;
    }
    CHECK(caught == 21);
    CHECK(plain < caught);
}

// #81: Smooth > face group borders only straightens a jagged border and leaves everything else where it was.
TEST(smooth_straightens_face_group_borders_only) {
    Document d = makeDoc(makePlane(64, 2.0f));
    Mesh& m = d.mesh;
    // A staircase border: group 1 above z = 0 or z = 0.0625, alternating every eighth of a unit along X.
    std::vector<uint32_t>& groups = m.ensurePolygroups();
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tr = m.triangles[t];
        const glm::vec3 c = (m.positions[tr.x] + m.positions[tr.y] + m.positions[tr.z]) / 3.0f;
        const float step = (int(std::floor(c.x * 8.0f)) & 1) ? 0.0625f : 0.0f;
        groups[t] = c.z > step ? 1u : 0u;
    }
    auto borderLength = [&]() {
        double len = 0.0;
        for (uint32_t v = 0; v < m.vertexCount(); ++v) {
            if (std::abs(m.positions[v].x) > 0.3f) continue;
            uint32_t nb[3];
            const int c = groupBorderNeighbours(m, v, nb);
            for (int k = 0; k < std::min(c, 2); ++k)
                if (nb[k] > v && std::abs(m.positions[nb[k]].x) <= 0.3f) len += glm::distance(m.positions[v], m.positions[nb[k]]);
        }
        return len;
    };
    // On the plane every border vertex has two border neighbours, an inner vertex none.
    uint32_t nb[3];
    CHECK(groupBorderNeighbours(m, 0, nb) == 0);
    const double before = borderLength();
    const std::vector<glm::vec3> start = m.positions;

    DabParams p;
    p.center = glm::vec3(0.0f, 0.0f, 0.03f);
    p.viewDir = glm::vec3(0, -1, 0);
    p.radius = 0.5f;
    p.strength = 1.0f;
    p.groupBorders = true;
    d.sculptor.beginStroke(m, "Smooth");
    for (int i = 0; i < 40; ++i) d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, kNone);
    d.undo.push(d.sculptor.endStroke());

    CHECK(borderLength() < 0.9 * before); // straighter
    int movedBorder = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        CHECK(std::abs(m.positions[v].y) < 1e-5f); // along the surface (its tangent plane), never off it
        if (m.positions[v] == start[v]) continue;
        CHECK(groupBorderNeighbours(m, v, nb) == 2); // only border vertices move
        ++movedBorder;
    }
    CHECK(movedBorder > 10);
    // Undo puts it all back.
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(m.positions == start);
}

TEST(colour_tints_the_material) {
    const glm::vec3 grey = materialTintFor(glm::vec3(0.22f)), black = materialTintFor(glm::vec3(0.0f));
    CHECK(grey == glm::vec3(1.0f) && black == glm::vec3(1.0f)); // the default colour leaves the material alone
    const glm::vec3 red = materialTintFor(glm::vec3(0.6f, 0.06f, 0.03f));
    CHECK(red.x == 1.0f && red.y < 0.2f && red.z < 0.2f);
    const glm::vec3 white = materialTintFor(glm::vec3(1.0f));
    CHECK(white == glm::vec3(1.0f));
}
