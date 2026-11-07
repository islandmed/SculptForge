// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// No whole-mesh copies on the stroke path (perf/stroke-no-whole-copy): what a dab and the frames after a stroke
// copy follows the brush's region (or a fixed slice), not the mesh; undo stays exact on a level-6 Part.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Timeline.h"

#include <random>

using namespace sf;

namespace {

void stroke(Document& d, glm::vec3 a, glm::vec3 b, float radius, int dabs = 4, Symmetry sym = {}) {
    d.sculptor.beginStroke(d.mesh, "Standard");
    for (int i = 0; i < dabs; ++i) {
        DabParams p;
        const glm::vec3 dir = glm::normalize(glm::mix(a, b, dabs > 1 ? float(i) / float(dabs - 1) : 0.0f));
        p.center = dir;
        p.viewDir = -dir;
        if (RayHit hit; d.bvh.raycast(d.mesh, dir * 4.0f, -dir, hit)) p.center = hit.position;
        p.radius = radius;
        p.strength = 0.6f;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, sym);
    }
    d.undo.push(d.sculptor.endStroke());
}

float maxDiff(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return 1e30f;
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, glm::length(a[i] - b[i]));
    return m;
}

} // namespace

// finishDab listed every vertex of the region beside its leaves (a copy of the region each dab: 10 MB at 240 px
// on Lucy 28M), though only remeshing reads them. Without it the normals still come out exact, mirrors included.
TEST(stroke_dab_copies_no_vertex_list) {
    Document d = makeDoc(makeBenchSphere(200000));
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const glm::vec3& q : d.mesh.positions) lo = glm::min(lo, q), hi = glm::max(hi, q);
    const glm::vec3 C = (lo + hi) * 0.5f;
    const float R = (hi.x - lo.x) * 0.5f; // (not a unit sphere at the origin)
    for (const bool mirrored : {false, true}) {
        Symmetry sym;
        sym.x = mirrored;
        d.sculptor.beginStroke(d.mesh, "Standard");
        DabParams p;
        const glm::vec3 dir = glm::normalize(glm::vec3(0.4f, 0.3f, 1.0f));
        p.center = C + dir * R;
        p.viewDir = -dir;
        if (RayHit hit; d.bvh.raycast(d.mesh, C + dir * (4.0f * R), -dir, hit)) p.center = hit.position;
        p.radius = R; // a big region: a quarter of the sphere (~25k of its 100k vertices)
        p.strength = 0.6f;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, sym);
        CHECK(d.sculptor.timings.vertices > 10000);
        CHECK(d.sculptor.dabVerticesListed() == 0);
        d.undo.push(d.sculptor.endStroke());
        d.sculptor.timings = {};
        std::vector<glm::vec3> normals = d.mesh.normals;
        d.mesh.recomputeAllNormals();
        CHECK(maxDiff(normals, d.mesh.normals) < 1e-5f);
    }
}

// A checkpoint copied all the positions in the frame after the stroke that made it (every 16th: 168 MB on Lucy
// 28M). Now each sync copies at most checkpointSlice vertices, and a change before it is done drops it.
TEST(stroke_timeline_checkpoint_in_slices) {
    Document d = makeDoc(makeIcosphere(5)); // 10242 vertices
    auto t = Timeline::attach(d);
    const size_t V = d.mesh.vertexCount();
    CHECK(t->checkpointCount() == 1); // the start's, one slice for a small mesh
    t->checkpointEvery = 2;
    t->checkpointSlice = V / 4 + 1;
    std::mt19937 rng(11);
    auto dir = [&] {
        std::normal_distribution<float> n;
        return glm::normalize(glm::vec3(n(rng), n(rng), n(rng)));
    };
    std::vector<std::vector<glm::vec3>> states{d.mesh.positions};
    stroke(d, dir(), dir(), 0.3f);
    t->sync(d);
    states.push_back(d.mesh.positions);
    stroke(d, dir(), dir(), 0.3f); // step 2: a checkpoint, a slice a sync
    states.push_back(d.mesh.positions);
    size_t syncs = 0;
    for (size_t before = 1; t->checkpointCount() == 1 && syncs < 10; ++syncs) {
        t->sync(d);
        CHECK(t->checkpointPending() <= before + t->checkpointSlice); // (0 once it is filed)
        before = t->checkpointPending();
    }
    CHECK(t->checkpointCount() == 2);
    CHECK(syncs == 4);
    // Steps 3 and 4: 4 begins one; a stroke before it is done drops it (step 5 makes none).
    stroke(d, dir(), dir(), 0.3f);
    t->sync(d);
    states.push_back(d.mesh.positions);
    stroke(d, dir(), dir(), 0.3f);
    t->sync(d);
    states.push_back(d.mesh.positions);
    CHECK(t->checkpointPending() > 0 && t->checkpointPending() < V);
    stroke(d, dir(), dir(), 0.3f);
    t->sync(d);
    states.push_back(d.mesh.positions);
    CHECK(t->checkpointPending() == 0);
    CHECK(t->checkpointCount() == 2);
    // Every state comes back exactly, by checkpoint or by walking.
    for (size_t target : {0u, 5u, 2u, 4u, 1u, 3u, 5u}) {
        CHECK(t->seek(d, target));
        CHECK(maxDiff(d.mesh.positions, states[target]) == 0.0f);
    }
}

