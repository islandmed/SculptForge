// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dynamic topology (core/Dyntopo.h): strokes remesh the surface under the brush and leave a valid mesh
// (manifold, no degenerate or flipped triangles, Euler characteristic kept, adjacency and BVH matching a fresh
// build) after every dab; symmetry makes mirrored topology; undo and redo restore exactly; attributes are
// carried; boundaries, face group borders, masks and subdivision levels are respected.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Multires.h"
#include "core/PathStroke.h"
#include "core/Primitives.h"
#include "core/Texture.h"
#include "core/ThreadPool.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>

using namespace sf;

namespace {

// Why `m` isn't a valid mesh ("" if it is): corners in range and distinct, no degenerate triangles, every
// vertex used, every edge on one or two triangles wound oppositely (closed: exactly two), the adjacency lists
// as a rebuild makes them, and for a closed mesh V - E + F == euler.
std::string invalid(const Mesh& m, bool closed, int euler = 2) {
    const size_t nv = m.vertexCount(), nt = m.triangleCount();
    std::vector<int> used(nv, 0);
    std::map<std::pair<uint32_t, uint32_t>, int> directed;
    for (size_t t = 0; t < nt; ++t) {
        const glm::uvec3 tri = m.triangles[t];
        for (int c = 0; c < 3; ++c)
            if (tri[c] >= nv) return "corner out of range";
        if (tri.x == tri.y || tri.y == tri.z || tri.x == tri.z) return "repeated corner";
        const glm::vec3 n = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
        if (!(glm::length(n) > 1e-12f)) {
            if (std::getenv("SF_STRESS"))
                std::printf("  degenerate %zu: edges %g %g %g at %g %g %g\n", t, glm::distance(m.positions[tri.x], m.positions[tri.y]),
                            glm::distance(m.positions[tri.y], m.positions[tri.z]), glm::distance(m.positions[tri.z], m.positions[tri.x]),
                            m.positions[tri.x].x, m.positions[tri.x].y, m.positions[tri.x].z);
            return "degenerate triangle " + std::to_string(t);
        }
        for (int c = 0; c < 3; ++c) {
            used[tri[c]] = 1;
            if (++directed[{tri[c], tri[(c + 1) % 3]}] > 1) return "edge used twice in one direction";
        }
    }
    for (size_t v = 0; v < nv; ++v)
        if (!used[v]) return "unused vertex " + std::to_string(v);
    size_t edges = 0;
    for (const auto& [e, n] : directed) {
        const bool twin = directed.count({e.second, e.first}) > 0;
        if (closed && !twin) return "open edge";
        if (!twin || e.first < e.second) ++edges;
    }
    if (closed && long(nv) - long(edges) + long(nt) != euler) return "Euler characteristic changed";
    Mesh fresh = m;
    fresh.rebuildTopology();
    for (uint32_t v = 0; v < nv; ++v) {
        const IndexRing a = m.vertexTriangles(v), b = fresh.vertexTriangles(v);
        if (!std::equal(a.begin(), a.end(), b.begin(), b.end())) return "triangle list of vertex " + std::to_string(v);
        const IndexRing c = m.neighbors(v), d = fresh.neighbors(v);
        if (!std::equal(c.begin(), c.end(), d.begin(), d.end())) return "ring of vertex " + std::to_string(v);
    }
    if (m.normals.size() != nv) return "normals";
    return {};
}

// The BVH finds what a brute-force search finds (vertex queries and ray casts) at a few spots.
bool bvhMatches(const Mesh& m, const BVH& bvh) {
    for (int i = 0; i < 12; ++i) {
        const glm::vec3 c = m.positions[(size_t(i) * 7919) % m.vertexCount()];
        const float r = 0.3f;
        std::vector<uint32_t> got, want;
        bvh.queryVertices(m, c, r, got);
        for (uint32_t v = 0; v < m.vertexCount(); ++v)
            if (glm::dot(m.positions[v] - c, m.positions[v] - c) <= r * r) want.push_back(v);
        if (got != want) {
            std::printf("  BVH vertex query: %zu found, %zu inside\n", got.size(), want.size());
            return false;
        }
        const glm::vec3 origin = c * 3.0f + glm::vec3(0.01f, 0.37f, 0.02f);
        // (Aimed a little off the origin: a ray through a vertex exactly is a coin toss for any ray caster.)
        const glm::vec3 dir = glm::normalize(glm::vec3(0.0131f, 0.0071f, 0.0213f) - origin);
        RayHit hit;
        const bool found = bvh.raycast(m, origin, dir, hit);
        float best = 1e30f;
        for (uint32_t ti = 0; ti < m.triangleCount(); ++ti) {
            if (m.triangleHidden(ti)) continue; // (picking skips hidden triangles)
            const glm::uvec3& t = m.triangles[ti];
            const glm::vec3 a = m.positions[t.x], e1 = m.positions[t.y] - a, e2 = m.positions[t.z] - a;
            const glm::vec3 pv = glm::cross(dir, e2);
            const float det = glm::dot(e1, pv);
            if (std::abs(det) < 1e-12f) continue;
            const glm::vec3 s = origin - a;
            const float u = glm::dot(s, pv) / det;
            const glm::vec3 q = glm::cross(s, e1);
            const float w = glm::dot(dir, q) / det, tt = glm::dot(e2, q) / det;
            if (u >= 0 && w >= 0 && u + w <= 1 && tt > 0) best = std::min(best, tt);
        }
        if (found != (best < 1e30f) || (found && std::abs(hit.t - best) > 1e-4f)) {
            std::printf("  BVH ray: hit %d at %f, nearest %f\n", int(found), found ? hit.t : 0.0f, best);
            return false;
        }
    }
    return true;
}

// A dab where a ray from `from` towards the origin hits the surface.
bool dabToward(Document& d, glm::vec3 from, float radius, BrushType type, Symmetry sym, float strength = 0.5f,
               glm::vec3 delta = glm::vec3(0.0f), bool invert = false) {
    RayHit hit;
    const glm::vec3 dir = glm::normalize(-from);
    if (!d.bvh.raycast(d.mesh, from, dir, hit)) return false;
    DabParams p;
    p.center = hit.position;
    p.viewDir = dir;
    p.radius = radius;
    p.strength = strength;
    p.strokeDelta = delta;
    p.invert = invert;
    d.sculptor.dab(d.mesh, d.bvh, type, p, sym);
    return true;
}

Document sphereDoc(int subdivisions = 3) {
    Document d = makeDoc(makeIcosphere(subdivisions, 1.0f));
    d.sculptor.dyntopo.enabled = true;
    return d;
}

// Mirror image of every vertex across x = 0 within `tol` (nearest, both ways), or empty when some vertex has none.
std::vector<uint32_t> mirrorMap(const Mesh& m, float tol) {
    std::vector<uint32_t> map(m.vertexCount(), ~0u);
    auto nearest = [&](const glm::vec3& p) {
        uint32_t best = ~0u;
        float bd = tol * tol;
        for (uint32_t w = 0; w < m.vertexCount(); ++w) {
            const glm::vec3 d = m.positions[w] - p;
            if (glm::dot(d, d) < bd) {
                bd = glm::dot(d, d);
                best = w;
            }
        }
        return best;
    };
    const glm::vec3 s(-1.0f, 1.0f, 1.0f);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const uint32_t w = nearest(m.positions[v] * s);
        if (w == ~0u || nearest(m.positions[w] * s) != v) {
            std::printf("  no mirror image for vertex %u at %f %f %f\n", v, m.positions[v].x, m.positions[v].y, m.positions[v].z);
            return {};
        }
        map[v] = w;
    }
    return map;
}

// Every triangle's mirror image (reversed winding) is a triangle too.
bool mirroredTopology(const Mesh& m, const std::vector<uint32_t>& map) {
    std::set<std::tuple<uint32_t, uint32_t, uint32_t>> tris;
    auto canon = [](uint32_t a, uint32_t b, uint32_t c) {
        // Rotation starting at the smallest corner.
        if (b < a && b < c) return std::make_tuple(b, c, a);
        if (c < a && c < b) return std::make_tuple(c, a, b);
        return std::make_tuple(a, b, c);
    };
    for (const glm::uvec3& t : m.triangles) tris.insert(canon(t.x, t.y, t.z));
    int bad = 0;
    for (const glm::uvec3& t : m.triangles)
        if (!tris.count(canon(map[t.x], map[t.z], map[t.y]))) {
            if (bad++ < 4) {
                const glm::vec3 c = (m.positions[t.x] + m.positions[t.y] + m.positions[t.z]) / 3.0f;
                std::printf("  unmirrored triangle at %f %f %f\n", c.x, c.y, c.z);
            }
        }
    return bad == 0;
}

} // namespace

