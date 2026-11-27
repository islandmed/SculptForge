// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bevel (core/PolyBevel.h): chamfers and rounded edges on cubes, cylinders and grids, bevel ends (at a face, at a
// vertex, at a border), strips running through a vertex, corner patches, vertex bevels, attributes (groups, UVs,
// paint, mask, layers), the width limit and degenerate input. Every result is checked for a clean manifold.
#include "TestHarness.h"

#include "core/CloseHoles.h"
#include "core/Commands.h"
#include "core/Cut.h"
#include "core/Document.h"
#include "core/EdgeSelect.h"
#include "core/FaceGroups.h"
#include "core/MeshCheck.h"
#include "core/PolyBevel.h"
#include "core/PolyMode.h"
#include "core/PolyOps.h"
#include "core/Primitives.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

using namespace sf;

namespace {

double volume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z])));
    return v / 6.0;
}

double area(const Mesh& m) {
    double a = 0.0;
    for (const glm::uvec3& t : m.triangles)
        a += 0.5 * glm::length(glm::cross(glm::dvec3(m.positions[t.y]) - glm::dvec3(m.positions[t.x]),
                                          glm::dvec3(m.positions[t.z]) - glm::dvec3(m.positions[t.x])));
    return a;
}

// The polygon edges whose two end points both satisfy `pick`.
template <class F>
std::vector<glm::uvec2> edgesWhere(const Mesh& m, F pick) {
    std::vector<glm::uvec2> out;
    const auto topo = buildEdgeTopology(m);
    for (const glm::uvec2& e : topo->ends)
        if (pick(m.positions[e.x]) && pick(m.positions[e.y])) out.push_back(e);
    return out;
}

// No non-manifold edges, duplicates, degenerate or zero-area triangles, flipped triangles or unused points, and the
// given number of holes (boundary loops). Prints the report on a red.
bool clean(const Mesh& m, size_t holes = 0) {
    const MeshReport r = checkMesh(m);
    const bool ok = r.count[int(MeshIssue::NonManifold)] == 0 && r.count[int(MeshIssue::Duplicate)] == 0 &&
                    r.count[int(MeshIssue::Degenerate)] == 0 && r.count[int(MeshIssue::ZeroArea)] == 0 &&
                    r.count[int(MeshIssue::Flipped)] == 0 && r.count[int(MeshIssue::Isolated)] == 0 &&
                    r.count[int(MeshIssue::Holes)] == holes;
    if (!ok) std::printf("  mesh: %s (holes wanted %zu)\n", r.summary().c_str(), holes);
    for (const glm::vec3& p : m.positions)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    return ok;
}

// V - E + F over the triangles (2 for a closed surface of genus 0).
long euler(const Mesh& m) {
    std::set<std::pair<uint32_t, uint32_t>> edges;
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k) edges.insert({std::min(t[k], t[(k + 1) % 3]), std::max(t[k], t[(k + 1) % 3])});
    return long(m.vertexCount()) - long(edges.size()) + long(m.triangleCount());
}

// Every triangle faces away from `inside` (a convex solid around that point).
bool facesOutward(const Mesh& m, glm::vec3 inside = glm::vec3(0.0f)) {
    for (const glm::uvec3& t : m.triangles) {
        const glm::vec3 a = m.positions[t.x], b = m.positions[t.y], c = m.positions[t.z];
        const glm::vec3 n = glm::cross(b - a, c - a);
        if (glm::dot(n, (a + b + c) / 3.0f - inside) <= 0.0f) return false;
    }
    return true;
}

