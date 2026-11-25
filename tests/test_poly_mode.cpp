// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygon mode (core/PolyMode.h, core/PolyOps.h): what a Part may enter, the Points / Edges / Faces selection on quads
// (both halves, counts per polygon), its conversions, Grow / Shrink, face groups, edge loops and rings, the box select
// facing the camera, the poly_mode / poly_select / poly_op commands, the gizmo's `transform selection=true` (one
// sparse undo step), the ops registry landing a topology change as one whole-mesh step, and the cost on a dense cage.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/EdgeSelect.h"
#include "core/Multires.h"
#include "core/PolyMode.h"
#include "core/PolyOps.h"
#include "core/Primitives.h"
#include "core/ScreenSelect.h"

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstdio>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

// A face of the +Z side of a cube from makeCube: the lower triangle of a quad whose vertices all have z > 0.
uint32_t frontFace(const Mesh& m) {
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3& tri = m.triangles[t];
        if (edgePolygon(m, t) != t) continue;
        if (m.positions[tri.x].z > 0.9f && m.positions[tri.y].z > 0.9f && m.positions[tri.z].z > 0.9f) return t;
    }
    return 0;
}

// An inner edge of a plane: both ends have four edges.
uint32_t innerEdge(const EdgeTopology& topo) {
    auto valence = [&](uint32_t v) { return topo.vertexOffsets[v + 1] - topo.vertexOffsets[v]; };
    for (uint32_t e = 0; e < topo.edgeCount(); ++e)
        if (!topo.open(e) && valence(topo.ends[e].x) == 4 && valence(topo.ends[e].y) == 4) return e;
    return kNoEdge;
}

} // namespace

TEST(poly_mode_refusal_says_why) {
    Mesh empty;
    CHECK(polyModeRefusal(empty).find("no faces") != std::string::npos);
    Mesh cube = makeCube(2);
    CHECK(polyModeRefusal(cube).empty());
    // Too dense: the message names both counts in short form and where to go.
    const std::string dense = polyModeRefusal(cube, 10);
    CHECK(dense.find("up to 10 triangles") != std::string::npos);
    CHECK(dense.find("this Part has 48") != std::string::npos);
    CHECK(dense.find("Remesh") != std::string::npos);
    Mesh tube = makeCube(1);
    tube.tube.points.push_back({});
    CHECK(polyModeRefusal(tube).find("Bake the Tube") != std::string::npos);
    Mesh spheres = makeCube(1);
    spheres.forgespheres.push_back({});
    CHECK(polyModeRefusal(spheres).find("Forge Spheres") != std::string::npos);
    // Subdivision levels: the ops rebuild the cage the levels are made from.
    Mesh levels = makeCube(1);
    std::string err;
    CHECK(subdivDivide(levels, true, err));
    CHECK(polyModeRefusal(levels).find("subdivision levels") != std::string::npos);
}

TEST(poly_selection_counts_quads_once_and_converts) {
    const Mesh m = makeCube(2); // 6 sides x 4 quads = 24 faces, 48 triangles, 26 points, 48 edges
    PolySelection sel;
    const EdgeTopology& topo = sel.sync(m);
    CHECK(topo.edgeCount() == 48);
    CHECK(sel.element == PolyElement::Faces);
    CHECK(sel.total(m) == 24);
    const uint32_t f = frontFace(m);
    sel.setFace(m, m.quadPartnerAt(f), true); // either half selects the quad
    CHECK(sel.count() == 1);
    CHECK(sel.faceSelected(f) && sel.faceSelected(m.quadPartnerAt(f)));
    CHECK(sel.triangles(m).size() == 2);
    CHECK(sel.vertices(m).size() == 4);
    CHECK(sel.edgeList(m).size() == 4);
    CHECK(polySelectionText(sel, m) == "1 face selected of 24");
    // Faces -> Points: its 4 corners; -> Edges: the 4 edges between them; -> Faces: the quad again.
    sel.setElement(m, PolyElement::Points);
    CHECK(sel.count() == 4);
    CHECK(sel.total(m) == 26);
    CHECK(polySelectionText(sel, m) == "4 points selected of 26");
    sel.setElement(m, PolyElement::Edges);
    CHECK(sel.count() == 4);
    sel.setElement(m, PolyElement::Faces);
    CHECK(sel.count() == 1);
    CHECK(sel.faceSelected(f));
    // A single point selects no whole face or edge.
    sel.setElement(m, PolyElement::Points);
    polySelectAll(sel, m, false);
    sel.setPoint(m.triangles[f].x, true);
    CHECK(sel.triangles(m).empty());
    CHECK(sel.edgeList(m).empty());
    CHECK(sel.vertices(m).size() == 1);
    const std::vector<float> w = sel.weights(m);
    CHECK(w.size() == m.vertexCount());
    CHECK(w[m.triangles[f].x] == 1.0f);
    size_t ones = 0;
    for (float x : w) ones += x == 1.0f;
    CHECK(ones == 1);
    // The selection follows the mesh: new triangles drop it.
    Mesh changed = makeCube(3);
    CHECK(!sel.synced(changed));
    sel.sync(changed);
    CHECK(sel.count() == 0);
    sel.setElement(changed, PolyElement::Faces);
    CHECK(sel.total(changed) == 54);
}

