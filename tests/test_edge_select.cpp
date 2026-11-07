// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edge selection on the base level (#108, core/EdgeSelect.h): polygon edges without quad diagonals, picking, loops
// across quads and along borders, select by angle (and within the selection), grow / shrink, the edge_select
// command and its base-level rule, and the cost on a 1M-vertex base. Creases (slice 2): set / clear on the selection,
// both sides of an edge, undo / redo, saved in projects, kept on the base level while stepping levels.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/EdgeSelect.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"

#include <chrono>
#include <cstdio>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

// The edge between vertices a and b, or kNoEdge.
uint32_t edgeBetween(const EdgeTopology& topo, uint32_t a, uint32_t b) {
    for (uint32_t i = topo.vertexOffsets[a]; i < topo.vertexOffsets[a + 1]; ++i) {
        const glm::uvec2 e = topo.ends[topo.vertexEdges[i]];
        if ((e.x == a && e.y == b) || (e.x == b && e.y == a)) return topo.vertexEdges[i];
    }
    return kNoEdge;
}

// An inner edge of a plane: one whose ends both have four edges.
uint32_t innerEdge(const EdgeTopology& topo) {
    auto valence = [&](uint32_t v) { return topo.vertexOffsets[v + 1] - topo.vertexOffsets[v]; };
    for (uint32_t e = 0; e < topo.edgeCount(); ++e)
        if (!topo.open(e) && valence(topo.ends[e].x) == 4 && valence(topo.ends[e].y) == 4) return e;
    return kNoEdge;
}

} // namespace

TEST(edge_select_polygon_edges_skip_quad_diagonals) {
    for (int n : {1, 3, 8}) {
        const Mesh plane = makePlane(n);
        const auto topo = buildEdgeTopology(plane);
        CHECK(topo->edgeCount() == size_t(2 * n * (n + 1))); // a grid's edges, no diagonals
        size_t open = 0;
        for (uint32_t e = 0; e < topo->edgeCount(); ++e) open += topo->open(e);
        CHECK(open == size_t(4 * n));
        const Mesh cube = makeCube(n);
        CHECK(buildEdgeTopology(cube)->edgeCount() == size_t(12 * n * n)); // V + F - 2 with F = 6 n^2 quads
    }
    // Triangles keep all three edges: an icosahedron's 30.
    CHECK(buildEdgeTopology(makeIcosphere(0))->edgeCount() == 30);
}

TEST(edge_select_nearest_edge_of_the_clicked_polygon) {
    const Mesh plane = makePlane(4);
    const auto topo = buildEdgeTopology(plane);
    for (uint32_t t = 0; t < plane.triangleCount(); ++t) {
        const glm::uvec3 tri = plane.triangles[t];
        // Next to each corner's edge, a little inside the triangle: that edge (never the quad's diagonal).
        const glm::vec3 centre = (plane.positions[tri.x] + plane.positions[tri.y] + plane.positions[tri.z]) / 3.0f;
        for (int k = 0; k < 3; ++k) {
            const uint32_t c = t * 3 + uint32_t(k);
            const glm::vec3 mid = (plane.positions[tri[k]] + plane.positions[tri[(k + 1) % 3]]) * 0.5f;
            const uint32_t e = edgeNearest(plane, *topo, t, mid + (centre - mid) * 0.05f);
            CHECK(e != kNoEdge); // (beside the diagonal: one of the quad's sides)
            if (topo->cornerEdge[c] != kNoEdge) CHECK(e == topo->cornerEdge[c]);
        }
    }
}

