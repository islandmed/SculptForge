// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inset faces (core/PolyInset.h and its Polygon mode op): shapes (a cell of a plane, a cube side, a cylinder cap, an icosphere, a curved
// cap, an L, two cells touching at a corner, a whole open plane), the thickness clamp, attributes (face groups,
// UVs, seams, creases, paint, mask, sculpt and paint layers), refusals and bad input.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/EdgeSelect.h"
#include "core/FaceGroups.h"
#include "core/MeshCheck.h"
#include "core/PaintLayer.h"
#include "core/PolyInset.h"
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

size_t issue(const Mesh& m, MeshIssue i) { return checkMesh(m).count[int(i)]; }

// A result that is as clean as its input: no non-manifold edges, nothing flipped, degenerate or zero-area, no new
// holes, every position finite, and the quad pairs the rim claims are real.
bool clean(const Mesh& before, const InsetResult& r) {
    if (!r.ok) {
        std::printf("  inset refused: %s\n", r.error.c_str());
        return false;
    }
    const Mesh& m = r.mesh;
    bool ok = true;
    for (const glm::vec3& p : m.positions)
        ok &= std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    const MeshReport b = checkMesh(before), a = checkMesh(m);
    for (MeshIssue i : {MeshIssue::NonManifold, MeshIssue::Flipped, MeshIssue::Degenerate, MeshIssue::ZeroArea, MeshIssue::Holes})
        if (a.count[int(i)] > b.count[int(i)]) {
            std::printf("  %s: %zu before, %zu after\n", meshIssueName(i), b.count[int(i)], a.count[int(i)]);
            ok = false;
        }
    for (size_t i = 0; i < r.rim.size(); ++i) { // a rim quad's halves are adjacent and paired; a merged corner leaves a triangle
        const uint32_t u = m.quadPartnerAt(r.rim[i]);
        if (u != Mesh::kNoQuad) ok &= (i + 1 < r.rim.size() && u == r.rim[i + 1]) || (i > 0 && u == r.rim[i - 1]);
    }
    ok &= m.triangleCount() <= before.triangleCount() + r.rim.size();
    ok &= r.selection.size() <= before.triangleCount();
    for (uint32_t t : r.selection) ok &= t < m.triangleCount();
    ok &= m.vertexCount() == before.vertexCount() + r.newVertices;
    return ok;
}

// The triangles of face group `g`.
std::vector<uint32_t> group(const Mesh& m, uint32_t g) {
    std::vector<uint32_t> out;
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        if (m.facegroupAt(t) == g) out.push_back(t);
    return out;
}

// The two triangles of plane cell (i, j) of makePlane(n).
std::vector<uint32_t> cell(int n, int i, int j) {
    const uint32_t t = uint32_t(2 * (j * n + i));
    return {t, t + 1};
}

glm::vec3 normal(const Mesh& m, uint32_t t) {
    const glm::uvec3& tri = m.triangles[t];
    return glm::normalize(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]));
}

std::vector<uint32_t> all(const Mesh& m) {
    std::vector<uint32_t> out(m.triangleCount());
    for (uint32_t t = 0; t < out.size(); ++t) out[t] = t;
    return out;
}

} // namespace

TEST(poly_inset_one_plane_cell_individual) {
    const Mesh plane = makePlane(4); // cells 0.5 wide, y up
    InsetOptions o;
    o.thickness = 0.1f;
    o.individual = true;
    const InsetResult r = insetFaces(plane, cell(4, 1, 2), o);
    CHECK(clean(plane, r));
    CHECK(r.regions == 1);
    CHECK(r.newVertices == 4);
    CHECK(r.rim.size() == 8);
    CHECK(!r.clamped);
    CHECK(std::abs(r.thickness - 0.1f) < 1e-6f);
    CHECK(r.selection == cell(4, 1, 2));
    // The inner quad: the cell shrunk by 0.1 on every side, still in the plane, facing up.
    const Mesh& m = r.mesh;
    for (uint32_t v = uint32_t(plane.vertexCount()); v < m.vertexCount(); ++v) {
        const glm::vec3 p = m.positions[v];
        CHECK(std::abs(p.y) < 1e-6f);
        // cell (1, 2): x in [-0.5, 0], z in [0, 0.5]
        CHECK(std::abs(std::abs(p.x + 0.25f) - 0.15f) < 1e-5f);
        CHECK(std::abs(std::abs(p.z - 0.25f) - 0.15f) < 1e-5f);
    }
    for (uint32_t t : r.selection) CHECK(glm::dot(normal(m, t), glm::vec3(0, 1, 0)) > 0.999f);
    for (uint32_t t : r.rim) CHECK(glm::dot(normal(m, t), glm::vec3(0, 1, 0)) > 0.999f);
    // The other cells are untouched.
    for (uint32_t t = 0; t < plane.triangleCount(); ++t)
        if (t != r.selection[0] && t != r.selection[1]) CHECK(m.triangles[t] == plane.triangles[t]);
}

