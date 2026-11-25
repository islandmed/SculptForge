// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edge loop insertion (core/EdgeLoopInsert.h): rings round a cube, across a grid, into cap triangles; counts and
// slide; UVs, seams, creases, groups, hidden flags, mask, paint and layers carried; a Moebius strip (its fold ends
// the ring), a ring that meets its own path, pure triangles, non-manifold and empty input; preview = result; speed.
#include "TestHarness.h"

#include "core/EdgeLoopInsert.h"
#include "core/PaintLayer.h"
#include "core/Primitives.h"
#include "core/Uv.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

using namespace sf;

namespace {

std::shared_ptr<const EdgeTopology> topoOf(const Mesh& m) { return buildEdgeTopology(m); }

// The edge between vertices a and b, or kNoEdge.
uint32_t edgeBetween(const EdgeTopology& t, uint32_t a, uint32_t b) {
    const glm::uvec2 ends{std::min(a, b), std::max(a, b)};
    for (uint32_t i = t.vertexOffsets[ends.x]; i < t.vertexOffsets[ends.x + 1]; ++i)
        if (t.ends[t.vertexEdges[i]] == ends) return t.vertexEdges[i];
    return kNoEdge;
}

// An edge whose midpoint `pick` accepts (the first found), or kNoEdge.
template <class F>
uint32_t edgeWhere(const Mesh& m, const EdgeTopology& t, F pick) {
    for (uint32_t e = 0; e < t.edgeCount(); ++e) {
        const glm::vec3 a = m.positions[t.ends[e].x], b = m.positions[t.ends[e].y];
        if (pick((a + b) * 0.5f, a, b)) return e;
    }
    return kNoEdge;
}

size_t polygons(const Mesh& m) { return m.triangleCount() - m.quadCount(); }

// Euler characteristic V - E + F over the polygons: a refinement never changes it.
long euler(const Mesh& m) {
    const auto t = topoOf(m);
    return long(m.vertexCount()) - long(t->edgeCount()) + long(polygons(m));
}

// Every triangle has three distinct corners; every edge (quad diagonals included) is on at most two triangles and
// those two run it opposite ways (no T-junction, no flipped face); returns the number of edges on one triangle.
size_t checkManifold(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> forward;
    size_t bad = 0;
    for (const glm::uvec3& t : m.triangles) {
        if (t.x == t.y || t.y == t.z || t.x == t.z) ++bad;
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = t[k], b = t[(k + 1) % 3];
            ++forward[{a, b}];
        }
    }
    size_t open = 0;
    for (const auto& [e, n] : forward) {
        if (n != 1) ++bad; // the same directed edge twice: a fold or a duplicate triangle
        const auto back = forward.find({e.second, e.first});
        if (back == forward.end()) ++open;
    }
    CHECK(bad == 0);
    return open; // each open edge is counted once per direction present, i.e. once
}

size_t zeroAreaTriangles(const Mesh& m) {
    size_t n = 0;
    for (const glm::uvec3& t : m.triangles) {
        const glm::vec3 c = glm::cross(m.positions[t.y] - m.positions[t.x], m.positions[t.z] - m.positions[t.x]);
        if (glm::length(c) < 1e-9f) ++n;
    }
    return n;
}

double volume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z])));
    return v / 6.0;
}

// Sizes of every optional attribute match the mesh.
void checkAttributeSizes(const Mesh& m) {
    const size_t nv = m.vertexCount(), nt = m.triangleCount();
    CHECK(m.normals.size() == nv);
    if (m.hasMask()) CHECK(m.mask.size() == nv);
    if (m.hasColors()) CHECK(m.colors.size() == nv);
    if (m.hasFaceGroups()) CHECK(m.facegroups.size() == nt);
    if (m.hasHidden()) CHECK(m.hidden.size() == nt);
    if (m.hasUvs()) CHECK(m.uvs.size() == nt * 3);
    if (m.hasUvFlags()) CHECK(m.uvFlags.size() == nt * 3);
    if (m.hasCreases()) CHECK(m.creases.size() == nt * 3);
    if (m.hasQuads()) CHECK(m.quadPartners.size() == nt);
    if (!m.layers.empty()) {
        CHECK(m.base.size() == nv);
        for (const SculptLayer& l : m.layers) CHECK(l.delta.size() == nv);
    }
    for (const PaintLayer& l : m.paintLayers) CHECK(l.color.size() == nv && l.alpha.size() == nv);
}

