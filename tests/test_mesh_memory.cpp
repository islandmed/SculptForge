// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Memory per triangle (v0.6): levels made by Divide keep no triangles or child maps (generated topology), the
// working mesh of such a level works out its adjacency from the level's topology (implicit adjacency) and gets
// exactly the lists the stored adjacency would give, and the stack keeps positions or details, not both.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"

#include <filesystem>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

CommandResult run(Document& d, const std::string& name, const Args& a = {}) {
    CommandResult r = registry().execute(d, name, a);
    if (!r.ok) std::printf("  (%s: %s)\n", name.c_str(), r.message.c_str());
    return r;
}

// The same mesh with stored (CSR) adjacency.
Mesh explicitCopy(const Mesh& m) {
    Mesh e = m;
    e.multires.reset();
    e.rebuildTopology();
    return e;
}

// Every vertex's ring and triangle list match the stored adjacency's, element for element.
bool sameAdjacency(const Mesh& a, const Mesh& b) {
    if (a.vertexCount() != b.vertexCount()) return false;
    for (uint32_t v = 0; v < a.vertexCount(); ++v) {
        const IndexRing na = a.neighbors(v), nb = b.neighbors(v), ta = a.vertexTriangles(v), tb = b.vertexTriangles(v);
        if (!std::equal(na.begin(), na.end(), nb.begin(), nb.end()) || !std::equal(ta.begin(), ta.end(), tb.begin(), tb.end())) {
            std::printf("  vertex %u differs (%zu/%zu neighbours, %zu/%zu triangles)\n", v, na.size(), nb.size(), ta.size(), tb.size());
            return false;
        }
    }
    return true;
}

Document divided(Mesh base, int levels) {
    Document d = makeDoc(std::move(base));
    for (int i = 0; i < levels; ++i) run(d, "subdiv_divide");
    return d;
}

} // namespace

TEST(implicit_adjacency_matches_the_stored_lists) {
    // Catmull-Clark on quads; on quads mixed with triangles; with a boundary (an open cube); Loop on triangles.
    Mesh mixed = makeCube(3);
    for (uint32_t t = 0; t < 12; t += 2) mixed.quadPartners[t] = mixed.quadPartners[t + 1] = Mesh::kNoQuad;
    mixed.rebuildTopology();
    Mesh open = makeCube(3);
    {
        std::vector<glm::uvec3> kept;
        std::vector<uint32_t> quads;
        for (uint32_t t = 0; t < open.triangleCount(); ++t) {
            const glm::vec3 c = (open.positions[open.triangles[t].x] + open.positions[open.triangles[t].y] + open.positions[open.triangles[t].z]) / 3.0f;
            if (c.y > 0.99f) continue; // no top face
            kept.push_back(open.triangles[t]);
        }
        Mesh m;
        m.positions = open.positions;
        m.triangles = kept;
        for (uint32_t t = 0; t + 1 < kept.size(); t += 2) m.pairQuad(t, t + 1); // makeCube writes quads as pairs
        m.rebuildTopology();
        m.recomputeAllNormals();
        open = std::move(m);
    }
    struct Case {
        const char* name;
        Mesh base;
    };
    std::vector<Case> cases;
    cases.push_back({"quads", makeCube(2)});
    cases.push_back({"quads and triangles", mixed});
    cases.push_back({"open", open});
    cases.push_back({"triangles (Loop)", makeIcosphere(1)});
    for (const Case& c : cases) {
        Document d = divided(c.base, 2);
        std::printf("  %s: %zu triangles, implicit %d\n", c.name, d.mesh.triangleCount(), int(d.mesh.implicitAdjacency()));
        CHECK(subdivLevelCount(d.mesh) == 3);
        CHECK(d.mesh.implicitAdjacency());
        CHECK(sameAdjacency(d.mesh, explicitCopy(d.mesh)));
        // A lower level (from the level cache or rebuilt) is a generated level too, except the base.
        CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
        CHECK(d.mesh.implicitAdjacency());
        CHECK(sameAdjacency(d.mesh, explicitCopy(d.mesh)));
        CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
        CHECK(!d.mesh.implicitAdjacency());
    }
}