TEST(poly_inset_cube_side_region_with_depth) {
    const Mesh cube = makeCube(2); // one face group per side (1..6), sides at +-1
    const std::vector<uint32_t> side = group(cube, 1); // +X
    CHECK(side.size() == 8);
    InsetOptions o;
    o.thickness = 0.2f;
    o.depth = -0.1f;
    const InsetResult r = insetFaces(cube, side, o);
    CHECK(clean(cube, r));
    CHECK(r.regions == 1);
    CHECK(r.newVertices == 8); // the side's border loop
    CHECK(r.rim.size() == 16);
    CHECK(issue(r.mesh, MeshIssue::Holes) == 0);
    // The inner faces sit 0.1 in, 0.2 from the edges; the rim slopes between.
    for (uint32_t v = uint32_t(cube.vertexCount()); v < r.mesh.vertexCount(); ++v) {
        const glm::vec3 p = r.mesh.positions[v];
        CHECK(std::abs(p.x - 0.9f) < 1e-5f);
        CHECK(std::abs(p.y) < 0.8f + 1e-5f && std::abs(p.z) < 0.8f + 1e-5f);
    }
    // Volume: the cube less a frustum of height 0.1 between a 2x2 and a 1.6x1.6 square.
    const double frustum = 0.1 / 3.0 * (4.0 + 2.56 + std::sqrt(4.0 * 2.56));
    CHECK(std::abs((volume(cube) - volume(r.mesh)) - frustum) < 1e-3);
    // Face groups: the inner faces and the rim keep the side's group; the rest is unchanged.
    for (uint32_t t : r.selection) CHECK(r.mesh.facegroupAt(t) == 1);
    for (uint32_t t : r.rim) CHECK(r.mesh.facegroupAt(t) == 1);
    for (uint32_t t = 0; t < cube.triangleCount(); ++t) CHECK(r.mesh.facegroupAt(t) == cube.facegroupAt(t));
    CHECK(r.mesh.quadCount() == cube.quadCount() + 8);
}

TEST(poly_inset_rim_in_its_own_group) {
    const Mesh cube = makeCube(1);
    InsetOptions o;
    o.thickness = 0.3f;
    o.rimGroup = true;
    const InsetResult r = insetFaces(cube, group(cube, 3), o);
    CHECK(clean(cube, r));
    const uint32_t g = nextFaceGroupId(cube);
    for (uint32_t t : r.rim) CHECK(r.mesh.facegroupAt(t) == g);
    for (uint32_t t : r.selection) CHECK(r.mesh.facegroupAt(t) == 3);
    CHECK(nextFaceGroupId(r.mesh) == g + 1);
    // A mesh without face groups gets them only for the rim option.
    Mesh plain = cube;
    plain.clearFaceGroups();
    plain.clearQuads();
    const InsetResult p = insetFaces(plain, std::vector<uint32_t>{0u, 1u}, o);
    CHECK(clean(plain, p));
    CHECK(p.mesh.hasFaceGroups());
    for (uint32_t t : p.rim) CHECK(p.mesh.facegroupAt(t) == 1);
    o.rimGroup = false;
    const InsetResult q = insetFaces(plain, std::vector<uint32_t>{0u, 1u}, o);
    CHECK(clean(plain, q));
    CHECK(!q.mesh.hasFaceGroups());
}

TEST(poly_inset_whole_open_plane_insets_along_its_border) {
    const Mesh plane = makePlane(3);
    InsetOptions o;
    o.thickness = 0.1f;
    const InsetResult r = insetFaces(plane, all(plane), o);
    CHECK(clean(plane, r));
    CHECK(r.regions == 1);
    CHECK(r.newVertices == 12); // the 12 border vertices of a 3x3 grid
    CHECK(r.rim.size() == 24);
    CHECK(issue(r.mesh, MeshIssue::Holes) == 1); // still one open border, the same one
    // Border vertices stay where they were (the interior follows the new ring); the new ring is 0.1 inside the border.
    for (uint32_t v = 0; v < plane.vertexCount(); ++v)
        if (std::max(std::abs(plane.positions[v].x), std::abs(plane.positions[v].z)) > 0.99f) CHECK(r.mesh.positions[v] == plane.positions[v]);
    for (uint32_t v = uint32_t(plane.vertexCount()); v < r.mesh.vertexCount(); ++v) {
        const glm::vec3 p = r.mesh.positions[v];
        CHECK(std::max(std::abs(p.x), std::abs(p.z)) < 0.9f + 1e-5f);
        CHECK(std::max(std::abs(p.x), std::abs(p.z)) > 0.9f - 1e-5f);
    }
}

TEST(poly_inset_refusals) {
    const Mesh cube = makeCube(1);
    InsetOptions o;
    o.thickness = 0.1f;
    const InsetResult whole = insetFaces(cube, all(cube), o);
    CHECK(!whole.ok);
    CHECK(!whole.error.empty());
    const InsetResult none = insetFaces(cube, std::vector<uint32_t>{}, o);
    CHECK(!none.ok);
    const InsetResult bad = insetFaces(cube, std::vector<uint32_t>{999u}, o);
    CHECK(!bad.ok);
    CHECK(insetMaxThickness(cube, all(cube), false) == 0.0f);
    // Individually every face of a closed mesh has a border.
    o.individual = true;
    const InsetResult each = insetFaces(cube, all(cube), o);
    CHECK(clean(cube, each));
    CHECK(each.regions == 6);
    CHECK(each.newVertices == 24);
    CHECK(each.rim.size() == 48);
    CHECK(issue(each.mesh, MeshIssue::Holes) == 0);
}

