// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dynamic + Smooth over a thin Tendril (GitHub #238, nino3thd): smoothing a pulled tendril flat with Dynamic on must not leave a
// hair-thin filament of triangles sticking out of the surface. Measured on a sphere: the part of the mesh that stands out of the
// sphere (vertex count, reach in tendril radii), and the thin sliver triangles in it, after N Smooth strokes along the tendril.
#include "GeomInvariants.h"

#include <cstdio>
#include <fstream>

using namespace sf;
using namespace sf::geom;

namespace {

struct Filament {
    size_t outside = 0;   // vertices standing out of the sphere by more than 5% of its radius
    float reach = 0.0f;   // the highest of them above the sphere, in tendril radii
    size_t tris = 0;      // triangles of the mesh
    size_t sliversOut = 0; // triangles with a vertex outside and an aspect quality under 0.1
    size_t invalid = 0;
    size_t dissolved = 0; // vertices the Smooth dabs' needle pass merged away
    double volume = 0.0;  // enclosed volume
    size_t bad = 0;       // invalid triangles / adjacency problems (invalid())
};

// Closed and consistently wound: every directed edge once, with its reverse once; no triangle with a repeated corner; 0 = fine.
size_t manifoldProblems(const Mesh& m) {
    std::unordered_map<uint64_t, int> directed;
    size_t bad = 0;
    for (const glm::uvec3& t : m.triangles) {
        if (t.x == t.y || t.y == t.z || t.z == t.x) ++bad;
        for (int c = 0; c < 3; ++c) ++directed[(uint64_t(t[c]) << 32) | t[(c + 1) % 3]];
    }
    for (const auto& [e, n] : directed) {
        const uint64_t rev = (e << 32) | (e >> 32);
        auto it = directed.find(rev);
        if (n != 1 || it == directed.end() || it->second != 1) ++bad;
    }
    return bad;
}

// SF_DUMP_STRAND=<dir>: the strand's start and its mesh after the Smooth strokes as OBJ, for a look in the app.
void dumpObj(const Mesh& m, const char* name) {
    const char* dir = std::getenv("SF_DUMP_STRAND");
    if (!dir) return;
    std::ofstream f(std::string(dir) + "/" + name);
    for (const glm::vec3& p : m.positions) f << "v " << p.x << " " << p.y << " " << p.z << "\n";
    for (const glm::uvec3& t : m.triangles) f << "f " << t.x + 1 << " " << t.y + 1 << " " << t.z + 1 << "\n";
}

Filament measure(const Mesh& m, float tendrilRadius) {
    Filament f;
    std::vector<uint8_t> out(m.vertexCount(), 0);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const float h = glm::length(m.positions[v]) - 1.0f;
        if (h > 0.05f) out[v] = 1, ++f.outside, f.reach = std::max(f.reach, h / tendrilRadius);
    }
    f.tris = m.triangleCount();
    for (const glm::uvec3& t : m.triangles) {
        if (!(out[t.x] || out[t.y] || out[t.z])) continue;
        const glm::vec3 a = m.positions[t.x], b = m.positions[t.y], c = m.positions[t.z];
        const float l2 = glm::dot(b - a, b - a) + glm::dot(c - b, c - b) + glm::dot(a - c, a - c);
        const float area = 0.5f * glm::length(glm::cross(b - a, c - a));
        if (l2 <= 0.0f || 4.0f * 1.7320508f * area / l2 < 0.1f) ++f.sliversOut;
    }
    f.invalid = nonFinite(m);
    return f;
}

// Tendril out of the +X side of the sphere (view along -Z), then `passes` Smooth strokes down the tendril, from tip to base.
Filament run(bool dynamic, int passes, float radius, float length, float smoothRadiusScale, Filament* pulled = nullptr, float thin = 1.0f) {
    Mesh sphere = makeIcosphere(4, 1.0f);
    sphere.recomputeAllNormals();
    Document d = makeDoc(std::move(sphere));
    d.sculptor.dyntopo.enabled = dynamic;
    const glm::vec3 axis(1.0f, 0.0f, 0.0f);
    glm::vec3 start(1.0f, 0.0f, 0.0f);
    RayHit hit;
    if (surfaceRaycast(d.mesh, d.bvh, start + glm::vec3(0, 0, 3), glm::vec3(0, 0, -1), hit)) start = hit.position;
    const Symmetry sym{false, false, false};

    {
        d.sculptor.beginStroke(d.mesh, "Tendril");
        DabParams p;
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = radius, p.strength = 1.0f, p.pressure = 1.0f;
        glm::vec3 tip = start;
        const glm::vec3 target = start + axis * (length * radius);
        const int steps = std::max(1, int(std::ceil(glm::length(target - start) / (radius * 0.15f))));
        p.strokeDelta = (target - start) / float(steps);
        for (int i = 0; i < steps; ++i) {
            p.center = tip;
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Tendril, p, sym);
            tip += p.strokeDelta;
        }
        d.undo.push(d.sculptor.endStroke());
    }
    if (thin < 1.0f) {
        // The strand of the report: the pulled tube's cross-section squashed to a hair (its length stays).
        for (glm::vec3& q : d.mesh.positions)
            if (q.x > 1.03f) q.y *= thin, q.z *= thin;
        d.mesh.recomputeAllNormals();
        d.bvh.build(d.mesh);
    }
    if (pulled) *pulled = measure(d.mesh, radius);
    if (thin < 1.0f && passes == 16 && smoothRadiusScale == 1.0f) dumpObj(d.mesh, "strand_start.obj");

    // Smooth strokes the way the app places dabs (every 0.15 of the radius), from the tip back to the base, `passes` times.
    const float sr = radius * smoothRadiusScale;
    for (int pass = 0; pass < passes; ++pass) {
        d.sculptor.beginStroke(d.mesh, "Smooth");
        DabParams p;
        p.viewDir = glm::vec3(0, 0, -1);
        p.radius = sr, p.strength = 1.0f, p.pressure = 1.0f;
        const float x0 = glm::length(start) + length * radius, x1 = 0.9f;
        const int steps = std::max(1, int((x0 - x1) / (sr * 0.15f)));
        p.strokeDelta = glm::vec3(-(x0 - x1) / float(steps), 0.0f, 0.0f);
        for (int i = 0; i <= steps; ++i) {
            p.center = glm::vec3(x0 + p.strokeDelta.x * float(i), 0.0f, 0.0f);
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, sym);
        }
        d.undo.push(d.sculptor.endStroke());
    }
    if (thin < 1.0f && passes == 16 && smoothRadiusScale == 1.0f) dumpObj(d.mesh, thin < 0.01f ? "strand_smoothed_thin.obj" : "strand_smoothed.obj");
    Filament f = measure(d.mesh, radius);
    f.volume = meshVolume(d.mesh);
    f.dissolved = d.sculptor.dynamicTopology().stats.dissolved;
    f.bad = manifoldProblems(d.mesh);
    return f;
}

} // namespace