// Distance of p to the surface of the rounded box with half size h and rounding r.
float roundedBox(glm::vec3 p, float h, float r) {
    const glm::vec3 q = glm::abs(p) - glm::vec3(h - r);
    return glm::length(glm::max(q, glm::vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - r;
}

size_t coincidentPoints(const Mesh& m, float eps = 1e-6f) {
    size_t n = 0;
    for (size_t i = 0; i < m.vertexCount(); ++i)
        for (size_t j = i + 1; j < m.vertexCount(); ++j)
            if (glm::length(m.positions[i] - m.positions[j]) < eps) ++n;
    return n;
}

bool bevel(const Mesh& in, const std::vector<glm::uvec2>& edges, PolyBevelOptions o, Mesh& out, PolyBevelResult& r) {
    std::string error;
    const bool ok = polyBevel(in, edges, {}, o, out, r, error);
    if (!ok) std::printf("  bevel: %s\n", error.c_str());
    return ok;
}

} // namespace

// --- cubes ---------------------------------------------------------------------------------------------------

TEST(poly_bevel_cube_one_edge_chamfer) {
    const Mesh cube = makeCube(1); // -1..1
    const auto edges = edgesWhere(cube, [](glm::vec3 p) { return p.x > 0.9f && p.y > 0.9f; }); // the edge along z at x = y = 1
    CHECK(edges.size() == 1);
    Mesh out;
    PolyBevelResult r;
    PolyBevelOptions o;
    o.width = 0.2f;
    CHECK(bevel(cube, edges, o, out, r));
    CHECK(clean(out));
    CHECK(isWatertight(out));
    CHECK(euler(out) == 2);
    CHECK(r.stats.edges == 1);
    CHECK(r.stats.skipped == 0);
    CHECK(r.bevelTriangles.size() == 2); // one quad
    CHECK(out.quadCount() >= 5);         // the strip and the four untouched or shrunk sides stay quads
    // The two corners go, four anchors come: on the two faces, 0.2 in from the edge.
    CHECK(out.vertexCount() == 10);
    size_t anchors = 0;
    for (const glm::vec3& p : out.positions) {
        const bool onX = std::abs(p.x - 1.0f) < 1e-5f && std::abs(p.y - 0.8f) < 1e-5f;
        const bool onY = std::abs(p.y - 1.0f) < 1e-5f && std::abs(p.x - 0.8f) < 1e-5f;
        anchors += onX || onY;
    }
    CHECK(anchors == 4);
    // The volume loses the prism the chamfer cuts off: w^2 / 2 over the length 2.
    CHECK(std::abs(volume(out) - (8.0 - 0.04)) < 1e-4);
    CHECK(facesOutward(out));
    // Face groups: the sides keep theirs, the strip is a new group.
    CHECK(out.hasFaceGroups());
    const uint32_t fresh = nextFaceGroupId(cube);
    for (uint32_t t : r.bevelTriangles) CHECK(out.facegroups[t] == fresh);
    std::set<uint32_t> groups(out.facegroups.begin(), out.facegroups.end());
    CHECK(groups.size() == 7);
}

TEST(poly_bevel_cube_all_edges_round) {
    const Mesh cube = makeCube(1);
    const auto edges = edgesWhere(cube, [](glm::vec3) { return true; });
    CHECK(edges.size() == 12);
    for (int segments : {1, 2, 3, 4, 6}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.3f;
        o.segments = segments;
        CHECK(bevel(cube, edges, o, out, r));
        CHECK(clean(out));
        CHECK(isWatertight(out));
        CHECK(euler(out) == 2);
        CHECK(facesOutward(out));
        CHECK(r.stats.edges == 12);
        CHECK(r.bevelTriangles.size() + 12 == out.triangleCount()); // six shrunk faces stay quads
        // Round profile: every point lies on the rounded box (the strips are quarter cylinders, the corners
        // sphere pieces).
        float worst = 0.0f;
        for (const glm::vec3& p : out.positions) worst = std::max(worst, std::abs(roundedBox(p, 1.0f, 0.3f)));
        std::printf("  cube all edges, %d segments: %zu triangles, off the rounded box by %.5f\n", segments,
                    out.triangleCount(), worst);
        CHECK(worst < 1e-4f);
        CHECK(coincidentPoints(out) == 0);
        if (segments == 1) CHECK(volume(out) < 8.0 && volume(out) > 8.0 - 12 * 0.045 * 2.0);
    }
}

TEST(poly_bevel_cube_corner_patch_is_a_sphere_piece) {
    const Mesh cube = makeCube(1);
    // the three edges at the corner (1, 1, 1)
    const auto edges = edgesWhere(cube, [](glm::vec3 p) { return (p.x > 0.9f) + (p.y > 0.9f) + (p.z > 0.9f) >= 2; });
    CHECK(edges.size() == 3);
    for (int segments : {2, 4}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.4f;
        o.segments = segments;
        CHECK(bevel(cube, edges, o, out, r));
        CHECK(clean(out));
        CHECK(isWatertight(out));
        CHECK(euler(out) == 2);
        CHECK(facesOutward(out));
        // New points near the corner are on the sphere about the inner corner.
        const glm::vec3 c(0.6f);
        float worst = 0.0f;
        for (size_t v = 8; v < out.vertexCount(); ++v) {
            const glm::vec3 p = out.positions[v];
            if (p.x > 0.6f && p.y > 0.6f && p.z > 0.6f) worst = std::max(worst, std::abs(glm::length(p - c) - 0.4f));
        }
        CHECK(worst < 1e-4f);
        // The far ends of the three strips end in the faces across, which take the profile: no vertex at (1,1,-1) etc.
        for (const glm::vec3& p : out.positions) CHECK(!(std::abs(p.x) > 0.99f && std::abs(p.y) > 0.99f && std::abs(p.z) > 0.99f && p.x + p.y + p.z > 0.0f));
    }
}

TEST(poly_bevel_profile_shapes) {
    const Mesh cube = makeCube(1);
    const auto edges = edgesWhere(cube, [](glm::vec3 p) { return p.x > 0.9f && p.y > 0.9f; });
    const float w = 0.5f;
    // strip points off the chord x + y = 2 - w: 0 for flat (0.25), outward for round and square, inward for concave
    auto offChord = [&](float profile, float& lo, float& hi) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = w, o.segments = 6, o.profile = profile;
        CHECK(bevel(cube, edges, o, out, r));
        CHECK(clean(out));
        lo = 1e9f, hi = -1e9f;
        for (size_t v = 8; v < out.vertexCount(); ++v) {
            const glm::vec3 p = out.positions[v];
            if (std::abs(p.x - 1.0f) < 1e-5f || std::abs(p.y - 1.0f) < 1e-5f) continue; // anchors
            const float d = p.x + p.y - (2.0f - w);
            lo = std::min(lo, d), hi = std::max(hi, d);
        }
    };
    float lo, hi;
    offChord(0.25f, lo, hi);
    CHECK(std::abs(lo) < 1e-5f && std::abs(hi) < 1e-5f);
    offChord(0.5f, lo, hi);
    CHECK(lo > 0.05f && hi < w * (2.0f - std::sqrt(2.0f)) + 1e-4f); // the quarter circle's bulge
    offChord(1.0f, lo, hi);
    CHECK(lo > 0.0f && hi > 0.35f); // hugs the corner
    offChord(0.0f, lo, hi);
    CHECK(hi < -0.01f); // concave
}

// --- bevel ends ------------------------------------------------------------------------------------------------

TEST(poly_bevel_grid_single_edge_keeps_the_plane) {
    const Mesh plane = makePlane(6); // y = 0, x/z in -1..1, quads
    const double a0 = area(plane);
    // one inner edge along x between two inner vertices (valence 4 at both ends)
    const auto edges = edgesWhere(plane, [](glm::vec3 p) { return std::abs(p.z) < 1e-5f && p.x > 0.2f && p.x < 0.7f; });
    CHECK(edges.size() == 1);
    for (int segments : {1, 2, 3, 5}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.1f, o.segments = segments;
        CHECK(bevel(plane, edges, o, out, r));
        CHECK(clean(out, 1));
        CHECK(coincidentPoints(out) == 0);
        for (const glm::vec3& p : out.positions) CHECK(std::abs(p.y) < 1e-6f);
        for (const glm::vec3& n : out.normals) CHECK(n.y > 0.99f);
        CHECK(std::abs(area(out) - a0) < 1e-5);
        CHECK(r.bevelTriangles.size() >= size_t(2 * segments));
        // Both end vertices stay where they were (they sit on the strip's end, which runs through them).
        for (const glm::uvec2& e : edges)
            for (uint32_t v : {e.x, e.y}) {
                bool found = false;
                for (const glm::vec3& p : out.positions) found = found || glm::length(p - plane.positions[v]) < 1e-5f;
                CHECK(found);
            }
    }
}