TEST(dyntopo_edge_length_follows_brush_or_mesh) {
    DyntopoSettings s;
    CHECK(std::abs(dyntopoEdgeLength(s, 0.1f, 2.0f) - 0.1f * 0.4f * std::pow(0.1f, 0.5f)) < 1e-6f);
    s.detail = 1.0f;
    const float fine = dyntopoEdgeLength(s, 0.1f, 2.0f);
    s.detail = 0.0f;
    CHECK(dyntopoEdgeLength(s, 0.1f, 2.0f) > fine * 9.0f);
    CHECK(dyntopoEdgeLength(s, 0.2f, 2.0f) > dyntopoEdgeLength(s, 0.1f, 2.0f) * 1.9f); // follows the brush
    s.constantDetail = true;
    CHECK(dyntopoEdgeLength(s, 0.2f, 2.0f) == dyntopoEdgeLength(s, 0.1f, 2.0f)); // follows the mesh
    CHECK(std::abs(dyntopoEdgeLength(s, 0.1f, 2.0f) - 0.1f) < 1e-6f);             // diagonal / 20
}

TEST(dyntopo_adds_detail_under_the_brush_only) {
    Document d = sphereDoc(2); // 320 triangles, edges ~0.3
    const size_t before = d.mesh.triangleCount();
    const glm::vec3 far = d.mesh.positions[0];
    d.sculptor.dyntopo.detail = 0.8f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    for (int i = 0; i < 6; ++i) CHECK(dabToward(d, glm::vec3(0.1f * float(i), 0.2f, 3.0f), 0.35f, BrushType::Standard, Symmetry{false, false, false}));
    CHECK(invalid(d.mesh, true).empty());
    CHECK(bvhMatches(d.mesh, d.bvh));
    UndoRecord r = d.sculptor.endStroke();
    CHECK(r.topology != nullptr);
    CHECK(d.mesh.triangleCount() > before * 2);
    CHECK(invalid(d.mesh, true).empty());
    CHECK(!d.mesh.dynamicAdjacency());
    // Edges near the stroke are about the detail size, the far side kept its coarse triangles.
    const float target = dyntopoEdgeLength(d.sculptor.dyntopo, 0.35f, 2.0f);
    float longestNear = 0.0f;
    for (const glm::uvec3& t : d.mesh.triangles)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 a = d.mesh.positions[t[c]], b = d.mesh.positions[t[(c + 1) % 3]];
            if (glm::distance((a + b) * 0.5f, glm::vec3(0.0f, 0.1f, 1.0f)) < 0.15f) longestNear = std::max(longestNear, glm::distance(a, b));
        }
    CHECK(longestNear <= target * 4.0f / 3.0f * 1.25f);
    CHECK(d.mesh.positions[0] == far);
}

TEST(nightly_dyntopo_random_strokes_stay_valid) {
    Document d = sphereDoc(3);
    uint32_t rng = 12345;
    auto rnd = [&rng] {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return float(rng & 0xffff) / 65535.0f;
    };
    const BrushType types[] = {BrushType::Standard, BrushType::Tendril, BrushType::Smooth, BrushType::Clay,
                               BrushType::Pinch, BrushType::Inflate, BrushType::Crease, BrushType::Flatten};
    bool allValid = true, bvhOk = true;
    std::string why;
    for (int stroke = 0; stroke < 24; ++stroke) {
        const BrushType type = types[stroke % 8];
        d.sculptor.dyntopo.detail = rnd();
        d.sculptor.dyntopo.constantDetail = stroke % 5 == 4;
        const Symmetry sym{stroke % 3 == 0, false, stroke % 7 == 0};
        CHECK(d.sculptor.beginStroke(d.mesh, brushName(type)));
        glm::vec3 dir = glm::normalize(glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) + glm::vec3(0.0f, 0.0f, 0.01f));
        const glm::vec3 step = glm::normalize(glm::cross(dir, glm::vec3(0.3f, 1.0f, 0.1f))) * 0.08f;
        const float radius = 0.15f + 0.35f * rnd();
        for (int i = 0; i < 8; ++i) {
            dabToward(d, (dir + step * float(i)) * 3.0f, radius, type, sym, 0.6f, step * 0.5f, rnd() < 0.3f);
            if (allValid) {
                why = invalid(d.mesh, true);
                allValid = why.empty();
                if (!allValid) std::printf("  stroke %d dab %d: %s\n", stroke, i, why.c_str());
            }
        }
        bvhOk = bvhOk && bvhMatches(d.mesh, d.bvh);
        d.undo.push(d.sculptor.endStroke());
        if (allValid) {
            why = invalid(d.mesh, true);
            allValid = why.empty();
            if (!allValid) std::printf("  after stroke %d: %s\n", stroke, why.c_str());
        }
    }
    CHECK(allValid);
    CHECK(bvhOk);
    // Undo all the way back gives the sphere again, exactly.
    const Mesh sphere = makeIcosphere(3, 1.0f);
    while (d.undo.canUndo()) d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.triangles == sphere.triangles);
    CHECK(d.mesh.positions == sphere.positions);
    CHECK(bvhMatches(d.mesh, d.bvh));
}

TEST(dyntopo_undo_redo_restore_exactly) {
    Document d = sphereDoc(3);
    d.mesh.ensureMask();
    d.mesh.ensureColors(glm::vec3(0.2f, 0.5f, 0.9f));
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].y > 0.5f ? 0.5f : 0.0f;
    const Mesh before = d.mesh;
    d.sculptor.dyntopo.detail = 0.7f;
    for (int s = 0; s < 3; ++s) {
        CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
        for (int i = 0; i < 6; ++i) dabToward(d, glm::vec3(0.15f * float(i) - 0.4f, 0.3f * float(s), 3.0f), 0.3f, BrushType::Standard, Symmetry{});
        d.undo.push(d.sculptor.endStroke());
    }
    const Mesh after = d.mesh;
    CHECK(after.triangleCount() != before.triangleCount());
    while (d.undo.canUndo()) d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.triangles == before.triangles);
    CHECK(d.mesh.positions == before.positions);
    CHECK(d.mesh.mask == before.mask);
    CHECK(d.mesh.colors == before.colors);
    CHECK(invalid(d.mesh, true).empty());
    while (d.undo.canRedo()) d.undo.redo(d.mesh, d.bvh);
    CHECK(d.mesh.triangles == after.triangles);
    CHECK(d.mesh.positions == after.positions);
    CHECK(d.mesh.mask == after.mask);
    CHECK(invalid(d.mesh, true).empty());
    CHECK(bvhMatches(d.mesh, d.bvh));
}

TEST(dyntopo_x_symmetry_makes_mirrored_topology) {
    Document d = sphereDoc(3);
    d.sculptor.dyntopo.detail = 0.75f;
    const Symmetry sym{true, false, false};
    // Strokes on one side, across the middle and pulling with Tendril.
    for (int s = 0; s < 3; ++s) {
        const BrushType type = s == 2 ? BrushType::Tendril : BrushType::Standard;
        CHECK(d.sculptor.beginStroke(d.mesh, brushName(type)));
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 from = s == 0 ? glm::vec3(0.5f, 0.1f * float(i) - 0.3f, 1.0f)
                                 : s == 1 ? glm::vec3(0.1f * float(i) - 0.35f, -0.2f, 1.0f)
                                          : glm::vec3(0.6f, 0.4f, 0.8f);
            dabToward(d, from * 3.0f, 0.3f, type, sym, 0.5f, s == 2 ? glm::vec3(0.02f, 0.02f, 0.03f) : glm::vec3(0.0f));
        }
        d.undo.push(d.sculptor.endStroke());
        CHECK(invalid(d.mesh, true).empty());
        const std::vector<uint32_t> map = mirrorMap(d.mesh, dyntopoEdgeLength(d.sculptor.dyntopo, 0.3f, 2.0f) * 0.3f);
        CHECK(map.size() == d.mesh.vertexCount() && mirroredTopology(d.mesh, map));
        CHECK(d.sculptor.dynamicTopology().stats.unpaired == 0);
    }
    CHECK(d.mesh.triangleCount() > 1280 * 2);
    const float target = dyntopoEdgeLength(d.sculptor.dyntopo, 0.3f, 2.0f);
    const std::vector<uint32_t> map = mirrorMap(d.mesh, target * 0.3f);
    CHECK(map.size() == d.mesh.vertexCount());
    if (map.size() == d.mesh.vertexCount()) CHECK(mirroredTopology(d.mesh, map));
}

