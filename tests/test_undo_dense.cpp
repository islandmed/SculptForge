// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Undo on very dense meshes (#181): a stroke's undo / redo follows the touched vertices, not the mesh, and undoing
// a whole-mesh step (Voxel Remesh, Divide) swaps meshes without copying one.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Primitives.h"
#include "core/Timeline.h"
#include "core/UploadOrder.h"

#include <chrono>
#include <cstdio>

using namespace sf;

namespace {

double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

void strokeOnce(Document& d, float radius = 0.15f) {
    d.sculptor.beginStroke(d.mesh, "Standard");
    const glm::vec3 dir = glm::normalize(glm::vec3(0.4f, 0.3f, 1.0f));
    DabParams p;
    p.center = dir;
    p.viewDir = -dir;
    if (RayHit hit; d.bvh.raycast(d.mesh, dir * 4.0f, -dir, hit)) p.center = hit.position;
    p.radius = radius;
    p.strength = 0.6f;
    for (int i = 0; i < 4; ++i) d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, {});
    d.undo.push(d.sculptor.endStroke());
}

} // namespace

// A stroke's step holds the touched vertices (not the mesh) and undoes / redoes in time that follows them.
TEST(undo_dense_stroke_follows_touched_vertices) {
    Document d = makeDoc(makeBenchSphere(2000000)); // (#181: "a couple million polygons")
    auto tl = Timeline::attach(d);
    const std::vector<glm::vec3> before = d.mesh.positions;
    strokeOnce(d);
    const UndoRecord* step = d.undo.lastUndo();
    CHECK(step != nullptr);
    const size_t touched = step->positions.indices.size();
    CHECK(touched > 1000 && touched < d.mesh.vertexCount() / 10);
    CHECK(step->approxBytes() <= touched * 2 * (sizeof(uint32_t) + sizeof(glm::vec3))); // O(touched), not O(mesh)
    const std::vector<glm::vec3> after = d.mesh.positions;
    auto t = std::chrono::steady_clock::now();
    d.undo.undo(d.mesh, d.bvh);
    const double undoMs = msSince(t);
    CHECK(d.mesh.positions == before);
    t = std::chrono::steady_clock::now();
    d.undo.redo(d.mesh, d.bvh);
    const double redoMs = msSince(t);
    CHECK(d.mesh.positions == after);
    CHECK(undoMs < timeLimit(25.0));
    CHECK(redoMs < timeLimit(25.0));
}

// Undoing a remesh (a whole-mesh step) swaps the meshes and their trees: no tree is rebuilt, nothing is copied, and
// the tree answers for the mesh that came back (#181: a 12M triangle undo spent 0.7 s in BVH::build).
TEST(undo_dense_whole_mesh_step_swaps_the_tree) {
    Document d = makeDoc(makeBenchSphere(1000000));
    const Mesh original = d.mesh;
    BVH reference;
    auto t = std::chrono::steady_clock::now();
    reference.build(original);
    const double buildMs = msSince(t);
    d.replaceMeshUndoable(makeBenchSphere(300000), "Remesh");
    const size_t smallTris = d.mesh.triangleCount();
    CHECK(smallTris < original.triangleCount() / 2);
    CHECK(d.undo.undoBytes() > d.undo.lastUndo()->mesh->memoryBytes() / 2);
    const uint64_t serial = d.bvh.structureSerial();
    t = std::chrono::steady_clock::now();
    CHECK(d.undo.undo(d.mesh, d.bvh));
    const double undoMs = msSince(t);
    CHECK(d.mesh.triangleCount() == original.triangleCount());
    CHECK(d.mesh.positions == original.positions);
    CHECK(d.bvh.structureSerial() != serial);
    CHECK(d.bvh.nodeCount() == reference.nodeCount());
    CHECK(undoMs < buildMs * 0.25 + timeLimit(2.0));
    // The restored tree answers like a fresh one.
    for (const glm::vec3 dir : {glm::vec3(0.4f, 0.3f, 1.0f), glm::vec3(-1.0f, 0.2f, 0.1f), glm::vec3(0.0f, 1.0f, 0.05f)}) {
        const glm::vec3 n = glm::normalize(dir);
        RayHit a, b;
        const bool ha = d.bvh.raycast(d.mesh, n * 4.0f, -n, a), hb = reference.raycast(original, n * 4.0f, -n, b);
        CHECK(ha && hb);
        CHECK(a.triangle == b.triangle);
    }
    std::vector<uint32_t> found, expect;
    d.bvh.queryVertices(d.mesh, glm::vec3(0.0f, 0.0f, 1.0f), 0.2f, found);
    reference.queryVertices(original, glm::vec3(0.0f, 0.0f, 1.0f), 0.2f, expect);
    CHECK(!found.empty() && found == expect);
    // Redo brings the small mesh and its tree back, and a stroke on it still undoes exactly.
    CHECK(d.undo.redo(d.mesh, d.bvh));
    CHECK(d.mesh.triangleCount() == smallTris);
    RayHit hit;
    CHECK(d.bvh.raycast(d.mesh, glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f, 0.0f, -1.0f), hit));
    const std::vector<glm::vec3> small = d.mesh.positions;
    // The kept tree isn't counted against the byte budget: a stroke after the remesh doesn't push its step out.
    d.undo.byteBudget = d.undo.lastUndo()->budgetBytes() + (size_t(4) << 20);
    CHECK(d.undo.lastUndo()->approxBytes() > d.undo.lastUndo()->budgetBytes());
    strokeOnce(d);
    CHECK(d.undo.undoCount() == 2);
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(d.mesh.positions == small);
}