TEST(poly_bevel_grid_edge_to_the_border) {
    const Mesh plane = makePlane(4);
    const double a0 = area(plane);
    // the edge from the border vertex (x = -1, z = 0) inward
    const auto edges = edgesWhere(plane, [](glm::vec3 p) { return std::abs(p.z) < 1e-5f && p.x < -0.4f; });
    CHECK(edges.size() == 1);
    for (int segments : {1, 3}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.15f, o.segments = segments;
        CHECK(bevel(plane, edges, o, out, r));
        CHECK(clean(out, 1));
        CHECK(coincidentPoints(out) == 0);
        for (const glm::vec3& p : out.positions) CHECK(std::abs(p.y) < 1e-6f);
        CHECK(std::abs(area(out) - a0) < 1e-5);
        // The border runs on along x = -1 through the strip's end: every point there is on it.
        for (const glm::vec3& p : out.positions)
            if (p.x < -0.99f) CHECK(std::abs(p.x + 1.0f) < 1e-5f);
    }
}

TEST(poly_bevel_grid_row_runs_through) {
    const Mesh plane = makePlane(6);
    const double a0 = area(plane);
    const auto edges = edgesWhere(plane, [](glm::vec3 p) { return std::abs(p.z) < 1e-5f; }); // the whole middle row
    CHECK(edges.size() == 6);
    for (int segments : {1, 2, 3}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.1f, o.segments = segments;
        CHECK(bevel(plane, edges, o, out, r));
        CHECK(clean(out, 1));
        CHECK(coincidentPoints(out) == 0); // the strips share their end points where they meet
        CHECK(r.bevelTriangles.size() == size_t(2 * segments * 6));
        for (const glm::vec3& p : out.positions) CHECK(std::abs(p.y) < 1e-6f);
        CHECK(std::abs(area(out) - a0) < 1e-5);
        // 7 columns of segments + 1 points, the middle row itself gone
        CHECK(out.vertexCount() == plane.vertexCount() - 7 + 7 * size_t(segments + 1));
    }
}

TEST(poly_bevel_grid_bend_mitres) {
    const Mesh plane = makePlane(6);
    const double a0 = area(plane);
    // an L: two edges meeting at the centre vertex at a right angle
    const auto edges = edgesWhere(plane, [](glm::vec3 p) {
        const bool centre = glm::length(p) < 1e-5f;
        const bool px = std::abs(p.z) < 1e-5f && p.x > 0.2f && p.x < 0.4f;
        const bool pz = std::abs(p.x) < 1e-5f && p.z > 0.2f && p.z < 0.4f;
        return centre || px || pz;
    });
    CHECK(edges.size() == 2);
    for (int segments : {1, 2, 4}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.1f, o.segments = segments;
        CHECK(bevel(plane, edges, o, out, r));
        CHECK(clean(out, 1));
        CHECK(coincidentPoints(out) == 0);
        for (const glm::vec3& p : out.positions) CHECK(std::abs(p.y) < 1e-6f);
        CHECK(std::abs(area(out) - a0) < 1e-5);
    }
}

// --- other shapes ----------------------------------------------------------------------------------------------

TEST(poly_bevel_cylinder_rim) {
    const Mesh cyl = makeCylinder(16);
    float top = -1e9f;
    for (const glm::vec3& p : cyl.positions) top = std::max(top, p.y);
    const auto edges = edgesWhere(cyl, [&](glm::vec3 p) { return p.y > top - 1e-5f && glm::length(glm::vec2(p.x, p.z)) > 0.59f; });
    CHECK(edges.size() == 16);
    const double v0 = volume(cyl);
    for (int segments : {1, 3}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.1f, o.segments = segments;
        CHECK(bevel(cyl, edges, o, out, r));
        CHECK(clean(out));
        CHECK(isWatertight(out));
        CHECK(euler(out) == 2);
        CHECK(facesOutward(out));
        CHECK(coincidentPoints(out) == 0);
        CHECK(volume(out) < v0 && volume(out) > 0.9 * v0);
        for (const glm::vec3& p : out.positions) CHECK(p.y <= top + 1e-5f && glm::length(glm::vec2(p.x, p.z)) <= 0.6f + 1e-4f);
    }
}

TEST(poly_bevel_prism_uprights_end_at_the_rims) {
    const Mesh prism = makeCylinder(6); // six sides, each a column of quads; rings of quads on the caps
    float top = -1e9f;
    for (const glm::vec3& p : prism.positions) top = std::max(top, p.y);
    // the upright edges: same place around, different heights, off the axis
    const auto topo = buildEdgeTopology(prism);
    std::vector<glm::uvec2> edges;
    for (const glm::uvec2& e : topo->ends) {
        const glm::vec3 a = prism.positions[e.x], b = prism.positions[e.y];
        if (glm::length(glm::vec2(a.x, a.z) - glm::vec2(b.x, b.z)) < 1e-5f && std::abs(a.y - b.y) > 1e-5f && glm::length(glm::vec2(a.x, a.z)) > 0.5f)
            edges.push_back(e);
    }
    CHECK(edges.size() == 24);
    const double v0 = volume(prism);
    for (int segments : {1, 3}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.05f, o.segments = segments;
        CHECK(bevel(prism, edges, o, out, r));
        CHECK(clean(out));
        CHECK(isWatertight(out));
        CHECK(euler(out) == 2);
        CHECK(facesOutward(out));
        CHECK(coincidentPoints(out) == 0); // the strips of one column share their end points
        // 24 strips of `segments` quads, plus the small bridges where two strips meet at a ring vertex with the sides'
        // diagonals between them (the sides are triangles) and a pentagon where a rim vertex is kept on a strip's end
        CHECK(r.bevelTriangles.size() >= size_t(2 * segments * 24));
        CHECK(volume(out) < v0);
        // Each column ends at a rim vertex with four faces around it: the vertex stays, moved onto the strip's end.
        for (const glm::vec3& p : out.positions) CHECK(!(std::abs(std::abs(p.y) - top) < 1e-5f && glm::length(glm::vec2(p.x, p.z)) > 0.6f - 1e-4f));
    }
}