TEST(poly_inset_cylinder_cap_fan) {
    const Mesh cyl = makeCylinder(12); // radius 0.6, top at y = 1; the top cap (group 3) is a fan of 12 inside rings of quads
    const std::vector<uint32_t> cap = group(cyl, 3);
    CHECK(cap.size() == 12 + 3 * 24);
    InsetOptions o;
    o.thickness = 0.1f; // (the cap's rings are 0.15 apart: 0.15 would land the new ring on the next one)
    o.depth = -0.05f;
    const InsetResult r = insetFaces(cyl, cap, o);
    CHECK(clean(cyl, r));
    CHECK(r.regions == 1);
    CHECK(r.newVertices == 12); // the cap's outer ring
    CHECK(r.rim.size() == 24);
    CHECK(issue(r.mesh, MeshIssue::Holes) == 0);
    const float top = 1.0f, radius = 0.6f;
    // The whole inner cap is flat, 0.05 down: the new ring and the cap's own vertices alike.
    std::vector<uint8_t> isCapVertex(cyl.vertexCount(), 0);
    for (uint32_t t : cap)
        for (int k = 0; k < 3; ++k) isCapVertex[cyl.triangles[t][k]] = 1;
    for (uint32_t t : r.selection)
        for (int k = 0; k < 3; ++k) CHECK(std::abs(r.mesh.positions[r.mesh.triangles[t][k]].y - (top - 0.05f)) < 1e-5f);
    // The rim's outer vertices (the side's top ring) did not move.
    for (uint32_t v = 0; v < cyl.vertexCount(); ++v)
        if (!isCapVertex[v] || glm::length(glm::vec2(cyl.positions[v].x, cyl.positions[v].z)) > radius - 1e-4f)
            CHECK(r.mesh.positions[v] == cyl.positions[v]);
    for (uint32_t v = uint32_t(cyl.vertexCount()); v < r.mesh.vertexCount(); ++v) {
        const float rr = glm::length(glm::vec2(r.mesh.positions[v].x, r.mesh.positions[v].z));
        CHECK(std::abs(rr - (radius - 0.1f * 1.0353f)) < 2e-3f); // a 12-gon's miter is 1 / cos(15 deg)
    }
    for (uint32_t t : r.selection) CHECK(normal(r.mesh, t).y > 0.999f);
    // The side's top edge keeps its creases' home: the rim slopes from the old ring down to the new one.
    for (uint32_t t : r.rim) CHECK(normal(r.mesh, t).y > 0.0f);
    const float maxT = insetMaxThickness(cyl, cap, false);
    CHECK(maxT > 0.5f && maxT <= radius);
}

TEST(poly_inset_every_icosphere_triangle_individually_clamps_and_never_flips) {
    const Mesh sphere = makeIcosphere(1);
    InsetOptions o;
    o.thickness = 10.0f; // far more than any triangle allows
    o.individual = true;
    const InsetResult r = insetFaces(sphere, all(sphere), o);
    CHECK(clean(sphere, r));
    CHECK(r.clamped);
    CHECK(r.thickness < 1.0f);
    CHECK(r.regions == sphere.triangleCount());
    CHECK(r.newVertices == sphere.triangleCount() * 3);
    CHECK(issue(r.mesh, MeshIssue::Holes) == 0);
    for (uint32_t t : r.selection) CHECK(glm::dot(normal(r.mesh, t), normal(sphere, t)) > 0.99f);
    // Not clamped: the smaller inner triangles keep the original's shape.
    o.thickness = 0.02f;
    const InsetResult s = insetFaces(sphere, all(sphere), o);
    CHECK(clean(sphere, s));
    CHECK(!s.clamped);
    for (uint32_t t : s.selection) CHECK(glm::dot(normal(s.mesh, t), normal(sphere, t)) > 0.999f);
}

TEST(poly_inset_clamp_matches_the_inradius) {
    const Mesh plane = makePlane(4); // square cells 0.5 wide: inradius 0.25
    const std::vector<uint32_t> one = cell(4, 0, 0);
    CHECK(std::abs(insetMaxThickness(plane, one, true) - 0.25f) < 1e-4f);
    InsetOptions o;
    o.thickness = 10.0f;
    o.individual = true;
    const InsetResult r = insetFaces(plane, one, o);
    CHECK(clean(plane, r));
    CHECK(r.clamped);
    CHECK(r.thickness < 0.25f && r.thickness > 0.2f);
    for (uint32_t t : r.selection) {
        CHECK(normal(r.mesh, t).y > 0.999f);
        const glm::uvec3& tri = r.mesh.triangles[t];
        CHECK(glm::length(glm::cross(r.mesh.positions[tri.y] - r.mesh.positions[tri.x], r.mesh.positions[tri.z] - r.mesh.positions[tri.x])) > 1e-5f);
    }
    // A rectangle 2 x 1 (two cells): the short side decides.
    std::vector<uint32_t> two = cell(4, 0, 0);
    for (uint32_t t : cell(4, 1, 0)) two.push_back(t);
    CHECK(std::abs(insetMaxThickness(plane, two, false) - 0.25f) < 1e-4f);
}