bool insert(Mesh& m, uint32_t seed, EdgeLoopResult& r, int count = 1, float slide = 0.0f) {
    EdgeLoopOptions o;
    o.count = count;
    o.slide = slide;
    std::string error;
    const auto t = topoOf(m);
    const bool ok = insertEdgeLoops(m, *t, seed, o, r, error);
    if (!ok) std::printf("  insertEdgeLoops: %s\n", error.c_str());
    return ok;
}

// A strip of `k` quads between two rails, closed back on itself; `twist` joins the rails crosswise (a Moebius strip).
Mesh makeStrip(int k, bool twist) {
    Mesh m;
    for (int i = 0; i < k; ++i) {
        const float a = float(i) / float(k) * 6.2831853f;
        const glm::vec3 c(std::cos(a), 0.0f, std::sin(a));
        const float h = twist ? a * 0.5f : 0.0f; // the rails turn half round on a Moebius strip
        const glm::vec3 up = glm::vec3(std::cos(h) * c.x, std::sin(h), std::cos(h) * c.z) * 0.3f;
        m.positions.push_back(c - up);
        m.positions.push_back(c + up);
    }
    for (int i = 0; i < k; ++i) {
        const uint32_t a0 = uint32_t(2 * i), b0 = a0 + 1;
        uint32_t a1 = uint32_t(2 * ((i + 1) % k)), b1 = a1 + 1;
        if (twist && i == k - 1) std::swap(a1, b1);
        m.triangles.push_back({a0, b0, b1});
        m.triangles.push_back({a0, b1, a1});
        m.pairQuad(uint32_t(m.triangles.size() - 2), uint32_t(m.triangles.size() - 1));
    }
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

} // namespace

TEST(edge_ring_closes_round_a_cube) {
    const Mesh cube = makeCube(1);
    const auto t = topoOf(cube);
    // A vertical edge: the ring is the four vertical edges, through the four side faces.
    const uint32_t e = edgeWhere(cube, *t, [](glm::vec3, glm::vec3 a, glm::vec3 b) { return a.x == b.x && a.z == b.z; });
    CHECK(e != kNoEdge);
    const EdgeRing ring = edgeRing(cube, *t, e);
    CHECK(ring.closed);
    CHECK(ring.edges.size() == 4 && ring.quads.size() == 4);
    CHECK(ring.startEnd == kNoEdge && ring.finishEnd == kNoEdge);
    for (size_t k = 0; k < ring.edges.size(); ++k) {
        // Every ring edge is vertical, and each rail stays on its own side (top or bottom) all the way round.
        CHECK(cube.positions[ring.left[k]].y == cube.positions[ring.left[0]].y);
        CHECK(cube.positions[ring.right[k]].y == cube.positions[ring.right[0]].y);
        CHECK(cube.positions[ring.left[k]].y != cube.positions[ring.right[k]].y);
    }
    const uint32_t res4 = edgeWhere(makeCube(4), *topoOf(makeCube(4)), [](glm::vec3 c, glm::vec3 a, glm::vec3 b) {
        return a.x == b.x && a.z == b.z && std::abs(c.x) == 1.0f && std::abs(c.z) < 0.6f;
    });
    const Mesh big = makeCube(4);
    const EdgeRing ring4 = edgeRing(big, *topoOf(big), res4);
    CHECK(ring4.closed && ring4.edges.size() == 16 && ring4.quads.size() == 16);
}

