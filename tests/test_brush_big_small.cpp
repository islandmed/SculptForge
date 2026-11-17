// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A brush much bigger or much smaller than the Part it works on (tester report, 0.13.3: a fresh 82k-triangle sphere, Symmetry
// on): bigger than the sphere, Inflate pulled shards, Move a spiky star, Sharp Crease long wings, Clay a rocky crumple, Trim
// a crater; smaller, "the sphere is barely touched". Measured on the sphere at 0.05x .. 4x its radius, Symmetry X on and off.
// Before the fix (0.13.3), on the 82k sphere with Symmetry X: after 4 strokes at 4x Clay's area normal pointed away from the
// surface (dot -0.99), Clay / Crease / Trim flipped 16k-38k triangles; one Trim stroke at 4x flipped 7,194 (118,242 on 1.3M).
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/Benchmark.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace sf;

namespace {

constexpr Symmetry kNone{false, false, false};
constexpr Symmetry kX{true, false, false};

struct Brush {
    const char* name;
    BrushType type;
};
const Brush kBrushes[] = {
    {"Standard", BrushType::Standard}, {"Clay", BrushType::Clay},           {"Inflate", BrushType::Inflate},
    {"Crease", BrushType::Crease},     {"SharpCrease", BrushType::SharpCrease}, {"Trim", BrushType::Trim},
    {"Flatten", BrushType::Flatten},   {"Pinch", BrushType::Pinch},
};

const Mesh& sphere1m() {
    static const Mesh m = makeIcosphere(8); // 1.31M triangles: a big model
    return m;
}
const Mesh& sphere82k() {
    static const Mesh m = makeIcosphere(6); // 81,920 triangles, radius 1 (the tester's fresh sphere: 81.9k)
    return m;
}

struct Result {
    float maxMove = 0.0f;    // the largest vertex move, in sphere radii
    float meanFront = 0.0f;  // mean move of the vertices within one brush radius of the stroke's first dab, in radii
    size_t flipped = 0, inverted = 0, folded = 0, nonFinite = 0;
    float minNormalDot = 1.0f; // the worst dab's area normal against the surface normal under its centre
    float flips = 0.0f;        // area normal turned > 90 degrees from the previous dab's
    float symError = 0.0f;
};

// One stroke over the front of the sphere: dabs along an arc through the front pole, 0.1 of the radius apart (at least 0.12 rad:
// a big brush's stroke is a few dabs, as on screen), viewed straight on.
Result stroke(const Brush& b, float radiusK, Symmetry sym, float strength = 0.5f, bool front = false, int reps = 1, bool big = false) {
    Document d = makeDoc(big ? sphere1m() : sphere82k());
    const std::vector<glm::vec3> before = d.mesh.positions;
    const std::vector<glm::vec3> nrm0 = d.mesh.normals;
    const float step = std::max(0.12f, 0.1f * radiusK);
    const int count = std::clamp(int(1.2f / step) + 1, 3, 12);
    std::vector<DabParams> dabs = geom::arc(glm::normalize(glm::vec3(-0.5f, 0.2f, 1.0f)), glm::vec3(0.5f, 0.2f, 1.0f), count, step * 0.6f,
                                            radiusK, strength, 1.0f, 1.0f);
    for (DabParams& p : dabs) p.viewDir = glm::vec3(0, 0, -1), p.frontFacesOnly = front;
    glm::vec3 prevN(0.0f);
    Result r;
    for (int rep = 0; rep < reps; ++rep) {
    d.sculptor.beginStroke(d.mesh, b.name);
    for (DabParams p : dabs) {
        p.center = glm::normalize(p.center + glm::vec3(0.0f, 0.15f * float(rep % 3 - 1), 0.0f));
        d.sculptor.dab(d.mesh, d.bvh, b.type, p, sym);
        const glm::vec3 n = d.sculptor.lastFrame.normal;
        r.minNormalDot = std::min(r.minNormalDot, glm::dot(n, glm::normalize(p.center)));
        if (glm::length(prevN) > 0.0f && glm::dot(n, prevN) < 0.0f) r.flips += 1.0f;
        prevN = n;
    }
    d.undo.push(d.sculptor.endStroke());
    }
    double sum = 0.0;
    size_t n = 0;
    for (size_t v = 0; v < before.size(); ++v) {
        const float m = glm::distance(d.mesh.positions[v], before[v]);
        r.maxMove = std::max(r.maxMove, m);
        if (glm::distance(before[v], dabs[0].center) < radiusK) sum += m, ++n;
    }
    r.meanFront = n ? float(sum / double(n)) : 0.0f;
    r.flipped = geom::flippedFaces(before, d.mesh);
    r.inverted = geom::invertedFaces(before, d.mesh);
    r.folded = geom::foldedFaces(before, d.mesh);
    r.nonFinite = geom::nonFinite(d.mesh);
    return r;
}

const Brush* find(const char* name) {
    for (const Brush& b : kBrushes)
        if (std::string(b.name) == name) return &b;
    return nullptr;
}

} // namespace

