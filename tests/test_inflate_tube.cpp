// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inflate on a coarse quad tube (member report, #member-bug-reports): the brush wider than the tube must thicken it
// evenly, each vertex along its own normal, never carry a whole section sideways as one lump.
#include "TestHarness.h"
#include "GeomInvariants.h"

#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/Tube.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

using namespace sf;
using glm::vec3;

namespace {
// A coarse quad tube along x (about 1.2k triangles), the sweep a Tube Part shows.
Mesh coarseTube(float length, float radius) {
    TubeShape s;
    s.points = {{vec3(0, 0, 0), radius}, {vec3(length * 0.5f, 0, 0), radius}, {vec3(length, 0, 0), radius}};
    s.edge = 0.13f;
    return tubeMesh(s);
}
struct Section {
    float x, shift, rMin, rMax, rMean;
};
// Each ring of vertices (same start x): how far its centre left the axis, and its radii about the centre.
std::vector<Section> sections(const Mesh& m, const Mesh& start) {
    std::map<int, std::vector<uint32_t>> rings;
    for (uint32_t v = 0; v < start.vertexCount(); ++v) rings[int(std::lround(start.positions[v].x * 1000.0f))].push_back(v);
    std::vector<Section> out;
    for (auto& [key, vs] : rings) {
        if (vs.size() < 8) continue; // (the caps' tips)
        vec3 c(0.0f);
        for (uint32_t v : vs) c += m.positions[v];
        c /= float(vs.size());
        Section s{float(key) / 1000.0f, std::hypot(c.y, c.z), 1e9f, 0.0f, 0.0f};
        for (uint32_t v : vs) {
            const float r = std::hypot(m.positions[v].y - c.y, m.positions[v].z - c.z);
            s.rMin = std::min(s.rMin, r);
            s.rMax = std::max(s.rMax, r);
            s.rMean += r / float(vs.size());
        }
        out.push_back(s);
    }
    return out;
}
} // namespace

TEST(inflate_coarse_tube_invariant_each_vertex_along_its_own_normal) {
    // Rings about 2:1 (0.1 round, 0.2 along) is just a coarse tube: no packed grid, so no dab carries its vertices along
    // one area normal. Brushes narrower and wider than the tube, a stroke of overlapping dabs along it.
    const float radius = 0.25f;
    const Mesh start = coarseTube(6.0f, radius);
    std::printf("coarse tube: %zu vertices, %zu triangles\n", start.vertexCount(), start.triangleCount());
    for (float brush : {0.15f, 0.4f, 0.8f, 1.5f}) {
        Document d = makeDoc(start);
        CHECK(d.sculptor.beginStroke(d.mesh, "Inflate"));
        float worstTurn = 1.0f; // the least cosine between a vertex's move and its own normal before the dab
        size_t moved = 0;
        for (int i = 0; i <= 30; ++i) {
            const std::vector<vec3> before = d.mesh.positions, normals = d.mesh.normals;
            DabParams p;
            p.center = vec3(1.0f + 0.15f * float(i), 0.0f, radius);
            p.viewDir = vec3(0, 0, -1);
            p.radius = brush;
            p.strength = 0.5f;
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Inflate, p, Symmetry{false, false, false});
            for (uint32_t v = 0; v < before.size(); ++v) {
                const vec3 move = d.mesh.positions[v] - before[v];
                const float len = glm::length(move);
                if (len < 1e-6f) continue;
                ++moved;
                worstTurn = std::min(worstTurn, glm::dot(move / len, glm::normalize(normals[v])));
            }
        }
        d.undo.push(d.sculptor.endStroke());
        float grown = 0.0f;
        for (const Section& s : sections(d.mesh, start)) grown = std::max(grown, s.rMean / radius - 1.0f);
        std::printf("brush %.2f: %zu moves, the least cosine to the vertex's own normal %.4f, thickest ring +%.0f%%\n", brush,
                    moved, worstTurn, 100.0f * grown);
        CHECK(moved > 0);
        CHECK(worstTurn > 0.999f);
    }
}

TEST(inflate_dense_sphere_invariant_build_only_clean_edge) {
    // The promise on a dense curved mesh (the detector change must not touch it): Inflate only builds (no vertex sinks along
    // its own normal) and leaves no spike at the falloff edge.
    Document d = geom::denseSphere();
    const std::vector<vec3> before = d.mesh.positions, normals = d.mesh.normals;
    const float edge = geom::meanEdge(geom::denseSphereFixture().mesh), radius = 0.25f;
    std::vector<vec3> centers;
    CHECK(d.sculptor.beginStroke(d.mesh, "Inflate"));
    for (int i = 0; i < 12; ++i) {
        DabParams p;
        p.center = glm::normalize(vec3(-0.2f + 0.04f * float(i), 0.1f, 1.0f)) * 1.0f;
        p.viewDir = -glm::normalize(p.center);
        p.radius = radius;
        p.strength = 0.5f;
        centers.push_back(p.center);
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Inflate, p, Symmetry{false, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
    const float sink = geom::maxSink(before, d.mesh, normals), moved = geom::maxMove(before, d.mesh);
    const geom::EdgeReport e = geom::edgeOutliers(d.mesh, before, centers, radius, edge);
    std::printf("  inflate on the dense sphere: moved %.4f (edge %.4f), sink %.2g, edge umbrella %.3f (%d spikes of %d)\n", moved,
                edge, sink, e.worst, e.spikes, e.ring);
    CHECK(moved > 0.05f * edge);
    CHECK(sink < 1e-3f * edge);
    CHECK(e.spikes == 0);
    CHECK(geom::nonFinite(d.mesh) == 0);
}