TEST(dyntopo_symmetry_remeshes_both_sides_of_an_asymmetric_mesh) {
    // #23: a symmetric shape whose triangles aren't mirrored (here a sphere turned a little; in the report a
    // QRemeshed head) found no mirror images for the remeshing, so with X symmetry the side under the brush got
    // fine triangles and the mirrored side, which the brush moves just the same, kept its coarse ones. Both
    // sides must get the detail (not the same triangles: there were none to mirror).
    Document d = sphereDoc(3);
    const glm::mat3 turn = glm::mat3(glm::rotate(glm::mat4(1.0f), 0.3f, glm::vec3(0.2f, 1.0f, 0.4f)));
    for (glm::vec3& p : d.mesh.positions) p = turn * p;
    d.mesh.recomputeAllNormals();
    d.mesh.rebuildTopology();
    d.bvh.build(d.mesh);
    d.sculptor.dyntopo.detail = 0.75f;
    const Symmetry sym{true, false, false};
    const float radius = 0.3f, target = dyntopoEdgeLength(d.sculptor.dyntopo, radius, 2.0f);
    std::vector<glm::vec3> path;
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 from = glm::vec3(0.55f, 0.1f * float(i) - 0.35f, 0.9f) * 3.0f;
        RayHit hit;
        if (d.bvh.raycast(d.mesh, from, glm::normalize(-from), hit)) path.push_back(hit.position);
        CHECK(dabToward(d, from, radius, BrushType::Standard, sym));
    }
    d.undo.push(d.sculptor.endStroke());
    CHECK(invalid(d.mesh, true).empty());
    CHECK(bvhMatches(d.mesh, d.bvh));
    // Along the stroke and along its mirror image: how many triangles, and the longest edge.
    auto near = [&](float sign, size_t& count, float& longest) {
        count = 0;
        longest = 0.0f;
        for (const glm::uvec3& t : d.mesh.triangles) {
            const glm::vec3 c = (d.mesh.positions[t.x] + d.mesh.positions[t.y] + d.mesh.positions[t.z]) / 3.0f;
            bool in = false;
            for (const glm::vec3& p : path) in = in || glm::distance(c, glm::vec3(p.x * sign, p.y, p.z)) < radius * 0.6f;
            if (!in) continue;
            ++count;
            for (int k = 0; k < 3; ++k)
                longest = std::max(longest, glm::distance(d.mesh.positions[t[k]], d.mesh.positions[t[(k + 1) % 3]]));
        }
    };
    size_t brushSide = 0, mirrorSide = 0;
    float brushLongest = 0.0f, mirrorLongest = 0.0f;
    near(1.0f, brushSide, brushLongest);
    near(-1.0f, mirrorSide, mirrorLongest);
    std::printf("  brush side %zu triangles (longest edge %.3f), mirrored side %zu (%.3f), target %.3f\n", brushSide,
                brushLongest, mirrorSide, mirrorLongest, target);
    CHECK(brushSide > 200);
    CHECK(mirrorSide * 10 > brushSide * 7); // within 30%
    CHECK(mirrorLongest <= target * 4.0f / 3.0f * 1.5f);

    // Every side remeshed on its own stays a valid mesh, with X and Z symmetry (three images) and any brush,
    // and undo takes it all back exactly.
    const Mesh turned = [&] {
        Mesh m = makeIcosphere(3, 1.0f);
        for (glm::vec3& p : m.positions) p = turn * p;
        return m;
    }();
    uint32_t rng = 777;
    auto rnd = [&rng] {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return float(rng & 0xffff) / 65535.0f;
    };
    const BrushType types[] = {BrushType::Standard, BrushType::Tendril, BrushType::Smooth, BrushType::Clay, BrushType::Pinch};
    bool valid = true;
    for (int stroke = 0; stroke < 10 && valid; ++stroke) {
        const BrushType type = types[stroke % 5];
        d.sculptor.dyntopo.detail = 0.4f + 0.5f * rnd();
        CHECK(d.sculptor.beginStroke(d.mesh, brushName(type)));
        const glm::vec3 dir = glm::normalize(glm::vec3(0.3f + 0.6f * rnd(), rnd() - 0.5f, 0.2f + 0.6f * rnd()));
        const glm::vec3 step = glm::normalize(glm::cross(dir, glm::vec3(0.1f, 1.0f, 0.2f))) * 0.07f;
        for (int i = 0; i < 6 && valid; ++i) {
            dabToward(d, (dir + step * float(i)) * 3.0f, 0.2f + 0.2f * rnd(), type, Symmetry{true, false, true}, 0.6f, step * 0.5f);
            const std::string why = invalid(d.mesh, true);
            if (!why.empty()) std::printf("  stroke %d dab %d: %s\n", stroke, i, why.c_str());
            valid = why.empty();
        }
        d.undo.push(d.sculptor.endStroke());
    }
    CHECK(valid);
    CHECK(bvhMatches(d.mesh, d.bvh));
    while (d.undo.canUndo()) d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.triangles == turned.triangles);
    CHECK(invalid(d.mesh, true).empty());
}

TEST(dyntopo_carries_attributes_and_keeps_uvs) {
    Document d = sphereDoc(2);
    Mesh& m = d.mesh;
    m.ensureColors();
    m.ensureFaceGroups();
    m.uvs.assign(m.triangleCount() * 3, glm::vec2(0.5f));
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.colors[v] = glm::vec3(m.positions[v].x * 0.5f + 0.5f, 0.2f, 0.1f);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        m.facegroups[t] = (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]).y > 0.0f ? 7u : 3u;
    }
    d.activeLayer = m.addLayer("Detail");
    m.rebuildTopology();
    d.bvh.build(m);
    // Group border vertices, and the border's length and each group's area (base positions).
    auto borderVerts = [](const Mesh& mesh) {
        std::set<uint32_t> out;
        for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
            std::set<uint32_t> groups;
            for (uint32_t t : mesh.vertexTriangles(v)) groups.insert(mesh.facegroups[t]);
            if (groups.size() > 1) out.insert(v);
        }
        return out;
    };
    auto borderLength = [](const Mesh& mesh) {
        std::map<std::pair<uint32_t, uint32_t>, std::set<uint32_t>> groups;
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t)
            for (int c = 0; c < 3; ++c) {
                const uint32_t a = mesh.triangles[t][c], b = mesh.triangles[t][(c + 1) % 3];
                groups[{std::min(a, b), std::max(a, b)}].insert(mesh.facegroups[t]);
            }
        float len = 0.0f;
        for (const auto& [e, g] : groups)
            if (g.size() > 1) len += glm::distance(mesh.base[e.first], mesh.base[e.second]);
        return len;
    };
    auto groupArea = [](const Mesh& mesh, uint32_t group) {
        float area = 0.0f;
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
            if (mesh.facegroups[t] != group) continue;
            const glm::uvec3 tri = mesh.triangles[t];
            area += 0.5f * glm::length(glm::cross(mesh.base[tri.y] - mesh.base[tri.x], mesh.base[tri.z] - mesh.base[tri.x]));
        }
        return area;
    };
    const float lengthBefore = borderLength(m), area7 = groupArea(m, 7), area3 = groupArea(m, 3);
    const size_t borderBefore = borderVerts(m).size();
    d.sculptor.dyntopo.detail = 0.9f;
    CHECK(d.sculptor.beginStroke(m, "Standard", d.activeLayer));
    for (int i = 0; i < 8; ++i) dabToward(d, glm::vec3(0.1f * float(i) - 0.4f, 0.05f, 3.0f), 0.35f, BrushType::Standard, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
    CHECK(invalid(m, true).empty());
    CHECK(m.uvs.size() == m.triangleCount() * 3); // (kept: every corner of every triangle has its UV)
    CHECK(m.colors.size() == m.vertexCount());
    CHECK(m.facegroups.size() == m.triangleCount());
    CHECK(m.layers[0].delta.size() == m.vertexCount() && m.base.size() == m.vertexCount());
    // Interpolated colours: red follows x everywhere.
    bool colorsOk = true;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        colorsOk = colorsOk && std::abs(m.colors[v].x - (m.positions[v].x * 0.5f + 0.5f)) < 0.12f && m.colors[v].y == 0.2f;
    CHECK(colorsOk);
    // Positions are still base + layer offsets, and the stroke went into the layer.
    bool layered = true;
    float moved = 0.0f;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        layered = layered && glm::distance(m.positions[v], m.base[v] + m.layers[0].delta[v]) < 1e-5f;
        moved = std::max(moved, glm::length(m.layers[0].delta[v]));
    }
    CHECK(layered);
    CHECK(moved > 0.0f);
    // The border is still there, now with more vertices along it, and both groups stayed in their halves.
    CHECK(borderVerts(m).size() > borderBefore);
    CHECK(std::abs(borderLength(m) - lengthBefore) < 0.1f * lengthBefore);
    CHECK(std::abs(groupArea(m, 7) - area7) < 0.05f * area7);
    CHECK(std::abs(groupArea(m, 3) - area3) < 0.05f * area3);
    d.undo.undo(m, d.bvh);
    CHECK(m.uvs.size() == m.triangleCount() * 3);
    CHECK(m.uvs == std::vector<glm::vec2>(m.triangleCount() * 3, glm::vec2(0.5f)));
    CHECK(borderVerts(m).size() == borderBefore);
}

