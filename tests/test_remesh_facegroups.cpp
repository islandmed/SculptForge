// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Remesh: Voxel Remesh > Keep Face Groups (core/Voxel.h transferFaceGroups) and the topology refresh gesture
// (Sculptor::refreshTopologyDab: the Quick Remesh drag key held while dragging on the model refreshes only the stretched
// triangles under the brush, at the Part's detail, with no collapses). Geometric promises, measured: the groups come
// through with hard borders and no specks; a refresh never moves the surface, never coarsens detail, keeps face group
// borders, leaves everything outside the brush bit for bit, and is one exact undo step.
#include "TestHarness.h"

#include "core/Dyntopo.h"
#include "core/Primitives.h"
#include "core/QuickRemesh.h"
#include "core/Sculpt.h"
#include "core/SurfaceQuery.h"
#include "core/Voxel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>
#include <vector>

using namespace sf;

namespace {

// Face group per triangle from a predicate on its centroid.
template <class F> void groupBy(Mesh& m, F group) {
    m.ensureFaceGroups();
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        m.facegroups[t] = group((m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f);
    }
}

glm::vec3 centroid(const Mesh& m, uint32_t t) {
    const glm::uvec3 tri = m.triangles[t];
    return (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f;
}

std::set<uint32_t> groupsOf(const Mesh& m) { return std::set<uint32_t>(m.facegroups.begin(), m.facegroups.end()); }

// Polygons (quad pairs, else triangles) whose edge neighbours all have one other group: specks a border transfer leaves.
size_t specks(const Mesh& m) {
    const size_t nt = m.triangleCount();
    std::vector<uint32_t> lead(nt);
    for (uint32_t t = 0; t < nt; ++t) {
        const uint32_t u = m.quadPartners.empty() ? Mesh::kNoQuad : m.quadPartners[t];
        lead[t] = (u < nt && u < t && m.quadPartners[u] == t) ? u : t;
    }
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> first;
    std::map<uint32_t, std::vector<uint32_t>> nb;
    for (uint32_t t = 0; t < nt; ++t) {
        const glm::uvec3 tri = m.triangles[t];
        for (int c = 0; c < 3; ++c) {
            const auto e = std::minmax(tri[c], tri[(c + 1) % 3]);
            const auto it = first.find({e.first, e.second});
            if (it == first.end()) first[{e.first, e.second}] = lead[t];
            else if (it->second != lead[t]) {
                nb[lead[t]].push_back(it->second);
                nb[it->second].push_back(lead[t]);
            }
        }
    }
    size_t n = 0;
    for (const auto& [p, ns] : nb) {
        if (ns.size() < 3) continue;
        bool all = true;
        for (uint32_t q : ns) all = all && m.facegroups[q] != m.facegroups[p] && m.facegroups[q] == m.facegroups[ns[0]];
        n += all;
    }
    return n;
}

// Every edge on exactly two triangles, wound oppositely; no degenerate triangle.
bool closedManifold(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> directed;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        if (tri.x == tri.y || tri.y == tri.z || tri.x == tri.z) return false;
        const glm::vec3 n = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
        if (!(glm::length(n) > 1e-14f)) return false;
        for (int c = 0; c < 3; ++c)
            if (++directed[{tri[c], tri[(c + 1) % 3]}] > 1) return false;
    }
    for (const auto& [e, n] : directed)
        if (!directed.count({e.second, e.first})) return false;
    return true;
}

float longestEdgeWithin(const Mesh& m, const glm::vec3& c, float r) {
    float longest = 0.0f;
    for (const glm::uvec3& tri : m.triangles)
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 a = m.positions[tri[k]], b = m.positions[tri[(k + 1) % 3]];
            if (glm::distance(a, c) <= r && glm::distance(b, c) <= r) longest = std::max(longest, glm::distance(a, b));
        }
    return longest;
}

