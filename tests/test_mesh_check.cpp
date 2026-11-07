// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Merge by distance and Check Mesh / Fix (core/MeshCheck.h; foirgairwolf on Discord).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/FaceGroups.h"
#include "core/MeshCheck.h"
#include "core/Primitives.h"

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