// A brush the size of the Part or bigger reaches both its sides: the dab's plane comes from what faces the camera, so one stroke
// leaves a sound surface (nothing non-finite, almost no flipped triangle) and the plane never turns away from the surface.
TEST(brush_as_big_as_the_part_or_bigger_leaves_a_sound_surface_with_symmetry_on_and_off) {
    for (Symmetry sym : {kNone, kX})
        for (const Brush& b : kBrushes)
            for (float k : {1.0f, 2.0f, 4.0f}) {
                const Result r = stroke(b, k, sym);
                const size_t tris = sphere82k().triangleCount();
                std::printf("  %-11s %.0fx sym=%d: max move %.2f R, flipped %zu, inverted %zu, normal dot %.2f\n", b.name, k, int(sym.x),
                            r.maxMove, r.flipped, r.inverted, r.minNormalDot);
                CHECK(r.nonFinite == 0);
                CHECK(r.flipped * 200 <= tris); // (0.5%: Trim at 4x flipped 8.8% before)
                CHECK(r.inverted * 1000 <= tris);
                CHECK(r.minNormalDot > 0.5f);
            }
}

// Strokes laid over each other with a brush 2-4x the Part: the plane never turns away from the surface (Clay's was at -0.99 by the
// fourth stroke) and the form is not thrown out of the scene.
TEST(brush_much_bigger_than_the_part_keeps_its_plane_on_the_surface_over_several_strokes) {
    for (const char* name : {"Standard", "Clay", "Inflate", "Trim"})
        for (float k : {2.0f, 4.0f}) {
            const Result r = stroke(*find(name), k, kX, 0.5f, false, 4);
            std::printf("  %-8s %.0fx, 4 strokes: max move %.2f R, normal dot %.2f\n", name, k, r.maxMove, r.minNormalDot);
            CHECK(r.nonFinite == 0);
            CHECK(r.minNormalDot > 0.3f);
            CHECK(r.maxMove < 3.0f);
        }
}

// Depth follows the Part, not a brush far bigger than it: a stroke of 4x moves the form about as much as one of 2x.
TEST(brush_depth_does_not_grow_past_the_parts_own_size) {
    for (const char* name : {"Standard", "Inflate"}) {
        const float one = stroke(*find(name), 1.0f, kNone).maxMove, four = stroke(*find(name), 4.0f, kNone).maxMove;
        std::printf("  %-8s max move 1x %.3f R, 4x %.3f R\n", name, one, four);
        CHECK(four < 2.0f * one);
    }
}

// A smaller brush still changes the surface visibly: one stroke at 5-100% of the Part's size at the default Intensity lifts or sinks
// it by at least 1.5% of the brush's radius (Flatten and Trim cut only what stands above a plane: a sphere has little).
TEST(brush_smaller_than_the_part_still_makes_a_visible_change_in_one_stroke) {
    for (Symmetry sym : {kNone, kX})
        for (const char* name : {"Standard", "Clay", "Inflate", "Crease", "SharpCrease", "Pinch"})
            for (float k : {0.05f, 0.1f, 0.25f, 0.5f, 1.0f}) {
                const Result r = stroke(*find(name), k, sym);
                CHECK(r.maxMove >= 0.015f * k);
                CHECK(r.flipped == 0);
            }
}

