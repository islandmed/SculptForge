// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygon mode's Edge loop op (core/PolyEdgeLoop.h) through the registry: one ring from one edge, the ring selected
// with Ring, count + position, the new loops selected afterwards, one undo step, the preview leaving the Part alone,
// two rings at once, the refusals, and the Ring button walking the same ring the op cuts.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/EdgeLoopInsert.h"
#include "core/PolyEdgeLoop.h"
#include "core/PolyMode.h"
#include "core/PolyOps.h"
#include "core/Primitives.h"
#include "core/Undo.h"

#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

// An edge of the cube along Y (a vertical one): its ring goes round the four sides.
uint32_t verticalEdge(const Mesh& m, const EdgeTopology& t) {
    for (uint32_t e = 0; e < t.edgeCount(); ++e) {
        const glm::vec3 a = m.positions[t.ends[e].x], b = m.positions[t.ends[e].y];
        if (a.x == b.x && a.z == b.z) return e;
    }
    return kNoEdge;
}

// An edge of the cube along X on the top side (y = 1): its ring crosses the top and bottom through the x = +-1 sides.
uint32_t topEdgeAlongX(const Mesh& m, const EdgeTopology& t) {
    for (uint32_t e = 0; e < t.edgeCount(); ++e) {
        const glm::vec3 a = m.positions[t.ends[e].x], b = m.positions[t.ends[e].y];
        if (a.y == 1.0f && b.y == 1.0f && a.z == b.z && a.z == 0.0f) return e;
    }
    return kNoEdge;
}

} // namespace

TEST(poly_edge_loop_cuts_the_ring_through_one_edge) {
    Document doc;
    doc.setMesh(makeCube(2)); // 24 quads, 26 vertices
    const CommandRegistry& reg = registry();
    const EdgeTopology& topo = doc.polySelection.sync(doc.mesh);
    const uint32_t e = verticalEdge(doc.mesh, topo);
    CHECK(e != kNoEdge);
    CHECK(PolyOpRegistry::instance().find("edge_loop") != nullptr);
    CHECK(PolyOpRegistry::instance().find("edge_loop")->preview);
    // Nothing selected: refused in the user's words, the Part untouched.
    CommandResult r = reg.execute(doc, "poly_op", {{"op", std::string("edge_loop")}});
    CHECK(!r.ok);
    CHECK(doc.mesh.vertexCount() == 26);
    CHECK(reg.execute(doc, "poly_select", {{"op", std::string("edge")}, {"index", double(e)}}).ok);
    CHECK(doc.polySelection.element == PolyElement::Edges && doc.polySelection.count() == 1);
    const size_t steps = doc.undo.undoCount();
    r = reg.execute(doc, "poly_op", {{"op", std::string("edge_loop")}});
    CHECK(r.ok);
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.message.find("8 edges round") != std::string::npos);
    // The ring round the cube has 8 edges: 8 new vertices, 8 quads split into 16.
    CHECK(doc.mesh.vertexCount() == 26 + 8);
    CHECK(doc.mesh.quadCount() == 24 + 8);
    CHECK(doc.undo.undoCount() == steps + 1);
    // The new loop is what is selected now (8 edges, in Edges).
    CHECK(doc.polySelection.element == PolyElement::Edges);
    CHECK(doc.polySelection.count() == 8);
    for (uint32_t v : doc.polySelection.vertices(doc.mesh)) CHECK(std::abs(doc.mesh.positions[v].y - 0.5f) < 1e-5f || std::abs(doc.mesh.positions[v].y + 0.5f) < 1e-5f);
    // Face groups: every new quad on a side keeps that side's group (one group per side on a cube).
    CHECK(doc.mesh.hasFaceGroups());
    // One undo step puts the cube back.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.vertexCount() == 26 && doc.mesh.quadCount() == 24);
}

