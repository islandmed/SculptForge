// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extrude (Polygon mode, core/PolyExtrude.h): regions and individual polygons on cubes, cylinders, grids and
// spheres, the inset, open-border edges, attributes, the live move and degenerate input.
#include "TestHarness.h"

#include "core/CloseHoles.h"
#include "core/Commands.h"
#include "core/Cut.h"
#include "core/FaceGroups.h"
#include "core/PolyExtrude.h"
#include "core/PolyMode.h"
#include "core/PolyOps.h"
#include "core/Primitives.h"
#include "core/Uv.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

double volume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z])));
    return v / 6.0;
}

// Every edge has exactly one twin wound the other way: a closed, consistently oriented surface.
bool closedAndOriented(const Mesh& m) {
    const std::vector<uint32_t> twin = cornerTwins(m);
    for (uint32_t c : twin)
        if (c == kNoCorner) return false;
    return true;
}

// Open edges of a mesh (one triangle only).
size_t openEdges(const Mesh& m) {
    size_t n = 0;
    for (uint32_t c : cornerTwins(m)) n += c == kNoCorner;
    return n;
}

// Creases agree on both sides of every edge (as the subdivision rule expects).
bool creasesMatch(const Mesh& m) {
    if (!m.hasCreases()) return true;
    const std::vector<uint32_t> twin = cornerTwins(m);
    for (size_t c = 0; c < twin.size(); ++c)
        if (twin[c] != kNoCorner && m.creases[c] != m.creases[twin[c]]) return false;
    return true;
}

std::vector<uint8_t> selectGroup(const Mesh& m, uint32_t group) {
    std::vector<uint8_t> sel(m.triangleCount(), 0);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) sel[t] = m.facegroupAt(t) == group;
    return sel;
}

template <class F>
std::vector<uint8_t> selectWhere(const Mesh& m, F pick) {
    std::vector<uint8_t> sel(m.triangleCount(), 0);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3& tri = m.triangles[t];
        sel[t] = pick((m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f);
    }
    return sel;
}

size_t count(const std::vector<uint8_t>& flags) {
    size_t n = 0;
    for (uint8_t f : flags) n += f != 0;
    return n;
}

bool near(float a, float b, float eps = 1e-5f) { return std::abs(a - b) <= eps; }
bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) { return glm::length(a - b) <= eps; }

// The cube's side facing +X is group 1 (Primitives.cpp: group f + 1, side 0 = +X).
constexpr uint32_t kPlusX = 1, kPlusY = 3;

} // namespace

TEST(poly_extrude_cube_side_region) {
    const Mesh cube = makeCube(2);
    const double v0 = volume(cube);
    Mesh out;
    std::vector<uint8_t> sel;
    std::string status;
    CHECK(extrudeFaces(cube, selectGroup(cube, kPlusX), {}, 0.5f, 0.0f, out, sel, status));
    std::printf("  %s\n", status.c_str());
    CHECK(status == "Extruded 4 faces (8 walls)");
    // 3 x 3 cap vertices: the 8 on the border split off, the centre one just moves.
    CHECK(out.vertexCount() == cube.vertexCount() + 8);
    CHECK(out.triangleCount() == cube.triangleCount() + 16);
    CHECK(closedAndOriented(out));
    CHECK(isWatertight(out));
    CHECK(near(float(volume(out) - v0), 2.0f, 1e-4f)); // a 2 x 2 side pushed out 0.5
    // The cap sits at x = 1.5, and nothing else moved.
    CHECK(count(sel) == 8);
    for (uint32_t t = 0; t < out.triangleCount(); ++t) {
        if (!sel[t]) continue;
        for (int k = 0; k < 3; ++k) CHECK(near(out.positions[out.triangles[t][k]].x, 1.5f));
        CHECK(out.facegroupAt(t) == kPlusX);
    }
    for (uint32_t v = 0; v < cube.vertexCount(); ++v)
        if (!near(cube.positions[v].x, 1.0f)) CHECK(out.positions[v] == cube.positions[v]);
    for (uint32_t t = 0; t < cube.triangleCount(); ++t)
        if (cube.facegroupAt(t) != kPlusX) CHECK(out.triangles[t] == cube.triangles[t]);
    // The walls are quads in a new group of their own, and every cap quad stays a quad.
    CHECK(out.quadCount() == cube.quadCount() + 8);
    const uint32_t wallGroup = nextFaceGroupId(cube);
    size_t walls = 0;
    for (uint32_t t = uint32_t(cube.triangleCount()); t < out.triangleCount(); ++t) {
        CHECK(out.facegroupAt(t) == wallGroup);
        CHECK(!sel[t]);
        ++walls;
    }
    CHECK(walls == 16);
    // Normals face outwards (volume grew; the cap's normals are +X).
    for (uint32_t v = uint32_t(cube.vertexCount()); v < out.vertexCount(); ++v) CHECK(out.normals[v].x > 0.5f);
}

TEST(poly_extrude_cube_side_inward_and_zero) {
    const Mesh cube = makeCube(2);
    const double v0 = volume(cube);
    Mesh out;
    std::vector<uint8_t> sel;
    std::string status;
    CHECK(extrudeFaces(cube, selectGroup(cube, kPlusX), {}, -0.5f, 0.0f, out, sel, status));
    CHECK(closedAndOriented(out));
    CHECK(near(float(volume(out) - v0), -2.0f, 1e-4f)); // a pocket
    // Zero distance: the topology is there (walls of no height), nothing moved yet.
    CHECK(extrudeFaces(cube, selectGroup(cube, kPlusX), {}, 0.0f, 0.0f, out, sel, status));
    CHECK(out.triangleCount() == cube.triangleCount() + 16);
    CHECK(closedAndOriented(out));
    CHECK(near(float(volume(out) - v0), 0.0f, 1e-6f));
}