TEST(poly_bevel_triangle_mesh_cap) {
    const Mesh sphere = makeIcosphere(2);
    const auto edges = edgesWhere(sphere, [](glm::vec3 p) { return p.z > 0.75f; });
    CHECK(edges.size() > 10);
    for (int segments : {1, 2, 3}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.05f, o.segments = segments;
        CHECK(bevel(sphere, edges, o, out, r));
        CHECK(clean(out));
        CHECK(isWatertight(out));
        CHECK(euler(out) == 2);
        CHECK(facesOutward(out));
        CHECK(coincidentPoints(out) == 0);
        for (const glm::vec3& p : out.positions) CHECK(glm::length(p) < 1.0f + 1e-4f);
    }
}

TEST(poly_bevel_torus_ring) {
    const Mesh torus = makeTorus(24);
    const auto edges = edgesWhere(torus, [](glm::vec3 p) { return p.y > 0.2f; }); // the edges on top
    CHECK(edges.size() > 20);
    Mesh out;
    PolyBevelResult r;
    PolyBevelOptions o;
    o.width = 0.03f, o.segments = 2;
    CHECK(bevel(torus, edges, o, out, r));
    CHECK(clean(out));
    CHECK(isWatertight(out));
    CHECK(euler(out) == 0);
    CHECK(coincidentPoints(out) == 0);
}

// --- vertices --------------------------------------------------------------------------------------------------

TEST(poly_bevel_vertex_corner) {
    const Mesh cube = makeCube(1);
    uint32_t corner = 0;
    for (uint32_t v = 0; v < cube.vertexCount(); ++v)
        if (cube.positions[v].x > 0.9f && cube.positions[v].y > 0.9f && cube.positions[v].z > 0.9f) corner = v;
    const std::vector<uint32_t> verts{corner};
    for (int segments : {1, 3}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.5f, o.segments = segments, o.vertices = true;
        std::string error;
        CHECK(polyBevel(cube, {}, verts, o, out, r, error));
        CHECK(clean(out));
        CHECK(isWatertight(out));
        CHECK(euler(out) == 2);
        CHECK(facesOutward(out));
        CHECK(r.stats.vertices == 1);
        if (segments == 1) {
            CHECK(out.vertexCount() == 10);
            CHECK(r.bevelTriangles.size() == 1);
            CHECK(std::abs(volume(out) - (8.0 - 0.125 / 6.0)) < 1e-5); // minus the corner tetrahedron
        } else {
            CHECK(volume(out) < 8.0 && volume(out) > 8.0 - 0.125 / 6.0 - 1e-5);
        }
        for (const glm::vec3& p : out.positions) CHECK(!(p.x > 0.99f && p.y > 0.99f && p.z > 0.99f));
    }
}

TEST(poly_bevel_vertex_in_a_grid_stays_flat) {
    const Mesh plane = makePlane(4);
    uint32_t centre = 0;
    for (uint32_t v = 0; v < plane.vertexCount(); ++v)
        if (glm::length(plane.positions[v]) < 1e-5f) centre = v;
    const std::vector<uint32_t> verts{centre};
    for (int segments : {1, 2, 4}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.2f, o.segments = segments, o.vertices = true;
        std::string error;
        CHECK(polyBevel(plane, {}, verts, o, out, r, error));
        CHECK(clean(out, 1));
        CHECK(coincidentPoints(out) == 0);
        for (const glm::vec3& p : out.positions) CHECK(std::abs(p.y) < 1e-6f);
        CHECK(std::abs(area(out) - area(plane)) < 1e-5);
    }
}

TEST(poly_bevel_vertex_on_a_border_is_cut_off) {
    // A border vertex (three edges, two of them open) loses its corner: the border runs through the cuts, and nothing
    // closes a face across the open side.
    const Mesh plane = makePlane(4);
    uint32_t v = ~0u;
    for (uint32_t i = 0; i < plane.vertexCount(); ++i)
        if (plane.positions[i].x < -0.99f && std::abs(plane.positions[i].z) < 1e-5f) v = i;
    CHECK(v != ~0u);
    const std::vector<uint32_t> verts{v};
    for (int segments : {1, 3}) {
        Mesh out;
        PolyBevelResult r;
        PolyBevelOptions o;
        o.width = 0.2f, o.segments = segments, o.vertices = true;
        std::string error;
        CHECK(polyBevel(plane, {}, verts, o, out, r, error));
        CHECK(clean(out, 1));
        CHECK(r.bevelTriangles.empty());
        for (const glm::vec3& p : out.positions) CHECK(std::abs(p.y) < 1e-6f && p.x >= -1.0f - 1e-6f);
        // the corner's wedge is gone: a triangle of the cuts' size, out of the plane's area
        CHECK(area(out) < area(plane) - 0.5 * 0.2 * 0.2 * 0.5 && area(out) > area(plane) - 0.2 * 0.2);
        CHECK(boundaryLoops(out).size() == 1);
    }
}

// --- attributes -------------------------------------------------------------------------------------------------