// Undo and redo on a Part with six subdivision levels: every stroke comes back exactly, at the top level and
// after a stroke on a lower level and stepping back up.
TEST(stroke_undo_round_trip_level_6) {
    Document d = makeDoc(makeIcosphere(1)); // 80 triangles; level 6: 327680
    std::string msg;
    for (int i = 0; i < 6; ++i) CHECK(divideUndoable(d, true, msg));
    CHECK(subdivCurrentLevel(d.mesh) == 6);
    d.undo.clear();
    std::vector<std::vector<glm::vec3>> states{d.mesh.positions};
    stroke(d, {0, 0, 1}, {0, 1, 0}, 0.4f, 6);
    states.push_back(d.mesh.positions);
    Symmetry sym;
    sym.x = true;
    stroke(d, {1, 0, 0}, {0.3f, 0.2f, -1.0f}, 0.6f, 6, sym);
    states.push_back(d.mesh.positions);
    stroke(d, {0, -1, 0}, {0.5f, -0.5f, 0.5f}, 0.25f, 8);
    states.push_back(d.mesh.positions);
    for (int k = 2; k >= 0; --k) {
        CHECK(d.undo.undo(d.mesh, d.bvh));
        CHECK(maxDiff(d.mesh.positions, states[size_t(k)]) == 0.0f);
    }
    for (int k = 1; k <= 3; ++k) {
        CHECK(d.undo.redo(d.mesh, d.bvh));
        CHECK(maxDiff(d.mesh.positions, states[size_t(k)]) == 0.0f);
    }
    // Down to level 3, a stroke there, back up to 6; then all of it undone.
    CHECK(setSubdivLevelUndoable(d, 3, msg));
    const std::vector<glm::vec3> low = d.mesh.positions;
    stroke(d, {0, 0, 1}, {1, 0, 0}, 0.5f, 4);
    const std::vector<glm::vec3> lowAfter = d.mesh.positions;
    CHECK(maxDiff(low, lowAfter) > 0.0f);
    CHECK(setSubdivLevelUndoable(d, 6, msg));
    CHECK(d.mesh.vertexCount() == states[3].size());
    CHECK(maxDiff(d.mesh.positions, states[3]) > 0.0f); // the low stroke shows at the top
    CHECK(d.undo.undo(d.mesh, d.bvh)); // up
    CHECK(maxDiff(d.mesh.positions, lowAfter) == 0.0f);
    CHECK(d.undo.undo(d.mesh, d.bvh)); // the low stroke
    CHECK(maxDiff(d.mesh.positions, low) == 0.0f);
    CHECK(d.undo.undo(d.mesh, d.bvh)); // down
    CHECK(subdivCurrentLevel(d.mesh) == 6);
    CHECK(maxDiff(d.mesh.positions, states[3]) == 0.0f);
    std::vector<glm::vec3> normals = d.mesh.normals;
    d.mesh.recomputeAllNormals();
    CHECK(maxDiff(normals, d.mesh.normals) < 1e-5f);
}