TEST(poly_extrude_cube_side_individual) {
    const Mesh cube = makeCube(2);
    const double v0 = volume(cube);
    ExtrudeOptions o;
    o.individual = true;
    Mesh out;
    std::vector<uint8_t> sel;
    std::string status;
    CHECK(extrudeFaces(cube, selectGroup(cube, kPlusX), o, 0.5f, 0.0f, out, sel, status));
    std::printf("  %s\n", status.c_str());
    CHECK(status == "Extruded 4 faces (16 walls)");
    // Each of the 4 quads gets its own 4 corners and 4 walls; the old corners stay for the walls' feet.
    CHECK(out.vertexCount() == cube.vertexCount() + 16);
    CHECK(out.triangleCount() == cube.triangleCount() + 32);
    CHECK(closedAndOriented(out));
    CHECK(near(float(volume(out) - v0), 2.0f, 1e-4f)); // 4 x (1 x 1 x 0.5)
    CHECK(count(sel) == 8);
    for (uint32_t t = 0; t < out.triangleCount(); ++t)
        if (sel[t])
            for (int k = 0; k < 3; ++k) CHECK(near(out.positions[out.triangles[t][k]].x, 1.5f));
    CHECK(out.quadCount() == cube.quadCount() + 16);
}

TEST(poly_extrude_inset_on_extrude) {
    const Mesh cube = makeCube(2);
    const double v0 = volume(cube);
    const ExtrudePlan plan = planExtrude(cube, selectGroup(cube, kPlusX), {});
    CHECK(plan.ok());
    Mesh out = buildExtrude(cube, plan, 0.5f, 0.125f); // 0.125 of the 2-wide side = 0.25 in
    CHECK(closedAndOriented(out));
    // The cap is a 1.5 x 1.5 square at x = 1.5: its corner (1, 1, 1) went to (1.5, 0.75, 0.75)...
    bool corner = false;
    for (uint32_t v = uint32_t(cube.vertexCount()); v < out.vertexCount(); ++v) {
        const glm::vec3 p = out.positions[v];
        CHECK(near(p.x, 1.5f));
        CHECK(std::abs(p.y) <= 0.75f + 1e-5f && std::abs(p.z) <= 0.75f + 1e-5f);
        corner |= near(p, glm::vec3(1.5f, 0.75f, 0.75f));
    }
    CHECK(corner);
    // ... and the solid grew by the frustum between the 2 x 2 side and the 1.5 x 1.5 cap, 0.5 high.
    const double frustum = 0.5 / 3.0 * (4.0 + 2.25 + std::sqrt(4.0 * 2.25));
    CHECK(near(float(volume(out) - v0), float(frustum), 1e-4f));
    // An inset deeper than the cap is scaled back until no cap polygon folds.
    Mesh deep = buildExtrude(cube, plan, 0.5f, 5.0f);
    CHECK(closedAndOriented(deep));
    for (uint32_t t = 0; t < deep.triangleCount(); ++t) {
        if (!plan.cap[t]) continue;
        const glm::uvec3& tri = deep.triangles[t];
        const glm::vec3 n = glm::cross(deep.positions[tri.y] - deep.positions[tri.x], deep.positions[tri.z] - deep.positions[tri.x]);
        CHECK(n.x > 0.0f);
    }
    const float applied = moveExtrude(cube, plan, 0.5f, 5.0f, deep);
    CHECK(applied > 0.0f && applied < 1.0f);
    std::printf("  inset 5 on a 2-wide cap applied as %.4f\n", applied);
    // An inset of 0.625 (1.25 in length on this 2-wide cap) pushes the border through the cap's centre and out the
    // other side (turned by 180 degrees, so the cap still faces +X): caught by the reversed edges and halved.
    CHECK(near(moveExtrude(cube, plan, 0.5f, 0.625f, deep), 0.3125f));
    CHECK(near(moveExtrude(cube, plan, 0.5f, 0.45f, deep), 0.45f));
}

// The area of the polygons `plan` marked as the cap.
double capArea(const Mesh& m, const ExtrudePlan& plan) {
    double a = 0.0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        if (!plan.cap[t]) continue;
        const glm::uvec3& tri = m.triangles[t];
        a += 0.5 * glm::length(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]));
    }
    return a;
}

// One cell of a +X side's grid (the cell holding the point nearest (1, 0.1, 0.1)).
std::vector<uint8_t> oneCell(const Mesh& m) {
    std::vector<uint8_t> sel(m.triangleCount(), 0);
    uint32_t best = 0;
    float bestDist = 1e9f;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        if (m.facegroupAt(t) != kPlusX) continue;
        const glm::uvec3& tri = m.triangles[t];
        const float d = glm::length((m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f - glm::vec3(1.0f, 0.1f, 0.1f));
        if (d < bestDist) bestDist = d, best = t;
    }
    sel[best] = 1;
    return sel;
}

TEST(poly_extrude_inset_is_a_share_of_the_selected_cell) {
    // Inset 0.1 takes a tenth of the CELL off each side whatever the grid: the cap is (1 - 2 * 0.1)^2 of the cell on
    // a 4 x 4 cage and on a 64 x 64 one (it was a share of the Part, which nearly closed a small cell).
    for (int n : {4, 64}) {
        const Mesh cube = makeCube(n);
        std::vector<uint8_t> sel = oneCell(cube);
        completePolygonSelection(cube, sel);
        const double cell = 4.0 / double(n * n); // a side is 2 x 2
        ExtrudePlan plan = planExtrude(cube, sel, {});
        CHECK(plan.ok());
        const Mesh flat = buildExtrude(cube, plan, 0.1f, 0.0f);
        CHECK(near(float(capArea(flat, plan)), float(cell), float(cell) * 1e-3f));
        const Mesh inset = buildExtrude(cube, plan, 0.1f, 0.1f);
        CHECK(closedAndOriented(inset));
        CHECK(near(float(capArea(inset, plan)), float(cell * 0.64), float(cell) * 1e-3f));
        // 0.25 (the slider's end) leaves half the width: a quarter of the area.
        const Mesh deep = buildExtrude(cube, plan, 0.1f, 0.25f);
        CHECK(near(float(capArea(deep, plan)), float(cell * 0.25), float(cell) * 1e-3f));
        // Individual polygons are pieces of their own too.
        ExtrudeOptions o;
        o.individual = true;
        plan = planExtrude(cube, sel, o);
        CHECK(plan.ok());
        CHECK(near(float(capArea(buildExtrude(cube, plan, 0.1f, 0.1f), plan)), float(cell * 0.64), float(cell) * 1e-3f));
    }
}

