// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Merge by distance and Find Problems / Fix (core/MeshCheck.h; foirgairwolf on Discord).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/FaceGroups.h"
#include "core/MeshCheck.h"
#include "core/Primitives.h"

#include <random>

using namespace sf;

namespace {

size_t issue(const Mesh& m, MeshIssue i) { return checkMesh(m).count[int(i)]; }

// The mesh's triangles with every corner its own point (as an unwelded import), positions unchanged.
Mesh unwelded(const Mesh& src) {
    Mesh m;
    for (const auto& t : src.triangles) {
        const uint32_t base = uint32_t(m.positions.size());
        for (int k = 0; k < 3; ++k) m.positions.push_back(src.positions[t[k]]);
        m.triangles.push_back({base, base + 1, base + 2});
    }
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

// The surface of the filled cells of an n x n x n grid (x major), faces between filled and empty cells, points
// shared: cells touching only along an edge or at a corner make the non-manifold edges a messy remesh has.
Mesh voxels(const std::vector<uint8_t>& on, int n, float jitter = 0.0f, uint32_t seed = 1) {
    auto at = [&](int x, int y, int z) {
        return x >= 0 && y >= 0 && z >= 0 && x < n && y < n && z < n && on[size_t((x * n + y) * n + z)];
    };
    auto P = [&](int x, int y, int z) { return uint32_t((x * (n + 1) + y) * (n + 1) + z); };
    std::mt19937 rng(seed);
    Mesh m;
    for (int x = 0; x <= n; ++x)
        for (int y = 0; y <= n; ++y)
            for (int z = 0; z <= n; ++z) m.positions.push_back(glm::vec3(x, y, z) + glm::vec3(jitter * float(rng() % 100) / 100.0f));
    for (int x = 0; x < n; ++x)
        for (int y = 0; y < n; ++y)
            for (int z = 0; z < n; ++z) {
                if (!at(x, y, z)) continue;
                for (int ax = 0; ax < 3; ++ax)
                    for (int side = 0; side < 2; ++side) {
                        int d[3] = {0, 0, 0}, b[3] = {x, y, z};
                        d[ax] = side ? 1 : -1;
                        if (at(x + d[0], y + d[1], z + d[2])) continue;
                        b[ax] += side;
                        const int u = (ax + 1) % 3, v = (ax + 2) % 3;
                        auto corner = [&](int du, int dv) {
                            int c[3] = {b[0], b[1], b[2]};
                            c[u] += du, c[v] += dv;
                            return P(c[0], c[1], c[2]);
                        };
                        const uint32_t q0 = corner(0, 0), q1 = corner(1, 0), q2 = corner(1, 1), q3 = corner(0, 1);
                        if (side) m.triangles.insert(m.triangles.end(), {{q0, q1, q2}, {q0, q2, q3}});
                        else m.triangles.insert(m.triangles.end(), {{q0, q2, q1}, {q0, q3, q2}});
                    }
            }
    m.rebuildTopology();
    Mesh out;
    size_t n0 = 0;
    return fixMesh(m, MeshIssue::Isolated, out, n0) ? out : m;
}

// What's left besides holes.
size_t notHoles(const MeshReport& r) { return r.total() - r.count[int(MeshIssue::Holes)]; }

Mesh fixed(const Mesh& m, MeshIssue i, size_t* n = nullptr) {
    Mesh out;
    size_t count = 0;
    CHECK(fixMesh(m, i, out, count));
    if (n) *n = count;
    return out;
}

} // namespace

TEST(mesh_check_clean_sphere) {
    const Mesh sphere = makeIcosphere(3);
    const MeshReport r = checkMesh(sphere);
    CHECK(r.total() == 0);
    CHECK(r.summary() == "No problems found");
    Mesh out;
    size_t n = 0;
    for (int i = 0; i < kMeshIssueCount; ++i) CHECK(!fixMesh(sphere, MeshIssue(i), out, n)); // nothing to fix
}

TEST(weld_points_by_distance) {
    const Mesh sphere = makeIcosphere(2);
    const Mesh loose = unwelded(sphere);
    CHECK(loose.vertexCount() == sphere.triangleCount() * 3);
    size_t merged = 0;
    Mesh welded = weldByDistance(loose, 0.0f, &merged);
    CHECK(welded.vertexCount() == sphere.vertexCount());
    CHECK(merged == loose.vertexCount() - sphere.vertexCount());
    CHECK(welded.triangleCount() == sphere.triangleCount());
    CHECK(checkMesh(welded).total() == 0);
    // The count only (preview) matches.
    size_t n = 0;
    weldTargets(loose, 0.0f, &n);
    CHECK(n == merged);
    // Points slightly apart merge only within the distance.
    Mesh nudged = loose;
    for (size_t v = 0; v < nudged.vertexCount(); v += 2) nudged.positions[v] += glm::vec3(1e-4f, 0, 0);
    weldTargets(nudged, 0.0f, &n);
    CHECK(n < merged);
    weldTargets(nudged, 2e-4f, &n);
    CHECK(n == merged);
    // A distance as big as the mesh collapses it to nothing that stays a triangle.
    welded = weldByDistance(sphere, 10.0f, &merged);
    CHECK(merged == sphere.vertexCount() - 1 && welded.triangleCount() == 0);

    // Masked only (foirgairwolf on Discord): only masked points merge, and only with each other.
    Mesh masked = loose;
    std::vector<float>& mask = masked.ensureMask();
    for (uint32_t v = 0; v < masked.vertexCount(); ++v) mask[v] = masked.positions[v].y > 0.3f ? 1.0f : 0.0f;
    size_t all = 0;
    weldTargets(loose, 0.0f, &all);
    const std::vector<uint32_t> target = weldTargets(masked, 0.0f, &n, true);
    CHECK(n > 0 && n < all);
    for (uint32_t v = 0; v < masked.vertexCount(); ++v)
        if (target[v] != v) CHECK(mask[v] >= 0.5f && mask[target[v]] >= 0.5f);
    welded = weldByDistance(masked, 0.0f, &merged, true);
    CHECK(merged == n && welded.vertexCount() == masked.vertexCount() - n);
    // No mask at all: nothing merges; everything masked: the same as without the option.
    weldTargets(loose, 0.0f, &n, true);
    CHECK(n == 0);
    std::fill(mask.begin(), mask.end(), 1.0f);
    weldTargets(masked, 0.0f, &n, true);
    CHECK(n == all);
}

TEST(mesh_check_holes_and_isolated) {
    Mesh plane = makePlane(4);
    CHECK(issue(plane, MeshIssue::Holes) == 1);
    CHECK(issue(plane, MeshIssue::Flipped) == 0); // an open piece: no inside to be out
    plane.positions.push_back(glm::vec3(5, 5, 5));
    plane.rebuildTopology();
    CHECK(issue(plane, MeshIssue::Isolated) == 1);
    size_t n = 0;
    Mesh m = fixed(plane, MeshIssue::Isolated, &n);
    CHECK(n == 1 && m.vertexCount() == plane.vertexCount() - 1 && issue(m, MeshIssue::Isolated) == 0);
    m = fixed(m, MeshIssue::Holes, &n);
    CHECK(n == 1 && issue(m, MeshIssue::Holes) == 0);
}

TEST(mesh_check_duplicate_degenerate_zero_area) {
    Mesh m = makeIcosphere(2);
    const size_t nt = m.triangleCount();
    m.triangles.push_back(m.triangles[0]);                                          // duplicate
    m.triangles.push_back({m.triangles[1].x, m.triangles[1].z, m.triangles[1].y}); // duplicate, other winding
    m.triangles.push_back({m.triangles[2].x, m.triangles[2].x, m.triangles[2].y}); // degenerate
    m.rebuildTopology();
    MeshReport r = checkMesh(m);
    CHECK(r.count[int(MeshIssue::Duplicate)] == 2);
    CHECK(r.count[int(MeshIssue::Degenerate)] == 1);
    CHECK(r.count[int(MeshIssue::NonManifold)] > 0); // the duplicates put a third triangle on their edges
    size_t n = 0;
    m = fixed(m, MeshIssue::Degenerate, &n);
    CHECK(n == 1 && m.triangleCount() == nt + 2);
    m = fixed(m, MeshIssue::Duplicate, &n);
    CHECK(n == 2 && m.triangleCount() == nt);
    CHECK(checkMesh(m).total() == 0);

    // Zero area: a sliver B M A (M on the middle of A B) between two triangles above it and one below.
    Mesh sliver;
    sliver.positions = {{0, 0, 0}, {2, 0, 0}, {1, 0, 0}, {1, 1, 0}, {1, -1, 0}}; // A B M C E
    sliver.triangles = {{0, 2, 3}, {2, 1, 3}, {1, 2, 0}, {0, 4, 1}};
    sliver.rebuildTopology();
    CHECK(issue(sliver, MeshIssue::ZeroArea) == 1 && issue(sliver, MeshIssue::Flipped) == 0);
    m = fixed(sliver, MeshIssue::ZeroArea, &n);
    CHECK(n == 1 && m.triangleCount() == 2); // B M collapsed: the sliver and M B C go
    CHECK(checkMesh(m).count[int(MeshIssue::ZeroArea)] == 0 && issue(m, MeshIssue::NonManifold) == 0);
}

TEST(mesh_check_flipped) {
    Mesh m = makeIcosphere(2);
    for (uint32_t t : {3u, 10u, 11u}) std::swap(m.triangles[t].y, m.triangles[t].z);
    m.rebuildTopology();
    size_t n = 0;
    CHECK(issue(m, MeshIssue::Flipped) == 3);
    m = fixed(m, MeshIssue::Flipped, &n);
    CHECK(n == 3 && checkMesh(m).total() == 0);
    // Inside out as a whole: every triangle flips.
    for (auto& t : m.triangles) std::swap(t.y, t.z);
    m.rebuildTopology();
    CHECK(issue(m, MeshIssue::Flipped) == m.triangleCount());
    m = fixed(m, MeshIssue::Flipped, &n);
    CHECK(checkMesh(m).total() == 0);
    // UVs turn with their triangle.
    Mesh uv = makeIcosphere(1);
    uv.uvs.resize(uv.triangleCount() * 3);
    for (size_t c = 0; c < uv.uvs.size(); ++c) uv.uvs[c] = glm::vec2(float(c), 0.0f);
    std::swap(uv.triangles[0].y, uv.triangles[0].z);
    uv.rebuildTopology();
    m = fixed(uv, MeshIssue::Flipped, &n);
    CHECK(n == 1 && m.uvs[1].x == 2.0f && m.uvs[2].x == 1.0f);
}

TEST(mesh_check_non_manifold) {
    // A fin: one extra triangle on an edge of a closed sphere.
    Mesh m = makeIcosphere(2);
    const glm::uvec3 t = m.triangles[0];
    m.positions.push_back(m.positions[t.x] * 1.5f);
    m.triangles.push_back({t.x, t.y, uint32_t(m.positions.size() - 1)});
    m.rebuildTopology();
    CHECK(issue(m, MeshIssue::NonManifold) == 1);
    size_t n = 0;
    const Mesh out = fixed(m, MeshIssue::NonManifold, &n);
    CHECK(n == 1 && issue(out, MeshIssue::NonManifold) == 0);
    CHECK(out.triangleCount() == m.triangleCount()); // nothing deleted: the fin comes loose
    CHECK(out.vertexCount() == m.vertexCount() + 2); // its own copies of the edge's two points
    CHECK(issue(out, MeshIssue::Holes) == 1);        // the loose fin is an open piece
}

TEST(mesh_check_commands_undo) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    const size_t nv = doc.mesh.vertexCount(), nt = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "mesh_check").message.find("No problems found") == 0);
    CHECK(!reg.execute(doc, "mesh_fix", {{"issue", std::string("all")}}).ok);
    CHECK(!reg.execute(doc, "mesh_fix", {{"issue", std::string("bogus")}}).ok);
    doc.replaceMeshUndoable(unwelded(doc.mesh), "Unweld");
    CHECK(reg.execute(doc, "weld_points", {{"distance", 0.0}, {"preview", true}}).ok);
    CHECK(doc.mesh.vertexCount() == nt * 3); // the preview changes nothing
    CHECK(reg.execute(doc, "weld_points", {{"distance", 0.0}}).ok);
    CHECK(doc.mesh.vertexCount() == nv);
    CHECK(reg.execute(doc, "undo").ok && doc.mesh.vertexCount() == nt * 3);
    CHECK(!reg.execute(doc, "weld_points", {{"distance", -1.0}}).ok);
    // Fix all: one undo step for every fix, holes left open.
    CHECK(reg.execute(doc, "redo").ok);
    Mesh broken = doc.mesh;
    broken.triangles.push_back(broken.triangles[0]);
    std::swap(broken.triangles[5].y, broken.triangles[5].z);
    broken.positions.push_back(glm::vec3(3, 3, 3));
    broken.rebuildTopology();
    doc.replaceMeshUndoable(std::move(broken), "Break");
    CHECK(reg.execute(doc, "mesh_fix", {{"issue", std::string("all")}}).ok);
    CHECK(checkMesh(doc.mesh).total() == 0);
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(checkMesh(doc.mesh).total() > 0);
}