// A sphere with a tall bump pulled out of its +Z pole (edges there stretched 2-4x), as a long Move pull leaves it.
Mesh pulledSphere(int subdivisions, uint32_t groupLeft = 0, uint32_t groupRight = 0) {
    Mesh m = makeIcosphere(subdivisions, 1.0f);
    for (glm::vec3& p : m.positions) {
        const float d = glm::distance(glm::vec2(p.x, p.y), glm::vec2(0.0f));
        if (p.z > 0.0f && d < 0.4f) {
            const float w = 1.0f - d / 0.4f;
            p.z += 0.9f * w * w;
            p.x *= 1.0f + 0.6f * w; // (the sides of the bump stretch too)
            p.y *= 1.0f + 0.6f * w;
        }
    }
    if (groupLeft != groupRight) groupBy(m, [&](const glm::vec3& c) { return c.x < 0.0f ? groupLeft : groupRight; });
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

} // namespace

TEST(voxel_remesh_keep_face_groups_invariant_hard_borders) {
    Mesh m = makeIcosphere(4, 1.0f);
    groupBy(m, [](const glm::vec3& c) { return c.y > 0.0f ? 7u : 3u; });
    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].y > 0.0f ? 1.0f : 0.0f;
    std::string err;
    // Off (the default): one group, as before.
    const Mesh plain = voxelRemesh(m, 96, &err);
    CHECK(plain.triangleCount() > 0);
    CHECK(!plain.hasFaceGroups() || groupsOf(plain) == std::set<uint32_t>{0u});
    // On: both groups, a hard border within two voxels of y = 0, no specks, quads whole, the mask still carried.
    const Mesh out = voxelRemesh(m, 96, &err, {}, false, true);
    CHECK(out.triangleCount() > 0);
    CHECK(out.facegroups.size() == out.triangleCount());
    CHECK(groupsOf(out) == (std::set<uint32_t>{3u, 7u}));
    const float cell = 2.0f / 96.0f;
    size_t wrongSide = 0, splitQuads = 0, up = 0, down = 0;
    for (uint32_t t = 0; t < out.triangleCount(); ++t) {
        const float y = centroid(out, t).y;
        if (out.facegroups[t] == 7u) ++up, wrongSide += y < -2.0f * cell;
        else ++down, wrongSide += y > 2.0f * cell;
        const uint32_t u = out.quadPartners.empty() ? Mesh::kNoQuad : out.quadPartners[t];
        if (u < out.triangleCount() && out.facegroups[u] != out.facegroups[t]) ++splitQuads;
    }
    std::printf("  Keep Face Groups: %zu triangles, %zu up / %zu down, %zu on the wrong side, %zu split quads, %zu specks\n",
                out.triangleCount(), up, down, wrongSide, splitQuads, specks(out));
    CHECK(wrongSide == 0);
    CHECK(splitQuads == 0);
    CHECK(specks(out) == 0);
    CHECK(up > out.triangleCount() / 3 && down > out.triangleCount() / 3);
    CHECK(out.mask.size() == out.vertexCount());
    size_t maskedUp = 0, maskedDown = 0;
    for (uint32_t v = 0; v < out.vertexCount(); ++v)
        if (out.mask[v] > 0.5f) (out.positions[v].y > 0.0f ? maskedUp : maskedDown)++;
    CHECK(maskedUp > 0 && maskedDown < maskedUp / 20 + 8);
}