TEST(poly_extrude_inset_speed_on_a_cage) {
    // The narrowest-width step costs one pass over the border: plan + build of one cell and of a whole side with Inset.
    for (int n : {4, 64}) {
        const Mesh cube = makeCube(n);
        std::vector<uint8_t> cell = oneCell(cube), side = selectGroup(cube, kPlusX);
        completePolygonSelection(cube, cell);
        for (const auto* sel : {&cell, &side}) {
            const auto t0 = std::chrono::steady_clock::now();
            const ExtrudePlan plan = planExtrude(cube, *sel, {});
            const Mesh out = buildExtrude(cube, plan, 0.1f, 0.1f);
            const double ms = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1000.0;
            std::printf("  n=%d %s: plan + build with inset %.1f ms\n", n, sel == &cell ? "one cell" : "one side", ms);
            CHECK(plan.ok() && closedAndOriented(out));
        }
    }
}

TEST(poly_extrude_inset_measures_each_piece_on_its_own) {
    // Two separate pieces on one side: a single cell and a 2 x 2 block. The same inset takes the same share of each
    // piece's narrowest width: 0.1 of 0.5 = 0.05 off the cell, 0.1 of 1.0 = 0.1 off the block.
    const Mesh cube = makeCube(4);
    std::vector<uint8_t> sel(cube.triangleCount(), 0);
    for (uint32_t t = 0; t < cube.triangleCount(); ++t) {
        if (cube.facegroupAt(t) != kPlusX) continue;
        const glm::uvec3& tri = cube.triangles[t];
        const glm::vec3 c = (cube.positions[tri.x] + cube.positions[tri.y] + cube.positions[tri.z]) / 3.0f;
        const bool cell = c.y > 0.55f && c.z > 0.55f;           // the cell at +y +z (0.5 .. 1)
        const bool block = c.y < -0.05f && c.z < -0.05f;        // the 2 x 2 block at -y -z
        sel[t] = cell || block;
    }
    completePolygonSelection(cube, sel);
    const ExtrudePlan plan = planExtrude(cube, sel, {});
    CHECK(plan.ok());
    const Mesh out = buildExtrude(cube, plan, 0.1f, 0.1f);
    double cellArea = 0.0, blockArea = 0.0;
    for (uint32_t t = 0; t < out.triangleCount(); ++t) {
        if (!plan.cap[t]) continue;
        const glm::uvec3& tri = out.triangles[t];
        const glm::vec3 c = (out.positions[tri.x] + out.positions[tri.y] + out.positions[tri.z]) / 3.0f;
        const double a = 0.5 * glm::length(glm::cross(out.positions[tri.y] - out.positions[tri.x], out.positions[tri.z] - out.positions[tri.x]));
        (c.y > 0.0f ? cellArea : blockArea) += a;
    }
    CHECK(near(float(cellArea), 0.4f * 0.4f, 1e-4f));  // (0.5 - 2 * 0.05)^2
    CHECK(near(float(blockArea), 0.8f * 0.8f, 1e-4f)); // (1 - 2 * 0.1)^2
}

TEST(poly_extrude_two_sides_normals_vs_region) {
    const Mesh cube = makeCube(2);
    std::vector<uint8_t> sel = selectGroup(cube, kPlusX);
    const std::vector<uint8_t> y = selectGroup(cube, kPlusY);
    for (size_t t = 0; t < sel.size(); ++t) sel[t] |= y[t];
    // Along normals: an offset of the surface, so each side moves exactly d along its own normal (the shared
    // edge's vertices go along the diagonal, mitred).
    ExtrudeOptions o;
    Mesh out;
    std::vector<uint8_t> cap;
    std::string status;
    CHECK(extrudeFaces(cube, sel, o, 0.5f, 0.0f, out, cap, status));
    std::printf("  %s\n", status.c_str());
    CHECK(status == "Extruded 8 faces (12 walls)");
    CHECK(closedAndOriented(out));
    for (uint32_t t = 0; t < out.triangleCount(); ++t) {
        if (!cap[t]) continue;
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = out.positions[out.triangles[t][k]];
            if (out.facegroupAt(t) == kPlusX) CHECK(near(p.x, 1.5f));
            else CHECK(near(p.y, 1.5f));
        }
    }
    // Without even thickness the shared edge goes out 0.5 along the averaged normal: each side moves 0.5 / sqrt 2
    // there and 0.5 at its far edge.
    o.even = false;
    CHECK(extrudeFaces(cube, sel, o, 0.5f, 0.0f, out, cap, status));
    CHECK(closedAndOriented(out));
    for (uint32_t v = uint32_t(cube.vertexCount()); v < out.vertexCount(); ++v) {
        const glm::vec3 p = out.positions[v];
        if (near(p.x, p.y)) CHECK(near(p.x, 1.0f + 0.5f / std::sqrt(2.0f)));
        else CHECK(near(std::max(p.x, p.y), 1.5f));
    }
    o.even = true;
    // Along the region's normal: the two sides move together along (1, 1, 0) / sqrt 2 and keep their shape.
    o.along = ExtrudeAlong::Region;
    CHECK(extrudeFaces(cube, sel, o, 0.5f, 0.0f, out, cap, status));
    CHECK(closedAndOriented(out));
    const float k = 0.5f / std::sqrt(2.0f);
    for (uint32_t t = 0; t < out.triangleCount(); ++t) {
        if (!cap[t]) continue;
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = out.positions[out.triangles[t][c]];
            if (out.facegroupAt(t) == kPlusX) CHECK(near(p.x, 1.0f + k));
            else CHECK(near(p.y, 1.0f + k));
        }
    }
}