TEST(mesh_check_touching_solids_come_apart_closed) {
    // Two cells touching along one edge (4 triangles on it): each stays a closed box, no hole opened.
    Mesh two = voxels({1, 0, 0, 1, 0, 0, 0, 0}, 2);
    CHECK(issue(two, MeshIssue::NonManifold) == 1 && issue(two, MeshIssue::Holes) == 0);
    Mesh out;
    MeshReport did, left;
    CHECK(fixMeshAll(two, out, did, &left));
    CHECK(did.count[int(MeshIssue::NonManifold)] == 1 && left.total() == 0 && checkMesh(out).total() == 0);
    // An L of three cells and one touching it diagonally: three such edges meeting at points.
    CHECK(fixMeshAll(voxels({1, 1, 1, 0, 1, 0, 0, 1}, 2), out, did, &left) && left.total() == 0);
    // A solid pinched to an edge it is joined round at both ends (a messy remesh, foirgairwolf's report): the old
    // split left non-manifold edges and opened holes; now no non-manifold edge is left.
    std::vector<uint8_t> grid(6 * 6 * 6);
    std::mt19937 rng(19);
    for (auto& c : grid) c = rng() % 1000 < 750;
    const Mesh messy = voxels(grid, 6, 0.1f);
    CHECK(issue(messy, MeshIssue::NonManifold) > 0);
    CHECK(fixMeshAll(messy, out, did, &left));
    CHECK(notHoles(left) == 0 && notHoles(checkMesh(out)) == 0);
}

