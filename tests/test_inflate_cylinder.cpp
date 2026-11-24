// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inflate on the stock Cylinder Part (member report on 0.13.10: strokes bent the tube into S-shapes and lumps instead of
// swelling it evenly). The promise: what a dab moves goes out along each vertex's own normal, with or without X symmetry, the
// circle on the tube or half off it, a brush as wide as the tube or about 3 radii wider. On 0.13.10 the packed-grid fallback carried the
// whole dab along one area normal (share along the own normals 0.07 to 0.5 at brushes of 2.7 radii and more).
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/Commands.h"
#include "core/Sculpt.h"
#include "core/StarterMeshes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace sf;
using glm::vec3;

namespace {
// Of everything the dab stroke moved: the share (by distance) that went along the vertex's own pre-dab normal, 1 = all of it.
float shareAlongNormals(const Mesh& start, float brushOverR, float angle, bool sym) {
    Document d;
    d.setMesh(Mesh(start), "Sculpt", nullptr);
    const AABB b = start.bounds();
    const float R = 0.5f * std::min(b.extent().x, b.extent().z), h = b.extent().y;
    double out = 0.0, total = 0.0;
    CHECK(d.sculptor.beginStroke(d.mesh, "Inflate"));
    const float step = std::max(0.15f * brushOverR * R, h / 60.0f);
    for (float y = -0.4f * h; y <= 0.4f * h; y += step) {
        const std::vector<vec3> before = d.mesh.positions, normals = d.mesh.normals;
        DabParams p;
        p.center = b.center() + vec3(R * std::sin(angle), y, R * std::cos(angle)); // (angle 0: facing the view; near pi/2: on the silhouette)
        p.viewDir = vec3(0, 0, -1);
        p.radius = brushOverR * R;
        p.strength = 0.25f;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Inflate, p, Symmetry{sym, false, false});
        for (uint32_t v = 0; v < before.size(); ++v) {
            const vec3 move = d.mesh.positions[v] - before[v];
            const float len = glm::length(move);
            if (len < 1e-7f) continue;
            out += double(glm::dot(move / len, glm::normalize(normals[v]))) * len;
            total += len;
        }
    }
    d.undo.push(d.sculptor.endStroke());
    return total > 0.0 ? float(out / total) : 0.0f;
}
} // namespace

TEST(inflate_stock_cylinder_invariant_moves_along_own_normals) {
    const StarterMesh* starter = findStarterMesh("cylinder");
    CHECK(starter != nullptr);
    // The stock Cylinder at 1 and 2 subdivision levels (12k and 49k triangles), as it comes and as the member had it: pulled
    // long and thin (a tube about 4 diameters long) with a brush 2.7 radii wide (253 mm across on a 93 mm tube).
    for (const int levels : {1, 2})
        for (const bool stretched : {false, true}) {
            Document d0;
            std::string error;
            CHECK(loadStarterMesh(*starter, d0, levels, error));
            Mesh m = d0.mesh;
            if (stretched) {
                for (vec3& p : m.positions) p *= vec3(0.47f, 1.75f, 0.47f);
                m.recomputeAllNormals();
            }
            for (const bool sym : {false, true})
                for (const float angle : {0.0f, 1.5f})
                    for (const float brush : {1.0f, 2.7f}) {
                        const float share = shareAlongNormals(m, brush, angle, sym);
                        std::printf("  cylinder %zu tris%s, X symmetry %d, circle %s, brush %.1f R: %.3f along the own normals\n",
                                    m.triangleCount(), stretched ? " stretched" : "", int(sym), angle > 1.0f ? "half off" : "on", brush, share);
                        CHECK(share > 0.99f);
                    }
        }
}

namespace {
// A stroke of dabs along the horizontal line across the tube's front at height y (x from -0.8 R to 0.8 R), each on the surface under it.
void acrossStroke(Document& d, BrushType type, const char* name, float y, float R, float radius, float strength) {
    CHECK(d.sculptor.beginStroke(d.mesh, name));
    const float step = std::max(0.15f * radius, 0.02f * R);
    for (float x = -0.8f * R; x <= 0.8f * R; x += step) {
        DabParams p;
        p.center = vec3(x, y, 2.0f * R);
        RayHit hit;
        if (d.bvh.raycast(d.mesh, p.center, vec3(0, 0, -1), hit)) {
            p.center = hit.position;
            p.pickNormal = hit.normal;
        }
        p.viewDir = vec3(0, 0, -1);
        p.radius = radius;
        p.strength = strength;
        d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{false, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
}
} // namespace

TEST(inflate_stock_cylinder_groove_stays_whole) {
    // The #194 groove on a tube (artist-qa): Crease a groove across the stock Cylinder, then Inflate wider than the tube across it.
    // The groove's packed rows must carry along together: no triangle turns over, no needle or open rip along the groove.
    const StarterMesh* starter = findStarterMesh("cylinder");
    CHECK(starter != nullptr);
    for (const int levels : {1, 2})
        for (const float brush : {1.0f, 2.0f, 5.0f}) {
            Document d0;
            std::string error;
            CHECK(loadStarterMesh(*starter, d0, levels, error));
            Mesh m = d0.mesh;
            for (vec3& p : m.positions) p *= vec3(0.47f, 1.75f, 0.47f);
            m.recomputeAllNormals();
            Document d;
            d.setMesh(std::move(m), "Sculpt", nullptr);
            const AABB b = d.mesh.bounds();
            const float R = 0.5f * std::min(b.extent().x, b.extent().z);
            for (int i = 0; i < 4; ++i) acrossStroke(d, BrushType::Crease, "Crease", 0.0f, R, 0.15f * R, 0.5f);
            const std::vector<vec3> grooved = d.mesh.positions;
            for (int i = 0; i < 3; ++i) acrossStroke(d, BrushType::Inflate, "Inflate", 0.0f, R, brush * R, 0.5f);
            if (brush == 5.0f) // (artist-qa's stacked case: a narrower pass first, the wide one then crosses the swollen groove)
                for (int i = 0; i < 2; ++i) acrossStroke(d, BrushType::Inflate, "Inflate", 0.0f, R, (i ? 5.0f : 2.0f) * R, i ? 1.0f : 0.5f);
            const size_t flipped = geom::flippedFaces(grooved, d.mesh), inverted = geom::invertedFaces(grooved, d.mesh);
            const size_t folded = geom::foldedFaces(grooved, d.mesh);
            std::printf("  groove on the stretched cylinder (%zu tris), Inflate %.1f R: %zu flipped, %zu inverted, %zu folded\n",
                        d.mesh.triangleCount(), brush, flipped, inverted, folded);
            CHECK(flipped == 0); // (a rule that turns the one-direction fallback off for a dab wrapped round the tube tore it: 686 flipped at 5 R)
            CHECK(inverted == 0);
            CHECK(folded == 0);
        }
}
