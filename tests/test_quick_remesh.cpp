// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Tool > Remesh > Remesh after each stroke (#121): which strokes stretched the surface enough to remesh.
#include "TestHarness.h"

#include "core/CloseHoles.h"
#include "core/Primitives.h"
#include "core/QuickRemesh.h"
#include "core/StarterMeshes.h"
#include "core/Voxel.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace sf;

namespace {

// Moves the vertices `moved` to `to(position)`, returning their old positions (as a stroke's undo step holds them).
template <class F> std::vector<glm::vec3> moveVertices(Mesh& mesh, const std::vector<uint32_t>& moved, F to) {
    std::vector<glm::vec3> before;
    for (uint32_t v : moved) {
        before.push_back(mesh.positions[v]);
        mesh.positions[v] = to(mesh.positions[v]);
    }
    return before;
}

double meshVolume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z])));
    return v / 6.0;
}

// Pieces of a mesh (connected by shared vertices).
size_t pieces(const Mesh& m) {
    std::vector<uint32_t> parent(m.vertexCount());
    for (uint32_t i = 0; i < parent.size(); ++i) parent[i] = i;
    auto find = [&](uint32_t a) {
        while (parent[a] != a) a = parent[a] = parent[parent[a]];
        return a;
    };
    for (const glm::uvec3& t : m.triangles) {
        parent[find(t.y)] = find(t.x);
        parent[find(t.z)] = find(t.x);
    }
    size_t n = 0;
    for (uint32_t i = 0; i < parent.size(); ++i) n += find(i) == i;
    return n;
}

// The Stylised Head starter's head (open at the neck), divided once.
Mesh stylisedHead() {
    Document d;
    std::string error;
    const StarterMesh* starter = findStarterMesh("stylised-head");
    CHECK(starter && loadStarterMesh(*starter, d, 1, error));
    int best = 0;
    for (int i = 0; i < d.partCount(); ++i)
        if (d.partMesh(i).triangleCount() > d.partMesh(best).triangleCount()) best = i;
    return d.partMesh(best);
}

// A Move pull: the vertices within `radius` of `at` follow `delta` with a smooth falloff.
void movePull(Mesh& m, glm::vec3 at, float radius, glm::vec3 delta) {
    for (glm::vec3& p : m.positions) {
        const float d = glm::distance(p, at) / radius;
        if (d < 1.0f) p += delta * (1.0f - d * d) * (1.0f - d * d);
    }
    m.recomputeAllNormals();
}

} // namespace

TEST(quick_remesh_no_stroke_no_stretch) {
    Mesh mesh = makeIcosphere(3);
    const StrokeStretch s = strokeStretch(mesh, {}, {});
    CHECK(s.edges == 0);
    CHECK(!stretchWantsRemesh(s));
}

TEST(quick_remesh_gentle_stroke_keeps_the_mesh) {
    // Every vertex 20% further out: every edge 1.2 times as long, under the 1.6 threshold.
    Mesh mesh = makeIcosphere(3);
    std::vector<uint32_t> all;
    for (uint32_t v = 0; v < mesh.positions.size(); ++v) all.push_back(v);
    const std::vector<glm::vec3> before = moveVertices(mesh, all, [](glm::vec3 p) { return p * 1.2f; });
    const StrokeStretch s = strokeStretch(mesh, all, before);
    CHECK(s.edges > 0);
    CHECK(s.stretched == 0);
    CHECK(s.worst > 1.15f && s.worst < 1.25f);
    CHECK(!stretchWantsRemesh(s));
}

TEST(quick_remesh_pulled_vertex_remeshes) {
    // One vertex pulled far out (a Move or Snake Hook pull): its whole ring of edges stretched. (Vertex 20 has six
    // neighbours; the twelve first ones, the icosahedron's corners, have five, one short of kStretchedEdges.)
    Mesh mesh = makeIcosphere(3);
    CHECK(mesh.neighbors(20).size() == 6);
    const std::vector<uint32_t> moved{20};
    const std::vector<glm::vec3> before = moveVertices(mesh, moved, [](glm::vec3 p) { return p * 3.0f; });
    const StrokeStretch s = strokeStretch(mesh, moved, before);
    CHECK(s.edges == mesh.neighbors(20).size());
    CHECK(s.stretched == s.edges);
    CHECK(s.worst > 5.0f);
    CHECK(stretchWantsRemesh(s));
}