TEST(poly_inset_two_cells_touching_at_a_corner_get_two_vertices_there) {
    const Mesh plane = makePlane(2);
    std::vector<uint32_t> sel = cell(2, 0, 0);
    for (uint32_t t : cell(2, 1, 1)) sel.push_back(t);
    InsetOptions o;
    o.thickness = 0.1f;
    const InsetResult r = insetFaces(plane, sel, o);
    CHECK(clean(plane, r));
    CHECK(r.regions == 2);
    CHECK(r.newVertices == 8); // 4 per cell; the shared centre vertex is split
    CHECK(r.rim.size() == 16);
    // Each cell's new vertices lie inside that cell.
    int inFirst = 0, inSecond = 0;
    for (uint32_t v = uint32_t(plane.vertexCount()); v < r.mesh.vertexCount(); ++v) {
        const glm::vec3 p = r.mesh.positions[v];
        inFirst += p.x < 0.0f && p.z < 0.0f;
        inSecond += p.x > 0.0f && p.z > 0.0f;
    }
    CHECK(inFirst == 4 && inSecond == 4);
}

TEST(poly_inset_concave_L_region) {
    const Mesh plane = makePlane(3); // cells 2/3 wide
    std::vector<uint32_t> sel;
    for (const auto [i, j] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}})
        for (uint32_t t : cell(3, i, j)) sel.push_back(t);
    InsetOptions o;
    o.thickness = 0.1f;
    const InsetResult r = insetFaces(plane, sel, o);
    CHECK(clean(plane, r));
    CHECK(r.regions == 1);
    CHECK(r.newVertices == 8); // an L of three squares has 8 corners (the inner vertex is surrounded, not a corner)
    // Every corner of the L is a right angle, so each new vertex is 0.1 * sqrt(2) from its old one; the reflex
    // corner moves into the L (toward negative x and z) like the others move inward.
    const float w = 2.0f / 3.0f;
    // The reflex corner of the L is the plane vertex at (-1 + w, 0, -1 + w).
    bool reflexHandled = false;
    for (uint32_t v = uint32_t(plane.vertexCount()); v < r.mesh.vertexCount(); ++v) {
        const glm::vec3 p = r.mesh.positions[v];
        const glm::vec3 reflex(-1.0f + w, 0.0f, -1.0f + w);
        if (glm::length(p - reflex) < 0.2f) {
            reflexHandled = true;
            CHECK(std::abs(glm::length(p - reflex) - 0.1f * std::sqrt(2.0f)) < 1e-4f);
            CHECK(p.x < reflex.x && p.z < reflex.z);
        }
    }
    CHECK(reflexHandled);
    for (uint32_t t : r.selection) CHECK(normal(r.mesh, t).y > 0.999f);
    for (uint32_t t : r.rim) CHECK(normal(r.mesh, t).y > 0.999f);
}

TEST(poly_inset_curved_cap_follows_the_surface_normals) {
    const Mesh sphere = makeQuadSphere(8);
    std::vector<uint32_t> cap;
    for (uint32_t t = 0; t < sphere.triangleCount(); ++t) {
        const glm::uvec3& tri = sphere.triangles[t];
        if (((sphere.positions[tri.x] + sphere.positions[tri.y] + sphere.positions[tri.z]) / 3.0f).z > 0.75f) cap.push_back(t);
    }
    CHECK(cap.size() > 20);
    InsetOptions o;
    o.thickness = 0.05f;
    o.depth = -0.1f;
    const InsetResult r = insetFaces(sphere, cap, o);
    CHECK(clean(sphere, r));
    CHECK(issue(r.mesh, MeshIssue::Holes) == 0);
    const float radius = glm::length(sphere.positions[0]);
    // Every vertex of the inner cap (new ring and the cap's own) sits about the depth below the sphere.
    for (uint32_t t : r.selection)
        for (int k = 0; k < 3; ++k) {
            const float rr = glm::length(r.mesh.positions[r.mesh.triangles[t][k]]);
            CHECK(rr < radius - 0.08f && rr > radius - 0.12f);
        }
    // Nothing outside the cap moved.
    for (uint32_t v = 0; v < sphere.vertexCount(); ++v)
        if (glm::length(r.mesh.positions[v]) > radius - 1e-4f) CHECK(r.mesh.positions[v] == sphere.positions[v]);
    for (uint32_t t : r.selection) CHECK(glm::dot(normal(r.mesh, t), normal(sphere, t)) > 0.98f);
    CHECK(volume(r.mesh) < volume(sphere));
}