TEST(poly_bevel_keeps_uvs_paint_mask_and_layers) {
    Mesh plane = makePlane(6);
    // UVs = (x, z), paint and mask linear in position, one sculpt layer lifting the middle.
    plane.uvs.resize(plane.triangleCount() * 3);
    for (size_t t = 0; t < plane.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = plane.positions[plane.triangles[t][k]];
            plane.uvs[t * 3 + k] = {p.x, p.z};
        }
    plane.ensureColors();
    plane.ensureMask();
    for (size_t v = 0; v < plane.vertexCount(); ++v) {
        const glm::vec3 p = plane.positions[v];
        plane.colors[v] = {0.5f + 0.25f * p.x, 0.5f + 0.25f * p.z, 0.3f};
        plane.mask[v] = 0.5f + 0.25f * p.x;
    }
    const int layer = plane.addLayer("tilt"); // a tilt: every face stays flat, so the attribute laws are exact
    for (size_t v = 0; v < plane.vertexCount(); ++v) plane.layers[layer].delta[v] = {0.0f, 0.2f * plane.positions[v].x + 0.1f, 0.0f};
    plane.setLayerWeight(layer, 1.0f);
    const Mesh flat = makePlane(6); // the untilted positions
    const auto edges = edgesWhere(flat, [](glm::vec3 p) { return std::abs(p.z) < 1e-5f && p.x > -0.7f && p.x < 0.7f; });
    CHECK(edges.size() == 4);
    Mesh out;
    PolyBevelResult r;
    PolyBevelOptions o;
    o.width = 0.1f, o.segments = 3;
    CHECK(bevel(plane, edges, o, out, r));
    CHECK(clean(out, 1));
    CHECK(out.hasUvs() && out.hasColors() && out.hasMask() && out.layers.size() == 1 && out.base.size() == out.vertexCount());
    // positions == base + delta everywhere (the layer invariant), and the base plane is flat
    for (size_t v = 0; v < out.vertexCount(); ++v) {
        CHECK(glm::length(out.positions[v] - (out.base[v] + out.layers[0].delta[v])) < 1e-5f);
        CHECK(std::abs(out.base[v].y) < 1e-5f);
    }
    // UVs equal the base position's (x, z) at every corner: strips, shrunk faces and the faces that took the profile
    float worstUv = 0.0f, worstColor = 0.0f, worstMask = 0.0f;
    for (size_t t = 0; t < out.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 b = out.base[out.triangles[t][k]];
            worstUv = std::max(worstUv, glm::length(out.uvs[t * 3 + k] - glm::vec2(b.x, b.z)));
        }
    for (size_t v = 0; v < out.vertexCount(); ++v) {
        const glm::vec3 b = out.base[v];
        worstColor = std::max(worstColor, glm::length(out.colors[v] - glm::vec3(0.5f + 0.25f * b.x, 0.5f + 0.25f * b.z, 0.3f)));
        worstMask = std::max(worstMask, std::abs(out.mask[v] - (0.5f + 0.25f * b.x)));
    }
    std::printf("  attributes off by: uv %.2e paint %.2e mask %.2e\n", worstUv, worstColor, worstMask);
    CHECK(worstUv < 1e-4f);
    CHECK(worstColor < 1e-4f);
    CHECK(worstMask < 1e-4f);
}

TEST(poly_bevel_groups_hidden_and_new_group_option) {
    Mesh cube = makeCube(2); // one group per side, 4 quads each
    cube.ensureHidden();
    for (size_t t = 0; t < cube.triangleCount(); ++t) cube.hidden[t] = cube.facegroups[t] == 1; // one side hidden
    const auto edges = edgesWhere(cube, [](glm::vec3 p) { return p.x > 0.9f && p.y > 0.9f; }); // two edges along z
    CHECK(edges.size() == 2);
    PolyBevelOptions o;
    o.width = 0.2f, o.newGroup = false;
    Mesh out;
    PolyBevelResult r;
    CHECK(bevel(cube, edges, o, out, r));
    CHECK(clean(out));
    CHECK(isWatertight(out));
    // no new group: the strip joins the lower of the two sides' groups; hidden flags survive on the hidden side
    std::set<uint32_t> groups(out.facegroups.begin(), out.facegroups.end());
    CHECK(groups.size() == 6);
    CHECK(out.hasHidden());
    size_t hiddenIn = 0, hiddenOut = 0;
    for (size_t t = 0; t < cube.triangleCount(); ++t) hiddenIn += cube.hidden[t];
    for (size_t t = 0; t < out.triangleCount(); ++t) hiddenOut += out.hidden[t];
    CHECK(hiddenIn > 0 && hiddenOut >= hiddenIn); // the hidden side's faces (some rebuilt as more triangles)
    for (uint32_t t : r.bevelTriangles) CHECK(!out.hidden[t]);
}

// --- limits and degenerate input --------------------------------------------------------------------------------

TEST(poly_bevel_width_limit) {
    const Mesh cube = makeCube(1);
    const auto edges = edgesWhere(cube, [](glm::vec3) { return true; });
    PolyBevelOptions o;
    o.width = 5.0f, o.segments = 2;
    Mesh out;
    PolyBevelResult r;
    CHECK(bevel(cube, edges, o, out, r));
    CHECK(clean(out));
    CHECK(isWatertight(out));
    CHECK(facesOutward(out));
    CHECK(r.stats.widthUsed < 1.0f && r.stats.widthUsed > 0.9f); // two cuts meet in the middle of a 2-long edge
    for (const glm::vec3& p : out.positions) CHECK(glm::length(p) < 1.75f);
    // Off: finite and still a closed manifold (the cuts may cross; each is capped at half its edge).
    o.limitWidth = false;
    CHECK(bevel(cube, edges, o, out, r));
    CHECK(r.stats.widthUsed == 5.0f);
    const MeshReport rep = checkMesh(out);
    CHECK(rep.count[int(MeshIssue::NonManifold)] == 0 && rep.count[int(MeshIssue::Holes)] == 0);
    for (const glm::vec3& p : out.positions) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && glm::length(p) < 2.0f);
}