TEST(implicit_adjacency_falls_back_to_stored_lists) {
    // A pole with more triangles than an IndexRing holds: stored lists, and still correct.
    Mesh fan;
    const int n = 40;
    fan.positions.push_back(glm::vec3(0, 1, 0));
    fan.positions.push_back(glm::vec3(0, -1, 0));
    for (int i = 0; i < n; ++i) {
        const float a = float(i) * 6.2831853f / float(n);
        fan.positions.push_back(glm::vec3(std::cos(a), 0, std::sin(a)));
    }
    for (int i = 0; i < n; ++i) {
        const uint32_t a = 2 + uint32_t(i), b = 2 + uint32_t((i + 1) % n);
        fan.triangles.push_back({0, b, a});
        fan.triangles.push_back({1, a, b});
    }
    fan.rebuildTopology();
    fan.recomputeAllNormals();
    Document d = divided(fan, 1);
    CHECK(subdivLevelCount(d.mesh) == 2);
    CHECK(!d.mesh.implicitAdjacency());
    CHECK(d.mesh.neighbors(0).size() == size_t(n));
    // Changed topology (a triangle flipped, same counts): the stack is dropped or the lists are stored again.
    Document e = divided(makeCube(2), 1);
    CHECK(e.mesh.implicitAdjacency());
    std::swap(e.mesh.triangles[0].y, e.mesh.triangles[0].z);
    e.mesh.rebuildTopology();
    CHECK(!e.mesh.implicitAdjacency());
    CHECK(sameAdjacency(e.mesh, explicitCopy(e.mesh)));
}

TEST(implicit_adjacency_sculpts_like_stored_lists) {
    // The same strokes on a mesh with implicit and with stored adjacency give bit-identical shapes.
    Document a = divided(makeCube(2), 3);
    Document b = makeDoc(explicitCopy(a.mesh));
    CHECK(a.mesh.implicitAdjacency() && !b.mesh.implicitAdjacency());
    for (Document* d : {&a, &b}) {
        CHECK(run(*d, "stroke", {{"points", "-0.3,0.2,2 0.3,0.25,2"}, {"brush", "Standard"}, {"radius", 0.3}}).ok);
        CHECK(run(*d, "stroke", {{"points", "0.1,-0.4,2 0.2,0.4,2"}, {"brush", "Smooth"}, {"radius", 0.3}}).ok);
    }
    CHECK(a.mesh.positions == b.mesh.positions);
    CHECK(a.mesh.normals == b.mesh.normals);
}

TEST(divided_levels_store_little) {
    // A 400k sphere with levels (as the benchmark builds): the stack and the working mesh stay small.
    Mesh m = makeBenchSphere(400000);
    const double nt = double(m.triangleCount());
    const double stack = double(subdivBytes(m)) / nt, mesh = double(m.memoryBytes()) / nt;
    std::printf("  stack %.1f B/triangle, mesh %.1f B/triangle\n", stack, mesh);
    CHECK(m.implicitAdjacency());
    CHECK(stack < 32.0); // was 74
    CHECK(mesh < 40.0);  // was 62
    // Saved and opened again, the levels take as little.
    {
        Document d = makeDoc(m);
        const std::string path = (std::filesystem::temp_directory_path() / "sf_mesh_memory_bench.sfproj").string();
        std::string err;
        CHECK(saveProject(path, d, err));
        Document e;
        CHECK(loadProject(path, e, err));
        CHECK(subdivBytes(e.mesh) == subdivBytes(d.mesh));
        CHECK(e.mesh.memoryBytes() == d.mesh.memoryBytes());
        CHECK(e.mesh.implicitAdjacency());
        std::filesystem::remove(path);
    }
    // Every generated level stores no triangles; the top keeps its positions but no detail.
    for (int i = 1; i < subdivLevelCount(m); ++i) {
        CHECK(subdivLevel(m, i).topo->generated);
        CHECK(subdivLevelData(m, i).triangles.empty());
        CHECK(!subdivLevel(m, i).detail);
    }
}