TEST(poly_extrude_cylinder_cap_fan) {
    // The top cap is a fan of triangles around a centre vertex (our n-gon): the centre is inside the region and
    // only moves, the rim splits.
    const Mesh cyl = makeCylinder(16);
    const double v0 = volume(cyl);
    float top = -1e9f;
    for (const glm::vec3& p : cyl.positions) top = std::max(top, p.y);
    std::vector<uint8_t> sel = selectWhere(cyl, [&](glm::vec3 c) { return near(c.y, top, 1e-4f); });
    const size_t polygons = completePolygonSelection(cyl, sel); // the fan and the rings of quads around it
    CHECK(polygons > 16);
    Mesh out;
    std::vector<uint8_t> cap;
    std::string status;
    CHECK(extrudeFaces(cyl, sel, {}, 0.3f, 0.0f, out, cap, status));
    std::printf("  %s\n", status.c_str());
    CHECK(status == "Extruded " + std::to_string(polygons) + " faces (16 walls)");
    CHECK(out.vertexCount() == cyl.vertexCount() + 16);
    CHECK(closedAndOriented(out));
    CHECK(isWatertight(out));
    // The cap's area (a regular 16-gon of the cylinder's radius) times 0.3.
    float r = 0.0f;
    for (const glm::vec3& p : cyl.positions)
        if (near(p.y, top, 1e-4f)) r = std::max(r, glm::length(glm::vec2(p.x, p.z)));
    const double area = 8.0 * r * r * std::sin(2.0 * 3.14159265358979 / 16.0);
    CHECK(near(float(volume(out) - v0), float(area * 0.3), 1e-4f));
    for (uint32_t t = 0; t < out.triangleCount(); ++t)
        if (cap[t])
            for (int k = 0; k < 3; ++k) CHECK(near(out.positions[out.triangles[t][k]].y, top + 0.3f));
}

TEST(poly_extrude_grid_region_and_whole_plane) {
    const Mesh plane = makePlane(4); // 4 x 4 quads, 25 vertices, open
    CHECK(boundaryLoops(plane).size() == 1);
    // The middle 2 x 2 cells: 8 walls, 8 split vertices, the one centre vertex just moves; still one hole.
    const std::vector<uint8_t> sel = selectWhere(plane, [](glm::vec3 c) { return std::abs(c.x) < 0.5f && std::abs(c.y) < 0.5f && std::abs(c.z) < 0.5f; });
    CHECK(count(sel) == 8);
    Mesh out;
    std::vector<uint8_t> cap;
    std::string status;
    CHECK(extrudeFaces(plane, sel, {}, 0.4f, 0.0f, out, cap, status));
    CHECK(status == "Extruded 4 faces (8 walls)");
    CHECK(out.vertexCount() == plane.vertexCount() + 8);
    CHECK(boundaryLoops(out).size() == 1);
    CHECK(openEdges(out) == openEdges(plane));
    // The whole plane: walls stand on its outer border, whose old vertices stay as their feet.
    const std::vector<uint8_t> all(plane.triangleCount(), 1);
    CHECK(extrudeFaces(plane, all, {}, 0.4f, 0.0f, out, cap, status));
    std::printf("  %s\n", status.c_str());
    CHECK(status == "Extruded 16 faces (16 walls)");
    CHECK(out.vertexCount() == plane.vertexCount() + 16);
    CHECK(out.triangleCount() == plane.triangleCount() + 32);
    CHECK(boundaryLoops(out).size() == 1);
    CHECK(openEdges(out) == 16);
    CHECK(count(cap) == plane.triangleCount());
}

TEST(poly_extrude_whole_closed_mesh_inflates) {
    const Mesh sphere = makeIcosphere(3);
    const std::vector<uint8_t> all(sphere.triangleCount(), 1);
    Mesh out;
    std::vector<uint8_t> cap;
    std::string status;
    CHECK(extrudeFaces(sphere, all, {}, 0.25f, 0.0f, out, cap, status));
    std::printf("  %s\n", status.c_str());
    CHECK(status.find("no border") != std::string::npos);
    CHECK(out.triangleCount() == sphere.triangleCount());
    CHECK(out.vertexCount() == sphere.vertexCount());
    CHECK(count(cap) == sphere.triangleCount());
    float worst = 0.0f;
    for (const glm::vec3& p : out.positions) worst = std::max(worst, std::abs(glm::length(p) - 1.25f));
    CHECK(worst < 0.01f); // an icosphere's vertex normals are radial, so it inflates to a bigger sphere
    CHECK(closedAndOriented(out));
}