TEST(mesh_check_fix_all_reaches_a_fixed_point) {
    // Messy meshes (random grids, welds that fold a sphere, broken spheres): after Fix all only holes are left, and
    // the Holes Fix never makes another problem or leaves more holes than it found.
    for (uint32_t seed = 0; seed < 90; ++seed) {
        std::mt19937 rng(seed);
        Mesh m;
        if (seed % 3 == 0) {
            const int n = 3 + int(seed % 4);
            std::vector<uint8_t> grid(size_t(n * n * n));
            for (auto& c : grid) c = rng() % 100 < 35 + (seed % 5) * 10;
            m = voxels(grid, n, 0.1f, seed);
        } else if (seed % 3 == 1) {
            m = weldByDistance(makeIcosphere(3), 0.05f + float(seed % 7) * 0.03f);
        } else {
            m = makeIcosphere(2);
            for (int k = 0; k < 12; ++k) {
                const uint32_t t = uint32_t(rng() % m.triangles.size());
                const glm::uvec3 tri = m.triangles[t];
                switch (rng() % 6) {
                case 0: m.triangles.push_back(tri); break;
                case 1: std::swap(m.triangles[t].y, m.triangles[t].z); break;
                case 2:
                    m.positions.push_back(m.positions[tri.x] * 1.4f);
                    m.triangles.push_back({tri.x, tri.y, uint32_t(m.positions.size() - 1)});
                    break;
                case 3: m.positions[tri.x] = 0.5f * (m.positions[tri.y] + m.positions[tri.z]); break;
                case 4: m.triangles[t].x = tri.y; break;
                default:
                    for (auto& q : m.triangles)
                        for (int c = 0; c < 3; ++c)
                            if (q[c] == tri.x) q[c] = tri.y;
                }
            }
            m.rebuildTopology();
        }
        if (m.triangles.empty()) continue;
        Mesh out;
        MeshReport did, left;
        const Mesh& all = fixMeshAll(m, out, did, &left) ? out : m;
        const MeshReport after = checkMesh(all);
        CHECK(notHoles(after) == 0);
        CHECK(after.total() == left.total());
        Mesh capped;
        size_t n = 0;
        if (fixMesh(all, MeshIssue::Holes, capped, n)) {
            const MeshReport r = checkMesh(capped);
            CHECK(notHoles(r) == 0 && r.count[int(MeshIssue::Holes)] < after.count[int(MeshIssue::Holes)]);
        }
    }
}