TEST(edge_loop_in_a_cube_splits_the_side_faces) {
    Mesh m = makeCube(1);
    const long chi = euler(m);
    const double v0 = volume(m);
    const auto t = topoOf(m);
    const uint32_t e = edgeWhere(m, *t, [](glm::vec3, glm::vec3 a, glm::vec3 b) { return a.x == b.x && a.z == b.z; });
    EdgeLoopResult r;
    CHECK(insert(m, e, r));
    CHECK(r.closed && r.quadsSplit == 4 && r.polygonsFanned == 0);
    CHECK(r.loops.size() == 1 && r.loops[0].size() == 4);
    CHECK(r.triangles.size() == 16); // 4 quads -> 8 quads, two triangles each
    for (uint32_t tri : r.triangles) CHECK(tri < m.triangleCount());
    CHECK(m.vertexCount() == 12 && polygons(m) == 10 && m.quadCount() == 10);
    CHECK(euler(m) == chi);
    CHECK(checkManifold(m) == 0);
    CHECK(zeroAreaTriangles(m) == 0);
    CHECK(std::abs(volume(m) - v0) < 1e-6);
    checkAttributeSizes(m);
    for (uint32_t v : r.loops[0]) CHECK(std::abs(m.positions[v].y) < 1e-6f); // the loop cuts the cube at half height
    // Each new quad keeps the face group of the side it came from (the cube has one per side).
    CHECK(m.hasFaceGroups());
    for (size_t tri = 0; tri < m.triangleCount(); ++tri) {
        const glm::uvec3& tr = m.triangles[tri];
        const glm::vec3 n = glm::cross(m.positions[tr.y] - m.positions[tr.x], m.positions[tr.z] - m.positions[tr.x]);
        // Same group for every triangle facing the same way.
        for (size_t u = 0; u < tri; ++u) {
            const glm::uvec3& tu = m.triangles[u];
            const glm::vec3 nu = glm::cross(m.positions[tu.y] - m.positions[tu.x], m.positions[tu.z] - m.positions[tu.x]);
            if (glm::dot(glm::normalize(n), glm::normalize(nu)) > 0.99f) CHECK(m.facegroups[tri] == m.facegroups[u]);
        }
    }
    // The new loop's edges exist and go round.
    const auto t2 = topoOf(m);
    for (size_t k = 0; k < 4; ++k) CHECK(edgeBetween(*t2, r.loops[0][k], r.loops[0][(k + 1) % 4]) != kNoEdge);
}

TEST(edge_loop_across_a_grid_ends_on_both_borders) {
    Mesh m = makePlane(4);
    const long chi = euler(m);
    const auto t = topoOf(m);
    // An edge along X in the middle row (z = 0) of vertices: the ring runs along that row across four quads.
    const uint32_t e = edgeWhere(m, *t, [](glm::vec3 c, glm::vec3 a, glm::vec3 b) {
        return std::abs(a.z - b.z) < 1e-6f && std::abs(c.z) < 1e-6f && c.x > 0.0f && c.x < 0.6f;
    });
    CHECK(e != kNoEdge);
    const EdgeRing ring = edgeRing(m, *t, e);
    CHECK(!ring.closed && ring.edges.size() == 5 && ring.quads.size() == 4);
    CHECK(ring.startEnd == kNoEdge && ring.finishEnd == kNoEdge);
    const size_t open0 = checkManifold(m);
    EdgeLoopResult r;
    CHECK(insert(m, e, r));
    CHECK(r.quadsSplit == 4 && r.polygonsFanned == 0 && !r.closed);
    CHECK(m.vertexCount() == 25 + 5 && m.quadCount() == 20 && polygons(m) == 20);
    CHECK(euler(m) == chi);
    CHECK(checkManifold(m) == open0 + 2); // the two border edges the ring ends on are split
    CHECK(zeroAreaTriangles(m) == 0);
    checkAttributeSizes(m);
    // The ring crosses one column of quads: its edges all run along X at the same x range, so the new loop is the
    // line x = the seed's midpoint, from border to border.
    const float cx = (m.positions[t->ends[e].x].x + m.positions[t->ends[e].y].x) * 0.5f;
    for (uint32_t v : r.loops[0]) CHECK(std::abs(m.positions[v].x - cx) < 1e-6f);
    float zmin = 1e9f, zmax = -1e9f;
    for (uint32_t v : r.loops[0]) zmin = std::min(zmin, m.positions[v].z), zmax = std::max(zmax, m.positions[v].z);
    CHECK(zmin == -1.0f && zmax == 1.0f);
}