TEST(dyntopo_keeps_boundaries_and_masked_areas) {
    Document d = makeDoc(makePlane(8, 2.0f)); // y = 0, x and z in -1..1
    d.sculptor.dyntopo.enabled = true;
    d.sculptor.dyntopo.detail = 0.8f;
    Mesh& m = d.mesh;
    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].x < -0.3f ? 1.0f : 0.0f;
    const Mesh before = m;
    CHECK(d.sculptor.beginStroke(m, "Standard"));
    for (int i = 0; i < 10; ++i) {
        RayHit hit;
        const glm::vec3 from(0.2f * float(i) - 1.0f, 3.0f, 0.9f);
        CHECK(d.bvh.raycast(m, from, glm::vec3(0, -1, 0), hit));
        DabParams p;
        p.center = hit.position;
        p.viewDir = glm::vec3(0, -1, 0);
        p.radius = 0.4f;
        p.strength = 0.3f;
        d.sculptor.dab(m, d.bvh, BrushType::Standard, p, Symmetry{false, false, false});
    }
    d.undo.push(d.sculptor.endStroke());
    CHECK(invalid(m, false).empty());
    CHECK(m.triangleCount() > before.triangleCount());
    // Boundary vertices stay on the square's edges; the edge at z = 1 got new vertices along it.
    size_t onEdge = 0;
    bool boundaryOk = true;
    std::map<std::pair<uint32_t, uint32_t>, int> count;
    for (const glm::uvec3& t : m.triangles)
        for (int c = 0; c < 3; ++c) ++count[{std::min(t[c], t[(c + 1) % 3]), std::max(t[c], t[(c + 1) % 3])}];
    for (const auto& [e, n] : count)
        if (n == 1)
            for (uint32_t v : {e.first, e.second}) {
                const glm::vec3 p = m.positions[v];
                const bool edge = std::abs(std::abs(p.x) - 1.0f) < 2e-3f || std::abs(std::abs(p.z) - 1.0f) < 2e-3f;
                boundaryOk = boundaryOk && edge;
                onEdge += std::abs(p.z - 1.0f) < 2e-3f;
            }
    CHECK(boundaryOk);
    CHECK(onEdge / 2 > 9); // more than the 9 vertices it started with (each counted on two boundary edges)
    // The fully masked part kept its vertices and triangles exactly.
    auto maskedTriangles = [](const Mesh& mesh) {
        std::multiset<std::vector<float>> out;
        for (const glm::uvec3& t : mesh.triangles) {
            if (mesh.mask[t.x] < 1.0f || mesh.mask[t.y] < 1.0f || mesh.mask[t.z] < 1.0f) continue;
            std::vector<float> key;
            for (int c = 0; c < 3; ++c)
                for (int k = 0; k < 3; ++k) key.push_back(mesh.positions[t[c]][k]);
            out.insert(key);
        }
        return out;
    };
    CHECK(!maskedTriangles(before).empty());
    CHECK(maskedTriangles(before) == maskedTriangles(m));
}

TEST(dyntopo_refuses_subdivision_levels) {
    Document d = sphereDoc(1);
    CHECK(dyntopoBlocker(d.mesh).empty());
    CommandRegistry reg;
    CHECK(reg.execute(d, "subdiv_divide", {}).ok);
    CHECK(!dyntopoBlocker(d.mesh).empty());
    const size_t before = d.mesh.triangleCount();
    d.sculptor.dyntopo.enabled = true;
    CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
    dabToward(d, glm::vec3(0, 0, 3), 0.3f, BrushType::Standard, Symmetry{});
    UndoRecord r = d.sculptor.endStroke();
    CHECK(!r.topology);
    CHECK(d.mesh.triangleCount() == before);
}

TEST(dyntopo_is_deterministic_with_any_thread_count) {
    auto run = [](bool serial) {
        ThreadPool::shared().setSerial(serial);
        Document d = sphereDoc(3);
        d.sculptor.dyntopo.detail = 0.8f;
        CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
        for (int i = 0; i < 10; ++i) dabToward(d, glm::vec3(0.1f * float(i) - 0.5f, 0.2f, 3.0f), 0.4f, BrushType::Clay, Symmetry{true, false, false});
        d.undo.push(d.sculptor.endStroke());
        ThreadPool::shared().setSerial(false);
        return d.mesh;
    };
    const Mesh a = run(true), b = run(false);
    CHECK(a.triangles == b.triangles);
    CHECK(a.positions == b.positions);
}

TEST(dyntopo_smooth_simplifies) {
    Document d = sphereDoc(4); // 5120 triangles
    const size_t before = d.mesh.triangleCount();
    d.sculptor.dyntopo.detail = 0.0f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Smooth"));
    for (int i = 0; i < 10; ++i) dabToward(d, glm::vec3(0.05f * float(i), 0.0f, 3.0f), 0.5f, BrushType::Smooth, Symmetry{});
    d.undo.push(d.sculptor.endStroke());
    CHECK(invalid(d.mesh, true).empty());
    CHECK(d.mesh.triangleCount() < before);
}

// Smooth remesh (from the discussions): 0 smooths without touching the topology, in between it remeshes less
// than the default, which simplifies the most.
TEST(dyntopo_smooth_remesh_setting) {
    auto run = [](float smoothRemesh) {
        Document d = sphereDoc(4);
        d.sculptor.dyntopo.detail = 0.0f;
        d.sculptor.dyntopo.smoothRemesh = smoothRemesh;
        const std::vector<glm::vec3> before = d.mesh.positions;
        CHECK(d.sculptor.beginStroke(d.mesh, "Smooth"));
        for (int i = 0; i < 10; ++i) dabToward(d, glm::vec3(0.05f * float(i), 0.0f, 3.0f), 0.5f, BrushType::Smooth, Symmetry{});
        d.undo.push(d.sculptor.endStroke());
        CHECK(invalid(d.mesh, true).empty());
        if (smoothRemesh == 0.0f) CHECK(d.mesh.positions.size() == before.size() && d.mesh.positions != before); // smoothed
        return d.mesh.triangleCount();
    };
    const size_t start = makeIcosphere(4, 1.0f).triangleCount();
    const size_t none = run(0.0f), half = run(0.5f), full = run(1.0f);
    CHECK(none == start);
    CHECK(full < start);
    CHECK(half <= start && half >= full);
    CHECK(dyntopoSmoothScale(DyntopoSettings{}) == 2.0f); // the default stays as before
}

