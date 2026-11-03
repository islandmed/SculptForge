// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Close Holes (#103): holes in a sphere, an open hemisphere, a plane, the size filter, attributes and undo.
#include "TestHarness.h"

#include "core/CloseHoles.h"
#include "core/Commands.h"
#include "core/Cut.h"
#include "core/Primitives.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

double volume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z])));
    return v / 6.0;
}

// Removes the triangles whose centre `drop` picks (their vertices stay, unused).
template <class F>
Mesh withHoles(Mesh m, F drop) {
    std::vector<glm::uvec3> kept;
    for (const glm::uvec3& t : m.triangles)
        if (!drop((m.positions[t.x] + m.positions[t.y] + m.positions[t.z]) / 3.0f)) kept.push_back(t);
    m.triangles = kept;
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

double meanEdge(const Mesh& m, size_t fromTriangle) {
    double sum = 0.0;
    size_t n = 0;
    for (size_t t = fromTriangle; t < m.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            sum += glm::length(m.positions[m.triangles[t][k]] - m.positions[m.triangles[t][(k + 1) % 3]]);
            ++n;
        }
    return n ? sum / double(n) : 0.0;
}

} // namespace

TEST(close_holes_two_holes_in_a_sphere) {
    const Mesh sphere = makeIcosphere(4);
    const double v0 = volume(sphere);
    Mesh open = withHoles(sphere, [](glm::vec3 c) { return c.z > 0.85f || c.x < -0.9f; });
    CHECK(!isWatertight(open));
    CHECK(boundaryLoops(open).size() == 2);
    const size_t t0 = open.triangleCount();
    Document d = makeDoc(open);
    const CommandResult r = registry().execute(d, "close_holes");
    CHECK(r.ok);
    CHECK(r.message.find("closed 2 holes") != std::string::npos);
    CHECK(isWatertight(d.mesh));
    CHECK(boundaryLoops(d.mesh).empty());
    // The rest of the Part is untouched, and the caps continue the sphere's curvature (round, not flat).
    for (size_t t = 0; t < t0; ++t) CHECK(d.mesh.triangles[t] == open.triangles[t]);
    for (size_t v = 0; v < open.vertexCount(); ++v) CHECK(d.mesh.positions[v] == open.positions[v]);
    const double v1 = volume(d.mesh);
    CHECK(std::abs(v1 - v0) < 0.005 * v0);
    float worst = 0.0f;
    for (size_t v = open.vertexCount(); v < d.mesh.vertexCount(); ++v) worst = std::max(worst, std::abs(glm::length(d.mesh.positions[v]) - 1.0f));
    CHECK(worst < 0.03f); // (within 3% of the radius over a hole 60 degrees across)
    // The patches are as dense as the sphere around them.
    const double around = meanEdge(open, 0), patch = meanEdge(d.mesh, t0);
    CHECK(patch > 0.6 * around && patch < 1.5 * around);
    // Each patch is its own new face group.
    CHECK(d.mesh.hasPolygroups());
    CHECK(d.mesh.polygroupAt(0) == 0 && d.mesh.polygroupAt(uint32_t(d.mesh.triangleCount() - 1)) == 1);
    // Undo brings the holes back; a second Close Holes finds none.
    CHECK(registry().execute(d, "undo").ok);
    CHECK(d.mesh.triangleCount() == t0);
    CHECK(registry().execute(d, "redo").ok);
    const CommandResult again = registry().execute(d, "close_holes");
    CHECK(again.ok && again.message.find("no holes") != std::string::npos);
}

TEST(close_holes_open_hemisphere_and_plane) {
    // The open bottom of a hemisphere: one big loop, capped watertight and rounded on (towards the full sphere).
    Mesh half = withHoles(makeIcosphere(4), [](glm::vec3 c) { return c.y < 0.0f; });
    CHECK(boundaryLoops(half).size() == 1);
    CloseHolesStats s;
    std::string error;
    Mesh m = half;
    CHECK(closeHoles(m, {}, s, error));
    CHECK(s.holes == 1 && s.filled == 1 && s.triangles > 50);
    CHECK(isWatertight(m));
    float lowest = 0.0f;
    for (size_t v = half.vertexCount(); v < m.vertexCount(); ++v) {
        CHECK(m.positions[v].y < 0.05f && m.positions[v].y > -1.1f);
        lowest = std::min(lowest, m.positions[v].y);
    }
    CHECK(lowest < -0.3f);
    // Without refining: only the loop's own vertices (an ear-clipped polygon, n - 2 triangles).
    m = half;
    CloseHolesOptions plain;
    plain.refine = false;
    CHECK(closeHoles(m, plain, s, error));
    CHECK(m.vertexCount() == half.vertexCount());
    CHECK(s.triangles == boundaryLoops(half)[0].size() - 2);
    CHECK(isWatertight(m));
    // A plane: its rim is the hole, capped on the back.
    Mesh plane = makePlane(8);
    m = plane;
    CHECK(closeHoles(m, {}, s, error));
    CHECK(s.filled == 1 && isWatertight(m));
}

TEST(close_holes_size_filter_and_attributes) {
    Mesh open = withHoles(makeIcosphere(4), [](glm::vec3 c) { return c.z > 0.97f || c.y < -0.3f; });
    const std::vector<std::vector<uint32_t>> loops = boundaryLoops(open);
    CHECK(loops.size() == 2);
    const size_t small = std::min(loops[0].size(), loops[1].size());
    open.ensureMask(0.5f);
    open.ensureColors(glm::vec3(0.2f, 0.4f, 0.6f));
    open.ensurePolygroups(7);
    open.uvs.assign(open.triangleCount() * 3, glm::vec2(0.25f));
    Document d = makeDoc(open);
    const CommandResult r = registry().execute(d, "close_holes", {{"max_edges", double(small)}});
    CHECK(r.ok);
    CHECK(r.message.find("closed 1 hole") != std::string::npos && r.message.find("left 1 bigger one") != std::string::npos);
    CHECK(boundaryLoops(d.mesh).size() == 1);
    const Mesh& m = d.mesh;
    CHECK(m.mask.size() == m.vertexCount() && m.colors.size() == m.vertexCount());
    CHECK(m.polygroups.size() == m.triangleCount() && m.uvs.size() == m.triangleCount() * 3);
    CHECK(m.polygroupAt(uint32_t(m.triangleCount() - 1)) == 8);
    CHECK(m.mask.back() == 0.0f);
    CHECK(glm::length(m.colors.back() - glm::vec3(0.2f, 0.4f, 0.6f)) < 1e-4f);
    // Forge Spheres and empty Parts are refused.
    Document empty = makeDoc(Mesh());
    CHECK(!registry().execute(empty, "close_holes").ok);
}

TEST(close_holes_big_hole_is_quick) {
    // A dense hemisphere's open bottom (a loop of hundreds of edges: the fan path) closes round in well under a second.
    Mesh half = withHoles(makeIcosphere(6), [](glm::vec3 c) { return c.y < 0.0f; });
    const size_t loop = boundaryLoops(half)[0].size();
    CHECK(loop > 300);
    CloseHolesStats s;
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    CHECK(closeHoles(half, {}, s, error));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("  close holes: %zu-edge loop, %zu triangles in %.0f ms\n", loop, s.triangles, seconds * 1000.0);
    CHECK(seconds < sf::test::timeLimit(1.5));
    CHECK(isWatertight(half));
    float lowest = 0.0f;
    for (const glm::vec3& p : half.positions) lowest = std::min(lowest, p.y);
    CHECK(lowest < -0.5f);
}