TEST(edge_loop_count_and_slide) {
    const auto pick = [](glm::vec3 c, glm::vec3 a, glm::vec3 b) {
        return std::abs(a.z - b.z) < 1e-6f && std::abs(c.z) < 1e-6f && c.x > 0.0f && c.x < 0.6f;
    };
    {
        Mesh m = makePlane(4);
        const long chi = euler(m);
        const uint32_t e = edgeWhere(m, *topoOf(m), pick);
        EdgeLoopResult r;
        CHECK(insert(m, e, r, 3));
        CHECK(r.loops.size() == 3 && r.quadsSplit == 4);
        CHECK(m.quadCount() == 16 + 4 * 3);
        CHECK(euler(m) == chi);
        checkManifold(m);
        CHECK(zeroAreaTriangles(m) == 0);
        // Even spacing along each 0.5-long ring edge: parameters 1/4, 1/2, 3/4 from the left rail (lower x).
        for (size_t i = 0; i < 3; ++i)
            for (uint32_t v : r.loops[i]) {
                const float f = std::fmod(m.positions[v].x + 100.0f, 0.5f) / 0.5f;
                CHECK(std::abs(f - 0.25f * float(i + 1)) < 1e-4f);
            }
    }
    for (float slide : {0.5f, -0.5f, 1.0f, -1.0f}) {
        Mesh m = makePlane(4);
        const uint32_t e = edgeWhere(m, *topoOf(m), pick);
        EdgeLoopResult r;
        CHECK(insert(m, e, r, 1, slide));
        const float want = slide > 0.0f ? 0.5f + 0.5f * std::min(slide, 0.98f) : 0.5f * (1.0f + std::max(slide, -0.98f));
        for (uint32_t v : r.loops[0]) {
            const float f = std::fmod(m.positions[v].x + 100.0f, 0.5f) / 0.5f;
            CHECK(std::abs(f - want) < 1e-4f);
        }
        CHECK(zeroAreaTriangles(m) == 0); // slide 1 stops short of the rail
        checkManifold(m);
    }
}

TEST(edge_loop_ends_on_cap_triangles_with_fans) {
    Mesh m = makeCylinder(8);
    const long chi = euler(m);
    const double v0 = volume(m);
    const size_t quads0 = m.quadCount(), tris0 = polygons(m) - quads0;
    const auto t = topoOf(m);
    // A top rim edge: its ring runs down the side quads to the bottom rim; across each rim lies a cap triangle.
    const uint32_t e = edgeWhere(m, *t, [](glm::vec3 c, glm::vec3 a, glm::vec3 b) {
        return a.y == b.y && a.y > 0.9f && c.x > 0.0f && c.z > 0.0f && c.x > c.z;
    });
    CHECK(e != kNoEdge);
    const EdgeRing ring = edgeRing(m, *t, e);
    CHECK(!ring.closed);
    CHECK(ring.startEnd != kNoEdge && ring.finishEnd != kNoEdge && ring.startEnd != ring.finishEnd);
    CHECK(ring.quads.size() + 1 == ring.edges.size());
    EdgeLoopResult r;
    CHECK(insert(m, e, r, 2));
    CHECK(r.polygonsFanned == 2 && r.quadsSplit == ring.quads.size());
    CHECK(euler(m) == chi);
    CHECK(checkManifold(m) == 0);
    CHECK(zeroAreaTriangles(m) == 0);
    CHECK(std::abs(volume(m) - v0) < 1e-6);
    CHECK(m.quadCount() == quads0 + 2 * ring.quads.size());
    // Each cap triangle became three (two new points on its rim edge, fanned from the cap centre).
    CHECK(polygons(m) - m.quadCount() == tris0 + 4);
    checkAttributeSizes(m);
    // Face groups: the fans keep the caps' group, the strips the sides'.
    CHECK(m.hasFaceGroups());
    std::set<uint32_t> capGroups, sideGroups;
    for (size_t tri = 0; tri < m.triangleCount(); ++tri) {
        const glm::uvec3& tr = m.triangles[tri];
        const glm::vec3 n = glm::normalize(glm::cross(m.positions[tr.y] - m.positions[tr.x], m.positions[tr.z] - m.positions[tr.x]));
        (std::abs(n.y) > 0.99f ? capGroups : sideGroups).insert(m.facegroups[tri]);
    }
    for (uint32_t g : capGroups) CHECK(!sideGroups.count(g));
}

TEST(edge_loop_rings_round_a_torus_both_ways) {
    for (int way = 0; way < 2; ++way) {
        Mesh m = makeTorus(12);
        const long chi = euler(m);
        const auto t = topoOf(m);
        // way 0: an edge round the tube's cross-section (the ring follows the tube); way 1: an edge along the tube.
        uint32_t e = kNoEdge;
        for (uint32_t i = 0; i < t->edgeCount() && e == kNoEdge; ++i) {
            const glm::vec3 a = m.positions[t->ends[i].x], b = m.positions[t->ends[i].y];
            const float angA = std::atan2(a.z, a.x), angB = std::atan2(b.z, b.x);
            const bool alongTube = std::abs(angA - angB) > 1e-4f;
            if ((way == 1) == alongTube) e = i;
        }
        CHECK(e != kNoEdge);
        const EdgeRing ring = edgeRing(m, *t, e);
        CHECK(ring.closed);
        CHECK(ring.quads.size() == ring.edges.size());
        EdgeLoopResult r;
        CHECK(insert(m, e, r));
        CHECK(r.closed && euler(m) == chi);
        CHECK(checkManifold(m) == 0);
        CHECK(zeroAreaTriangles(m) == 0);
        std::printf("  torus way %d: ring of %zu edges\n", way, ring.edges.size());
    }
}