// Long randomized sessions (set SF_STRESS=1, or another number for other seeds; skipped otherwise, it takes a while): every brush, detail, radius,
// symmetry and invert, with a partial mask, face groups, hidden triangles, a sculpt layer and vertex paint, random
// undo / redo between strokes; the mesh stays valid after every stroke and undoing everything gives the start back.
TEST(dyntopo_stress_random_sessions) {
    if (!std::getenv("SF_STRESS")) return;
    for (int session = 0; session < 3; ++session) {
        const bool open = session == 2;
        Document d = makeDoc(open ? makePlane(60, 2.0f) : makeIcosphere(4 + session, 1.0f));
        Mesh& m = d.mesh;
        m.ensureColors();
        m.ensureFaceGroups();
        m.ensureMask();
        m.ensureHidden();
        for (uint32_t v = 0; v < m.vertexCount(); ++v) {
            m.colors[v] = glm::vec3(0.5f + 0.5f * m.positions[v].x, 0.3f, 0.2f);
            m.mask[v] = m.positions[v].z < -0.6f ? 1.0f : m.positions[v].z < -0.3f ? 0.5f : 0.0f;
        }
        for (uint32_t t = 0; t < m.triangleCount(); ++t) {
            const glm::vec3 c = m.positions[m.triangles[t].x];
            m.facegroups[t] = c.y > 0.3f ? 2u : 1u;
            m.hidden[t] = c.x > 0.8f && c.y < -0.4f;
        }
        d.activeLayer = session == 1 ? m.addLayer("L") : -1;
        m.rebuildTopology();
        m.recomputeAllNormals();
        d.bvh.build(m);
        const Mesh start = m;
        CHECK(bvhMatches(m, d.bvh));
        d.sculptor.dyntopo.enabled = true;
        // SF_STRESS=N picks the seeds (any N gives a different run; 1 is the reference).
        uint32_t rng = 777u + uint32_t(session) + 1000u * uint32_t(std::max(0, std::atoi(std::getenv("SF_STRESS")) - 1));
        auto rnd = [&rng] {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return float(rng & 0xffff) / 65535.0f;
        };
        const BrushType types[] = {BrushType::Standard, BrushType::Tendril, BrushType::Smooth, BrushType::Clay,
                                   BrushType::Pinch,    BrushType::Inflate,   BrushType::Crease, BrushType::Flatten,
                                   BrushType::SharpCrease, BrushType::ClayBlock, BrushType::Layer, BrushType::Nudge,
                                   BrushType::Trim};
        std::string why;
        int strokes = 0, undos = 0;
        for (int step = 0; step < 120 && why.empty(); ++step) {
            if (rnd() < 0.2f && d.undo.canUndo()) {
                const int n = 1 + int(rnd() * 3);
                for (int i = 0; i < n && d.undo.canUndo(); ++i, ++undos) d.undo.undo(m, d.bvh);
                if (rnd() < 0.5f)
                    while (d.undo.canRedo() && rnd() < 0.7f) d.undo.redo(m, d.bvh);
            } else {
                const Mesh pre = m;
                const BrushType type = types[size_t(rnd() * 12.99f)];
                d.sculptor.dyntopo.detail = rnd();
                d.sculptor.dyntopo.constantDetail = rnd() < 0.2f;
                const Symmetry sym{rnd() < 0.5f, rnd() < 0.15f, rnd() < 0.15f};
                CHECK(d.sculptor.beginStroke(m, brushName(type), d.activeLayer));
                const glm::vec3 dir = open ? glm::vec3(rnd() * 2 - 1, 1.0f, rnd() * 2 - 1)
                                           : glm::normalize(glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) + glm::vec3(0, 0, 0.01f));
                const glm::vec3 step3 = glm::normalize(glm::cross(dir, glm::vec3(0.3f, 0.2f, 1.0f))) * (0.03f + 0.1f * rnd());
                const float radius = 0.05f + 0.4f * rnd() * rnd();
                const bool inv = rnd() < 0.3f;
                for (int i = 0; i < 12; ++i) {
                    RayHit hit;
                    const glm::vec3 from = (dir + step3 * float(i)) * 3.0f;
                    const glm::vec3 ray = open ? glm::vec3(0, -1, 0) : glm::normalize(-from);
                    if (!d.bvh.raycast(m, open ? glm::vec3(from.x, 3.0f, from.z) : from, ray, hit)) continue;
                    DabParams p;
                    p.center = hit.position;
                    p.viewDir = ray;
                    p.radius = radius;
                    p.strength = 0.3f + 0.6f * rnd();
                    p.invert = inv;
                    p.strokeDelta = step3 * 0.5f;
                    d.sculptor.dab(m, d.bvh, type, p, sym);
                    if (std::getenv("SF_STRESS_DEBUG")) std::printf("    dab %d at %g %g %g r %g\n", i, p.center.x, p.center.y, p.center.z, radius);
                }
                const size_t depth = d.undo.undoCount();
                d.undo.push(d.sculptor.endStroke());
                ++strokes;
                // Each stroke undoes to exactly where it started and redoes to exactly where it ended (a stroke that
                // changed nothing pushes no step).
                const Mesh post = m;
                if (d.undo.undoCount() == depth) {
                    if (m.triangles != pre.triangles || m.positions != pre.positions) why = "a stroke without an undo step changed the mesh";
                    goto checked;
                }
                d.undo.undo(m, d.bvh);
                if (m.triangles != pre.triangles || m.positions != pre.positions || m.colors != pre.colors || m.mask != pre.mask) {
                    size_t moved = 0;
                    for (size_t v = 0; v < std::min(m.vertexCount(), pre.vertexCount()); ++v) moved += m.positions[v] != pre.positions[v];
                    std::printf("  step %d %s detail %.2f sym %d%d%d: undo differs (tris %d, %zu positions, topology %d)\n", step,
                                brushName(type), d.sculptor.dyntopo.detail, sym.x, sym.y, sym.z, int(m.triangles != pre.triangles), moved,
                                int(post.triangles != pre.triangles));
                    why = "undo";
                }
                d.undo.redo(m, d.bvh);
                if (why.empty() && (m.triangles != post.triangles || m.positions != post.positions)) why = "redo";
            checked:;
            }
            if (why.empty()) why = invalid(m, !open);
            if (why.empty() && !bvhMatches(m, d.bvh)) why = "BVH";
            if (why.empty() && (m.colors.size() != m.vertexCount() || m.mask.size() != m.vertexCount() ||
                                m.facegroups.size() != m.triangleCount() || m.hidden.size() != m.triangleCount()))
                why = "attribute sizes";
            if (why.empty() && !m.layers.empty())
                for (uint32_t v = 0; v < m.vertexCount() && why.empty(); ++v)
                    if (glm::distance(m.positions[v], m.base[v] + m.layers[0].delta[v] * m.layers[0].weight) > 1e-4f) why = "layer sum";
            if (!why.empty()) std::printf("  session %d step %d: %s\n", session, step, why.c_str());
        }
        CHECK(why.empty());
        std::printf("  session %d: %d strokes, %d undos, %zu -> %zu triangles\n", session, strokes, undos, start.triangleCount(),
                    m.triangleCount());
        while (d.undo.canUndo()) d.undo.undo(m, d.bvh);
        CHECK(m.triangles == start.triangles);
        CHECK(m.positions == start.positions);
        CHECK(m.colors == start.colors && m.mask == start.mask && m.facegroups == start.facegroups && m.hidden == start.hidden);
    }
}

// A first Dynamic stroke on a dense Part with a big brush used to look at the whole brush disc every dab (100-380 ms a dab
// on 800k triangles, a stalled first stroke): the work per dab is bounded, in the triangles it looks at and the
// operations it does, whatever the brush size, and the surface is coarsened over the dabs that follow.
TEST(dyntopo_dab_work_is_bounded_on_a_dense_part_under_a_big_brush) {
    Document d = sphereDoc(6); // 81,920 triangles, edges ~0.02
    d.sculptor.dyntopo.detail = 0.23f;
    const Mesh before = d.mesh;
    CHECK(d.sculptor.beginStroke(d.mesh, "Clay Block"));
    DynamicTopology& dt = d.sculptor.dynamicTopology();
    dt.maxRegionTriangles = 24000; // (an 800k Part under a big brush is held to 100k triangles: this one's disc is cut the same way)
    dt.stats = {};
    size_t last = d.mesh.triangleCount();
    size_t shrinking = 0;
    const int dabs = 12;
    for (int sym = 0; sym < 2; ++sym) {
        for (int i = 0; i < dabs; ++i) {
            CHECK(dabToward(d, glm::vec3(0.12f * float(i), 0.2f, 3.0f), 0.9f, BrushType::ClayBlock, Symmetry{sym == 1, false, false}));
            shrinking += d.mesh.triangleCount() < last ? 1 : 0;
            last = d.mesh.triangleCount();
        }
        if (sym == 0) {
            d.undo.push(d.sculptor.endStroke());
            CHECK(d.sculptor.beginStroke(d.mesh, "Clay Block"));
        }
    }
    CHECK(dt.stats.limited > 0);
    CHECK(dt.stats.maxRegion <= dt.maxRegionTriangles * 3 / 2);
    CHECK(dt.stats.maxOps <= 6500);
    CHECK(shrinking >= size_t(dabs)); // (the detail comes over the dabs: most of them coarsen)
    CHECK(d.mesh.triangleCount() < before.triangleCount() * 9 / 10);
    CHECK(invalid(d.mesh, true).empty());
    CHECK(bvhMatches(d.mesh, d.bvh));
    UndoRecord r = d.sculptor.endStroke();
    // The stroke's undo holds the slots it changed, not the Part.
    CHECK(r.topology != nullptr);
    CHECK(r.topology->triangleSlots.size() < before.triangleCount() / 2);
    d.undo.push(std::move(r));
    while (d.undo.canUndo()) d.undo.undo(d.mesh, d.bvh);
    CHECK(d.mesh.triangles == before.triangles);
    CHECK(d.mesh.positions == before.positions);
}