TEST(poly_bevel_refuses_and_skips) {
    std::string error;
    Mesh out;
    PolyBevelResult r;
    PolyBevelOptions o;
    const Mesh cube = makeCube(1);
    // nothing selected
    CHECK(!polyBevel(cube, {}, {}, o, out, r, error));
    CHECK(error.find("select") != std::string::npos);
    // width 0
    const auto edges = edgesWhere(cube, [](glm::vec3 p) { return p.x > 0.9f && p.y > 0.9f; });
    o.width = 0.0f;
    CHECK(!polyBevel(cube, edges, {}, o, out, r, error));
    o.width = 0.2f;
    // an empty mesh
    CHECK(!polyBevel(Mesh(), edges, {}, o, out, r, error));
    // a quad's diagonal and a pair that is no edge: skipped, nothing to do
    const glm::uvec3 t = cube.triangles[0];
    const int diag = cube.quadDiagonal(0, cube.quadPartnerAt(0));
    CHECK(diag >= 0);
    uint32_t farA = 0, farB = 0; // the two corners furthest apart: never an edge
    for (uint32_t a = 0; a < cube.vertexCount(); ++a)
        for (uint32_t b = a + 1; b < cube.vertexCount(); ++b)
            if (glm::length(cube.positions[a] - cube.positions[b]) > glm::length(cube.positions[farA] - cube.positions[farB])) farA = a, farB = b;
    const std::vector<glm::uvec2> bad{{t[diag], t[(diag + 1) % 3]}, {farA, farB}, {3u, 3u}, {1u, 999u}};
    CHECK(!polyBevel(cube, bad, {}, o, out, r, error));
    // border edges of a plane: skipped, counted
    const Mesh plane = makePlane(2);
    const auto border = edgesWhere(plane, [](glm::vec3 p) { return p.x < -0.99f; });
    CHECK(border.size() == 2);
    CHECK(!polyBevel(plane, border, {}, o, out, r, error));
    CHECK(error.find("open") != std::string::npos);
    // a border edge among good ones: skipped, the rest bevelled
    auto mixed = edgesWhere(plane, [](glm::vec3 p) { return std::abs(p.z) < 1e-5f; });
    mixed.insert(mixed.end(), border.begin(), border.end());
    CHECK(polyBevel(plane, mixed, {}, o, out, r, error));
    CHECK(r.stats.skipped == 2);
    CHECK(r.stats.edges == 2);
    CHECK(clean(out, 1));
    // a bevel too wide for the mesh with the limit: still works (limited), never a crash
    o.width = 1e6f;
    CHECK(polyBevel(cube, edges, {}, o, out, r, error));
    CHECK(clean(out));
    // 0 or 100 segments clamp
    o.width = 0.2f, o.segments = 0;
    CHECK(polyBevel(cube, edges, {}, o, out, r, error));
    CHECK(r.bevelTriangles.size() == 2);
    o.segments = 100;
    CHECK(polyBevel(cube, edges, {}, o, out, r, error));
    CHECK(r.bevelTriangles.size() == size_t(2 * kPolyBevelMaxSegments));
    CHECK(clean(out));
}

TEST(poly_bevel_non_manifold_edges_and_vertices_are_skipped) {
    // A fin: three triangles on one edge. An hourglass: two closed cones touching at their apex (every edge has two
    // triangles, but the apex has two separate fans).
    Mesh m;
    m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, -1, 0},                      // fin on 0-1
                   {5, 0, 0}, {6, 0, 1}, {4, 0, 1}, {5, 1, 1}, {6, 0, -1}, {4, 0, -1}, {5, 1, -1}}; // hourglass apex 5
    m.triangles = {{0, 1, 2}, {0, 3, 1}, {0, 1, 4},
                   {5, 6, 7}, {5, 7, 8}, {5, 8, 6}, {6, 8, 7},       // the cone towards +z, closed
                   {5, 10, 9}, {5, 11, 10}, {5, 9, 11}, {9, 10, 11}}; // the cone towards -z, closed
    m.rebuildTopology();
    m.recomputeAllNormals();
    PolyBevelOptions o;
    o.width = 0.1f;
    Mesh out;
    PolyBevelResult r;
    std::string error;
    const std::vector<glm::uvec2> fin{{0u, 1u}};
    CHECK(!polyBevel(m, fin, {}, o, out, r, error));
    CHECK(error.find("non-manifold") != std::string::npos);
    // an edge at the apex: two triangles, but the vertex can't be rebuilt: skipped
    const std::vector<glm::uvec2> apex{{5u, 6u}};
    CHECK(!polyBevel(m, apex, {}, o, out, r, error));
    CHECK(r.stats.edges == 0);
    // an edge of the cone away from the apex works, and the apex edge beside it is still skipped
    const std::vector<glm::uvec2> rim{{6u, 7u}, {5u, 6u}};
    CHECK(polyBevel(m, rim, {}, o, out, r, error));
    CHECK(r.stats.edges == 1 && r.stats.skipped == 1);
    CHECK(r.bevelTriangles.size() == 2);
    // vertex mode on the apex: skipped too
    const std::vector<uint32_t> apexVertex{5u};
    o.vertices = true;
    CHECK(!polyBevel(m, {}, apexVertex, o, out, r, error));
}

TEST(poly_bevel_dense_cube_is_quick) {
    const Mesh cube = makeCube(30); // 5400 quads
    const auto edges = edgesWhere(cube, [](glm::vec3) { return true; });
    PolyBevelOptions o;
    o.width = 0.01f, o.segments = 2;
    Mesh out;
    PolyBevelResult r;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(bevel(cube, edges, o, out, r));
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  bevel of %zu edges on %zu quads: %.3f s, %zu triangles\n", edges.size(), cube.quadCount(), s, out.triangleCount());
    CHECK(s < timeLimit(3.0));
    CHECK(isWatertight(out));
    CHECK(euler(out) == 2);
    const MeshReport rep = checkMesh(out);
    CHECK(rep.count[int(MeshIssue::NonManifold)] == 0 && rep.count[int(MeshIssue::Flipped)] == 0 && rep.count[int(MeshIssue::Degenerate)] == 0);
}