TEST(poly_inset_keeps_uvs_paint_mask_and_layers) {
    Mesh plane = makePlane(4);
    // UVs = an affine map of the position, so the right UV for any point is known.
    plane.uvs.resize(plane.triangleCount() * 3);
    for (uint32_t t = 0; t < plane.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = plane.positions[plane.triangles[t][k]];
            plane.uvs[3 * t + k] = glm::vec2(0.5f + 0.25f * p.x, 0.5f - 0.25f * p.z);
        }
    plane.ensureColors();
    plane.ensureMask();
    for (uint32_t v = 0; v < plane.vertexCount(); ++v) {
        plane.colors[v] = glm::vec3(0.5f + 0.25f * plane.positions[v].x, 0.2f, 0.5f + 0.25f * plane.positions[v].z);
        plane.mask[v] = v % 3 == 0 ? 1.0f : 0.25f;
    }
    const int layer = plane.addLayer("bump");
    plane.layers[layer].delta[5] = glm::vec3(0.0f, 0.1f, 0.0f);
    plane.setLayerWeight(layer, 1.0f);
    const int paint = addPaintLayer(plane, "tint");
    CHECK(paint >= 0);
    plane.rebuildTopology();
    const Mesh before = plane;

    std::vector<uint32_t> sel = cell(4, 1, 1);
    for (uint32_t t : cell(4, 2, 1)) sel.push_back(t);
    InsetOptions o;
    o.thickness = 0.08f;
    o.depth = 0.05f;
    const InsetResult r = insetFaces(plane, sel, o);
    CHECK(clean(before, r));
    const Mesh& m = r.mesh;
    CHECK(m.uvs.size() == m.triangleCount() * 3);
    // Every corner's UV is the affine map of its position (the depth is along y and does not count).
    float worst = 0.0f;
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = m.positions[m.triangles[t][k]];
            worst = std::max(worst, glm::length(m.uvs[3 * t + k] - glm::vec2(0.5f + 0.25f * p.x, 0.5f - 0.25f * p.z)));
        }
    std::printf("  worst UV error %.2e\n", worst);
    CHECK(worst < 1e-4f);
    // The new vertices carry their source's paint and mask; the paint stack is still a stack of the right size.
    CHECK(m.colors.size() == m.vertexCount() && m.mask.size() == m.vertexCount());
    for (uint32_t v = uint32_t(before.vertexCount()); v < m.vertexCount(); ++v) {
        bool fromSome = false;
        for (uint32_t u = 0; u < before.vertexCount() && !fromSome; ++u)
            fromSome = before.colors[u] == m.colors[v] && before.mask[u] == m.mask[v] && glm::length(before.positions[u] - m.positions[v]) < 0.2f;
        CHECK(fromSome);
    }
    CHECK(hasPaintLayers(m));
    CHECK(m.paintBase.size() == m.vertexCount());
    for (const PaintLayer& l : m.paintLayers) CHECK(l.color.size() == m.vertexCount() && l.alpha.size() == m.vertexCount());
    // Sculpt layers: base and every delta sized to the mesh, positions = base + weight * delta.
    CHECK(m.base.size() == m.vertexCount());
    for (const SculptLayer& l : m.layers) CHECK(l.delta.size() == m.vertexCount());
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        glm::vec3 p = m.base[v];
        for (const SculptLayer& l : m.layers) p += l.weight * l.delta[v];
        CHECK(glm::length(p - m.positions[v]) < 1e-6f);
    }
    CHECK(glm::length(m.positions[5] - before.positions[5]) < 1e-6f); // the bumped vertex is where it was
}

TEST(poly_inset_seams_and_creases_stay_with_the_old_edge) {
    Mesh plane = makePlane(3);
    plane.ensureCreases();
    plane.uvFlags.assign(plane.triangleCount() * 3, 0);
    plane.uvs.assign(plane.triangleCount() * 3, glm::vec2(0.5f));
    const std::vector<uint32_t> sel = cell(3, 1, 1);
    // Crease + seam the cell's edges on both sides; pin one of its corners.
    const std::vector<uint32_t> twins = cornerTwins(plane);
    int creased = 0;
    for (uint32_t t : sel)
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = 3 * t + k, tw = twins[c];
            if (tw != kNoCorner && (tw / 3 == sel[0] || tw / 3 == sel[1])) continue; // the diagonal
            plane.creases[c] = 1.0f;
            plane.uvFlags[c] |= kUvSeam | kUvPin;
            if (tw != kNoCorner) {
                plane.creases[tw] = 1.0f;
                plane.uvFlags[tw] |= kUvSeam;
            }
            ++creased;
        }
    CHECK(creased == 4);
    InsetOptions o;
    o.thickness = 0.1f;
    const InsetResult r = insetFaces(plane, sel, o);
    CHECK(clean(plane, r));
    const Mesh& m = r.mesh;
    CHECK(m.creases.size() == m.triangleCount() * 3 && m.uvFlags.size() == m.triangleCount() * 3);
    // The inner faces: no crease, no seam, no pin left on their (moved) corners.
    for (uint32_t t : r.selection)
        for (int k = 0; k < 3; ++k) {
            CHECK(m.creases[3 * t + k] == 0.0f);
            CHECK(m.uvFlags[3 * t + k] == 0);
        }
    // The rim: its outer edge (corner 0 of the first triangle of each quad) carries the crease and seam, and the
    // pin of the old corner; the new corners nothing. The neighbours across the old edge are untouched.
    for (size_t i = 0; i < r.rim.size(); i += 2) {
        const uint32_t t1 = r.rim[i], t2 = r.rim[i + 1];
        CHECK(m.creases[3 * t1] == 1.0f);
        CHECK((m.uvFlags[3 * t1] & kUvSeam) != 0);
        CHECK((m.uvFlags[3 * t1] & kUvPin) != 0);
        CHECK(m.creases[3 * t1 + 1] == 0.0f && m.creases[3 * t1 + 2] == 0.0f);
        CHECK(m.uvFlags[3 * t1 + 2] == 0);
        for (int k = 0; k < 3; ++k) CHECK(m.creases[3 * t2 + k] == 0.0f);
        CHECK(m.uvFlags[3 * t2 + 1] == 0 && m.uvFlags[3 * t2 + 2] == 0);
    }
    for (uint32_t t = 0; t < plane.triangleCount(); ++t) {
        if (t == sel[0] || t == sel[1]) continue;
        for (int k = 0; k < 3; ++k) {
            CHECK(m.creases[3 * t + k] == plane.creases[3 * t + k]);
            CHECK(m.uvFlags[3 * t + k] == plane.uvFlags[3 * t + k]);
        }
    }
}