// Large triangles under a small brush: refined over the dabs, a bounded amount each.
TEST(dyntopo_dab_work_is_bounded_on_large_triangles_under_a_small_brush) {
    Document d = sphereDoc(2); // 320 triangles, edges ~0.5
    d.sculptor.dyntopo.detail = 0.23f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Clay Block"));
    DynamicTopology& dt = d.sculptor.dynamicTopology();
    dt.stats = {};
    for (int i = 0; i < 20; ++i)
        CHECK(dabToward(d, glm::vec3(0.02f * float(i), 0.2f, 3.0f), 0.04f, BrushType::ClayBlock, Symmetry{false, false, false}));
    CHECK(dt.stats.maxOps <= 6500);
    CHECK(d.mesh.triangleCount() > 1000); // (refined: the first dabs cannot do it all at once, the stroke does)
    CHECK(invalid(d.mesh, true).empty());
    CHECK(bvhMatches(d.mesh, d.bvh));
    d.undo.push(d.sculptor.endStroke());
}

// Candidates that fail don't count as operations: where face groups, borders or masks are everywhere (every edge is a feature
// edge here), a collapse pass used to try every short edge under the brush each dab (a tester's 803k Part froze the app,
// the sample all in collapseTarget / featureEdges). Tries per pass are bounded now, and the surface stays valid.
TEST(dyntopo_failing_candidates_are_bounded_per_dab) {
    Document d = sphereDoc(6); // 81,920 triangles
    d.sculptor.dyntopo.detail = 0.23f;
    d.mesh.facegroups.resize(d.mesh.triangleCount());
    for (size_t t = 0; t < d.mesh.triangleCount(); ++t) d.mesh.facegroups[t] = uint32_t(t) + 1; // every edge is a group border
    CHECK(d.sculptor.beginStroke(d.mesh, "Clay Block"));
    DynamicTopology& dt = d.sculptor.dynamicTopology();
    dt.stats = {};
    const int dabs = 6;
    for (int i = 0; i < dabs; ++i)
        CHECK(dabToward(d, glm::vec3(0.1f * float(i), 0.2f, 3.0f), 0.8f, BrushType::ClayBlock, Symmetry{false, false, false}));
    // (A dab remeshes up to twice: once, and again to repair triangles the brush squashed.)
    CHECK(dt.stats.collapseTries <= size_t(dabs) * 2 * 6000 * DynamicTopology::kTriesPerOp);
    CHECK(dt.stats.collapses == 0); // (nothing can collapse: every edge is a feature)
    CHECK(invalid(d.mesh, true).empty());
    d.undo.push(d.sculptor.endStroke());
}

// ---------------------------------------------------------------------------
// UVs and the painted texture (a Dynamic stroke used to drop the UVs, and with them the paint)
// ---------------------------------------------------------------------------

namespace {

// A flat Part (y = 0, x and z in -1..1) with two UV islands that meet along the seam x = 0: left x < 0, right x > 0,
// each mapped by its plane position (0.2 UV per unit) into its own half of the texture.
glm::vec2 seamUv(const glm::vec3& p, bool right) { return glm::vec2(p.x * 0.2f + (right ? 0.75f : 0.25f), p.z * 0.2f + 0.5f); }

bool rightIsland(const Mesh& m, uint32_t t) {
    const glm::uvec3 tri = m.triangles[t];
    return m.positions[tri.x].x + m.positions[tri.y].x + m.positions[tri.z].x > 0.0f;
}

// How far triangle t's UVs stray from the mapping of the island they follow (the better fit of the two): a seam end on the
// boundary may slide a little, and its chart goes with it.
float islandError(const Mesh& m, uint32_t t) {
    float best = 1e9f;
    for (bool right : {false, true}) {
        float worst = 0.0f;
        for (int c = 0; c < 3; ++c)
            worst = std::max(worst, glm::distance(m.uvs[size_t(t) * 3 + size_t(c)], seamUv(m.positions[m.triangles[t][c]], right)));
        best = std::min(best, worst);
    }
    return best;
}

Document seamPlane(int resolution) {
    Document d = makeDoc(makePlane(resolution, 2.0f));
    d.sculptor.dyntopo.enabled = true;
    d.sculptor.dyntopo.detail = 0.8f;
    Mesh& m = d.mesh;
    m.uvs.resize(m.triangleCount() * 3);
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) m.uvs[size_t(t) * 3 + size_t(c)] = seamUv(m.positions[m.triangles[t][c]], rightIsland(m, t));
    m.rebuildTopology();
    d.bvh.build(m);
    // A texture with a pattern in every texel, so any change is visible.
    m.texture = makeTexture(128, 128);
    PaintTexture& tex = *m.texture;
    for (size_t i = 0; i < tex.texelCount(); ++i) {
        tex.pixels[i * 4 + 0] = uint8_t(i * 37u);
        tex.pixels[i * 4 + 1] = uint8_t((i / 128u) * 11u);
        tex.pixels[i * 4 + 2] = uint8_t(i * 5u + 90u);
        tex.pixels[i * 4 + 3] = 255;
    }
    return d;
}

// A straight-down dab at (x, z) on the flat Part (strength small: the Part stays flat, so the UVs can be checked
// against the plane).
void flatDab(Document& d, float x, float z, float radius) {
    DabParams p;
    p.center = glm::vec3(x, 0.0f, z);
    p.viewDir = glm::vec3(0.0f, -1.0f, 0.0f);
    p.radius = radius;
    p.strength = 0.01f;
    Symmetry none;
    none.x = false;
    d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, none);
}

} // namespace

TEST(dyntopo_uv_invariants_paint_untouched_and_no_stretch_across_a_seam) {
    Document d = seamPlane(16);
    Mesh& m = d.mesh;
    const Mesh before = m;
    const std::vector<uint8_t> texels = m.texture->pixels;
    const auto texture = m.texture;
    // A stroke across the seam, small and large dabs (the Part gets finer and coarser under it).
    CHECK(d.sculptor.beginStroke(m, "Standard"));
    for (int i = 0; i < 10; ++i) flatDab(d, 0.12f * float(i) - 0.55f, 0.1f * float(i % 3) - 0.1f, i < 6 ? 0.25f : 0.5f);
    d.undo.push(d.sculptor.endStroke());
    CHECK(invalid(m, false, 1).empty());
    CHECK(m.triangleCount() != before.triangleCount());
    CHECK(m.uvs.size() == m.triangleCount() * 3);
    // The paint is untouched: the same texture object, the same texels.
    CHECK(m.texture == texture);
    CHECK(m.texture->pixels == texels);
    // Every corner is still the plane's mapping of its place on its own island (the seam stayed a seam), and no
    // triangle's texture is stretched: its UV area stays 0.04 of its area.
    float worstUv = 0.0f, lowRatio = 10.0f, highRatio = 0.0f;
    size_t seamCorners = 0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        worstUv = std::max(worstUv, islandError(m, t));
        for (int c = 0; c < 3; ++c)
            if (std::abs(m.positions[tri[c]].x) < 1e-5f) ++seamCorners;
        const glm::vec2 u1 = m.uvs[size_t(t) * 3 + 1] - m.uvs[size_t(t) * 3], u2 = m.uvs[size_t(t) * 3 + 2] - m.uvs[size_t(t) * 3];
        const float uvArea = 0.5f * std::abs(u1.x * u2.y - u1.y * u2.x);
        const float area = 0.5f * glm::length(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]));
        const float ratio = uvArea / area;
        lowRatio = std::min(lowRatio, ratio);
        highRatio = std::max(highRatio, ratio);
    }
    CHECK(worstUv < 2e-3f);
    CHECK(lowRatio > 0.04f * 0.9f && highRatio < 0.04f * 1.1f);
    CHECK(seamCorners > 0);
    // The seam is intact: a texture lookup on each side of it still reads that side's texels.
    CHECK(bvhMatches(m, d.bvh));
    // Undo brings back the exact UVs, redo the new ones.
    const Mesh after = m;
    d.undo.undo(m, d.bvh);
    CHECK(m.uvs == before.uvs);
    CHECK(m.triangles == before.triangles);
    CHECK(m.texture->pixels == texels);
    d.undo.redo(m, d.bvh);
    CHECK(m.uvs == after.uvs);
    CHECK(m.triangles == after.triangles);
    CHECK(m.texture->pixels == texels);
}