TEST(poly_select_all_invert_grow_shrink) {
    const Mesh m = makeCube(4); // 96 faces, 16 per side (one face group per side)
    PolySelection sel;
    sel.sync(m);
    CHECK(polySelectAll(sel, m, true) == 96);
    CHECK(polySelectInvert(sel, m) == 96);
    CHECK(sel.count() == 0);
    // The centre face of a side: Grow adds the 8 around it, Shrink takes them back.
    uint32_t centre = 0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        if (edgePolygon(m, t) != t) continue;
        glm::vec3 c(0.0f);
        const glm::uvec3& tri = m.triangles[t];
        c = (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f;
        if (c.z > 0.99f && std::abs(c.x) < 0.3f && std::abs(c.y) < 0.3f) {
            centre = t;
            break;
        }
    }
    sel.setFace(m, centre, true);
    CHECK(polySelectGrow(sel, m) == 8);
    CHECK(sel.count() == 9);
    CHECK(polySelectShrink(sel, m) == 8);
    CHECK(sel.count() == 1);
    CHECK(sel.faceSelected(centre));
    // By face group: the whole side.
    polySelectFaceGroup(sel, m, m.facegroupAt(centre), PolySelectMode::Replace);
    CHECK(sel.count() == 16);
    polySelectFaceGroup(sel, m, m.facegroupAt(centre), PolySelectMode::Remove);
    CHECK(sel.count() == 0);
    // Points: grow from one corner of the cube adds its 3 neighbours along polygon edges (no diagonals).
    sel.setElement(m, PolyElement::Points);
    sel.setPoint(0, true);
    const size_t grown = polySelectGrow(sel, m);
    CHECK(grown == 3 || grown == 4); // a corner (3) or an edge point (4)
    polySelectShrink(sel, m); // the neighbours touch unselected points; the corner itself is surrounded
    CHECK(sel.count() == 1);
    CHECK(sel.pointSelected(0));
}

TEST(poly_edge_loop_and_ring_on_a_grid) {
    const Mesh m = makePlane(4); // a 4x4 grid of quads
    PolySelection sel;
    const EdgeTopology& topo = sel.sync(m);
    const uint32_t e = innerEdge(topo);
    CHECK(e != kNoEdge);
    const std::vector<uint32_t> loop = edgeLoop(m, topo, e), ring = polyEdgeRing(m, topo, e);
    // A loop runs along a grid line (4 edges, ending at the border's 3-valence vertices); the ring crosses the 4
    // quads of the row the edge sits in (4 edges, border to border). They share only `e`.
    CHECK(loop.size() == 4);
    CHECK(ring.size() == 5); // (the border edges at both ends included)
    size_t shared = 0;
    for (uint32_t a : loop)
        for (uint32_t b : ring) shared += a == b;
    CHECK(shared == 1);
    sel.setElement(m, PolyElement::Edges);
    CHECK(polySelectEdgeLoop(sel, m, e, PolySelectMode::Replace) == 4);
    CHECK(sel.count() == 4);
    CHECK(polySelectEdgeRing(sel, m, e, PolySelectMode::Add) == 4);
    CHECK(sel.count() == 8);
    // In Faces, a loop selects the faces beside it: 8 (two rows); a ring the 4 quads it crosses.
    sel.setElement(m, PolyElement::Faces);
    polySelectEdgeLoop(sel, m, e, PolySelectMode::Replace);
    CHECK(sel.count() == 8);
    polySelectEdgeRing(sel, m, e, PolySelectMode::Replace);
    CHECK(sel.count() == 4);
    // A ring on a triangle mesh is just the edge.
    const Mesh ico = makeIcosphere(1);
    PolySelection s2;
    const EdgeTopology& t2 = s2.sync(ico);
    CHECK(polyEdgeRing(ico, t2, 0).size() == 1);
}