// Measurement only (SF_UNDO_DENSE_TRIS="4000000,8000000"): prints where an undo goes.
TEST(undo_dense_measure) {
    const char* env = std::getenv("SF_UNDO_DENSE_TRIS");
    if (!env) return;
    std::string list = env;
    for (size_t pos = 0; pos < list.size();) {
        size_t e = list.find(',', pos);
        if (e == std::string::npos) e = list.size();
        const size_t tris = std::stoull(list.substr(pos, e - pos));
        pos = e + 1;
        auto t = std::chrono::steady_clock::now();
        Document d = makeDoc(makeBenchSphere(tris));
        std::printf("[undo_dense] %zu tris: made + bvh %.0f ms\n", d.mesh.triangleCount(), msSince(t));
        {
            UploadOrder order;
            t = std::chrono::steady_clock::now();
            order.build(d.mesh);
            std::printf("[undo_dense]   renderer layout (UploadOrder::build, the CPU part of a full upload) %.0f ms\n", msSince(t));
        }
        auto tl = Timeline::attach(d);
        for (const float radius : {0.15f, 0.5f, 1.2f}) {
        strokeOnce(d, radius);
        t = std::chrono::steady_clock::now();
        d.undo.undo(d.mesh, d.bvh);
        const double u = msSince(t);
        t = std::chrono::steady_clock::now();
        d.undo.redo(d.mesh, d.bvh);
        std::printf("[undo_dense]   r=%.2f: %zu verts, undo %.1f ms redo %.1f ms\n", radius, d.undo.lastUndo()->positions.indices.size(), u, msSince(t));
        }
        strokeOnce(d);
        std::printf("[undo_dense]   stroke touched %zu verts, step %zu KB\n", d.undo.lastUndo()->positions.indices.size(),
                    d.undo.undoBytes() / 1024);
        t = std::chrono::steady_clock::now();
        d.undo.undo(d.mesh, d.bvh);
        std::printf("[undo_dense]   stroke undo %.1f ms\n", msSince(t));
        t = std::chrono::steady_clock::now();
        d.undo.redo(d.mesh, d.bvh);
        std::printf("[undo_dense]   stroke redo %.1f ms\n", msSince(t));
        t = std::chrono::steady_clock::now();
        d.replaceMeshUndoable(makeBenchSphere(tris), "Remesh");
        std::printf("[undo_dense]   replace (make + bvh) %.0f ms, undo bytes %zu MB\n", msSince(t), d.undo.undoBytes() >> 20);
        t = std::chrono::steady_clock::now();
        d.undo.undo(d.mesh, d.bvh);
        std::printf("[undo_dense]   remesh undo %.0f ms\n", msSince(t));
        t = std::chrono::steady_clock::now();
        d.undo.redo(d.mesh, d.bvh);
        std::printf("[undo_dense]   remesh redo %.0f ms\n", msSince(t));
        strokeOnce(d);
        t = std::chrono::steady_clock::now();
        d.undo.undo(d.mesh, d.bvh);
        std::printf("[undo_dense]   stroke undo after remesh %.1f ms (rss %zu MB)\n", msSince(t), residentBytes() >> 20);
    }
}