TEST(dyntopo_uv_invariants_seam_vertices_collapse_only_along_the_seam) {
    Document d = seamPlane(16);
    d.sculptor.dyntopo.detail = 0.0f; // coarse: the brush wants to collapse what is under it
    Mesh& m = d.mesh;
    auto seamVertices = [](const Mesh& mesh) { // (the vertices on the seam: they can be split, never removed)
        size_t n = 0;
        for (const glm::vec3& p : mesh.positions)
            if (std::abs(p.x) < 1e-5f) ++n;
        return n;
    };
    const size_t seamBefore = seamVertices(m);
    CHECK(d.sculptor.beginStroke(m, "Standard"));
    for (int i = 0; i < 6; ++i) flatDab(d, 0.0f, 0.3f * float(i) - 0.75f, 0.9f);
    d.undo.push(d.sculptor.endStroke());
    const size_t seamAfter = seamVertices(m);
    CHECK(invalid(m, false, 1).empty());
    CHECK(m.triangleCount() < size_t(2 * 16 * 16)); // (it did simplify)
    CHECK(seamAfter < seamBefore);  // (seam vertices merged into their neighbours along the seam, and the UV check below holds)
    for (uint32_t t = 0; t < m.triangleCount(); ++t) CHECK(islandError(m, t) < 2e-3f);
}

TEST(dyntopo_uv_invariants_on_a_curved_part_with_symmetry_and_undo) {
    Document d = sphereDoc(3);
    Mesh& m = d.mesh;
    // Two islands (upper and lower half, planar maps), so the equator is a seam all the way round.
    m.uvs.resize(m.triangleCount() * 3);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        const bool upper = m.positions[tri.x].y + m.positions[tri.y].y + m.positions[tri.z].y > 0.0f;
        for (int c = 0; c < 3; ++c)
            m.uvs[size_t(t) * 3 + size_t(c)] = glm::vec2(m.positions[tri[c]].x, m.positions[tri[c]].z) * 0.25f + glm::vec2(upper ? 0.25f : 0.75f, 0.5f);
    }
    m.rebuildTopology();
    d.bvh.build(m);
    const Mesh before = m;
    d.sculptor.dyntopo.detail = 0.8f;
    Symmetry sym; // (X mirror on, as by default)
    for (int s = 0; s < 3; ++s) {
        CHECK(d.sculptor.beginStroke(m, "Standard"));
        for (int i = 0; i < 8; ++i) dabToward(d, glm::vec3(0.12f * float(i) - 0.4f, 0.35f * float(s) - 0.3f, 3.0f), 0.3f, BrushType::Standard, sym);
        d.undo.push(d.sculptor.endStroke());
        CHECK(invalid(m, true).empty());
        CHECK(m.uvs.size() == m.triangleCount() * 3);
    }
    CHECK(m.triangleCount() != before.triangleCount());
    // Each island's UVs stay inside the range the planar map gave it (nothing flung out or extrapolated).
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec2 uv = m.uvs[size_t(t) * 3 + size_t(c)];
            const bool upper = uv.x < 0.5f;
            CHECK(std::abs(uv.x - (upper ? 0.25f : 0.75f)) < 0.3f && std::abs(uv.y - 0.5f) < 0.3f);
        }
    CHECK(bvhMatches(m, d.bvh));
    while (d.undo.canUndo()) d.undo.undo(m, d.bvh);
    CHECK(m.uvs == before.uvs);
    CHECK(m.triangles == before.triangles);
}

namespace {
// Where a Part's charts are: every texel a UV triangle touches and `pad` of 1024 around it (two: the padding painting leaves around a chart),
// and the triangles that sample outside that (a centre or a corner a little inside: a white speck on the paint).
constexpr int kGrid = 1024;
std::vector<uint8_t> chartFootprint(const Mesh& m, int pad = 2) {
    std::vector<uint8_t> occupied(size_t(kGrid) * kGrid, 0);
    for (uint32_t t = 0; t < m.triangleCount() && !m.uvs.empty(); ++t) {
        const glm::vec2 a = m.uvs[size_t(t) * 3], e1 = m.uvs[size_t(t) * 3 + 1] - a, e2 = m.uvs[size_t(t) * 3 + 2] - a;
        const int steps = std::max(8, int(std::max(glm::length(e1), glm::length(e2)) * float(kGrid)) + 1); // (a sample a pixel)
        for (int i = 0; i <= steps; ++i)
            for (int j = 0; i + j <= steps; ++j) {
                const glm::vec2 q = a + e1 * (float(i) / float(steps)) + e2 * (float(j) / float(steps));
                const int x = int(q.x * kGrid), y = int(q.y * kGrid);
                for (int dy = -pad; dy <= pad; ++dy)
                    for (int dx = -pad; dx <= pad; ++dx)
                        if (x + dx >= 0 && x + dx < kGrid && y + dy >= 0 && y + dy < kGrid) occupied[size_t(y + dy) * kGrid + size_t(x + dx)] = 1;
            }
    }
    return occupied;
}
size_t strayTriangles(const Mesh& m, const std::vector<uint8_t>& occupied) {
    size_t stray = 0;
    for (uint32_t t = 0; t < m.triangleCount() && !m.uvs.empty(); ++t) {
        const glm::vec2 c = (m.uvs[size_t(t) * 3] + m.uvs[size_t(t) * 3 + 1] + m.uvs[size_t(t) * 3 + 2]) / 3.0f;
        bool out = false;
        for (int k = 0; k < 4 && !out; ++k) { // (the centre and each corner, a little inside)
            const glm::vec2 q = k == 3 ? c : c + (m.uvs[size_t(t) * 3 + size_t(k)] - c) * 0.9f;
            const int x = int(q.x * kGrid), y = int(q.y * kGrid);
            out = x < 0 || x >= kGrid || y < 0 || y >= kGrid || !occupied[size_t(y) * kGrid + size_t(x)];
        }
        stray += out ? 1 : 0;
    }
    return stray;
}
// The same, for the whole triangle: any of a lattice of points over its area (a little inside its edge) outside the footprint. A long
// triangle along a chart's bending edge or across its hollow has its centre and corners on the paint and white texels between them.
size_t strictStrayTriangles(const Mesh& m, const std::vector<uint8_t>& occupied) {
    size_t stray = 0;
    const int n = 24;
    for (uint32_t t = 0; t < m.triangleCount() && !m.uvs.empty(); ++t) {
        const glm::vec2 a = m.uvs[size_t(t) * 3], b = m.uvs[size_t(t) * 3 + 1], c = m.uvs[size_t(t) * 3 + 2], centre = (a + b + c) / 3.0f;
        bool out = false;
        for (int i = 0; i <= n && !out; ++i)
            for (int j = 0; i + j <= n && !out; ++j) {
                const glm::vec2 p = a * (float(i) / float(n)) + b * (float(j) / float(n)) + c * (float(n - i - j) / float(n));
                const glm::vec2 q = centre + (p - centre) * 0.97f;
                const int x = int(q.x * kGrid), y = int(q.y * kGrid);
                out = x < 0 || x >= kGrid || y < 0 || y >= kGrid || !occupied[size_t(y) * kGrid + size_t(x)];
            }
        stray += out ? 1 : 0;
    }
    return stray;
}
} // namespace