TEST(poly_box_select_faces_the_camera) {
    const Mesh m = makeCube(2);
    PolySelection sel;
    sel.sync(m);
    const glm::mat4 vp = frontViewProj(m); // NDC -1..1 spans the cube seen from +Z
    const glm::vec3 eye(0.0f, 0.0f, 10.0f);
    // A box just inside the silhouette: the front and back sides' faces (their centres), not the sides' (on the rim).
    // (Replace counts the faces it drops too: check what is selected.)
    polySelectRect(sel, m, vp, {-0.9f, -0.9f}, {0.9f, 0.9f}, PolySelectMode::Replace, nullptr);
    CHECK(sel.count() == 8);
    polySelectRect(sel, m, vp, {-0.9f, -0.9f}, {0.9f, 0.9f}, PolySelectMode::Replace, &eye);
    CHECK(sel.count() == 4);
    for (uint32_t t : sel.triangles(m)) CHECK(m.positions[m.triangles[t].x].z > 0.9f);
    // Half the box, added: 2 more front faces at most... the left half holds 2 of the 4 front faces (already in).
    CHECK(polySelectRect(sel, m, vp, {-0.9f, -0.9f}, {0.0f, 0.9f}, PolySelectMode::Remove, &eye) == 2);
    CHECK(sel.count() == 2);
    // Points: the front corners inside the box (the 9 points of the +Z side; the rim ones at NDC +-1 are out).
    sel.setElement(m, PolyElement::Points);
    polySelectRect(sel, m, vp, {-0.9f, -0.9f}, {0.9f, 0.9f}, PolySelectMode::Replace, &eye);
    CHECK(sel.count() == 1);
    polySelectRect(sel, m, vp, {-1.1f, -1.1f}, {1.1f, 1.1f}, PolySelectMode::Replace, &eye);
    CHECK(sel.count() == 9);
    // Edges: both ends inside, facing.
    sel.setElement(m, PolyElement::Edges);
    polySelectRect(sel, m, vp, {-1.1f, -1.1f}, {1.1f, 1.1f}, PolySelectMode::Replace, &eye);
    CHECK(sel.count() == 12);
    polySelectRect(sel, m, vp, {-1.1f, -1.1f}, {1.1f, 1.1f}, PolySelectMode::Replace, nullptr);
    CHECK(sel.count() == 48);
}

TEST(poly_hidden_faces_are_never_selected) {
    Mesh m = makeCube(2);
    const uint32_t f = frontFace(m);
    m.ensureHidden()[f] = 1;
    m.hidden[m.quadPartnerAt(f)] = 1;
    PolySelection sel;
    sel.sync(m);
    CHECK(polySelectAll(sel, m, true) == 23);
    sel.setElement(m, PolyElement::Points);
    CHECK(sel.count() == 26); // every point is on some shown face
    polySelectAll(sel, m, false);
    polySelectFaceGroup(sel, m, m.facegroupAt(f), PolySelectMode::Replace);
    CHECK(sel.count() == 8); // the side's points less the hidden quad's own corner
}