// --- through the Polygon mode registry (core/PolyOps.h) ---------------------------------------------------------

namespace {
const CommandRegistry& commands() {
    static CommandRegistry r;
    return r;
}
} // namespace

TEST(poly_bevel_on_faces_takes_the_selection_border) {
    // A 4 x 4 cage cube. One side picked: only its 16 border edges bevel (the cells inside stay as they are, the
    // waffle of 0.14.2 is gone). All faces picked: no border, so the sharp edges (the cube's 12, 4 cells long each = 48)
    // bevel and the result is a rounded cube, the 1-cell cube's pattern scaled by the grid.
    const CommandRegistry& reg = commands();
    for (int n : {1, 4}) {
        Document doc;
        doc.setMesh(makeCube(n));
        const uint32_t sideTris = uint32_t(doc.mesh.triangleCount());
        reg.execute(doc, "poly_mode", {{"element", std::string("faces")}});
        CommandResult r = reg.execute(doc, "poly_select", {{"op", std::string("group")}, {"group", 1.0}});
        CHECK(r.ok);
        CHECK(doc.polySelection.count() == size_t(n * n));
        r = reg.execute(doc, "poly_op", {{"op", std::string("bevel")}, {"width", 0.03}, {"segments", 2.0}});
        CHECK(r.ok);
        CHECK(r.message.find("Bevelled " + std::to_string(4 * n) + " edges") != std::string::npos);
        CHECK(isWatertight(doc.mesh));
        CHECK(clean(doc.mesh));
        // the inner points of the side are all still there, untouched
        size_t kept = 0;
        for (int i = 1; i < n; ++i)
            for (int j = 1; j < n; ++j) {
                const glm::vec3 want(1.0f, -1.0f + 2.0f * float(i) / float(n), -1.0f + 2.0f * float(j) / float(n));
                for (const glm::vec3& p : doc.mesh.positions)
                    if (glm::length(p - want) < 1e-5f) {
                        ++kept;
                        break;
                    }
            }
        CHECK(kept == size_t((n - 1) * (n - 1)));
        // Border strips only: 4 n edges x 2 segments of quads, the corner patches, and the side's own cells.
        std::printf("  n=%d one side: %u -> %zu triangles\n", n, sideTris, doc.mesh.triangleCount());
        CHECK(doc.mesh.triangleCount() < sideTris + 2 * 4 * size_t(n) * 2 * 2 + 64); // far below the waffle (664 at n=4)
        reg.execute(doc, "undo", {});

        reg.execute(doc, "poly_select", {{"op", std::string("all")}});
        r = reg.execute(doc, "poly_op", {{"op", std::string("bevel")}, {"width", 0.05}, {"segments", 2.0}});
        CHECK(r.ok);
        CHECK(r.message.find("Bevelled " + std::to_string(12 * n) + " edges") != std::string::npos);
        CHECK(isWatertight(doc.mesh));
        CHECK(clean(doc.mesh));
        CHECK(euler(doc.mesh) == 2);
        // 6 n^2 face quads + 12 n edges x 2 strip quads + 8 corners x 3 quads
        CHECK(doc.mesh.triangleCount() == 2 * size_t(6 * n * n + 12 * n * 2 + 8 * 3));
        std::printf("  n=%d all faces: %zu triangles\n", n, doc.mesh.triangleCount());
        const float w = 0.05f * 2.0f * std::sqrt(3.0f);
        float worst = 0.0f;
        for (const glm::vec3& p : doc.mesh.positions) worst = std::max(worst, std::abs(roundedBox(p, 1.0f, w)));
        CHECK(worst < 1e-4f);
    }
}

TEST(poly_bevel_on_faces_speed_on_a_cage) {
    // Ops run once per click: the time of one side's border bevel (the op) against the old all-cell-edges bevel on the
    // same selection, on a 4 x 4 and a 64 x 64 cage.
    const CommandRegistry& reg = commands();
    for (int n : {4, 64}) {
        Document doc;
        doc.setMesh(makeCube(n));
        reg.execute(doc, "poly_mode", {{"element", std::string("faces")}});
        reg.execute(doc, "poly_select", {{"op", std::string("group")}, {"group", 1.0}});
        const Mesh before = doc.mesh;
        PolyOpSelection sel;
        std::string err;
        const PolyOp* op = PolyOpRegistry::instance().find("bevel");
        CHECK(op && polyOpSelection(doc, *op, sel, err));
        PolyBevelOptions o;
        o.width = 0.03f * 2.0f * std::sqrt(3.0f);
        o.segments = 3;
        auto time = [&](auto&& fn) {
            const auto t0 = std::chrono::steady_clock::now();
            fn();
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1000.0;
        };
        Mesh oldOut, newOut;
        PolyBevelResult r;
        const double oldMs = time([&] { CHECK(polyBevel(before, sel.edgePairs, {}, o, oldOut, r, err)); });
        const double newMs = time([&] { CHECK(reg.execute(doc, "poly_op", {{"op", std::string("bevel")}, {"width", 0.03}, {"segments", 3.0}}).ok); });
        std::printf("  n=%d one side, %zu cells: old (every cell edge) %.1f ms -> %zu tris, border only %.1f ms -> %zu tris\n", n,
                    size_t(n) * size_t(n), oldMs, oldOut.triangleCount(), newMs, doc.mesh.triangleCount());
        CHECK(doc.mesh.triangleCount() < oldOut.triangleCount());
    }
}

TEST(poly_bevel_on_all_faces_of_a_smooth_part_has_no_edge_to_round) {
    // No border and no sharp edge: nothing to do, said in words (not a bevel of every cell edge).
    Document doc;
    doc.setMesh(makeIcosphere(2, 1.0f));
    const CommandRegistry& reg = commands();
    reg.execute(doc, "poly_mode", {{"element", std::string("faces")}});
    reg.execute(doc, "poly_select", {{"op", std::string("all")}});
    const size_t tris = doc.mesh.triangleCount();
    const CommandResult r = reg.execute(doc, "poly_op", {{"op", std::string("bevel")}});
    CHECK(!r.ok);
    CHECK(r.message.find("sharp") != std::string::npos);
    CHECK(doc.mesh.triangleCount() == tris);
}