// The border of the kept groups, relaxed at the scale of the source triangles: a mask's edge drawn along a coarse
// sphere's triangles comes through as a clean line, not their saw teeth; a stripe thinner than those triangles survives.
TEST(voxel_remesh_keep_face_groups_invariant_border_relax) {
    Mesh m = makeIcosphere(4, 1.0f);
    groupBy(m, [](const glm::vec3& c) { return c.y > 0.0f ? 7u : 3u; });
    std::string err;
    Mesh raw = voxelRemesh(m, 160, &err); // (voxels 8x smaller than the source triangles: the teeth are 3-4 voxels deep)
    CHECK(raw.triangleCount() > 0);
    Mesh relaxed = raw;
    transferFaceGroups(m, raw, false);
    transferFaceGroups(m, relaxed, true);
    // Polygons on the wrong side of y = 0 by more than `cells` voxels: the raw transfer follows the source triangles'
    // saw teeth (half a source edge deep); the relaxed border sits within a voxel or two of the mid-line.
    const float cell = 2.0f / 160.0f;
    auto wrongBeyond = [&](const Mesh& out, float cells) {
        size_t n = 0;
        for (uint32_t t = 0; t < out.triangleCount(); ++t) {
            const float y = centroid(out, t).y;
            n += (out.facegroups[t] == 7u) != (y > 0.0f) && std::abs(y) > cells * cell;
        }
        return n;
    };
    std::printf("  border relax: wrong side beyond 1 / 2 cells: raw %zu / %zu, relaxed %zu / %zu (cell %.4f, source edge %.4f)\n",
                wrongBeyond(raw, 1.0f), wrongBeyond(raw, 2.0f), wrongBeyond(relaxed, 1.0f), wrongBeyond(relaxed, 2.0f), cell,
                typicalEdgeLength(m));
    CHECK(groupsOf(relaxed) == (std::set<uint32_t>{3u, 7u}));
    CHECK(specks(relaxed) == 0);
    CHECK(wrongBeyond(raw, 2.0f) > 100);          // the teeth are there to remove
    CHECK(wrongBeyond(relaxed, 2.0f) == 0);
    CHECK(wrongBeyond(relaxed, 1.0f) < relaxed.triangleCount() / 200); // (the voxel staircase itself: within two voxels)
    // A stripe of its own group, thinner than the source's triangles: it keeps its polygons (never relaxed away).
    Mesh striped = makeIcosphere(4, 1.0f);
    groupBy(striped, [](const glm::vec3& c) { return std::abs(c.y) < 0.05f ? 5u : c.y > 0.0f ? 7u : 3u; });
    const Mesh out = voxelRemesh(striped, 96, &err, {}, false, true);
    CHECK(groupsOf(out) == (std::set<uint32_t>{3u, 5u, 7u}));
    size_t stripe = 0;
    for (uint32_t g : out.facegroups) stripe += g == 5u;
    std::printf("  stripe group: %zu of %zu triangles\n", stripe, out.triangleCount());
    CHECK(stripe > out.triangleCount() / 40);
}

TEST(voxel_remesh_keep_face_groups_sparse_and_open) {
    // The brick grid (above 512 in the app; forced low here) and an open mesh (capped first) keep the groups the same way.
    Mesh m = makeIcosphere(4, 1.0f);
    groupBy(m, [](const glm::vec3& c) { return c.x > 0.0f ? 11u : 12u; });
    setVoxelSparseAbove(64);
    std::string err;
    const Mesh sparse = voxelRemesh(m, 128, &err, {}, false, true);
    setVoxelSparseAbove(kVoxelDenseMax);
    CHECK(sparse.triangleCount() > 0);
    CHECK(groupsOf(sparse) == (std::set<uint32_t>{11u, 12u}));
    // (The source border zigzags along the coarse sphere's triangles, which the transfer follows faithfully: the
    // tolerance is that zigzag plus two voxels.)
    const float zig = typicalEdgeLength(m) * 0.5f;
    size_t wrong = 0;
    for (uint32_t t = 0; t < sparse.triangleCount(); ++t) {
        const glm::vec3 c = centroid(sparse, t);
        const bool bad = (sparse.facegroups[t] == 11u) != (c.x > 0.0f) && std::abs(c.x) > zig + 2.0f * 2.0f / 128.0f;
        if (bad && wrong < 4) std::printf("  sparse: triangle %u group %u at %.3f %.3f %.3f\n", t, sparse.facegroups[t], c.x, c.y, c.z);
        wrong += bad;
    }
    std::printf("  sparse: %zu triangles, %zu on the wrong side, %zu specks\n", sparse.triangleCount(), wrong, specks(sparse));
    CHECK(wrong == 0);
    CHECK(specks(sparse) == 0);

    Mesh bowl = makeIcosphere(4, 1.0f); // open: the lower half cut away; the rim is capped first
    {
        std::vector<glm::uvec3> keep;
        for (const glm::uvec3& t : bowl.triangles)
            if ((bowl.positions[t.x] + bowl.positions[t.y] + bowl.positions[t.z]).z > -0.3f) keep.push_back(t);
        bowl.triangles = keep;
        bowl.rebuildTopology();
    }
    groupBy(bowl, [](const glm::vec3& c) { return c.x > 0.0f ? 1u : 2u; });
    const Mesh capped = voxelRemesh(bowl, 64, &err, {}, false, true);
    CHECK(capped.triangleCount() > 0);
    CHECK(groupsOf(capped) == (std::set<uint32_t>{1u, 2u}));
    wrong = 0;
    for (uint32_t t = 0; t < capped.triangleCount(); ++t)
        wrong += (capped.facegroups[t] == 1u) != (centroid(capped, t).x > 0.0f) && std::abs(centroid(capped, t).x) > zig + 2.0f * 2.0f / 64.0f;
    std::printf("  capped bowl: %zu triangles, %zu on the wrong side\n", capped.triangleCount(), wrong);
    CHECK(wrong == 0);
}