TEST(edge_loop_carries_uvs_seams_creases_and_vertex_data) {
    Mesh m = makePlane(4);
    const size_t nt0 = m.triangleCount();
    // UVs = a linear map of the position, so an interpolated corner must match its vertex.
    auto uvOf = [](glm::vec3 p) { return glm::vec2(p.x * 0.25f + 0.5f, p.z * 0.25f + 0.5f); };
    m.uvs.resize(nt0 * 3);
    m.uvFlags.assign(nt0 * 3, 0);
    m.ensureCreases();
    for (size_t t = 0; t < nt0; ++t)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = m.triangles[t][k], b = m.triangles[t][(k + 1) % 3];
            m.uvs[t * 3 + k] = uvOf(m.positions[a]);
            // The row z = 0 is a seam with a full crease; the corner at the origin is pinned.
            if (std::abs(m.positions[a].z) < 1e-6f && std::abs(m.positions[b].z) < 1e-6f) {
                m.uvFlags[t * 3 + k] |= kUvSeam;
                m.creases[t * 3 + k] = 1.0f;
            }
            if (glm::length(m.positions[a]) < 1e-6f) m.uvFlags[t * 3 + k] |= kUvPin;
        }
    // Mask and colour follow x; a sculpt layer lifts the plane by z; a paint layer's alpha follows x too.
    m.ensureMask();
    m.ensureColors();
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        m.mask[v] = m.positions[v].x * 0.25f + 0.5f;
        m.colors[v] = glm::vec3(m.mask[v], 0.5f, 1.0f - m.mask[v]);
    }
    const int layer = m.addLayer("lift");
    for (size_t v = 0; v < m.vertexCount(); ++v) m.layers[size_t(layer)].delta[v] = glm::vec3(0.0f, m.positions[v].z, 0.0f);
    m.setLayerWeight(layer, 0.5f);
    m.paintBase = m.colors;
    PaintLayer pl;
    pl.name = "tint";
    pl.color.assign(m.vertexCount(), glm::vec3(1.0f, 0.0f, 0.0f));
    pl.alpha.resize(m.vertexCount());
    for (size_t v = 0; v < m.vertexCount(); ++v) pl.alpha[v] = m.mask[v];
    m.paintLayers.push_back(pl);

    const auto t = topoOf(m);
    // The ring along the seam row: its edges are split, the seam and crease must survive on every piece.
    const uint32_t e = edgeWhere(m, *t, [](glm::vec3 c, glm::vec3 a, glm::vec3 b) {
        return std::abs(a.z - b.z) < 1e-6f && std::abs(c.z) < 1e-6f && c.x > 0.0f && c.x < 0.6f;
    });
    EdgeLoopResult r;
    CHECK(insert(m, e, r, 2));
    checkAttributeSizes(m);
    checkManifold(m);
    const size_t nt = m.triangleCount();
    size_t seamCorners = 0, pins = 0;
    for (size_t tri = 0; tri < nt; ++tri)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = m.triangles[tri][k], b = m.triangles[tri][(k + 1) % 3];
            const glm::vec2 uv = m.uvs[tri * 3 + k];
            CHECK(glm::length(uv - uvOf(m.positions[a])) < 1e-5f);
            const bool onRow = std::abs(m.positions[a].z) < 1e-6f && std::abs(m.positions[b].z) < 1e-6f;
            const bool seam = m.uvFlags[tri * 3 + k] & kUvSeam;
            CHECK(seam == onRow);
            CHECK((m.creases[tri * 3 + k] == 1.0f) == onRow);
            seamCorners += seam;
            if (m.uvFlags[tri * 3 + k] & kUvPin) {
                ++pins;
                CHECK(glm::length(m.positions[a]) < 1e-6f);
            }
        }
    // The row had 4 edges with 2 sides each = 8 seam corners; the ring split one of them into 3 pieces: 12.
    CHECK(seamCorners == 12);
    CHECK(pins > 0);
    // Vertex data: every new vertex interpolates its rail ends (mask and colour follow x; layers consistent).
    for (const auto& loop : r.loops)
        for (uint32_t v : loop) {
            const float want = m.positions[v].x * 0.25f + 0.5f;
            CHECK(std::abs(m.mask[v] - want) < 1e-5f);
            CHECK(glm::length(m.colors[v] - glm::vec3(want, 0.5f, 1.0f - want)) < 1e-5f);
            CHECK(std::abs(m.paintLayers[0].alpha[v] - want) < 1e-5f);
            CHECK(glm::length(m.positions[v] - (m.base[v] + 0.5f * m.layers[size_t(layer)].delta[v])) < 1e-5f);
            CHECK(std::abs(m.layers[size_t(layer)].delta[v].y - m.base[v].z) < 1e-5f);
        }
}