TEST(quick_remesh_edges_between_moved_vertices_count_once) {
    // Everything scaled 3x: each edge is counted once (both ends moved) and every one is stretched.
    Mesh mesh = makeIcosphere(2);
    std::vector<uint32_t> all;
    size_t ends = 0;
    for (uint32_t v = 0; v < mesh.positions.size(); ++v) {
        all.push_back(v);
        ends += mesh.neighbors(v).size();
    }
    const std::vector<glm::vec3> before = moveVertices(mesh, all, [](glm::vec3 p) { return p * 3.0f; });
    const StrokeStretch s = strokeStretch(mesh, all, before);
    CHECK(s.edges == ends / 2);
    CHECK(s.stretched == s.edges);
    CHECK(stretchWantsRemesh(s));
}

TEST(quick_remesh_vertices_out_of_range_are_skipped) {
    Mesh mesh = makeIcosphere(1);
    const std::vector<uint32_t> moved{uint32_t(mesh.positions.size()) + 5};
    const std::vector<glm::vec3> before{glm::vec3(0.0f)};
    CHECK(strokeStretch(mesh, moved, before).edges == 0);
}

// Launch clips (11-05): Quick Remesh and Voxel Remesh of the Stylised Head (open at the neck) after a Move pull near
// the neck gave a thin shell full of holes (the outside flooded in through the neck). Now the neck is capped first:
// one closed piece about as big as the head (it was 6-17 % of its volume in 3-17 pieces).
TEST(voxel_remesh_open_starter_head_stays_solid) {
    Mesh head = stylisedHead();
    CHECK(!boundaryLoops(head).empty()); // (open at the neck)
    const AABB box = head.bounds();
    const float size = glm::length(box.extent());
    // Near the neck: the lowest tenth, at the front; pulled forward and down.
    glm::vec3 neck(box.center().x, box.min.y + 0.1f * box.extent().y, box.max.z);
    float bestD = 1e30f;
    glm::vec3 at = neck;
    for (const glm::vec3& p : head.positions)
        if (const float d = glm::distance(p, neck); d < bestD) bestD = d, at = p;
    for (int pulled = 0; pulled < 2; ++pulled) {
        if (pulled) movePull(head, at, 0.15f * size, glm::vec3(0.0f, -0.05f, 0.12f) * size);
        Mesh closed = head;
        CloseHolesOptions o;
        o.refine = false;
        CloseHolesStats stats;
        std::string err;
        CHECK(closeHoles(closed, o, stats, err));
        const double expected = std::abs(meshVolume(closed));
        for (int res : {123, 64, 200}) {
            const auto t0 = std::chrono::steady_clock::now();
            Mesh r = voxelRemesh(head, res, &err);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("  stylised head%s res %d: %zu triangles, volume %.3f of the capped head, %zu piece(s), %.0f ms\n",
                        pulled ? " pulled" : "", res, r.triangleCount(), meshVolume(r) / expected, pieces(r), ms);
            CHECK(r.triangleCount() > 1000);
            CHECK(boundaryLoops(r).empty());
            CHECK(pieces(r) == 1);
            CHECK(std::abs(meshVolume(r) / expected - 1.0) < 0.1);
        }
    }
}

// A mesh that can't be made solid keeps the original: an empty result and a reason, never a holed shell. A sphere
// with its top cut off and a fin on one edge of the opening: that edge has two triangles, so the border is no loop
// Fill Holes can cap, and the outside floods in.
TEST(voxel_remesh_uncappable_open_mesh_says_why) {
    Mesh bowl = makeIcosphere(4);
    std::vector<glm::uvec3> kept;
    for (const glm::uvec3& t : bowl.triangles)
        if ((bowl.positions[t.x].y + bowl.positions[t.y].y + bowl.positions[t.z].y) / 3.0f <= 0.6f) kept.push_back(t);
    bowl.triangles = kept;
    bowl.rebuildTopology();
    std::string err;
    Mesh r = voxelRemesh(bowl, 64, &err);
    CHECK(r.triangleCount() > 1000 && err.empty()); // (capped: solid)
    CHECK(boundaryLoops(r).empty() && pieces(r) == 1);
    CHECK(meshVolume(r) > 3.5); // (the unit sphere less a thin cap: 4.19 less about 0.44)

    const std::vector<std::vector<uint32_t>> loops = boundaryLoops(bowl);
    CHECK(loops.size() == 1);
    Mesh fin = bowl;
    const uint32_t a = loops[0][0], b = loops[0][1];
    fin.positions.push_back((fin.positions[a] + fin.positions[b]) * 0.5f + glm::vec3(0.0f, 0.3f, 0.0f));
    fin.triangles.push_back(glm::uvec3(a, b, uint32_t(fin.positions.size() - 1))); // (a -> b twice)
    fin.rebuildTopology();
    CHECK(boundaryLoops(fin).empty()); // (nothing Fill Holes could cap)
    err.clear();
    CHECK(voxelRemesh(fin, 64, &err).triangleCount() == 0);
    CHECK(err.find("open borders") != std::string::npos);
    CHECK(err.find("unchanged") != std::string::npos);
}