TEST(typical_edge_length_is_robust_to_a_few_stretched_edges) {
    Mesh m = makeIcosphere(3, 1.0f);
    const float even = typicalEdgeLength(m);
    double sum = 0.0;
    for (const glm::uvec3& t : m.triangles) sum += glm::distance(m.positions[t.x], m.positions[t.y]);
    const float mean = float(sum / double(m.triangleCount()));
    CHECK(std::abs(even - mean) < 0.1f * mean);
    // Pull 3% of the vertices far out: the median hardly moves (a mean would).
    for (uint32_t v = 0; v < m.vertexCount(); v += 33) m.positions[v] *= 6.0f;
    const float pulled = typicalEdgeLength(m);
    std::printf("  typical edge: even %.4f (mean %.4f), after pulling %.4f\n", even, mean, pulled);
    CHECK(std::abs(pulled - even) < 0.1f * even);
    CHECK(typicalEdgeLength(Mesh()) == 0.0f);
}

TEST(refresh_topology_invariant_splits_stretched_edges_only_under_the_brush) {
    Document d = makeDoc(pulledSphere(5, 4u, 9u)); // 20k triangles, groups split at x = 0
    Mesh& m = d.mesh;
    d.bvh.build(m);
    const Mesh before = m;
    const float edge = typicalEdgeLength(makeIcosphere(5, 1.0f)); // the Part's detail away from the bump
    const glm::vec3 top(0.0f, 0.0f, 1.9f);
    const float radius = 0.55f;
    // Dynamic topology off: the detail of the surface around the brush (the stretched triangles are the few big ones).
    const float around = d.sculptor.refreshEdgeLength(m, d.bvh, top, radius);
    std::printf("  refresh target around the bump %.4f, the even sphere's edge %.4f\n", around, edge);
    CHECK(std::abs(around - edge) < 0.25f * edge);
    const float stretchedBefore = longestEdgeWithin(m, top, radius);
    CHECK(stretchedBefore > 2.0f * edge);
    TriangleBVH surface;
    surface.build(before.positions, before.triangles);

    CHECK(d.sculptor.beginStroke(m, "Refresh Topology", -1));
    int dabs = 0;
    bool changed = true;
    while (changed && dabs < 30) changed = d.sculptor.refreshTopologyDab(m, d.bvh, top, radius, edge, Symmetry{}), ++dabs;
    // A dab away from the bump, towards a coarser target: refine only, so nothing is collapsed (no coarsening).
    const size_t trisBeforeCoarse = m.triangleCount();
    CHECK(!d.sculptor.refreshTopologyDab(m, d.bvh, glm::vec3(0.0f, 0.0f, -1.0f), 0.5f, 2.5f * edge, Symmetry{}));
    CHECK(m.triangleCount() == trisBeforeCoarse);
    UndoRecord r = d.sculptor.endStroke();
    CHECK(r.topology != nullptr);

    const float stretchedAfter = longestEdgeWithin(m, top, radius - 1.5f * edge);
    std::printf("  refresh: %d dabs, %zu -> %zu triangles, longest edge under the brush %.3f -> %.3f (target %.3f, split above %.3f)\n",
                dabs, before.triangleCount(), m.triangleCount(), stretchedBefore, stretchedAfter, edge, edge * 1.5f);
    CHECK(closedManifold(m));
    CHECK(m.triangleCount() > before.triangleCount());
    CHECK(stretchedAfter <= edge * 1.5f * 1.05f);
    // The surface didn't move: every vertex lies on the old surface (relaxing slides along it), and outside the brush
    // the mesh is bit for bit what it was (the original vertices keep their slots: no collapses, so nothing renumbered).
    float off = 0.0f;
    for (const glm::vec3& p : m.positions) {
        SurfaceHit hit;
        CHECK(surface.closestPoint(p, 1.0f, hit));
        off = std::max(off, hit.distance);
    }
    size_t movedOutside = 0;
    CHECK(m.vertexCount() >= before.vertexCount());
    for (uint32_t v = 0; v < before.vertexCount(); ++v)
        if (glm::distance(before.positions[v], top) > radius + 2.0f * edge && m.positions[v] != before.positions[v]) ++movedOutside;
    size_t movedOriginal = 0;
    for (uint32_t v = 0; v < before.vertexCount(); ++v) movedOriginal += m.positions[v] != before.positions[v];
    // (New points sit on the surface as it was, within the sag of the chords the stretched triangles already had: a
    // flipped diagonal is such a chord, and a later split lands on it.)
    std::printf("  surface drift %.5f (edge %.4f, stretched %.4f), %zu original vertices moved (%zu outside the brush)\n", off, edge,
                stretchedBefore, movedOriginal, movedOutside);
    CHECK(off < 0.1f * stretchedBefore);
    CHECK(movedOutside == 0);
    CHECK(movedOriginal == 0); // refine only: the vertices that were there stay bit for bit, the new ones settle
    // Face groups came through and their border is the same curve: every border vertex lies on an old border edge.
    CHECK(m.facegroups.size() == m.triangleCount());
    CHECK(groupsOf(m) == (std::set<uint32_t>{4u, 9u}));
    auto borderSegments = [](const Mesh& mesh) {
        std::map<std::pair<uint32_t, uint32_t>, std::set<uint32_t>> groups;
        for (uint32_t t = 0; t < mesh.triangleCount(); ++t)
            for (int c = 0; c < 3; ++c) {
                const auto e = std::minmax(mesh.triangles[t][c], mesh.triangles[t][(c + 1) % 3]);
                groups[{e.first, e.second}].insert(mesh.facegroups[t]);
            }
        std::vector<std::pair<glm::vec3, glm::vec3>> segs;
        for (const auto& [e, g] : groups)
            if (g.size() > 1) segs.emplace_back(mesh.positions[e.first], mesh.positions[e.second]);
        return segs;
    };
    const auto oldBorder = borderSegments(before), newBorder = borderSegments(m);
    float borderOff = 0.0f;
    glm::vec3 worst(0.0f);
    for (const auto& [a, b] : newBorder)
        for (const glm::vec3 p : {a, b}) {
            float best = 1e30f;
            for (const auto& [c, d] : oldBorder) {
                const glm::vec3 e = d - c;
                const float t = glm::clamp(glm::dot(p - c, e) / std::max(glm::dot(e, e), 1e-20f), 0.0f, 1.0f);
                best = std::min(best, glm::distance(p, c + e * t));
            }
            if (best > borderOff) borderOff = best, worst = p;
        }
    const DyntopoStats& st = d.sculptor.dynamicTopology().stats;
    std::printf("  group border: %zu -> %zu edges, farthest new border vertex from the old border %.2e at %.5f %.5f %.5f (dist to brush %.3f); "
                "ops: %zu splits %zu collapses %zu flips\n",
                oldBorder.size(), newBorder.size(), borderOff, worst.x, worst.y, worst.z, glm::distance(worst, top), st.splits, st.collapses, st.flips);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        if (m.positions[v] != worst) continue;
        std::set<uint32_t> g;
        for (uint32_t t : m.vertexTriangles(v)) g.insert(m.facegroups[t]);
        std::printf("    vertex %u (%s), groups %zu, ring:", v, v < before.vertexCount() ? "original" : "new", g.size());
        for (uint32_t u : m.neighbors(v)) std::printf(" %u(%s %.4f,%.4f,%.4f)", u, u < before.vertexCount() ? "old" : "new", m.positions[u].x, m.positions[u].y, m.positions[u].z);
        std::printf("\n");
    }
    // (A split lands on the curve the end normals describe, as dynamic topology splits, so a new border vertex bends off
    // the old chord by less than the chord's sag.)
    float borderX = 0.0f;
    for (const auto& [a, b] : newBorder) borderX = std::max({borderX, std::abs(a.x), std::abs(b.x)});
    // (The border zigzags off x = 0 where an icosphere triangle straddles the plane, so only the polyline test applies.)
    std::printf("  group border: farthest vertex off x = 0 %.2e\n", borderX);
    CHECK(newBorder.size() > oldBorder.size());
    CHECK(borderOff < 0.1f * stretchedBefore);
    // One undo step puts the pulled mesh back exactly.
    d.undo.push(std::move(r));
    CHECK(d.undo.canUndo());
    d.undo.undo(m, d.bvh);
    CHECK(m.triangles == before.triangles);
    CHECK(m.positions == before.positions);
    CHECK(m.facegroups == before.facegroups);
    CHECK(!d.undo.canUndo());
}