TEST(edge_loop_keeps_hidden_faces_hidden) {
    Mesh m = makeCube(2);
    m.ensureHidden();
    // Hide the +X side.
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3& tr = m.triangles[t];
        if (m.positions[tr.x].x == 1.0f && m.positions[tr.y].x == 1.0f && m.positions[tr.z].x == 1.0f) m.hidden[t] = 1;
    }
    const auto t = topoOf(m);
    const uint32_t e = edgeWhere(m, *t, [](glm::vec3 c, glm::vec3 a, glm::vec3 b) {
        return a.x == b.x && a.z == b.z && c.x == 1.0f && c.z == 0.0f;
    });
    CHECK(e != kNoEdge);
    EdgeLoopResult r;
    CHECK(insert(m, e, r));
    checkAttributeSizes(m);
    for (size_t tri = 0; tri < m.triangleCount(); ++tri) {
        const glm::uvec3& tr = m.triangles[tri];
        const bool plusX = m.positions[tr.x].x == 1.0f && m.positions[tr.y].x == 1.0f && m.positions[tr.z].x == 1.0f;
        CHECK(bool(m.hidden[tri]) == plusX);
    }
}

TEST(edge_loop_round_a_moebius_strip) {
    // A Moebius strip cannot have all its quads wound the same way: the fold where the rails swap is two open edges on
    // the same two vertices (a non-manifold edge to the ring walk), so the ring ends there, fanning the quads on
    // either side, and the mesh is still refined cleanly (no crash, no T-junction, no change of its Euler number).
    Mesh m = makeStrip(12, true);
    const long chi = euler(m);
    const auto t = topoOf(m);
    const uint32_t rung = edgeBetween(*t, 4, 5);
    CHECK(rung != kNoEdge);
    const EdgeRing ring = edgeRing(m, *t, rung);
    CHECK(!ring.closed);
    CHECK(ring.edges.size() == 11 && ring.quads.size() == 10);
    CHECK(ring.startEnd != kNoEdge && ring.finishEnd != kNoEdge);
    EdgeLoopResult r;
    CHECK(insert(m, rung, r));
    CHECK(!r.closed && r.quadsSplit == 10 && r.polygonsFanned == 2);
    CHECK(m.vertexCount() == 24 + 11 && m.triangleCount() == 24 + 20 + 2);
    CHECK(euler(m) == chi);
    CHECK(zeroAreaTriangles(m) == 0);
    checkAttributeSizes(m);
    // The plain band closes: the ring goes all the way round and three loops make four bands of quads.
    Mesh band = makeStrip(12, false);
    const long chiBand = euler(band);
    const size_t openBand = checkManifold(band);
    const auto tb = topoOf(band);
    const EdgeRing rb = edgeRing(band, *tb, edgeBetween(*tb, 4, 5));
    CHECK(rb.closed && rb.edges.size() == 12 && rb.quads.size() == 12);
    CHECK(insert(band, edgeBetween(*tb, 4, 5), r, 3));
    CHECK(r.closed && band.quadCount() == 48 && euler(band) == chiBand);
    CHECK(checkManifold(band) == openBand);
    CHECK(zeroAreaTriangles(band) == 0);
}