TEST(edge_select_angle) {
    // A cube's hard edges are its 12 sides, n edges each; every other edge is flat.
    const Mesh cube = makeCube(4);
    const auto topo = buildEdgeTopology(cube);
    size_t sharp = 0, flat = 0;
    for (uint32_t e = 0; e < topo->edgeCount(); ++e) {
        const float a = edgeAngle(cube, *topo, e);
        if (std::abs(a - 90.0f) < 0.01f) ++sharp;
        else if (a < 0.01f) ++flat;
    }
    CHECK(sharp == 48);
    CHECK(sharp + flat == topo->edgeCount());
    EdgeSelection sel;
    edgeSelectByAngle(cube, sel, 30.0f, false);
    CHECK(sel.count == 48);
    edgeSelectByAngle(cube, sel, 95.0f, false);
    CHECK(sel.count == 0);
    // Within the selection: a hard side's loop plus one flat edge keeps just the hard side.
    edgeSelectByAngle(cube, sel, 30.0f, false);
    uint32_t hard = kNoEdge;
    for (uint32_t e = 0; e < topo->edgeCount() && hard == kNoEdge; ++e)
        if (sel.selected[e]) hard = e;
    CHECK(hard != kNoEdge);
    const std::vector<uint32_t> side = edgeLoop(cube, *sel.topo, hard);
    CHECK(side.size() == 4); // corner to corner (the cube's corners have three edges)
    edgeSelectAll(sel, false);
    for (uint32_t e : side) sel.set(e, true);
    for (uint32_t e = 0; e < topo->edgeCount(); ++e)
        if (edgeAngle(cube, *sel.topo, e) < 1.0f) {
            sel.set(e, true);
            break;
        }
    CHECK(sel.count == 5);
    edgeSelectByAngle(cube, sel, 30.0f, true);
    CHECK(sel.count == 4);
    for (uint32_t e : side) CHECK(sel.selected[e] != 0);
    // Open edges have no angle.
    const Mesh plane = makePlane(3);
    const auto pt = buildEdgeTopology(plane);
    for (uint32_t e = 0; e < pt->edgeCount(); ++e) CHECK((edgeAngle(plane, *pt, e) < 0.0f) == pt->open(e));
}

TEST(edge_select_loops) {
    // A plane: an inner edge's loop runs border to border; a border edge's loop is the whole border.
    const Mesh plane = makePlane(6);
    const auto topo = buildEdgeTopology(plane);
    const uint32_t inner = innerEdge(*topo);
    CHECK(inner != kNoEdge);
    const std::vector<uint32_t> line = edgeLoop(plane, *topo, inner);
    CHECK(line.size() == 6);
    for (size_t i = 0; i + 1 < line.size(); ++i) { // consecutive edges share a vertex: ordered along the loop
        const glm::uvec2 a = topo->ends[line[i]], b = topo->ends[line[i + 1]];
        CHECK(a.x == b.x || a.x == b.y || a.y == b.x || a.y == b.y);
    }
    uint32_t border = kNoEdge;
    for (uint32_t e = 0; e < topo->edgeCount() && border == kNoEdge; ++e)
        if (topo->open(e)) border = e;
    CHECK(edgeLoop(plane, *topo, border).size() == 24);
    // A cube: a loop through the middle of a face goes round the cube (4 sides of n).
    const Mesh cube = makeCube(4);
    const auto ct = buildEdgeTopology(cube);
    size_t closed = 0;
    for (uint32_t e = 0; e < ct->edgeCount(); ++e)
        if (edgeAngle(cube, *ct, e) < 1.0f) {
            const size_t n = edgeLoop(cube, *ct, e).size();
            CHECK(n == 16 || n == 4); // round the cube, or (beside a corner) stopped by the corners' three edges
            closed += n == 16;
        }
    CHECK(closed > 0);
    // Triangles: no loop past a vertex with six edges.
    const Mesh ico = makeIcosphere(2);
    CHECK(edgeLoop(ico, *buildEdgeTopology(ico), 0).size() >= 1);
}

TEST(edge_select_grow_shrink_open) {
    const Mesh plane = makePlane(6);
    EdgeSelection sel;
    const EdgeTopology& topo = sel.sync(plane);
    const uint32_t e = innerEdge(topo);
    sel.set(e, true);
    CHECK(edgeSelectGrow(sel) == 6); // the other three edges at each end
    CHECK(sel.count == 7);
    CHECK(edgeSelectShrink(sel) == 6);
    CHECK(sel.count == 1 && sel.selected[e]);
    // A closed loop keeps every edge.
    edgeSelectAll(sel, false);
    edgeSelectOpen(sel, true);
    CHECK(sel.count == 24);
    CHECK(edgeSelectShrink(sel) == 0);
    edgeSelectInvert(sel);
    CHECK(sel.count == topo.edgeCount() - 24);
    CHECK(edgeSelectedVertices(sel).size() == plane.vertexCount() - 4); // (the corners have only border edges)
    // A changed mesh drops the selection.
    const uint64_t rev = sel.revision;
    sel.sync(makePlane(5));
    CHECK(sel.count == 0 && sel.revision != rev);
}