TEST(poly_inset_negative_thickness_outsets) {
    const Mesh plane = makePlane(4);
    InsetOptions o;
    o.thickness = -0.1f;
    o.individual = true;
    const InsetResult r = insetFaces(plane, cell(4, 1, 1), o);
    CHECK(r.ok);
    CHECK(!r.clamped);
    CHECK(issue(r.mesh, MeshIssue::NonManifold) == 0);
    // The inner quad grew by 0.1 on every side.
    for (uint32_t v = uint32_t(plane.vertexCount()); v < r.mesh.vertexCount(); ++v) {
        const glm::vec3 p = r.mesh.positions[v];
        CHECK(std::abs(std::abs(p.x + 0.25f) - 0.35f) < 1e-5f);
        CHECK(std::abs(std::abs(p.z + 0.25f) - 0.35f) < 1e-5f);
    }
    // Its rim is turned over (the usual outset look); the inner face still faces up.
    for (uint32_t t : r.selection) CHECK(normal(r.mesh, t).y > 0.999f);
}

TEST(poly_inset_selection_input_is_forgiving) {
    const Mesh cube = makeCube(2);
    InsetOptions o;
    o.thickness = 0.2f;
    const std::vector<uint32_t> side = group(cube, 2);
    const InsetResult whole = insetFaces(cube, side, o);
    // Duplicates, unsorted, only one half of each quad: the same result.
    std::vector<uint32_t> messy;
    for (size_t i = side.size(); i-- > 0;)
        if (i % 2 == 0) {
            messy.push_back(side[i]);
            messy.push_back(side[i]);
        }
    const InsetResult half = insetFaces(cube, messy, o);
    CHECK(clean(cube, half));
    CHECK(half.selection == whole.selection);
    CHECK(half.newVertices == whole.newVertices);
    CHECK(half.mesh.triangleCount() == whole.mesh.triangleCount());
    CHECK(std::abs(volume(half.mesh) - volume(whole.mesh)) < 1e-9);
}

TEST(poly_inset_degenerate_and_open_input_never_crashes) {
    // A plane with a degenerate triangle (one point twice), a zero-area one and a dangling fin.
    Mesh m = makePlane(3);
    m.clearQuads();
    const uint32_t a = m.triangles[0].x, b = m.triangles[0].y;
    m.triangles.push_back({a, a, b});
    const uint32_t n = uint32_t(m.positions.size());
    m.positions.push_back(m.positions[a] + 0.5f * (m.positions[b] - m.positions[a])); // on the edge
    m.triangles.push_back({a, n, b});                                                 // zero area
    m.positions.push_back(m.positions[a] + glm::vec3(0.0f, 1.0f, 0.0f));
    m.triangles.push_back({a, b, n + 1}); // a fin on edge a-b: non-manifold there
    m.rebuildTopology();
    m.recomputeAllNormals();
    for (bool individual : {false, true}) {
        InsetOptions o;
        o.thickness = 0.1f;
        o.individual = individual;
        const InsetResult r = insetFaces(m, all(m), o);
        CHECK(r.ok);
        for (const glm::vec3& p : r.mesh.positions) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        std::printf("  non-manifold edges: %zu before, %zu after\n", issue(m, MeshIssue::NonManifold), issue(r.mesh, MeshIssue::NonManifold));
        CHECK(issue(r.mesh, MeshIssue::NonManifold) <= issue(m, MeshIssue::NonManifold) + 6); // the fin's edge and its rims, nothing else
        r.mesh.bounds();
    }
    // An isolated triangle (nothing around it).
    Mesh tri;
    tri.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    tri.triangles = {{0, 1, 2}};
    tri.rebuildTopology();
    tri.recomputeAllNormals();
    InsetOptions o;
    o.thickness = 0.1f;
    const InsetResult r = insetFaces(tri, std::vector<uint32_t>{0u}, o);
    CHECK(clean(tri, r));
    CHECK(r.newVertices == 3 && r.rim.size() == 6);
    CHECK(normal(r.mesh, 0).z > 0.999f);
    for (uint32_t t : r.rim) CHECK(normal(r.mesh, t).z > 0.999f);
    // Thickness beyond the inradius is clamped, not inverted.
    o.thickness = 5.0f;
    const InsetResult big = insetFaces(tri, std::vector<uint32_t>{0u}, o);
    CHECK(clean(tri, big));
    CHECK(big.clamped && normal(big.mesh, 0).z > 0.999f);
}