// A hair-thin strand (the pulled tube squashed to 5% and 0.5% of its width) is gone after Smooth strokes along it with
// Dynamic on: no vertex stands out of the sphere, no sliver triangle is left, the mesh stays valid (before: 330-450 vertices
// and 620-860 sliver triangles still out after 16 strokes, the strand shrinking by under one radius).
TEST(dyn_smooth_invariant_dissolves_a_hair_thin_strand) {
    const double base = meshVolume(makeIcosphere(4, 1.0f));
    for (float thin : {0.05f, 0.005f})
        for (float scale : {1.0f, 2.0f})
            for (int passes : {1, 4, 16}) {
                Filament pulled;
                const Filament f = run(true, passes, 0.15f, 8.0f, scale, &pulled, thin);
                std::printf("  thin %.3f smooth x%.1f passes %2d: before %4zu out | after %4zu out, reach %.2f, slivers %zu, %zu tris, %zu dissolved\n", thin,
                            scale, passes, pulled.outside, f.outside, f.reach, f.sliversOut, f.tris, f.dissolved);
                CHECK(f.bad == 0);
                CHECK(f.invalid == 0);
                if (passes == 16) {
                    CHECK(f.sliversOut == 0);
                    CHECK(f.outside == 0);
                    CHECK(std::abs(volumeChange(base, f.volume)) < 0.01); // smoothing keeps the sphere's volume
                }
            }
}

// The pass acts only on vertices whose every triangle is a needle (height under 5% of the target edge): Smooth with Dynamic on a
// sphere, a torus, a capsule and a quad sphere (even triangles, curved), at several Details and brush sizes, merges nothing away,
// so the strokes are the ones without the pass, byte for byte. (One exception, by design: at the finest Detail on a 24-segment
// Part, 24 times coarser than the target, the splits leave a few flat fans of zero-area triangles; those few vertices are merged.)
TEST(dyn_smooth_invariant_leaves_solid_forms_alone) {
    struct Shape { const char* name; Mesh mesh; };
    const Shape shapes[] = {{"icosphere4", makeIcosphere(4)}, {"torus", makeTorus(48)}, {"capsule", makeCapsule(32)}, {"quadsphere", makeQuadSphere(24)}};
    for (const Shape& sh : shapes)
        for (float detail : {0.0f, 0.5f, 1.0f})
            for (float radius : {0.15f, 0.4f}) {
                Mesh mesh = sh.mesh;
                mesh.recomputeAllNormals();
                Document d = makeDoc(std::move(mesh));
                d.sculptor.dyntopo.enabled = true;
                d.sculptor.dyntopo.detail = detail;
                d.sculptor.beginStroke(d.mesh, "Smooth");
                DabParams p;
                p.viewDir = glm::vec3(0, 0, -1);
                p.radius = radius, p.strength = 1.0f, p.pressure = 1.0f;
                for (int i = 0; i < 40; ++i) {
                    p.center = glm::vec3(-0.5f + 0.025f * float(i), 0.1f * std::sin(0.3f * float(i)), 1.5f);
                    RayHit hit;
                    if (!surfaceRaycast(d.mesh, d.bvh, p.center, glm::vec3(0, 0, -1), hit)) continue;
                    p.center = hit.position;
                    d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, Symmetry{false, false, false});
                }
                d.undo.push(d.sculptor.endStroke());
                const size_t dissolved = d.sculptor.dynamicTopology().stats.dissolved;
                std::printf("  %-10s detail %.1f r %.2f: %zu dissolved, %zu tris\n", sh.name, detail, radius, dissolved, d.mesh.triangleCount());
                CHECK(dissolved == 0 || (detail >= 1.0f && dissolved * 1000 < d.mesh.triangleCount()));
                if (sh.name[0] == 'i') CHECK(dissolved == 0);
                CHECK(manifoldProblems(d.mesh) == 0);
            }
}