TEST(edge_select_command) {
    Document doc;
    doc.mesh = makeCube(4);
    const CommandRegistry& reg = registry();
    CommandResult r = reg.execute(doc, "edge_select", {{"op", std::string("angle")}});
    CHECK(r.ok);
    CHECK(doc.edgeSelection.count == 48);
    CHECK(r.message.find("48 edges selected") != std::string::npos);
    r = reg.execute(doc, "edge_select", {{"op", std::string("edge")}, {"a", 0.0}, {"b", 1.0}});
    const bool joined = r.ok;
    if (joined) CHECK(doc.edgeSelection.count == 1);
    r = reg.execute(doc, "edge_select", {{"op", std::string("edge")}, {"edge", 3.0}, {"add", true}});
    CHECK(r.ok);
    r = reg.execute(doc, "edge_select", {{"op", std::string("loop")}, {"edge", 3.0}, {"remove", true}});
    CHECK(r.ok);
    CHECK(!doc.edgeSelection.selected[3]);
    CHECK(!reg.execute(doc, "edge_select", {{"op", std::string("edge")}, {"edge", 1e9}}).ok);
    CHECK(!reg.execute(doc, "edge_select", {{"op", std::string("frobnicate")}}).ok);
    CHECK(reg.execute(doc, "edge_select", {{"op", std::string("all")}}).ok);
    CHECK(doc.edgeSelection.count == 192);
    // Only on the base level: a Part at SDiv 2 is told to step down; at SDiv 1 its base edges are there.
    std::string err;
    CHECK(subdivDivide(doc.mesh, true, err));
    CHECK(!reg.execute(doc, "edge_select", {{"op", std::string("info")}}).ok);
    CHECK(reg.execute(doc, "subdiv_level", {{"level", 1.0}}).ok);
    r = reg.execute(doc, "edge_select", {{"op", std::string("info")}});
    CHECK(r.ok);
    CHECK(doc.edgeSelection.topo->edgeCount() == 192);
}

TEST(edge_select_million_vertex_base_stays_interactive) {
    const Mesh m = makeQuadSphere(408); // 6 * 408^2 + 2 = 1.0M vertices, 2M triangles in 1M quads
    CHECK(m.vertexCount() > 990000);
    const auto t0 = std::chrono::steady_clock::now();
    EdgeSelection sel;
    const EdgeTopology& topo = sel.sync(m);
    const auto t1 = std::chrono::steady_clock::now();
    edgeSelectByAngle(m, sel, 30.0f, false);
    const auto t2 = std::chrono::steady_clock::now();
    const std::vector<uint32_t> loop = edgeLoop(m, topo, uint32_t(topo.edgeCount() / 2));
    const auto t3 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::printf("  1M base: edges %zu built %.0f ms, by angle %.0f ms, loop of %zu %.2f ms, %.0f MB\n", topo.edgeCount(),
                ms(t0, t1), ms(t1, t2), loop.size(), ms(t2, t3), double(topo.bytes()) / 1e6);
    CHECK(topo.edgeCount() == 2 * m.vertexCount() - 4); // V + F - 2 with F = V - 2 quads
    CHECK(sel.count == 0); // a quad sphere has no 30-degree edges
    CHECK(loop.size() > 100);
    // Generous for CI and parallel ctest; the laptop does it in a fraction (REPORT.md).
    CHECK(ms(t0, t1) < timeLimit(1500.0));
    CHECK(ms(t1, t2) < timeLimit(500.0));
    CHECK(ms(t2, t3) < timeLimit(50.0));
}