// Measurement only (SF_UNDO_DENSE_TRIS): undo / redo of each brush's stroke, with and without a sculpt layer, then the
// whole-attribute steps (mask, paint, hide), on a mesh of that size.
TEST(undo_dense_matrix) {
    const char* env = std::getenv("SF_UNDO_DENSE_TRIS");
    if (!env) return;
    const size_t tris = std::stoull(env);
    for (const bool layer : {false, true}) {
        Document d = makeDoc(makeBenchSphere(tris));
        auto tl = Timeline::attach(d);
        if (layer) {
            d.mesh.addLayer("L");
            d.activeLayer = 0;
        }
        std::printf("[undo_dense_matrix] %zu tris, sculpt layer %d\n", d.mesh.triangleCount(), int(layer));
        for (int b = 0; b < int(BrushType::Count); ++b) {
            const BrushType type = BrushType(b);
            if (type == BrushType::Cloth || type == BrushType::Vdm || type == BrushType::DetailClone || type == BrushType::Morph) continue;
            d.sculptor.beginStroke(d.mesh, brushName(type));
            const glm::vec3 dir = glm::normalize(glm::vec3(0.4f, 0.3f, 1.0f));
            for (int i = 0; i < 4; ++i) {
                DabParams p;
                p.center = dir;
                p.viewDir = -dir;
                if (RayHit hit; d.bvh.raycast(d.mesh, dir * 4.0f, -dir, hit)) p.center = hit.position;
                p.center += glm::vec3(0.02f * float(i), 0.0f, 0.0f);
                p.radius = 0.2f;
                p.strength = 0.6f;
                p.strokeDelta = glm::vec3(0.02f, 0.0f, 0.0f);
                d.sculptor.dab(d.mesh, d.bvh, type, p, Symmetry{});
            }
            auto t = std::chrono::steady_clock::now();
            d.undo.push(d.sculptor.endStroke());
            const double endMs = msSince(t);
            const UndoRecord* r = d.undo.lastUndo();
            const size_t n = r ? r->positions.indices.size() : 0;
            t = std::chrono::steady_clock::now();
            d.undo.undo(d.mesh, d.bvh);
            const double u = msSince(t);
            t = std::chrono::steady_clock::now();
            d.undo.redo(d.mesh, d.bvh);
            std::printf("[undo_dense_matrix]   %-12s %7zu verts  push %6.1f  undo %6.1f  redo %6.1f ms\n", brushName(type), n, endMs, u, msSince(t));
        }
        auto time = [&](const char* what, UndoRecord r) {
            auto t = std::chrono::steady_clock::now();
            d.undo.push(std::move(r));
            const double p = msSince(t);
            t = std::chrono::steady_clock::now();
            d.undo.undo(d.mesh, d.bvh);
            const double u = msSince(t);
            t = std::chrono::steady_clock::now();
            d.undo.redo(d.mesh, d.bvh);
            std::printf("[undo_dense_matrix]   %-12s push %6.1f  undo %6.1f  redo %6.1f ms\n", what, p, u, msSince(t));
        };
        time("mask all", UndoRecord::captureMask(d.mesh, "mask"));
        time("paint all", UndoRecord::captureColors(d.mesh, "paint"));
        time("hide all", UndoRecord::captureHidden(d.mesh, "hide"));
        time("groups all", UndoRecord::captureFaceGroups(d.mesh, "groups"));
        // Twenty strokes in a row, then every one undone.
        for (int i = 0; i < 20; ++i) strokeOnce(d, 0.1f + 0.01f * float(i));
        auto t = std::chrono::steady_clock::now();
        int n = 0;
        while (d.undo.undoCount() > 0 && n < 20) n += d.undo.undo(d.mesh, d.bvh) ? 1 : 0;
        std::printf("[undo_dense_matrix]   20 strokes undone in %.1f ms (undo history %zu MB)\n", msSince(t), d.undo.undoBytes() >> 20);
    }
}