TEST(poly_extrude_open_border_edges) {
    const Mesh plane = makePlane(2); // 3 x 3 vertices from -1 to 1 in x and z (y up? see below), open
    // The border edges on the side with the largest first coordinate that varies (the plane may lie in xz or xy).
    AABB box = plane.bounds();
    const int axis = box.extent().x > 1e-6f ? 0 : 1;              // a coordinate the plane spans
    const int other = box.extent().z > 1e-6f ? 2 : 1;             // the other one
    const float hi = box.max[axis];
    std::vector<glm::uvec2> edges;
    for (const glm::uvec3& t : plane.triangles)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = t[k], b = t[(k + 1) % 3];
            if (near(plane.positions[a][axis], hi, 1e-5f) && near(plane.positions[b][axis], hi, 1e-5f)) edges.push_back({a, b});
        }
    CHECK(edges.size() == 2);
    edges.push_back(edges.front()); // a repeat is taken once
    Mesh out;
    std::vector<glm::uvec2> border;
    std::string status;
    EdgeExtrudeOptions o;
    const ExtrudePlan plan = planEdgeExtrude(plane, edges, o);
    CHECK(plan.ok());
    auto plane_source = [&](uint32_t v) { return plan.source[v]; };
    CHECK(extrudeEdges(plane, edges, o, 0.5f, out, border, status));
    std::printf("  %s\n", status.c_str());
    CHECK(status == "Extruded 2 edges");
    CHECK(out.vertexCount() == plane.vertexCount() + 3);
    CHECK(out.triangleCount() == plane.triangleCount() + 4);
    CHECK(out.quadCount() == plane.quadCount() + 2);
    CHECK(border.size() == 2);
    CHECK(boundaryLoops(out).size() == 1); // the border grew, no new hole
    CHECK(openEdges(out) == openEdges(plane) + 2);
    // Outward: the strip continues the plane, 0.5 further out, the other coordinates kept.
    for (uint32_t v = uint32_t(plane.vertexCount()); v < out.vertexCount(); ++v) {
        CHECK(near(out.positions[v][axis], hi + 0.5f));
        CHECK(near(out.positions[v][other], plane.positions[plane_source(v)][other]));
        CHECK(near(out.positions[v][3 - axis - other], box.max[3 - axis - other]));
    }
    for (const glm::uvec2& e : border) CHECK(e.x >= plane.vertexCount() && e.y >= plane.vertexCount());
    // The strip faces the same way as the plane (consistent orientation: every inner edge paired).
    CHECK(openEdges(out) == 8 + 2);
    // Along the normals: a wall standing on the plane.
    o.along = EdgeExtrudeAlong::Normals;
    CHECK(extrudeEdges(plane, edges, o, 0.5f, out, border, status));
    for (uint32_t v = uint32_t(plane.vertexCount()); v < out.vertexCount(); ++v) {
        CHECK(near(out.positions[v][axis], hi));
        CHECK(near(std::abs(out.positions[v][3 - axis - other] - box.max[3 - axis - other]), 0.5f));
    }
    // Along a vector.
    o.along = EdgeExtrudeAlong::Vector;
    o.direction = glm::vec3(0.0f);
    o.direction[other] = 2.0f; // (any length: it is a direction)
    CHECK(extrudeEdges(plane, edges, o, 0.5f, out, border, status));
    for (uint32_t v = uint32_t(plane.vertexCount()); v < out.vertexCount(); ++v) {
        CHECK(near(out.positions[v][axis], hi));
        CHECK(near(out.positions[v][3 - axis - other], box.max[3 - axis - other]));
    }
    // The whole border, outward: corners go out diagonally by the mitre (0.5 in both coordinates).
    edges.clear();
    const std::vector<uint32_t> twin = cornerTwins(plane);
    for (uint32_t c = 0; c < twin.size(); ++c)
        if (twin[c] == kNoCorner) edges.push_back({plane.triangles[c / 3][c % 3], plane.triangles[c / 3][(c % 3 + 1) % 3]});
    CHECK(edges.size() == 8);
    o.along = EdgeExtrudeAlong::Outward;
    CHECK(extrudeEdges(plane, edges, o, 0.5f, out, border, status));
    CHECK(status == "Extruded 8 edges");
    CHECK(out.vertexCount() == plane.vertexCount() + 8);
    CHECK(boundaryLoops(out).size() == 1);
    size_t corners = 0;
    for (uint32_t v = uint32_t(plane.vertexCount()); v < out.vertexCount(); ++v) {
        const glm::vec3 p = out.positions[v];
        const bool cornerA = near(std::abs(p[axis]), 1.5f), cornerB = near(std::abs(p[other]), 1.5f);
        CHECK(cornerA || cornerB);
        corners += cornerA && cornerB;
    }
    CHECK(corners == 4);
    // An inner edge is no border: refused with a reason.
    std::vector<glm::uvec2> inner;
    for (uint32_t c = 0; c < twin.size(); ++c)
        if (twin[c] != kNoCorner) {
            inner.push_back({plane.triangles[c / 3][c % 3], plane.triangles[c / 3][(c % 3 + 1) % 3]});
            break;
        }
    CHECK(!extrudeEdges(plane, inner, o, 0.5f, out, border, status));
    CHECK(status.find("open border") != std::string::npos);
    std::printf("  %s\n", status.c_str());
}