TEST(edge_loop_stops_where_the_ring_meets_its_own_path) {
    // A chain of quads whose last one attaches to the side of the first: the ring from a middle rung runs on into
    // the first quad crosswise, so the other way round it must stop before that quad and fan the quad before it.
    Mesh m;
    const int k = 6;
    for (int i = 0; i < k; ++i) {
        m.positions.push_back(glm::vec3(float(i), 0.0f, 0.0f));
        m.positions.push_back(glm::vec3(float(i), 1.0f, 0.0f));
    }
    for (int i = 0; i + 1 < k; ++i) {
        const uint32_t a0 = uint32_t(2 * i), b0 = a0 + 1, a1 = a0 + 2, b1 = a0 + 3;
        m.triangles.push_back({a0, b0, b1});
        m.triangles.push_back({a0, b1, a1});
        m.pairQuad(uint32_t(m.triangles.size() - 2), uint32_t(m.triangles.size() - 1));
    }
    // The last quad: from the far rung (a_{k-1}, b_{k-1}) to the first quad's bottom rail edge (a1, a0).
    {
        const uint32_t a = uint32_t(2 * (k - 1)), b = a + 1;
        m.positions[a] = glm::vec3(1.0f, -1.0f, 0.5f);
        m.positions[b] = glm::vec3(0.0f, -1.0f, 0.5f);
        m.triangles.push_back({a, b, 0});
        m.triangles.push_back({a, 0, 2});
        m.pairQuad(uint32_t(m.triangles.size() - 2), uint32_t(m.triangles.size() - 1));
    }
    m.rebuildTopology();
    m.recomputeAllNormals();
    const long chi = euler(m);
    const size_t open0 = checkManifold(m);
    const auto t = topoOf(m);
    const uint32_t rung = edgeBetween(*t, 6, 7);
    CHECK(rung != kNoEdge);
    const EdgeRing ring = edgeRing(m, *t, rung);
    CHECK(!ring.closed);
    CHECK(ring.startEnd != kNoEdge || ring.finishEnd != kNoEdge); // one end is a fanned quad
    EdgeLoopResult r;
    CHECK(insert(m, rung, r));
    CHECK(r.polygonsFanned == 1);
    CHECK(euler(m) == chi);
    CHECK(checkManifold(m) == open0 + 1); // the ring reached one border edge (split) and was stopped at the other end
    CHECK(zeroAreaTriangles(m) == 0);
    checkAttributeSizes(m);
    std::printf("  self-meeting ring: %zu edges, %zu quads split, %zu fanned\n", ring.edges.size(), r.quadsSplit,
                r.polygonsFanned);
}

TEST(edge_loop_on_pure_triangles_splits_one_edge) {
    Mesh m = makeIcosphere(1);
    const long chi = euler(m);
    const size_t nt0 = m.triangleCount();
    const auto t = topoOf(m);
    const EdgeRing ring = edgeRing(m, *t, 0);
    CHECK(ring.edges.size() == 1 && ring.quads.empty());
    CHECK(ring.startEnd != kNoEdge && ring.finishEnd != kNoEdge && ring.startEnd != ring.finishEnd);
    EdgeLoopResult r;
    CHECK(insert(m, 0, r));
    CHECK(r.quadsSplit == 0 && r.polygonsFanned == 2);
    CHECK(m.triangleCount() == nt0 + 2 && !m.hasQuads());
    CHECK(euler(m) == chi);
    CHECK(checkManifold(m) == 0);
    CHECK(zeroAreaTriangles(m) == 0);
}