TEST(divided_levels_project_round_trip) {
    // Format 6 keeps no triangles or child maps for generated levels; they come back generated, with implicit
    // adjacency, and the same shapes at every level.
    Document d = divided(makeCube(3), 3);
    CHECK(run(d, "stroke", {{"points", "-0.3,0.2,2 0.3,0.25,2"}, {"brush", "Standard"}, {"radius", 0.3}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(run(d, "stroke", {{"points", "0,0,2"}, {"brush", "Inflate"}, {"radius", 0.5}}).ok);
    const std::string path = (std::filesystem::temp_directory_path() / "sf_mesh_memory.sfproj").string();
    std::string err;
    CHECK(run(d, "save_project", {{"path", path}}).ok);
    Document e;
    CHECK(loadProject(path, e, err));
    CHECK(subdivLevelText(e.mesh) == "Level 2/4");
    CHECK(e.mesh.implicitAdjacency());
    CHECK(e.mesh.positions == d.mesh.positions);
    for (int level : {4, 1, 3}) {
        CHECK(run(d, "subdiv_level", {{"level", double(level)}}).ok);
        CHECK(run(e, "subdiv_level", {{"level", double(level)}}).ok);
        CHECK(e.mesh.positions == d.mesh.positions);
        CHECK(e.mesh.triangles == d.mesh.triangles);
        CHECK(e.mesh.quadPartners == d.mesh.quadPartners);
    }
    for (int i = 1; i < subdivLevelCount(e.mesh); ++i) CHECK(subdivLevel(e.mesh, i).topo->generated);
    std::filesystem::remove(path);
}

TEST(cubes_are_closed_at_every_resolution) {
    // makeCube used to leave a few vertices along the sides' shared edges unwelded at some resolutions (13, 26:
    // the 33M benchmark sphere had cracks); a closed quad cube has 6 n^2 + 2 vertices.
    for (int n = 1; n <= 48; ++n) {
        const Mesh m = makeCube(n);
        if (m.vertexCount() != size_t(6 * n * n + 2)) std::printf("  cube %d: %zu vertices\n", n, m.vertexCount());
        CHECK(m.vertexCount() == size_t(6 * n * n + 2));
    }
}

// Format 5 projects (tests/data/levels_v5_*.sfproj, saved by 0.5: an icosphere (Loop) and a plane (Catmull-Clark)
// divided three times, a stroke and a cavity mask at the top, then an Inflate stroke at Level 2, saved there with
// Level 3 and 4 waiting to be re-evaluated; and the same saved at Level 4): they open, their levels become
// generated ones, and they evaluate as 0.5 did, before and after saving them again in the current format.
TEST(format5_projects_open_as_generated_levels) {
    for (const char* name : {"sphere", "plane"}) {
        Document ref, d;
        std::string err;
        const std::string dir = SF_TEST_DATA_DIR;
        CHECK(loadProject(dir + "/levels_v5_" + name + "_top.sfproj", ref, err));
        CHECK(loadProject(dir + "/levels_v5_" + name + ".sfproj", d, err));
        CHECK(subdivLevelText(ref.mesh) == "Level 4/4" && subdivLevelText(d.mesh) == "Level 2/4");
        CHECK(d.mesh.implicitAdjacency() && ref.mesh.implicitAdjacency());
        for (int i = 1; i < subdivLevelCount(d.mesh); ++i) {
            CHECK(subdivLevel(d.mesh, i).topo->generated);
            CHECK(subdivLevelData(d.mesh, i).triangles.empty());
        }
        CHECK(run(d, "subdiv_level", {{"level", 4.0}}).ok);
        float diff = 0.0f;
        for (size_t v = 0; v < d.mesh.vertexCount() && v < ref.mesh.vertexCount(); ++v)
            diff = std::max(diff, glm::length(d.mesh.positions[v] - ref.mesh.positions[v]));
        std::printf("  %s: %zu triangles, %zu quads, top level %.2g from 0.5's\n", name, d.mesh.triangleCount(), d.mesh.quadCount(), diff);
        CHECK(d.mesh.vertexCount() == ref.mesh.vertexCount() && d.mesh.triangles == ref.mesh.triangles);
        CHECK(d.mesh.quadPartners == ref.mesh.quadPartners);
        CHECK(diff < 1e-5f);
        CHECK(d.mesh.mask == ref.mesh.mask && d.mesh.hasMask());
        // Saved again (format 6) and opened: the same.
        const std::string path = (std::filesystem::temp_directory_path() / (std::string("sf_v6_") + name + ".sfproj")).string();
        CHECK(saveProject(path, d, err));
        Document e;
        CHECK(loadProject(path, e, err));
        CHECK(e.mesh.positions == d.mesh.positions && e.mesh.mask == d.mesh.mask);
        CHECK(run(e, "subdiv_level", {{"level", 1.0}}).ok);
        CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
        CHECK(e.mesh.positions == d.mesh.positions);
        std::filesystem::remove(path);
    }
}