TEST(poly_extrude_keeps_attributes) {
    Mesh cube = makeCube(2);
    cube.ensureMask();
    cube.ensureColors();
    for (uint32_t v = 0; v < cube.vertexCount(); ++v) {
        cube.mask[v] = float(v) / float(cube.vertexCount());
        cube.colors[v] = glm::vec3(float(v % 7) / 7.0f, 0.25f, float(v % 3) / 3.0f);
    }
    // UVs: each side laid out by its two in-plane coordinates.
    cube.uvs.resize(cube.triangleCount() * 3);
    for (uint32_t t = 0; t < cube.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = cube.positions[cube.triangles[t][k]];
            const uint32_t g = cube.facegroupAt(t);
            cube.uvs[t * 3 + k] = g <= 2 ? glm::vec2(p.y, p.z) : g <= 4 ? glm::vec2(p.x, p.z) : glm::vec2(p.x, p.y);
        }
    // Creases: every edge of the +X side's border (both sides of each edge), 0.7.
    cube.ensureCreases();
    {
        const std::vector<uint32_t> twin = cornerTwins(cube);
        for (uint32_t c = 0; c < twin.size(); ++c) {
            const uint32_t t = c / 3, u = twin[c] == kNoCorner ? t : twin[c] / 3;
            if ((cube.facegroupAt(t) == kPlusX) != (cube.facegroupAt(u) == kPlusX)) cube.creases[c] = 0.7f;
        }
        CHECK(creasesMatch(cube));
    }
    // A sculpt layer with an offset on every vertex.
    const int layer = cube.addLayer("Bumps");
    for (uint32_t v = 0; v < cube.vertexCount(); ++v) cube.layers[layer].delta[v] = glm::vec3(0.0f, 0.01f * float(v % 5), 0.0f);
    cube.layers[layer].weight = 0.5f;
    cube.evaluateAll();
    cube.rebuildTopology();
    cube.recomputeAllNormals();

    const ExtrudePlan plan = planExtrude(cube, selectGroup(cube, kPlusX), {});
    CHECK(plan.ok());
    const Mesh out = buildExtrude(cube, plan, 0.5f, 0.0f);
    CHECK(out.hasMask() && out.hasColors() && out.hasUvs() && out.hasCreases() && out.layers.size() == 1);
    // New vertices copy their source.
    for (uint32_t v = uint32_t(cube.vertexCount()); v < out.vertexCount(); ++v) {
        const uint32_t s = plan.source[v];
        CHECK(s < cube.vertexCount());
        CHECK(out.mask[v] == cube.mask[s]);
        CHECK(out.colors[v] == cube.colors[s]);
        CHECK(out.layers[0].delta[v] == cube.layers[0].delta[s]);
    }
    // Positions still equal base + weight * delta everywhere.
    for (uint32_t v = 0; v < out.vertexCount(); ++v)
        CHECK(near(out.positions[v], out.base[v] + 0.5f * out.layers[0].delta[v], 1e-6f));
    // The kept triangles keep their UVs and creases; every crease still agrees across its edge.
    for (size_t c = 0; c < cube.uvs.size(); ++c) {
        CHECK(out.uvs[c] == cube.uvs[c]);
        CHECK(out.creases[c] == cube.creases[c]);
    }
    CHECK(creasesMatch(out));
    // Each wall's bottom corners take the cap edge's UVs; its top corners sit the wall's height (0.5, at the cap
    // edge's texel density) further out, so the wall is a rectangle beside the cap's edge.
    for (size_t t = plan.firstWall; t < out.triangleCount(); ++t) {
        const glm::uvec2 e = plan.wallEdge[t - plan.firstWall];
        const uint32_t owner = plan.wallSource[t - plan.firstWall];
        int k = 0;
        while (!(cube.triangles[owner][k] == e.x && cube.triangles[owner][(k + 1) % 3] == e.y)) ++k;
        const glm::vec2 uvA = cube.uvs[owner * 3 + k], uvB = cube.uvs[owner * 3 + (k + 1) % 3];
        const float density = glm::length(uvB - uvA) / glm::length(cube.positions[e.y] - cube.positions[e.x]);
        for (int c = 0; c < 3; ++c) {
            const uint32_t v = out.triangles[t][c];
            const glm::vec2 uv = out.uvs[t * 3 + c];
            const glm::vec2 want = plan.source[v] == e.x ? uvA : uvB;
            if (v < cube.vertexCount()) CHECK(uv == want);
            else CHECK(near(glm::length(uv - want), 0.5f * density, 1e-5f));
        }
    }
    // Creases on the walls match their twins (checked above); the uprights carry none.
    for (size_t t = plan.firstWall; t < out.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = out.triangles[t][c], b = out.triangles[t][(c + 1) % 3];
            const bool upright = (a < cube.vertexCount()) != (b < cube.vertexCount());
            if (upright) CHECK(out.creases[t * 3 + c] == 0.0f);
        }
    // Inherit: the walls take the cap's group; the cube's groups stay 1..6.
    ExtrudeOptions o;
    o.sideGroup = ExtrudeSideGroup::Inherit;
    Mesh inh;
    std::vector<uint8_t> cap;
    std::string status;
    CHECK(extrudeFaces(cube, selectGroup(cube, kPlusX), o, 0.5f, 0.0f, inh, cap, status));
    for (size_t t = cube.triangleCount(); t < inh.triangleCount(); ++t) CHECK(inh.facegroupAt(uint32_t(t)) == kPlusX);
    CHECK(nextFaceGroupId(inh) == nextFaceGroupId(cube));
    // A mesh without face groups gets them only for the walls (group 1).
    Mesh plain = makeIcosphere(2);
    CHECK(!plain.hasFaceGroups());
    const std::vector<uint8_t> top = selectWhere(plain, [](glm::vec3 c) { return c.y > 0.6f; });
    Mesh pout;
    CHECK(extrudeFaces(plain, top, {}, 0.2f, 0.0f, pout, cap, status));
    CHECK(pout.hasFaceGroups());
    for (uint32_t t = 0; t < pout.triangleCount(); ++t) CHECK(pout.facegroupAt(t) == (t < plain.triangleCount() ? 0u : 1u));
    CHECK(closedAndOriented(pout));
}

TEST(poly_extrude_move_matches_build) {
    const Mesh cube = makeCube(3);
    std::vector<uint8_t> sel = selectGroup(cube, kPlusX);
    const std::vector<uint8_t> y = selectGroup(cube, kPlusY);
    for (size_t t = 0; t < sel.size(); ++t) sel[t] |= y[t];
    const ExtrudePlan plan = planExtrude(cube, sel, {});
    CHECK(plan.ok());
    Mesh live = buildExtrude(cube, plan, 0.2f, 0.05f);
    CHECK(near(moveExtrude(cube, plan, 0.9f, 0.1f, live), 0.1f));
    const Mesh built = buildExtrude(cube, plan, 0.9f, 0.1f);
    CHECK(live.vertexCount() == built.vertexCount());
    for (uint32_t v = 0; v < live.vertexCount(); ++v) {
        CHECK(live.positions[v] == built.positions[v]);
        CHECK(near(live.normals[v], built.normals[v], 1e-5f));
    }
    // The moved vertices are marked for the GPU.
    size_t dirty = 0;
    for (const ExtrudePlan::Moved& m : plan.moved) dirty += live.vertexDirty(m.vertex);
    CHECK(dirty == plan.moved.size());
    // Back to zero height and up again: the same mesh as a fresh build.
    moveExtrude(cube, plan, 0.0f, 0.0f, live);
    moveExtrude(cube, plan, 0.3f, 0.0f, live);
    const Mesh again = buildExtrude(cube, plan, 0.3f, 0.0f);
    for (uint32_t v = 0; v < live.vertexCount(); ++v) CHECK(live.positions[v] == again.positions[v]);
}

