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
    Document d = makeDoc(makeBenchSphere(1000000));
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