TEST(edge_crease_set_clear_undo) {
    Document doc = makeDoc(makeCube(2));
    const CommandRegistry& reg = registry();
    CHECK(!reg.execute(doc, "edge_crease", {}).ok); // nothing selected
    CHECK(reg.execute(doc, "edge_select", {{"op", std::string("angle")}}).ok); // the cube's 12 hard edges, 2 spans each
    CHECK(doc.edgeSelection.count == 24);
    CommandResult r = reg.execute(doc, "edge_crease", {{"value", 0.5}});
    CHECK(r.ok);
    CHECK(r.message.find("24 creased edges of 48") != std::string::npos);
    const EdgeTopology& topo = *doc.edgeSelection.topo;
    CHECK(doc.mesh.creases.size() == doc.mesh.triangleCount() * 3);
    size_t corners = 0;
    for (float c : doc.mesh.creases) corners += c == 0.5f;
    CHECK(corners == 48); // both sides of every creased edge
    for (uint32_t e = 0; e < topo.edgeCount(); ++e)
        CHECK(edgeCrease(doc.mesh, topo, e) == (doc.edgeSelection.selected[e] ? 0.5f : 0.0f));
    // Sharper on one edge, then undo / redo restore exactly.
    const std::vector<float> half = doc.mesh.creases;
    CHECK(reg.execute(doc, "edge_select", {{"op", std::string("edge")}, {"edge", 0.0}}).ok);
    const uint32_t one = uint32_t(std::find(doc.edgeSelection.selected.begin(), doc.edgeSelection.selected.end(), 1) -
                                  doc.edgeSelection.selected.begin());
    CHECK(reg.execute(doc, "edge_crease", {}).ok); // default 1
    CHECK(edgeCrease(doc.mesh, topo, one) == 1.0f);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.creases == half);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(edgeCrease(doc.mesh, topo, one) == 1.0f);
    // Clearing every crease frees the array; undo brings them back.
    r = reg.execute(doc, "edge_crease", {{"clear", true}, {"all", true}});
    CHECK(r.ok);
    CHECK(!doc.mesh.hasCreases());
    CHECK(!reg.execute(doc, "edge_crease", {{"clear", true}, {"all", true}}).ok); // nothing creased
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(edgeCreasedCount(doc.mesh, topo) == 24);
    // Out-of-range values clamp; the same value again changes nothing (no undo step).
    const size_t steps = doc.undo.undoCount();
    CHECK(reg.execute(doc, "edge_select", {{"op", std::string("all")}}).ok);
    CHECK(reg.execute(doc, "edge_crease", {{"value", 7.0}}).ok);
    CHECK(edgeCreasedCount(doc.mesh, topo) == 48);
    CHECK(reg.execute(doc, "edge_crease", {{"value", 1.0}}).ok);
    CHECK(doc.undo.undoCount() == steps + 1);
    CHECK(reg.execute(doc, "edge_crease", {{"info", true}}).message.find("48 creased edges") != std::string::npos);
}

TEST(edge_crease_saved_and_kept_on_the_base_level) {
    Document doc = makeDoc(makeCube(3));
    const CommandRegistry& reg = registry();
    CHECK(reg.execute(doc, "edge_select", {{"op", std::string("angle")}}).ok);
    CHECK(reg.execute(doc, "edge_crease", {{"value", 0.75}}).ok);
    const std::vector<float> creases = doc.mesh.creases;
    // Up two levels (the creases are the base level's; the levels above have none) and back down.
    CHECK(reg.execute(doc, "subdiv_divide", {}).ok);
    CHECK(reg.execute(doc, "subdiv_divide", {}).ok);
    CHECK(!doc.mesh.hasCreases());
    CHECK(!reg.execute(doc, "edge_crease", {}).ok); // base level only
    std::string err;
    CHECK(saveProject(tempPath("edge_crease_levels.sfproj"), doc, err));
    CHECK(reg.execute(doc, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(doc.mesh.creases == creases);
    // Saved with the levels (stepped up) and as a plain mesh; files without creases load without.
    Document loaded;
    CHECK(loadProject(tempPath("edge_crease_levels.sfproj"), loaded, err));
    CHECK(subdivCurrentLevel(loaded.mesh) == 2);
    CHECK(subdivLevelData(loaded.mesh, 0).creases == creases);
    CHECK(reg.execute(loaded, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(loaded.mesh.creases == creases);
    Document plain = makeDoc(makeCube(3));
    CHECK(reg.execute(plain, "edge_select", {{"op", std::string("angle")}}).ok);
    CHECK(reg.execute(plain, "edge_crease", {{"value", 0.25}}).ok);
    CHECK(saveProject(tempPath("edge_crease_plain.sfproj"), plain, err));
    Document back;
    CHECK(loadProject(tempPath("edge_crease_plain.sfproj"), back, err));
    CHECK(back.mesh.creases == plain.mesh.creases);
    Document none = makeDoc(makeCube(3));
    CHECK(saveProject(tempPath("edge_crease_none.sfproj"), none, err));
    CHECK(loadProject(tempPath("edge_crease_none.sfproj"), back, err));
    CHECK(!back.mesh.hasCreases());
    for (const char* f : {"edge_crease_levels.sfproj", "edge_crease_plain.sfproj", "edge_crease_none.sfproj"})
        std::remove(tempPath(f).c_str());
}