TEST(poly_commands_select_and_run_ops_as_one_undo_step) {
    Document doc;
    doc.setMesh(makeCube(2));
    const CommandRegistry& reg = registry();
    CommandResult r = reg.execute(doc, "poly_mode");
    CHECK(r.ok);
    CHECK(r.message.find("24 faces, 48 edges, 26 points") != std::string::npos);
    r = reg.execute(doc, "poly_select", {{"op", std::string("face")}, {"index", double(frontFace(doc.mesh))}});
    CHECK(r.ok);
    CHECK(doc.polySelection.count() == 1);
    r = reg.execute(doc, "poly_select", {{"op", std::string("grow")}});
    CHECK(r.ok);
    CHECK(doc.polySelection.count() == 8); // the other 3 of its side (the centre point) and 2 on each of the two sides it touches
    CHECK(!reg.execute(doc, "poly_select", {{"op", std::string("face")}, {"index", 1e9}}).ok);
    CHECK(!reg.execute(doc, "poly_select", {{"op", std::string("frobnicate")}}).ok);
    r = reg.execute(doc, "poly_mode", {{"element", std::string("points")}});
    CHECK(r.ok);
    CHECK(doc.polySelection.element == PolyElement::Points);
    CHECK(doc.polySelection.count() > 4);
    r = reg.execute(doc, "poly_select", {{"op", std::string("none")}});
    CHECK(doc.polySelection.count() == 0);

    // poly_op list names the built-in ops.
    r = reg.execute(doc, "poly_op", {{"op", std::string("list")}});
    CHECK(r.ok);
    CHECK(r.message.find("delete") != std::string::npos && r.message.find("flip") != std::string::npos);
    CHECK(PolyOpRegistry::instance().find("delete") != nullptr);
    // No selection: a clear ask.
    r = reg.execute(doc, "poly_op", {{"op", std::string("delete")}});
    CHECK(!r.ok);
    CHECK(r.message.find("needs faces") != std::string::npos);
    // A point selected covers no whole face: told to switch.
    reg.execute(doc, "poly_select", {{"op", std::string("point")}, {"index", 0.0}});
    r = reg.execute(doc, "poly_op", {{"op", std::string("delete")}});
    CHECK(!r.ok);
    CHECK(r.message.find("switch to Faces") != std::string::npos);

    // Delete two faces: a topology change, one undo step, the selection cleared; undo brings the cube back.
    reg.execute(doc, "poly_mode", {{"element", std::string("faces")}});
    reg.execute(doc, "poly_select", {{"op", std::string("face")}, {"index", double(frontFace(doc.mesh))}});
    reg.execute(doc, "poly_select", {{"op", std::string("grow")}});
    CHECK(doc.polySelection.count() == 8);
    const size_t before = doc.undo.undoCount();
    r = reg.execute(doc, "poly_op", {{"op", std::string("delete")}});
    CHECK(r.ok);
    CHECK(r.message.find("Deleted 8 faces") != std::string::npos);
    CHECK(doc.mesh.triangleCount() == 48 - 16);
    CHECK(doc.undo.undoCount() == before + 1);
    CHECK(doc.polySelection.count() == 0);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.triangleCount() == 48);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.triangleCount() == 32);
    doc.undo.undo(doc.mesh, doc.bvh);
    // Deleting everything is refused.
    reg.execute(doc, "poly_select", {{"op", std::string("all")}});
    CHECK(doc.polySelection.count() == 24);
    r = reg.execute(doc, "poly_op", {{"op", std::string("delete")}});
    CHECK(!r.ok);
    CHECK(doc.mesh.triangleCount() == 48);

    // Flip one face: its normal turns, the result stays selected, one step, undo restores the winding.
    reg.execute(doc, "poly_select", {{"op", std::string("face")}, {"index", double(frontFace(doc.mesh))}});
    const uint32_t f = frontFace(doc.mesh);
    const glm::uvec3 wound = doc.mesh.triangles[f];
    const size_t steps = doc.undo.undoCount();
    r = reg.execute(doc, "poly_op", {{"op", std::string("flip")}});
    CHECK(r.ok);
    CHECK(doc.undo.undoCount() == steps + 1);
    CHECK(doc.mesh.triangles[f] != wound);
    CHECK(doc.polySelection.count() == 1);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.triangles[f] == wound);
}

TEST(poly_transform_selection_moves_only_the_selection) {
    Document doc;
    doc.setMesh(makeCube(2));
    const CommandRegistry& reg = registry();
    CHECK(!reg.execute(doc, "transform", {{"translate", std::string("0,0,1")}, {"selection", true}}).ok); // nothing selected
    reg.execute(doc, "poly_select", {{"op", std::string("face")}, {"index", double(frontFace(doc.mesh))}});
    const std::vector<glm::vec3> before = doc.mesh.positions;
    const std::vector<uint32_t> moving = doc.polySelection.vertices(doc.mesh);
    CHECK(moving.size() == 4);
    const size_t steps = doc.undo.undoCount();
    CommandResult r = reg.execute(doc, "transform", {{"translate", std::string("0,0,1")}, {"selection", true}});
    CHECK(r.ok);
    CHECK(doc.undo.undoCount() == steps + 1);
    size_t moved = 0;
    for (uint32_t v = 0; v < before.size(); ++v) moved += doc.mesh.positions[v] != before[v];
    CHECK(moved == 4);
    for (uint32_t v : moving) CHECK(std::abs(doc.mesh.positions[v].z - before[v].z - 1.0f) < 1e-5f);
    CHECK(doc.mesh.triangleCount() == 48); // topology untouched
    CHECK(doc.polySelection.count() == 1); // the selection survives a move
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    for (uint32_t v = 0; v < before.size(); ++v) CHECK(doc.mesh.positions[v] == before[v]);
}