// Measurement only (SF_UNDO_DENSE_ICO=8 : an icosphere of 20 * 4^n triangles): dynamic topology strokes.
TEST(undo_dense_dyntopo_measure) {
    const char* env = std::getenv("SF_UNDO_DENSE_ICO");
    if (!env) return;
    Document d = makeDoc(makeIcosphere(std::atoi(env)));
    auto tl = Timeline::attach(d);
    d.sculptor.dyntopo.enabled = true;
    d.sculptor.dyntopo.detail = 0.5f;
    std::printf("[undo_dense_dyn] %zu tris\n", d.mesh.triangleCount());
    for (const float radius : {0.1f, 0.3f}) {
        auto t = std::chrono::steady_clock::now();
        strokeOnce(d, radius);
        const double strokeMs = msSince(t);
        const UndoRecord* r = d.undo.lastUndo();
        const size_t slots = r->topology ? r->topology->triangleSlots.size() : 0;
        t = std::chrono::steady_clock::now();
        d.undo.undo(d.mesh, d.bvh);
        const double u = msSince(t);
        t = std::chrono::steady_clock::now();
        d.undo.redo(d.mesh, d.bvh);
        std::printf("[undo_dense_dyn]   r=%.1f: stroke %.0f ms, topology step %s (%zu tri slots), undo %.1f ms redo %.1f ms, now %zu tris\n",
                    radius, strokeMs, r->topology ? "yes" : "no", slots, u, msSince(t), d.mesh.triangleCount());
    }
}

namespace {

// The tree `d.bvh` answers like a fresh one over the same mesh: rays from many sides, and a vertex query.
bool treeMatchesFresh(Document& d) {
    BVH fresh;
    fresh.build(d.mesh);
    for (int i = 0; i < 60; ++i) {
        const float a = float(i) * 0.7f, b = float(i % 7) * 0.45f - 1.2f;
        const glm::vec3 n = glm::normalize(glm::vec3(std::cos(a), b, std::sin(a)));
        RayHit h1, h2;
        const bool r1 = d.bvh.raycast(d.mesh, n * 4.0f, -n, h1), r2 = fresh.raycast(d.mesh, n * 4.0f, -n, h2);
        if (r1 != r2 || (r1 && std::abs(h1.t - h2.t) > 1e-5f)) return false;
    }
    for (int i = 0; i < 12; ++i) {
        const glm::vec3 c = glm::normalize(glm::vec3(std::cos(float(i)), 0.3f * float(i % 5) - 0.6f, std::sin(float(i)))) * 0.95f;
        std::vector<uint32_t> a, b;
        d.bvh.queryVertices(d.mesh, c, 0.25f, a);
        fresh.queryVertices(d.mesh, c, 0.25f, b);
        if (a != b) return false;
    }
    return d.bvh.triangleCount() == d.mesh.triangleCount();
}

} // namespace

// Undoing a dynamic-topology stroke updates the tree for the slots the stroke changed instead of building it again
// (a rebuild was most of a 115 ms undo at 1.3M triangles); the tree stays exact through undo / redo cycles.
TEST(undo_dense_dyntopo_tree_stays_exact) {
    Document d = makeDoc(makeIcosphere(6)); // 81920 triangles
    auto tl = Timeline::attach(d);
    d.sculptor.dyntopo.enabled = true;
    d.sculptor.dyntopo.detail = 0.6f;
    const std::vector<glm::vec3> start = d.mesh.positions;
    const size_t startTris = d.mesh.triangleCount();
    for (int s = 0; s < 4; ++s) strokeOnce(d, 0.2f + 0.08f * float(s));
    CHECK(d.mesh.triangleCount() != startTris);
    CHECK(treeMatchesFresh(d));
    std::vector<size_t> tris;
    while (d.undo.canUndo()) {
        tris.push_back(d.mesh.triangleCount());
        CHECK(d.undo.undo(d.mesh, d.bvh));
        CHECK(treeMatchesFresh(d));
    }
    CHECK(d.mesh.triangleCount() == startTris);
    CHECK(d.mesh.positions == start);
    for (size_t i = tris.size(); i-- > 0;) {
        CHECK(d.undo.redo(d.mesh, d.bvh));
        CHECK(d.mesh.triangleCount() == tris[i]);
        CHECK(treeMatchesFresh(d));
    }
    // and sculpting on after the undo still finds the surface
    CHECK(d.undo.undo(d.mesh, d.bvh));
    strokeOnce(d, 0.3f);
    CHECK(treeMatchesFresh(d));
}