TEST(poly_extrude_selection_completion_and_degenerate_input) {
    Mesh cube = makeCube(1);
    // One half of a quad selects the whole quad.
    std::vector<uint8_t> half(cube.triangleCount(), 0);
    uint32_t t0 = 0;
    while (cube.facegroupAt(t0) != kPlusX) ++t0;
    half[t0] = 1;
    CHECK(completePolygonSelection(cube, half) == 1);
    CHECK(count(half) == 2);
    CHECK(half[cube.quadPartnerAt(t0)]);
    // A short flag list is fine (the rest unselected).
    const std::vector<uint8_t> shortSel(t0 + 1, 0);
    std::vector<uint8_t> one(t0 + 1, 0);
    one[t0] = 1;
    Mesh out;
    std::vector<uint8_t> cap;
    std::string status;
    CHECK(extrudeFaces(cube, one, {}, 0.5f, 0.0f, out, cap, status));
    CHECK(status == "Extruded 1 face (4 walls)");
    CHECK(closedAndOriented(out));
    CHECK(cap.size() == out.triangleCount());
    // Nothing selected, a hidden selection and an empty mesh are refused with a reason.
    CHECK(!extrudeFaces(cube, shortSel, {}, 0.5f, 0.0f, out, cap, status));
    CHECK(status.find("select") != std::string::npos);
    std::printf("  %s\n", status.c_str());
    Mesh hidden = cube;
    hidden.ensureHidden();
    hidden.hidden[t0] = hidden.hidden[cube.quadPartnerAt(t0)] = 1;
    CHECK(!extrudeFaces(hidden, one, {}, 0.5f, 0.0f, out, cap, status));
    const Mesh empty;
    CHECK(!extrudeFaces(empty, one, {}, 0.5f, 0.0f, out, cap, status));
    CHECK(status.find("empty") != std::string::npos);
    std::vector<glm::uvec2> border;
    CHECK(!extrudeEdges(empty, {}, {}, 0.5f, out, border, status));
    CHECK(!extrudeEdges(cube, {}, {}, 0.5f, out, border, status));
    std::printf("  %s\n", status.c_str());
    const glm::uvec2 bad[] = {{0u, 0u}, {7u, 999u}, {0u, 1u}}; // a loop, out of range, an inner edge of a closed cube
    CHECK(!extrudeEdges(cube, bad, {}, 0.5f, out, border, status));
    // A degenerate triangle in the selection is skipped, its neighbours still extrude.
    Mesh broken = makeCube(2);
    uint32_t tb = 0;
    while (broken.facegroupAt(tb) != kPlusX) ++tb;
    broken.triangles[tb].y = broken.triangles[tb].x;
    broken.rebuildTopology();
    CHECK(extrudeFaces(broken, selectGroup(broken, kPlusX), {}, 0.5f, 0.0f, out, cap, status));
    std::printf("  %s\n", status.c_str());
    CHECK(out.triangleCount() > broken.triangleCount());
    for (const glm::vec3& p : out.positions) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    // A pinched vertex (two fans) with one fan selected: the fan splits off, the other stays put.
    Mesh pinched = makeCube(1);
    {
        Mesh other = makeCube(1);
        for (glm::vec3& p : other.positions) p += glm::vec3(2.0f, 2.0f, 0.0f); // touches at the corner (1, 1, z)
        const uint32_t base = uint32_t(pinched.vertexCount()), tbase = uint32_t(pinched.triangleCount());
        pinched.positions.insert(pinched.positions.end(), other.positions.begin(), other.positions.end());
        for (glm::uvec3 t : other.triangles) pinched.triangles.push_back(t + glm::uvec3(base));
        for (uint32_t t = 0; t < other.triangleCount(); ++t) {
            pinched.facegroups.push_back(other.facegroups[t] + 6);
            if (other.quadPartners[t] != Mesh::kNoQuad) pinched.pairQuad(tbase + t, tbase + other.quadPartners[t]);
        }
        pinched = weldImported(pinched); // welds the two corner vertices at (1, 1, +-1): two fans each
        CHECK(pinched.vertexCount() == 2 * 8 - 2);
    }
    CHECK(extrudeFaces(pinched, selectGroup(pinched, kPlusX), {}, 0.5f, 0.0f, out, cap, status));
    for (uint32_t v = 0; v < pinched.vertexCount(); ++v)
        if (pinched.positions[v].x > 1.5f) CHECK(out.positions[v] == pinched.positions[v]); // the other cube stays
    CHECK(out.vertexCount() == pinched.vertexCount() + 4);
}

TEST(poly_extrude_big_selection_is_quick) {
    const Mesh cube = makeCube(200); // 240k triangles
    const std::vector<uint8_t> sel = selectGroup(cube, kPlusX); // 80k triangles
    const auto t0 = std::chrono::steady_clock::now();
    const ExtrudePlan plan = planExtrude(cube, sel, {});
    const auto t1 = std::chrono::steady_clock::now();
    Mesh out = buildExtrude(cube, plan, 0.1f, 0.0f);
    const auto t2 = std::chrono::steady_clock::now();
    moveExtrude(cube, plan, 0.2f, 0.0f, out);
    const auto t3 = std::chrono::steady_clock::now();
    const double plan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double build_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    const double move_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
    std::printf("  240k triangles, 40k polygons: plan %.0f ms, build %.0f ms, move %.0f ms\n", plan_ms, build_ms, move_ms);
    CHECK(plan.ok());
    CHECK(plan.walls == 800);
    CHECK(closedAndOriented(out));
    CHECK(plan_ms < timeLimit(1500.0));
    CHECK(build_ms < timeLimit(3000.0));
    CHECK(move_ms < timeLimit(300.0));
}

// --- The Polygon mode op (core/PolyOps.h): poly_op op=extrude on faces and on open border edges, undo and preview ---
namespace {
const CommandRegistry& commands() {
    static CommandRegistry r;
    return r;
}
// A face of the +X side of a cube from makeCube: the lower triangle of a quad whose vertices all have x > 0.9.
uint32_t plusXFace(const Mesh& m) {
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3& tri = m.triangles[t];
        if (m.facegroupAt(t) == kPlusX && m.quadPartnerAt(t) > t) return t;
    }
    return 0;
}
} // namespace

