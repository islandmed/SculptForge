// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Tool > Remesh > Remesh after each stroke (#121): which strokes stretched the surface enough to remesh.
#include "TestHarness.h"

#include "core/Primitives.h"
#include "core/QuickRemesh.h"

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
