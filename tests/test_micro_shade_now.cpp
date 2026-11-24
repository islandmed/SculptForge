// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// gh#184 (0.13.10): on a streamed Micro level a Clay stroke's tiles get their shading within a per-frame budget, and
// Undo, Redo and Move went through the same budget, so they showed stale shading for seconds. The renderer reads two
// flags of the level (MicroMesh::grabbing, shadeNow) to shade those at once: a Move sets grabbing while it runs and
// shadeNow at its end, Undo and Redo set shadeNow, a Clay stroke sets neither, and the renderer's clearDirty clears
// shadeNow. Geometric invariant beside it: Undo puts every grid point back bit-identical, Redo brings the stroke back.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Commands.h"
#include "core/Micro.h"

#include <vector>

using namespace sf;

namespace {

DabParams dabAt(const glm::vec3& at, float radius) {
    DabParams p;
    p.center = glm::normalize(at), p.radius = radius, p.strength = 0.8f, p.viewDir = -p.center;
    return p;
}

} // namespace

TEST(micro_shade_now_invariant_undo_redo_exact_and_flags) {
    Document doc = makeDoc(makeBenchSphere(20000));
    std::string err;
    CHECK(microDivide(doc.mesh, 4, err));
    Sculptor& sc = doc.sculptor;
    sc.microMoveAlongNormals = true; // (the grid's own Move: MicroStroke::beginGrab)
    Symmetry none;
    none.x = false;
    doc.mesh.micro->clearDirty();
    const std::vector<int16_t> start = doc.mesh.micro->disp.toVector();

    // Clay: budgeted, so neither flag.
    CHECK(sc.beginStroke(doc.mesh, "Clay"));
    for (int i = 0; i < 4; ++i) sc.dab(doc.mesh, doc.bvh, BrushType::Clay, dabAt(glm::vec3(0.1f * float(i), 0.0f, 1.0f), 0.2f), none);
    CHECK(!doc.mesh.micro->grabbing);
    doc.undo.push(sc.endStroke());
    CHECK(!doc.mesh.micro->grabbing && !doc.mesh.micro->shadeNow);
    const std::vector<int16_t> clay = doc.mesh.micro->disp.toVector();
    CHECK(clay != start);

    // Move: grabbing while it runs, shadeNow once it ends.
    CHECK(sc.beginStroke(doc.mesh, "Move"));
    sc.beginGrab(doc.mesh, doc.bvh, dabAt(glm::vec3(0.0f, 0.0f, 1.0f), 0.3f), none);
    CHECK(doc.mesh.micro->grabbing);
    for (int i = 0; i < 20; ++i) sc.grab(doc.mesh, doc.bvh, glm::vec3(0.0f, 0.0f, 0.003f));
    doc.undo.push(sc.endStroke());
    CHECK(!doc.mesh.micro->grabbing && doc.mesh.micro->shadeNow);
    doc.mesh.micro->clearDirty();
    CHECK(!doc.mesh.micro->shadeNow);
    const std::vector<int16_t> moved = doc.mesh.micro->disp.toVector();
    CHECK(moved != clay);

    // Undo / Redo: shadeNow each time; the grid exactly as it was.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->shadeNow);
    CHECK(doc.mesh.micro->disp.toVector() == clay);
    doc.mesh.micro->clearDirty();
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->shadeNow);
    CHECK(doc.mesh.micro->disp.toVector() == start);
    doc.mesh.micro->clearDirty();
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->shadeNow);
    CHECK(doc.mesh.micro->disp.toVector() == clay);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp.toVector() == moved);
}