TEST(poly_edge_loop_count_position_and_the_ring_button) {
    Document doc;
    doc.setMesh(makeCube(1)); // 6 quads, 8 vertices
    const CommandRegistry& reg = registry();
    const EdgeTopology& topo = doc.polySelection.sync(doc.mesh);
    const uint32_t e = verticalEdge(doc.mesh, topo);
    // In Faces, Ring selects the 4 faces the ring crosses: the op wants Edges and says so (a band of faces would be
    // three rings, not what anyone meant).
    CHECK(reg.execute(doc, "poly_select", {{"op", std::string("ring")}, {"index", double(e)}}).ok);
    CHECK(doc.polySelection.element == PolyElement::Faces && doc.polySelection.count() == 4);
    CommandResult refused = reg.execute(doc, "poly_op", {{"op", std::string("edge_loop")}});
    CHECK(!refused.ok && refused.message.find("in Edges") != std::string::npos);
    CHECK(doc.mesh.vertexCount() == 8);
    // In Edges, Ring selects the ring the op cuts (the same walker): 4 edges round.
    CHECK(reg.execute(doc, "poly_select", {{"op", std::string("edge")}, {"index", double(e)}}).ok);
    CHECK(reg.execute(doc, "poly_select", {{"op", std::string("ring")}, {"index", double(e)}}).ok);
    CHECK(doc.polySelection.element == PolyElement::Edges && doc.polySelection.count() == 4);
    CHECK(polyEdgeRing(doc.mesh, topo, e) == edgeRing(doc.mesh, topo, e).edges);
    // Three loops at position 0.25: every quad of the ring becomes four, the loops sit in the lower half.
    CommandResult r = reg.execute(doc, "poly_op", {{"op", std::string("edge_loop")}, {"count", 3.0}, {"position", 0.25}});
    CHECK(r.ok);
    CHECK(doc.mesh.vertexCount() == 8 + 12);
    CHECK(doc.mesh.quadCount() == 6 + 12);
    CHECK(doc.polySelection.count() == 12);
    float lo = 2.0f, hi = -2.0f;
    for (uint32_t v = 8; v < doc.mesh.vertexCount(); ++v) lo = std::min(lo, doc.mesh.positions[v].y), hi = std::max(hi, doc.mesh.positions[v].y);
    // position 0.25 = slide -0.5: parameters 1/4, 1/2, 3/4 scaled by 0.5 from the lower rail (y = -1 .. 1).
    CHECK(std::abs(lo - (-1.0f + 2.0f * 0.125f)) < 1e-5f);
    CHECK(std::abs(hi - (-1.0f + 2.0f * 0.375f)) < 1e-5f);
    // The count is clamped to the slider's range and rounded.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(reg.execute(doc, "poly_select", {{"op", std::string("edge")}, {"index", double(e)}}).ok);
    r = reg.execute(doc, "poly_op", {{"op", std::string("edge_loop")}, {"count", 99.0}});
    CHECK(r.ok);
    CHECK(doc.mesh.vertexCount() == 8 + 4 * 20);
}

TEST(poly_edge_loop_preview_leaves_the_part_alone) {
    Document doc;
    doc.setMesh(makeCube(2));
    const CommandRegistry& reg = registry();
    const EdgeTopology& topo = doc.polySelection.sync(doc.mesh);
    reg.execute(doc, "poly_select", {{"op", std::string("edge")}, {"index", double(verticalEdge(doc.mesh, topo))}});
    const size_t steps = doc.undo.undoCount();
    PolyOpOutput out;
    std::string error;
    CHECK(previewPolyOp(doc, "edge_loop", {{"count", 2.0}, {"position", 0.7}}, out, error));
    CHECK(out.mesh.vertexCount() == 26 + 16 && out.element == PolyElement::Edges);
    CHECK(out.edges.size() == 16); // two closed loops of 8 edges, as vertex pairs in the output mesh
    for (const glm::uvec2& pr : out.edges) CHECK(pr.x < out.mesh.vertexCount() && pr.y < out.mesh.vertexCount());
    CHECK(doc.mesh.vertexCount() == 26 && doc.undo.undoCount() == steps);
    CHECK(doc.polySelection.count() == 1);
}