TEST(poly_inset_deep_inset_of_a_gridded_side_flows_corners_in) {
    // A 10 x 10 side of a cube inset by three cells: without merging, the edges beside each corner would fold the rim
    // over (the corner's new vertex passes its neighbours'); with it they flow into the corner and the inner panel is
    // a clean 7 x 7-ish grid, the interior rings having followed the border.
    const Mesh cube = makeCube(10); // cells 0.2 wide
    const std::vector<uint32_t> side = group(cube, 3); // +Y
    CHECK(side.size() == 200);
    InsetOptions o;
    o.thickness = 0.6f;
    o.depth = -0.1f;
    const InsetResult r = insetFaces(cube, side, o);
    CHECK(clean(cube, r));
    CHECK(!r.clamped);
    CHECK(issue(r.mesh, MeshIssue::Holes) == 0);
    CHECK(issue(r.mesh, MeshIssue::Flipped) == 0);
    // The new border ring is the square 0.6 in from the side's edges (|x|, |z| <= 0.4), 0.1 down.
    size_t atCorner = 0;
    for (uint32_t v = uint32_t(cube.vertexCount()); v < r.mesh.vertexCount(); ++v) {
        const glm::vec3 p = r.mesh.positions[v];
        CHECK(std::abs(p.y - 0.9f) < 1e-4f);
        CHECK(std::max(std::abs(p.x), std::abs(p.z)) < 0.4f + 1e-3f);
        CHECK(std::max(std::abs(p.x), std::abs(p.z)) > 0.4f - 1e-3f);
        atCorner += std::abs(std::abs(p.x) - 0.4f) < 1e-3f && std::abs(std::abs(p.z) - 0.4f) < 1e-3f;
    }
    CHECK(r.newVertices < 40);  // the 40 border vertices less the merged ones beside the corners
    CHECK(atCorner == 4);        // one vertex per corner, the merged ones are no extras
    // Every inner face faces up and has area; the interior rings moved in with the border (no vertex of the inner
    // panel is left outside the new ring).
    float worstOut = 0.0f;
    uint32_t worstV = 0;
    int badNormals = 0;
    for (uint32_t t : r.selection) {
        badNormals += normal(r.mesh, t).y > 0.999f ? 0 : 1;
        for (int k = 0; k < 3; ++k) {
            const uint32_t v = r.mesh.triangles[t][k];
            const glm::vec3 p = r.mesh.positions[v];
            if (std::max(std::abs(p.x), std::abs(p.z)) > worstOut) {
                worstOut = std::max(std::abs(p.x), std::abs(p.z));
                worstV = v;
            }
            CHECK(std::abs(p.y - 0.9f) < 1e-4f);
        }
    }
    std::printf("  inner faces with a tilted normal: %d; farthest inner vertex %u (%s) at %.3f from the centre line\n", badNormals, worstV,
                worstV < cube.vertexCount() ? "original" : "new", worstOut);
    CHECK(badNormals == 0);
    CHECK(worstOut < 0.4f + 1e-3f);
    CHECK(r.selection.size() < side.size()); // the cells that collapsed into the corners are gone
    CHECK(r.selection.size() > side.size() / 2);
    // The other sides did not move at all.
    for (uint32_t v = 0; v < cube.vertexCount(); ++v)
        if (cube.positions[v].y < 0.999f) CHECK(r.mesh.positions[v] == cube.positions[v]);
    // The limit of the side as one shape is half its width, not one cell.
    const float maxT = insetMaxThickness(cube, side, false);
    CHECK(maxT > 0.9f && maxT <= 1.0f + 1e-3f);
}

TEST(poly_inset_interior_follows_the_border_evenly) {
    const Mesh plane = makePlane(8); // cells 0.25 wide
    InsetOptions o;
    o.thickness = 0.5f; // two cells
    const InsetResult r = insetFaces(plane, all(plane), o);
    CHECK(clean(plane, r));
    CHECK(!r.clamped);
    // The inner grid is close to the 8 x 8 grid scaled by 0.5 (interior vertex (i, j) near ((i / 8 - 0.5), (j / 8 - 0.5))):
    // exact along the middle, pulled a little toward the corners beside them (the border vertices beside each corner
    // merged into it), and never crossing: x grows along every row, z along every column.
    float worstMiddle = 0.0f, worstAll = 0.0f;
    for (int j = 1; j < 8; ++j)
        for (int i = 1; i < 8; ++i) {
            const uint32_t v = uint32_t(j * 9 + i);
            const glm::vec3 p = r.mesh.positions[v];
            const glm::vec3 want((float(i) / 8.0f - 0.5f), 0.0f, (float(j) / 8.0f - 0.5f));
            const float off = glm::length(p - want);
            worstAll = std::max(worstAll, off);
            if (i >= 3 && i <= 5 && j >= 3 && j <= 5) worstMiddle = std::max(worstMiddle, off);
            CHECK(std::max(std::abs(p.x), std::abs(p.z)) < 0.5f - 0.02f);
            if (i > 1) CHECK(p.x > r.mesh.positions[uint32_t(j * 9 + i - 1)].x + 0.02f);
            if (j > 1) CHECK(p.z > r.mesh.positions[uint32_t((j - 1) * 9 + i)].z + 0.02f);
        }
    std::printf("  interior off the evenly shrunk grid by at most %.4f in the middle, %.4f beside the corners (cell 0.25)\n", worstMiddle, worstAll);
    CHECK(worstMiddle < 0.03f);
    CHECK(worstAll < 0.15f);
    for (uint32_t t : r.selection) CHECK(normal(r.mesh, t).y > 0.999f);
    for (uint32_t t : r.rim) CHECK(normal(r.mesh, t).y > 0.999f);
}

TEST(poly_inset_speed_on_a_medium_mesh) {
    const Mesh plane = makePlane(300); // 180k triangles, 90k quads
    InsetOptions o;
    o.thickness = 0.001f;
    o.individual = true;
    const auto t0 = std::chrono::steady_clock::now();
    const InsetResult r = insetFaces(plane, all(plane), o);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  inset of 90k quads individually: %.0f ms\n", ms);
    CHECK(r.ok);
    CHECK(r.regions == 90000);
    CHECK(ms < timeLimit(2500.0));
    const auto t1 = std::chrono::steady_clock::now();
    o.individual = false;
    const InsetResult s = insetFaces(plane, all(plane), o);
    const double ms2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    std::printf("  inset of the whole plane as one region: %.0f ms\n", ms2);
    CHECK(s.ok && s.newVertices == 1200);
    CHECK(ms2 < timeLimit(2500.0));
}