// The same on a big model (1.3M triangles): Trim at 4x cut a crater (118k flipped triangles) and every brush stays sound.
TEST(brush_bigger_than_a_big_model_leaves_a_sound_surface) {
    for (const Brush& b : kBrushes)
        for (float k : {1.0f, 4.0f}) {
            const Result r = stroke(b, k, kX, 0.5f, false, 1, true);
            const size_t tris = sphere1m().triangleCount();
            std::printf("  1.3M %-11s %.0fx: max move %.2f R, flipped %zu, normal dot %.2f\n", b.name, k, r.maxMove, r.flipped, r.minNormalDot);
            CHECK(r.nonFinite == 0);
            CHECK(r.flipped * 200 <= tris);
            CHECK(r.minNormalDot > 0.5f);
        }
}

// Move with a brush bigger than the Part and Symmetry X: the copies' regions overlap across the mirror plane, and each vertex
// there was moved by both (Move pulled a sphere into a spiky cone). It goes no farther than the pointer did, and the surface stays whole.
TEST(move_brush_bigger_than_the_part_with_symmetry_does_not_move_a_vertex_past_the_pointer) {
    for (float k : {2.0f, 4.0f}) {
        Document d = makeDoc(sphere82k());
        const std::vector<glm::vec3> before = d.mesh.positions;
        DabParams p;
        p.center = glm::vec3(0.3f, 0.0f, 1.0f);
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = k;
        p.strength = 0.5f;
        d.sculptor.beginStroke(d.mesh, "Move");
        d.sculptor.beginGrab(d.mesh, d.bvh, p, kX);
        for (int i = 0; i < 4; ++i) d.sculptor.grab(d.mesh, d.bvh, glm::vec3(0.0f, 0.3f, 0.0f)); // (up, 1.2 R in all)
        d.undo.push(d.sculptor.endStroke());
        const float moved = geom::maxMove(before, d.mesh);
        std::printf("  Move %.0fx sym X: max move %.2f R (pointer 1.2), flipped %zu\n", k, moved, geom::flippedFaces(before, d.mesh));
        CHECK(moved <= 1.2f * 1.01f);
        CHECK(geom::nonFinite(d.mesh) == 0);
        CHECK(geom::flippedFaces(before, d.mesh) * 200 <= d.mesh.triangleCount());
    }
}

// An organic model, not a ball: the 3M-triangle bench head, brushes 2x its size, Symmetry X, the fast paths on as in the app (adaptive
// dabs: the plane from a sample of the region).
TEST(brush_bigger_than_a_three_million_triangle_head_leaves_a_sound_surface) {
    static const Mesh head = makeBenchHead(3'000'000);
    const AABB box = head.bounds();
    const glm::vec3 ext = box.max - box.min;
    const float reach = 0.5f * std::max({ext.x, ext.y, ext.z}), mid = 0.5f * (box.min.z + box.max.z);
    for (const Brush& b : kBrushes)
        for (float k : {1.0f, 2.0f}) {
            Document d = makeDoc(head);
            d.sculptor.adaptiveDabs = true;
            const std::vector<glm::vec3> before = d.mesh.positions;
            d.sculptor.beginStroke(d.mesh, b.name);
            float minDot = 1.0f;
            for (int i = 0; i < 5; ++i) {
                DabParams p;
                p.center = glm::vec3(-0.4f * reach + 0.2f * reach * float(i), box.min.y + 0.55f * ext.y, box.max.z);
                p.viewDir = p.pickDir = glm::vec3(0, 0, -1);
                if (RayHit hit; d.bvh.raycast(d.mesh, glm::vec3(p.center.x, p.center.y, mid + 4.0f * reach), glm::vec3(0, 0, -1), hit)) {
                    p.center = hit.position;
                    p.pickNormal = glm::normalize(hit.normal);
                }
                p.radius = k * reach;
                p.strength = 0.5f;
                p.strokeDelta = glm::vec3(0.2f * reach, 0, 0);
                d.sculptor.dab(d.mesh, d.bvh, b.type, p, kX);
                minDot = std::min(minDot, glm::dot(d.sculptor.lastFrame.normal, glm::vec3(0, 0, 1)));
            }
            d.undo.push(d.sculptor.endStroke());
            const size_t tris = d.mesh.triangleCount(), flipped = geom::flippedFaces(before, d.mesh);
            std::printf("  head %-11s %.0fx: max move %.2f R, flipped %zu of %zu, front plane dot %.2f\n", b.name, k,
                        geom::maxMove(before, d.mesh) / reach, flipped, tris, minDot);
            CHECK(geom::nonFinite(d.mesh) == 0);
            CHECK(flipped * 200 <= tris);
            CHECK(minDot > 0.3f);
        }
}