TEST(refresh_topology_invariant_mask_and_symmetry) {
    Document d = makeDoc(pulledSphere(5));
    Mesh& m = d.mesh;
    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].y > 0.05f ? 1.0f : 0.0f; // the +Y half locked
    d.bvh.build(m);
    const Mesh before = m;
    const float edge = typicalEdgeLength(makeIcosphere(5, 1.0f));
    Symmetry sym;
    sym.x = true;
    CHECK(d.sculptor.beginStroke(m, "Refresh Topology", -1));
    const glm::vec3 at(0.25f, -0.1f, 1.7f); // one side of the bump: the mirror image gets the same treatment
    int dabs = 0;
    bool changed = true;
    while (changed && dabs < 30) changed = d.sculptor.refreshTopologyDab(m, d.bvh, at, 0.35f, edge, sym), ++dabs;
    d.undo.push(d.sculptor.endStroke());
    CHECK(closedManifold(m));
    CHECK(m.triangleCount() > before.triangleCount());
    // Masked (fully) vertices never moved, and no triangle made of masked corners only was split.
    size_t maskedMoved = 0;
    for (uint32_t v = 0; v < before.vertexCount(); ++v)
        if (before.mask[v] >= 1.0f && m.positions[v] != before.positions[v]) ++maskedMoved;
    CHECK(maskedMoved == 0);
    // Mirrored: every vertex has its mirror image, and every triangle its mirror triangle (by position).
    using Key = std::tuple<int, int, int>;
    auto key = [](const glm::vec3& p) { return std::make_tuple(int(std::lround(p.x * 1e4f)), int(std::lround(p.y * 1e4f)), int(std::lround(p.z * 1e4f))); };
    // (Keys at 1e-4; a mirrored position that rounds across a cell boundary is looked up in the cells around it.)
    std::map<Key, uint32_t> cells;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) cells[key(m.positions[v])] = v;
    auto mirrorVertex = [&](const glm::vec3& p) -> int64_t {
        const glm::vec3 q(-p.x, p.y, p.z);
        const Key k = key(q);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    const auto it = cells.find({std::get<0>(k) + dx, std::get<1>(k) + dy, std::get<2>(k) + dz});
                    if (it != cells.end() && glm::distance(m.positions[it->second], q) < 2e-4f) return it->second;
                }
        return -1;
    };
    std::vector<int64_t> mirrorOf(m.vertexCount());
    size_t unmirrored = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) unmirrored += (mirrorOf[v] = mirrorVertex(m.positions[v])) < 0;
    std::set<std::set<uint32_t>> tris;
    for (const glm::uvec3& t : m.triangles) tris.insert({t.x, t.y, t.z});
    size_t unmirroredTris = 0;
    for (const glm::uvec3& t : m.triangles) {
        std::set<uint32_t> mirror;
        for (int c = 0; c < 3; ++c)
            if (mirrorOf[t[c]] >= 0) mirror.insert(uint32_t(mirrorOf[t[c]]));
        unmirroredTris += mirror.size() != 3 || !tris.count(mirror);
    }
    std::printf("  symmetry: %zu triangles, %zu unmirrored vertices, %zu unmirrored triangles, %zu unpaired ops, %d dabs\n", m.triangleCount(),
                unmirrored, unmirroredTris, d.sculptor.dynamicTopology().stats.unpaired, dabs);
    CHECK(unmirrored == 0);
    CHECK(unmirroredTris == 0);
    d.undo.undo(m, d.bvh);
    CHECK(m.triangles == before.triangles && m.positions == before.positions);
}