TEST(poly_extrude_op_runs_through_the_registry) {
    CHECK(PolyOpRegistry::instance().find("extrude") != nullptr);
    Document doc;
    doc.setMesh(makeCube(2));
    const Mesh before = doc.mesh;
    const CommandRegistry& reg = commands();
    // The whole +X side (its face group) as a region, at the default distance: one whole-mesh undo step, the cap
    // selected afterwards, the walls in a new group.
    CommandResult r = reg.execute(doc, "poly_select", {{"op", std::string("group")}, {"group", double(kPlusX)}});
    CHECK(r.ok);
    CHECK(doc.polySelection.count() == 4);
    const size_t steps = doc.undo.undoCount();
    r = reg.execute(doc, "poly_op", {{"op", std::string("extrude")}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok);
    CHECK(r.message.find("Extruded 4 faces (8 walls)") != std::string::npos);
    CHECK(doc.mesh.triangleCount() == before.triangleCount() + 16);
    CHECK(doc.undo.undoCount() == steps + 1);
    CHECK(doc.polySelection.element == PolyElement::Faces);
    CHECK(doc.polySelection.count() == 4);
    const float size = glm::length(before.bounds().extent());
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t)
        if (doc.polySelection.faceSelected(t))
            for (int k = 0; k < 3; ++k) CHECK(near(doc.mesh.positions[doc.mesh.triangles[t][k]].x, 1.0f + 0.1f * size, 1e-4f));
    CHECK(nextFaceGroupId(doc.mesh) == nextFaceGroupId(before) + 1);
    CHECK(closedAndOriented(doc.mesh));
    // Undo takes it back in one step.
    r = reg.execute(doc, "undo", {});
    CHECK(r.ok);
    CHECK(doc.mesh.triangleCount() == before.triangleCount());
    CHECK(doc.mesh.positions == before.positions);
    // Individual, sunk in: four blocks, the volume down.
    reg.execute(doc, "poly_select", {{"op", std::string("group")}, {"group", double(kPlusX)}});
    r = reg.execute(doc, "poly_op", {{"op", std::string("extrude")}, {"distance", -0.1}, {"individual", true}});
    CHECK(r.ok);
    CHECK(r.message.find("(16 walls)") != std::string::npos);
    CHECK(volume(doc.mesh) < volume(before));
    CHECK(closedAndOriented(doc.mesh));
    reg.execute(doc, "undo", {});
    // Points can't be extruded: a pointer to Faces / Edges.
    reg.execute(doc, "poly_select", {{"op", std::string("group")}, {"group", double(kPlusX)}});
    reg.execute(doc, "poly_mode", {{"element", std::string("points")}});
    r = reg.execute(doc, "poly_op", {{"op", std::string("extrude")}});
    CHECK(!r.ok);
    CHECK(r.message.find("Faces") != std::string::npos);
    std::printf("  %s\n", r.message.c_str());
    // The preview never touches the Part and gives the same mesh the op lands.
    reg.execute(doc, "poly_mode", {{"element", std::string("faces")}});
    reg.execute(doc, "poly_select", {{"op", std::string("group")}, {"group", double(kPlusX)}});
    PolyOpOutput out;
    std::string err;
    CHECK(previewPolyOp(doc, "extrude", {{"distance", 0.2}}, out, err));
    CHECK(doc.mesh.triangleCount() == before.triangleCount());
    CHECK(out.mesh.triangleCount() == before.triangleCount() + 16);
    CHECK(out.triangles.size() == 8);
    r = reg.execute(doc, "poly_op", {{"op", std::string("extrude")}, {"distance", 0.2}});
    CHECK(r.ok);
    CHECK(doc.mesh.positions == out.mesh.positions);
    CHECK(doc.mesh.triangles == out.mesh.triangles);
}

TEST(poly_extrude_op_on_open_border_edges) {
    Document doc;
    doc.setMesh(makeCube(2));
    const CommandRegistry& reg = commands();
    // Delete the +X side, select every edge: only the hole's 8 border edges extrude, the rest are left alone.
    reg.execute(doc, "poly_select", {{"op", std::string("group")}, {"group", double(kPlusX)}});
    CommandResult r = reg.execute(doc, "poly_op", {{"op", std::string("delete")}});
    CHECK(r.ok);
    const Mesh open = doc.mesh;
    CHECK(boundaryLoops(open).size() == 1);
    reg.execute(doc, "poly_mode", {{"element", std::string("edges")}});
    reg.execute(doc, "poly_select", {{"op", std::string("all")}});
    r = reg.execute(doc, "poly_op", {{"op", std::string("extrude")}, {"distance", 0.1}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok);
    CHECK(r.message.find("Extruded 8 edges") != std::string::npos);
    CHECK(doc.mesh.triangleCount() == open.triangleCount() + 16);
    CHECK(doc.mesh.vertexCount() == open.vertexCount() + 8);
    CHECK(boundaryLoops(doc.mesh).size() == 1);
    // The new border is selected (8 edges), outward: the strip's points sit beyond x = 1.
    CHECK(doc.polySelection.element == PolyElement::Edges);
    CHECK(doc.polySelection.count() == 8);
    for (uint32_t v = uint32_t(open.vertexCount()); v < doc.mesh.vertexCount(); ++v) CHECK(doc.mesh.positions[v].x > 1.0f + 1e-4f);
    // Along the normals instead: the strip stands on the sides (x stays 1, the points move out sideways).
    reg.execute(doc, "undo", {});
    reg.execute(doc, "poly_select", {{"op", std::string("all")}});
    r = reg.execute(doc, "poly_op", {{"op", std::string("extrude")}, {"distance", 0.1}, {"normals", true}});
    CHECK(r.ok);
    for (uint32_t v = uint32_t(open.vertexCount()); v < doc.mesh.vertexCount(); ++v) CHECK(near(doc.mesh.positions[v].x, 1.0f, 1e-4f));
    // Only inner edges selected: refused with the reason.
    reg.execute(doc, "undo", {});
    doc.setMesh(makeCube(2));
    reg.execute(doc, "poly_mode", {{"element", std::string("edges")}});
    reg.execute(doc, "poly_select", {{"op", std::string("all")}});
    r = reg.execute(doc, "poly_op", {{"op", std::string("extrude")}});
    CHECK(!r.ok);
    CHECK(r.message.find("open border") != std::string::npos);
}