TEST(poly_edge_loop_cuts_two_rings_at_once) {
    Document doc;
    doc.setMesh(makeCube(2));
    const CommandRegistry& reg = registry();
    const EdgeTopology& topo = doc.polySelection.sync(doc.mesh);
    const uint32_t a = verticalEdge(doc.mesh, topo), b = topEdgeAlongX(doc.mesh, topo);
    CHECK(a != kNoEdge && b != kNoEdge);
    reg.execute(doc, "poly_select", {{"op", std::string("edge")}, {"index", double(a)}});
    reg.execute(doc, "poly_select", {{"op", std::string("edge")}, {"index", double(b)}, {"add", true}});
    CHECK(doc.polySelection.count() == 2);
    const CommandResult r = reg.execute(doc, "poly_op", {{"op", std::string("edge_loop")}});
    CHECK(r.ok);
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.message.find("2 rings") != std::string::npos);
    // Two rings of 8 crossing each other: the second ring, walked on the cut mesh, crosses the first loop's strip on two
    // sides too (10 edges, two of them the first loop's), so 8 + 10 new vertices; the selection is both loops, the first
    // one's two split edges as their pieces: 8 + 2 + 10 edges.
    CHECK(doc.mesh.vertexCount() == 26 + 8 + 10);
    CHECK(doc.polySelection.element == PolyElement::Edges);
    CHECK(doc.polySelection.count() == 8 + 2 + 10);
    CHECK(doc.undo.undoCount() == 1); // one step for both rings
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.vertexCount() == 26);
}

TEST(poly_edge_loop_ends_on_a_triangle_and_refuses_a_shared_edge) {
    Document doc;
    doc.setMesh(makeCylinder(8));
    const CommandRegistry& reg = registry();
    const EdgeTopology& topo = doc.polySelection.sync(doc.mesh);
    // A top rim edge: the ring runs down the sides into the two cap triangles.
    uint32_t rim = kNoEdge;
    for (uint32_t e = 0; e < topo.edgeCount() && rim == kNoEdge; ++e) {
        const glm::vec3 a = doc.mesh.positions[topo.ends[e].x], b = doc.mesh.positions[topo.ends[e].y];
        if (a.y == b.y && a.y > 0.9f && (a.x != 0.0f || a.z != 0.0f) && (b.x != 0.0f || b.z != 0.0f)) rim = e;
    }
    CHECK(rim != kNoEdge);
    reg.execute(doc, "poly_select", {{"op", std::string("edge")}, {"index", double(rim)}});
    const size_t tris = doc.mesh.triangleCount();
    const CommandResult r = reg.execute(doc, "poly_op", {{"op", std::string("edge_loop")}});
    CHECK(r.ok);
    CHECK(r.message.find("ended on 2 triangles") != std::string::npos);
    CHECK(doc.mesh.triangleCount() > tris);
    // An edge shared by three faces: no ring, refused with the reason.
    Document bad;
    Mesh m;
    m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, -1, 0}};
    m.triangles = {{0, 1, 2}, {1, 0, 3}, {0, 1, 4}};
    m.rebuildTopology();
    m.recomputeAllNormals();
    bad.setMesh(std::move(m));
    const EdgeTopology& t2 = bad.polySelection.sync(bad.mesh);
    uint32_t shared = kNoEdge;
    for (uint32_t e = 0; e < t2.edgeCount() && shared == kNoEdge; ++e)
        if (t2.ends[e] == glm::uvec2(0, 1)) shared = e;
    CHECK(shared != kNoEdge);
    reg.execute(bad, "poly_select", {{"op", std::string("edge")}, {"index", double(shared)}});
    const CommandResult r2 = reg.execute(bad, "poly_op", {{"op", std::string("edge_loop")}});
    CHECK(!r2.ok);
    CHECK(r2.message.find("three faces") != std::string::npos);
    CHECK(bad.mesh.triangleCount() == 3);
}