// --- The Polygon mode op (core/PolyOps.h) ---

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

// The lower triangle of a quad on the +Z side of a cube from makeCube.
uint32_t frontFace(const Mesh& m) {
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3& tri = m.triangles[t];
        if (edgePolygon(m, t) != t) continue;
        if (m.positions[tri.x].z > 0.9f && m.positions[tri.y].z > 0.9f && m.positions[tri.z].z > 0.9f) return t;
    }
    return 0;
}

} // namespace

TEST(poly_inset_op_runs_through_poly_op_as_one_undo_step) {
    Document doc;
    doc.setMesh(makeCube(2)); // size 2: the diagonal is 2 sqrt(3) = 3.46
    const CommandRegistry& reg = registry();
    const size_t tris = doc.mesh.triangleCount(), verts = doc.mesh.vertexCount();
    // Listed with its parameters.
    CommandResult r = reg.execute(doc, "poly_op", {{"op", std::string("list")}});
    CHECK(r.ok);
    CHECK(r.message.find("inset") != std::string::npos);
    CHECK(r.message.find("thickness") != std::string::npos);
    // Nothing selected: the registry says what to select.
    r = reg.execute(doc, "poly_op", {{"op", std::string("inset")}});
    CHECK(!r.ok);
    CHECK(r.message.find("Inset needs") != std::string::npos);
    // One face, the defaults: 0.05 of the diagonal in, no depth; the inner face stays selected; one whole-mesh undo step.
    reg.execute(doc, "poly_select", {{"op", std::string("face")}, {"index", double(frontFace(doc.mesh))}});
    CHECK(doc.polySelection.count() == 1);
    const size_t undos = doc.undo.undoCount();
    r = reg.execute(doc, "poly_op", {{"op", std::string("inset")}});
    CHECK(r.ok);
    CHECK(r.message.find("Inset 1 face") != std::string::npos);
    CHECK(doc.mesh.triangleCount() == tris + 8);
    CHECK(doc.mesh.vertexCount() == verts + 4);
    CHECK(doc.undo.undoCount() == undos + 1);
    CHECK(doc.undo.lastUndo() && doc.undo.lastUndo()->mesh); // the topology changed: a whole-mesh step
    CHECK(doc.polySelection.count() == 1);
    CHECK(issue(doc.mesh, MeshIssue::NonManifold) == 0 && issue(doc.mesh, MeshIssue::Holes) == 0);
    const float expected = 0.05f * glm::length(makeCube(2).bounds().extent());
    float lo = 1e9f, hi = -1e9f; // the inset face (a 1 x 1 face of the side) spans 1 - 2 expected in x
    for (size_t v = verts; v < doc.mesh.vertexCount(); ++v) {
        lo = std::min(lo, doc.mesh.positions[v].x);
        hi = std::max(hi, doc.mesh.positions[v].x);
        CHECK(std::abs(doc.mesh.positions[v].z - 1.0f) < 1e-5f); // no depth
    }
    CHECK(std::abs((hi - lo) - (1.0f - 2.0f * expected)) < 1e-4f);
    // Undo puts the cube back.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.triangleCount() == tris && doc.mesh.vertexCount() == verts);
    // Parameters: a depth and Each face on the whole cube (a closed Part has no border as one shape).
    reg.execute(doc, "poly_select", {{"op", std::string("all")}});
    r = reg.execute(doc, "poly_op", {{"op", std::string("inset")}, {"thickness", 0.3}, {"depth", -0.02}});
    CHECK(!r.ok);
    CHECK(r.message.find("no border") != std::string::npos);
    r = reg.execute(doc, "poly_op", {{"op", std::string("inset")}, {"thickness", 0.3}, {"depth", -0.02}, {"individual", true}});
    CHECK(r.ok);
    CHECK(r.message.find("Inset 24 faces") != std::string::npos);
    CHECK(r.message.find("held") != std::string::npos); // 0.3 of the diagonal is more than a 1 x 1 face allows
    CHECK(doc.mesh.triangleCount() == tris + 24 * 8);
    CHECK(issue(doc.mesh, MeshIssue::Flipped) == 0 && issue(doc.mesh, MeshIssue::Holes) == 0);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    // The preview never touches the Part.
    reg.execute(doc, "poly_select", {{"op", std::string("face")}, {"index", double(frontFace(doc.mesh))}});
    PolyOpOutput out;
    std::string err;
    CHECK(previewPolyOp(doc, "inset", {{"thickness", 0.1}}, out, err));
    CHECK(out.mesh.triangleCount() == tris + 8 && doc.mesh.triangleCount() == tris);
    CHECK(out.triangles.size() == 2);
    // Rim group: the rim gets a new group, the inner face keeps the side's.
    r = reg.execute(doc, "poly_op", {{"op", std::string("inset")}, {"rim_group", true}});
    CHECK(r.ok);
    const uint32_t side = makeCube(2).facegroupAt(frontFace(makeCube(2)));
    CHECK(doc.mesh.facegroupAt(frontFace(doc.mesh)) == side);
    CHECK(nextFaceGroupId(doc.mesh) == 8);
    for (size_t t = tris; t < doc.mesh.triangleCount(); ++t) CHECK(doc.mesh.facegroupAt(uint32_t(t)) == 7);
}