TEST(mesh_check_hollow_and_nested_pieces) {
    // A hollow: an outer sphere and an inner one wound inward (its wall faces into the hollow). Not flipped.
    Mesh outer = makeIcosphere(2, 1.0f), inner = makeIcosphere(2, 0.5f);
    Mesh hollow = outer;
    const uint32_t base = uint32_t(hollow.positions.size());
    hollow.positions.insert(hollow.positions.end(), inner.positions.begin(), inner.positions.end());
    for (const auto& t : inner.triangles) hollow.triangles.push_back({t.x + base, t.z + base, t.y + base});
    hollow.rebuildTopology();
    CHECK(checkMesh(hollow).total() == 0);
    // An eye in a head (wound outward) is fine too.
    Mesh eye = outer;
    eye.positions.insert(eye.positions.end(), inner.positions.begin(), inner.positions.end());
    for (const auto& t : inner.triangles) eye.triangles.push_back(t + glm::uvec3(base));
    eye.rebuildTopology();
    CHECK(checkMesh(eye).total() == 0);
    // The outer one inside out is still turned the right way out.
    for (size_t t = 0; t < outer.triangleCount(); ++t) std::swap(eye.triangles[t].y, eye.triangles[t].z);
    eye.rebuildTopology();
    CHECK(issue(eye, MeshIssue::Flipped) == outer.triangleCount());
    size_t n = 0;
    CHECK(checkMesh(fixed(eye, MeshIssue::Flipped, &n)).total() == 0 && n == outer.triangleCount());
}