TEST(edge_loop_refuses_bad_input) {
    EdgeLoopResult r;
    std::string error;
    EdgeLoopOptions o;
    {
        Mesh empty;
        empty.rebuildTopology();
        const auto t = topoOf(empty);
        CHECK(edgeRing(empty, *t, 0).empty());
        CHECK(!insertEdgeLoops(empty, *t, 0, o, r, error));
        CHECK(!error.empty());
    }
    {
        Mesh m = makeCube(1);
        const auto t = topoOf(m);
        const Mesh before = m;
        CHECK(!insertEdgeLoops(m, *t, t->edgeCount(), o, r, error)); // out of range
        o.count = 0;
        CHECK(!insertEdgeLoops(m, *t, 0, o, r, error));
        o.count = EdgeLoopOptions::kMaxLoops + 1;
        CHECK(!insertEdgeLoops(m, *t, 0, o, r, error));
        CHECK(m.vertexCount() == before.vertexCount() && m.triangleCount() == before.triangleCount());
        o.count = 1;
        // A stale topology (another mesh's) is refused, and so is a ring found on another mesh.
        const auto other = topoOf(makeCube(2));
        CHECK(!insertEdgeLoops(m, *other, 0, o, r, error));
        const Mesh big = makeCube(2);
        CHECK(!insertEdgeLoops(m, *t, edgeRing(big, *other, 0), o, r, error));
        CHECK(!insertEdgeLoops(m, *t, EdgeRing(), o, r, error));
    }
    {
        // Three triangles on one edge: not splittable; the ring is empty and the mesh untouched.
        Mesh m;
        m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, -1, 0}};
        m.triangles = {{0, 1, 2}, {1, 0, 3}, {0, 1, 4}};
        m.rebuildTopology();
        m.recomputeAllNormals();
        const auto t = topoOf(m);
        const uint32_t e = edgeBetween(*t, 0, 1);
        CHECK(e != kNoEdge);
        CHECK(edgeRing(m, *t, e).empty());
        CHECK(!insertEdgeLoops(m, *t, e, o, r, error));
        CHECK(m.triangleCount() == 3 && m.vertexCount() == 5);
        // Through one of the free edges the ring is that edge alone (its triangle fanned); the shared edge stays whole.
        const uint32_t f = edgeBetween(*t, 1, 2);
        CHECK(insertEdgeLoops(m, *t, f, o, r, error));
        CHECK(m.triangleCount() == 4 && m.vertexCount() == 6);
        CHECK(topoOf(m)->edgeCount() == t->edgeCount() + 2);
    }
    {
        // A zero-length ring edge (two coincident vertices) still splits without a crash.
        Mesh m = makePlane(2);
        const auto t = topoOf(m);
        m.positions[t->ends[0].y] = m.positions[t->ends[0].x];
        m.recomputeAllNormals();
        CHECK(insertEdgeLoops(m, *t, 0, o, r, error));
        checkManifold(m);
    }
}

TEST(edge_loop_preview_matches_the_result) {
    Mesh m = makeQuadSphere(6);
    const long chi = euler(m);
    const auto t = topoOf(m);
    EdgeLoopOptions o;
    o.count = 3;
    o.slide = 0.3f;
    const uint32_t e = 17;
    const auto lines = edgeLoopPreview(m, *t, e, o);
    const EdgeRing ring = edgeRing(m, *t, e);
    CHECK(lines.size() == 3);
    CHECK(edgeLoopPreview(m, ring, o) == lines); // the overload on a found ring (what a slider calls) is the same
    for (const auto& l : lines) CHECK(l.size() == ring.edges.size());
    EdgeLoopResult r;
    std::string error;
    CHECK(insertEdgeLoops(m, *t, ring, o, r, error)); // the overload on the previewed ring
    CHECK(r.loops.size() == 3);
    CHECK(r.triangles.size() == ring.quads.size() * 8);
    for (size_t i = 0; i < 3; ++i)
        for (size_t k = 0; k < ring.edges.size(); ++k) CHECK(glm::length(m.positions[r.loops[i][k]] - lines[i][k]) < 1e-6f);
    CHECK(r.closed && euler(m) == chi);
    CHECK(checkManifold(m) == 0);
    CHECK(zeroAreaTriangles(m) == 0);
    // A quad sphere has no poles: every ring closes without a fan.
    CHECK(r.polygonsFanned == 0);
    // Out-of-range options give no preview.
    o.count = 0;
    CHECK(edgeLoopPreview(m, *topoOf(m), e, o).empty());
}

TEST(edge_loop_is_quick_on_a_dense_quad_mesh) {
    Mesh m = makeQuadSphere(160); // 6 * 160 * 160 quads
    const auto t = topoOf(m);
    const auto t0 = std::chrono::steady_clock::now();
    const EdgeRing ring = edgeRing(m, *t, 12345);
    const double ringMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    EdgeLoopResult r;
    std::string error;
    const auto t1 = std::chrono::steady_clock::now();
    CHECK(insertEdgeLoops(m, *t, 12345, EdgeLoopOptions{}, r, error));
    const double insertMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    std::printf("  %zu quads: ring of %zu edges in %.2f ms, insert %.1f ms\n", size_t(6 * 160 * 160), ring.edges.size(),
                ringMs, insertMs);
    CHECK(ring.closed && ring.edges.size() == 4 * 160);
    CHECK(ringMs < timeLimit(20.0));    // the walk touches only the ring (a preview runs while the slider moves)
    CHECK(insertMs < timeLimit(1500.0)); // the rebuild of topology and normals dominates
    CHECK(checkManifold(m) == 0);
}