// A strip whose chart is bent into an arc: a long triangle across the strip's middle crosses the hollow the arc encloses, where
// there is no paint. Coarse dabs collapse the strip's triangles into longer ones; none may end up across the hollow or turned over.
TEST(dyntopo_uv_invariants_a_bent_chart_is_not_bridged_by_big_triangles) {
    Document d = makeDoc(makePlane(24, 2.0f));
    d.sculptor.dyntopo.enabled = true;
    d.sculptor.dyntopo.detail = 0.0f;
    Mesh& m = d.mesh;
    m.uvs.resize(m.triangleCount() * 3);
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = m.positions[m.triangles[t][c]]; // (x along the arc, z across it)
            const float angle = (0.15f + 1.7f * (p.x + 1.0f) * 0.5f) * 3.14159265f, radius = 0.30f + 0.10f * (p.z + 1.0f) * 0.5f;
            m.uvs[size_t(t) * 3 + size_t(c)] = glm::vec2(0.5f + radius * std::cos(angle), 0.5f + radius * std::sin(angle));
        }
    m.rebuildTopology();
    d.bvh.build(m);
    const std::vector<uint8_t> footprint = chartFootprint(m), tight = chartFootprint(m, 1);
    CHECK(strayTriangles(m, footprint) == 0);
    auto turn = [&](uint32_t t) { // (the sign of the triangle's area in UV space)
        const glm::vec2 e1 = m.uvs[size_t(t) * 3 + 1] - m.uvs[size_t(t) * 3], e2 = m.uvs[size_t(t) * 3 + 2] - m.uvs[size_t(t) * 3];
        return e1.x * e2.y - e1.y * e2.x;
    };
    const float side = turn(0) > 0.0f ? 1.0f : -1.0f;
    const size_t before = m.triangleCount();
    for (int s = 0; s < 4; ++s) {
        CHECK(d.sculptor.beginStroke(m, "Standard"));
        for (int i = 0; i < 12; ++i) flatDab(d, 0.15f * float(i) - 0.85f + 0.04f * float(s), 0.2f * float(s) - 0.3f, 0.7f);
        d.undo.push(d.sculptor.endStroke());
    }
    CHECK(invalid(m, false, 1).empty());
    CHECK(m.triangleCount() < before * 3 / 4); // (it did simplify)
    CHECK(strayTriangles(m, footprint) <= 4); // (a few of ~230 at the chart's thin ends; 115 of 161 before the fix)
    CHECK(strictStrayTriangles(m, tight) <= 4);
    size_t turned = 0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) turned += turn(t) * side <= 0.0f ? 1 : 0;
    CHECK(turned == 0);
}

namespace {
// Edges whose two triangles face away from each other (a fold-over).
size_t foldedEdges(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, std::vector<glm::vec3>> e;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        const glm::vec3 n = glm::normalize(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]));
        for (int c = 0; c < 3; ++c) e[{std::min(tri[c], tri[(c + 1) % 3]), std::max(tri[c], tri[(c + 1) % 3])}].push_back(n);
    }
    size_t folds = 0;
    for (const auto& [k, v] : e)
        if (v.size() == 2 && glm::dot(v[0], v[1]) < 0.0f) ++folds;
    return folds;
}
} // namespace

// The Human head (one level, Auto UV: seams all round the eyes and the face), six strokes across the eyes with the
// brushes an artist uses: Dynamic must leave it about as clean as on a head without UVs (a Part whose seam vertices
// could not merge or relax tore open at the eyelids: 140 folded edges against 6), with sane UVs everywhere.
TEST(nightly_dyntopo_uv_invariants_painted_head_stays_as_clean_as_an_unpainted_one) {
    size_t folds[2] = {0, 0};
    for (int painted = 0; painted < 2; ++painted) {
        Document d;
        CommandRegistry reg;
        CHECK(reg.execute(d, "new_starter_mesh", {{"name", std::string("human-head")}}).ok);
        CHECK(reg.execute(d, "subdiv_freeze", {}).ok);
        CHECK(reg.execute(d, "dyntopo", {{"enabled", true}, {"detail", 50.0}}).ok);
        if (painted) CHECK(reg.execute(d, "uv_auto", {{"resolution", 2048.0}}).ok);
        Mesh& m = d.mesh;
        if (painted) m.texture = makeTexture(2048, 2048); // (its size sets how far a UV may stray: the padding is 4 texels)
        d.bvh.build(m);
        const std::vector<uint8_t> tight = chartFootprint(m, 1); // (the painted area and one cell round it)
        const AABB b = m.bounds();
        const glm::vec3 lo = b.min, hi = b.max, ext = hi - lo;
        const float diag = glm::length(ext);
        const std::vector<uint8_t> occupied = chartFootprint(m); // (where the charts are: a triangle's UVs must stay in there)
        auto countStray = [&]() { return strayTriangles(m, occupied); };
        if (painted) CHECK(countStray() == 0);
        double dabMs = 0.0;
        int dabs = 0;
        struct Stroke {
            BrushType type;
            float radius; // of the head's size
        };
        const Stroke strokes[] = {{BrushType::Standard, 0.08f}, {BrushType::Standard, 0.35f}, {BrushType::Clay, 0.25f},
                                  {BrushType::Clay, 0.06f},     {BrushType::Smooth, 0.30f},   {BrushType::Tendril, 0.15f}}; // (as brush sizes on a head filling a quarter of the window)
        for (const Stroke& s : strokes) {
            CHECK(d.sculptor.beginStroke(m, "Standard"));
            for (int i = 0; i < 100; ++i) { // a wavy line across the eyes, seen from the front
                const float fx = 0.25f + 0.5f * float(i) / 99.0f, fy = 0.62f + 0.04f * std::sin(float(i) * 0.17f);
                RayHit hit;
                if (!d.bvh.raycast(m, glm::vec3(lo.x + ext.x * fx, lo.y + ext.y * fy, hi.z + ext.z), glm::vec3(0, 0, -1), hit)) continue;
                DabParams p;
                p.center = hit.position;
                p.viewDir = glm::vec3(0, 0, -1);
                p.radius = s.radius * diag;
                p.strength = 0.5f;
                const auto t0 = std::chrono::steady_clock::now();
                d.sculptor.dab(m, d.bvh, s.type, p, Symmetry{});
                dabMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                ++dabs;
            }
            d.undo.push(d.sculptor.endStroke());
        }
        for (const Stroke& s2 : strokes) { // and down the forehead (a vertical band, as an artist paints one)
            CHECK(d.sculptor.beginStroke(m, "Standard"));
            for (int i = 0; i < 100; ++i) {
                const float fx = 0.5f + 0.03f * std::sin(float(i) * 0.2f), fy = 0.6f + 0.3f * float(i) / 99.0f;
                RayHit hit;
                if (!d.bvh.raycast(m, glm::vec3(lo.x + ext.x * fx, lo.y + ext.y * fy, hi.z + ext.z), glm::vec3(0, 0, -1), hit)) continue;
                DabParams p;
                p.center = hit.position;
                p.viewDir = glm::vec3(0, 0, -1);
                p.radius = s2.radius * diag;
                p.strength = 0.5f;
                const auto t0 = std::chrono::steady_clock::now();
                d.sculptor.dab(m, d.bvh, s2.type, p, Symmetry{});
                dabMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                ++dabs;
            }
            d.undo.push(d.sculptor.endStroke());
        }
        const std::string broken = invalid(m, false, 0);
        if (!broken.empty()) std::printf("  mesh invalid: %s\n", broken.c_str());
        CHECK(broken.empty());
        std::printf("  %s: %d dabs, %.2f ms per dab\n", painted ? "painted" : "plain", dabs, dabMs / double(std::max(dabs, 1)));
        folds[painted] = foldedEdges(m);
        if (!painted) continue;
        CHECK(m.uvs.size() == m.triangleCount() * 3);
        size_t outliers = 0;
        for (uint32_t t = 0; t < m.triangleCount(); ++t) {
            const glm::vec2 u0 = m.uvs[size_t(t) * 3], u1 = m.uvs[size_t(t) * 3 + 1], u2 = m.uvs[size_t(t) * 3 + 2];
            for (const glm::vec2& u : {u0, u1, u2})
                if (u.x < -0.01f || u.x > 1.01f || u.y < -0.01f || u.y > 1.01f) ++outliers;
        }
        CHECK(outliers == 0);
        const size_t stray = countStray();
        std::printf("  %zu triangles, %zu with UVs outside every chart\n", m.triangleCount(), stray);
        const size_t strict = strictStrayTriangles(m, tight);
        std::printf("  %zu of them reach past one cell of padding (a triangle's whole UV area, not just its centre and corners)\n", strict);
        CHECK(strict <= 100); // (the same promise as the centre-and-corners count; 545 on one build before the fix, 192 with the centre-and-corners check, 1-68 after)
        CHECK(stray <= 100); // (of ~25000, simplified from 135000: a corner just past the padding of a chart is a white speck on the paint)
    }
    std::printf("  folded edges: %zu without UVs, %zu with\n", folds[0], folds[1]);
    CHECK(folds[1] <= 6 * folds[0] + 40); // (heavy simplification under head-sized brushes: the seams keep some triangles)

}