TEST(mesh_check_fix_says_what_is_left) {
    Document doc;
    CommandRegistry reg;
    // A fin on a sphere: Fix all loosens it, which opens it; the hole is left open and the status says so.
    Mesh m = makeIcosphere(2);
    const glm::uvec3 t = m.triangles[0];
    m.positions.push_back(m.positions[t.x] * 1.5f);
    m.triangles.push_back({t.x, t.y, uint32_t(m.positions.size() - 1)});
    m.rebuildTopology();
    doc.replaceMeshUndoable(std::move(m), "Fin");
    CommandResult r = reg.execute(doc, "mesh_fix", {{"issue", std::string("all")}});
    CHECK(r.ok && r.message.find("Fixed 1 non-manifold edge") == 0);
    CHECK(r.message.find("Left: 1 hole left open (Fix all doesn't cap holes") != std::string::npos);
    // The loose fin's edge can't be capped (a cap would lie on the fin): nothing changes, and it says why.
    r = reg.execute(doc, "mesh_fix", {{"issue", std::string("holes")}});
    CHECK(!r.ok && r.message.find("Nothing fixed: 1 hole couldn't be capped (the edge of a loose sheet or fin") == 0);
    CHECK(issue(doc.mesh, MeshIssue::Holes) == 1);
    // unfixedSummary names each kind with its reason.
    MeshReport left;
    left.count[int(MeshIssue::Flipped)] = 2;
    CHECK(unfixedSummary(left, true) ==
          "2 flipped triangles couldn't be turned (a one-sided, twisted piece can't be wound one way everywhere)");
    CHECK(unfixedSummary(MeshReport{}, true).empty());
}