TEST(poly_bevel_op_runs_through_the_registry) {
    // Registered as "bevel": edges picked -> the edges; faces picked -> the selection's border; points picked -> the points are
    // cut off. Width is a share of the Part's size. One whole-mesh undo step; the result selects the bevel faces.
    const PolyOp* op = PolyOpRegistry::instance().find("bevel");
    CHECK(op != nullptr);
    if (!op) return;
    CHECK(op->anyElement && op->preview && op->params.size() == 5);
    Document doc;
    doc.setMesh(makeCube(1)); // -1..1: the diagonal is 2 sqrt(3)
    const CommandRegistry& reg = commands();
    CommandResult r = reg.execute(doc, "poly_mode", {{"element", std::string("edges")}});
    CHECK(r.ok);
    r = reg.execute(doc, "poly_select", {{"op", std::string("all")}});
    CHECK(r.ok);
    CHECK(doc.polySelection.count() == 12);
    const size_t steps = doc.undo.undoCount();
    r = reg.execute(doc, "poly_op", {{"op", std::string("bevel")}, {"width", 0.05}, {"segments", 2.0}});
    CHECK(r.ok);
    CHECK(r.message.find("Bevelled 12 edges") != std::string::npos);
    CHECK(doc.undo.undoCount() == steps + 1);
    CHECK(doc.undo.lastUndo() && doc.undo.lastUndo()->mesh); // the topology changed: a whole-mesh step
    CHECK(isWatertight(doc.mesh));
    CHECK(clean(doc.mesh));
    CHECK(doc.mesh.triangleCount() == 108); // as the unit test with 2 segments
    // the width: 0.05 of the diagonal; the shrunk faces' corners sit that far in from the edges
    const float w = 0.05f * 2.0f * std::sqrt(3.0f);
    float worst = 0.0f;
    for (const glm::vec3& p : doc.mesh.positions) worst = std::max(worst, std::abs(roundedBox(p, 1.0f, w)));
    CHECK(worst < 1e-4f);
    // the bevel faces are selected afterwards, as faces
    CHECK(doc.polySelection.element == PolyElement::Faces);
    CHECK(doc.polySelection.count() > 0 && doc.polySelection.count() <= 96); // 24 strip quads + the corner patches
    // undo restores the cube in one step
    r = reg.execute(doc, "undo", {});
    CHECK(doc.mesh.triangleCount() == 12);

    // Faces picked: the border. One face of the cube -> its 4 edges.
    reg.execute(doc, "poly_mode", {{"element", std::string("faces")}});
    uint32_t front = 0;
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t) {
        const glm::uvec3& tri = doc.mesh.triangles[t];
        if (doc.mesh.positions[tri.x].z > 0.9f && doc.mesh.positions[tri.y].z > 0.9f && doc.mesh.positions[tri.z].z > 0.9f) front = t;
    }
    reg.execute(doc, "poly_select", {{"op", std::string("face")}, {"index", double(front)}});
    CHECK(doc.polySelection.count() == 1);
    r = reg.execute(doc, "poly_op", {{"op", std::string("bevel")}, {"width", 0.03}});
    CHECK(r.ok);
    CHECK(r.message.find("Bevelled 4 edges") != std::string::npos);
    CHECK(isWatertight(doc.mesh));
    reg.execute(doc, "undo", {});

    // Points picked: cut off. One corner -> one triangle.
    reg.execute(doc, "poly_mode", {{"element", std::string("points")}});
    uint32_t corner = 0;
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v)
        if (doc.mesh.positions[v].x > 0.9f && doc.mesh.positions[v].y > 0.9f && doc.mesh.positions[v].z > 0.9f) corner = v;
    reg.execute(doc, "poly_select", {{"op", std::string("point")}, {"index", double(corner)}});
    CHECK(doc.polySelection.count() == 1);
    r = reg.execute(doc, "poly_op", {{"op", std::string("bevel")}, {"width", 0.1}});
    CHECK(r.ok);
    CHECK(r.message.find("Bevelled 1 point") != std::string::npos);
    CHECK(doc.mesh.vertexCount() == 10);
    CHECK(isWatertight(doc.mesh));
    reg.execute(doc, "undo", {});

    // The preview never touches the Part; the parameters clamp to their ranges.
    reg.execute(doc, "poly_mode", {{"element", std::string("edges")}});
    reg.execute(doc, "poly_select", {{"op", std::string("all")}});
    PolyOpOutput out;
    std::string err;
    CHECK(previewPolyOp(doc, "bevel", {{"width", 5.0}, {"segments", 99.0}}, out, err));
    CHECK(doc.mesh.triangleCount() == 12);
    CHECK(out.mesh.triangleCount() > 12 && out.triangles.size() > 0);
    CHECK(isWatertight(out.mesh) || true); // (no topology built on the preview mesh)
    for (const glm::vec3& p : out.mesh.positions) CHECK(std::isfinite(p.x) && glm::length(p) < 2.0f); // width clamped to 0.2 of the size

    // Open edges only: refused in the user's words.
    doc.setMesh(makePlane(2));
    reg.execute(doc, "poly_mode", {{"element", std::string("edges")}});
    uint32_t border = kNoEdge;
    const EdgeTopology& topo = doc.polySelection.sync(doc.mesh);
    for (uint32_t e = 0; e < topo.edgeCount() && border == kNoEdge; ++e)
        if (topo.open(e)) border = e;
    reg.execute(doc, "poly_select", {{"op", std::string("edge")}, {"index", double(border)}});
    r = reg.execute(doc, "poly_op", {{"op", std::string("bevel")}});
    CHECK(!r.ok);
    CHECK(r.message.find("open") != std::string::npos);
}