TEST(poly_ops_registry_lands_an_op_with_parameters) {
    // An op registered at run time (as the modelling ops are) runs through poly_op with its defaults filled in,
    // and a positions-only result lands as a sparse step.
    PolyOp op;
    op.id = "test_lift";
    op.label = "Lift";
    op.needs = PolyElement::Faces;
    op.params = {{"height", "Height", 0.0, 2.0, 0.5, "How far"}};
    op.run = [](const PolyOpInput& in, PolyOpOutput& out, std::string& error) {
        if (in.triangles.empty()) {
            error = "nothing";
            return false;
        }
        out.mesh = in.mesh;
        const float h = float(argNumber(in.args, "height", 0.0));
        size_t flagged = 0;
        for (uint8_t f : in.triangleFlags) flagged += f;
        if (flagged != in.triangles.size() || in.edgePairs.size() != in.edges.size()) {
            error = "the shapes of the selection disagree";
            return false;
        }
        for (uint32_t v : in.vertices) out.mesh.positions[v].z += h;
        out.element = PolyElement::Faces;
        out.triangles = in.triangles;
        return true;
    };
    PolyOpRegistry::instance().add(op);
    Document doc;
    doc.setMesh(makeCube(2));
    const CommandRegistry& reg = registry();
    reg.execute(doc, "poly_select", {{"op", std::string("face")}, {"index", double(frontFace(doc.mesh))}});
    const std::vector<glm::vec3> before = doc.mesh.positions;
    CommandResult r = reg.execute(doc, "poly_op", {{"op", std::string("test_lift")}});
    CHECK(r.ok);
    const uint32_t v0 = doc.mesh.triangles[frontFace(doc.mesh)].x;
    CHECK(std::abs(doc.mesh.positions[v0].z - before[v0].z - 0.5f) < 1e-5f); // the default
    CHECK(doc.undo.lastUndo() && !doc.undo.lastUndo()->positions.empty() && !doc.undo.lastUndo()->mesh); // sparse
    r = reg.execute(doc, "poly_op", {{"op", std::string("test_lift")}, {"height", 9.0}}); // clamped to 2
    CHECK(r.ok);
    CHECK(std::abs(doc.mesh.positions[v0].z - before[v0].z - 2.5f) < 1e-5f);
    CHECK(doc.polySelection.count() == 1);
    // The preview never touches the Part.
    PolyOpOutput out;
    std::string err;
    CHECK(previewPolyOp(doc, "test_lift", {{"height", 1.0}}, out, err));
    CHECK(std::abs(doc.mesh.positions[v0].z - before[v0].z - 2.5f) < 1e-5f);
    CHECK(std::abs(out.mesh.positions[v0].z - before[v0].z - 3.5f) < 1e-5f);
    // Sculpt layers: refused with a pointer.
    doc.mesh.addLayer("Wrinkles");
    r = reg.execute(doc, "poly_op", {{"op", std::string("test_lift")}});
    CHECK(!r.ok);
    CHECK(r.message.find("sculpt layers") != std::string::npos);
}

TEST(poly_selection_cost_on_a_dense_cage) {
    // A 2M-triangle cage is the mode's ceiling: syncing, selecting everything and a box over it stay interactive.
    const Mesh m = makePlane(1000); // 1,000,000 quads, 2M triangles
    CHECK(m.triangleCount() == 2000000);
    CHECK(polyModeRefusal(m).empty());
    PolySelection sel;
    const auto t0 = std::chrono::steady_clock::now();
    sel.sync(m);
    const auto t1 = std::chrono::steady_clock::now();
    polySelectAll(sel, m, true);
    const std::vector<uint32_t> verts = sel.vertices(m);
    const auto t2 = std::chrono::steady_clock::now();
    // The plane lies in XZ: a box seen from above over its middle (no facing test: either side counts).
    const glm::mat4 fromAbove = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 20.0f) *
                                glm::lookAt(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    polySelectRect(sel, m, fromAbove, {-0.5f, -0.5f}, {0.5f, 0.5f}, PolySelectMode::Replace, nullptr);
    const auto t3 = std::chrono::steady_clock::now();
    const double sync = std::chrono::duration<double>(t1 - t0).count(), all = std::chrono::duration<double>(t2 - t1).count(),
                 box = std::chrono::duration<double>(t3 - t2).count();
    std::printf("  poly 2M: sync %.2f s, select all + vertices %.2f s, box %.2f s\n", sync, all, box);
    CHECK(verts.size() == m.vertexCount());
    CHECK(sel.count() > 0 && sel.count() < 1000000);
    CHECK(sync < sf::test::timeLimit(3.0));
    CHECK(all + box < sf::test::timeLimit(3.0));
}
