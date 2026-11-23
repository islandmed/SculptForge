// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// gh#184 (0.13.10 freeze): on a 2.6M-triangle base with a streamed Micro level 4, the release of every stroke held the
// frame for ~0.5-1 s and the first Draw stroke once for ~1.3 s.
// - The stroke's undo journal is packed on a background thread (MicroStroke::end, MicroUndo::pending): undo and redo
//   right after the release, and a second stroke over the same tiles before the packing ends, are still exact.
// - The height range growing (MicroMesh::widen) keeps the surface: the renderer re-sends heights only and keeps the
//   shading (MicroFocus::resendHeights), so it must not mark everything dirty.
// - Merge by Distance re-counted every base point after each micro stroke: UndoStack::baseRevision skips micro-only steps.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Micro.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace sf;

namespace {

DabParams dabAt(const glm::vec3& at, float radius, float strength = 0.8f) {
    DabParams p;
    p.center = glm::normalize(at), p.radius = radius, p.strength = strength, p.viewDir = -p.center;
    return p;
}

Document microDoc() {
    Document doc = makeDoc(makeBenchSphere(20000));
    std::string err;
    CHECK(microDivide(doc.mesh, 4, err));
    return doc;
}

UndoRecord stroke(Document& doc, float x, int dabs = 6, float strength = 0.8f) {
    Symmetry none;
    none.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < dabs; ++i)
        doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(x + 0.05f * float(i), 0.0f, 1.0f), 0.25f, strength), none);
    return doc.sculptor.endStroke();
}

} // namespace

TEST(micro_freeze_invariant_undo_exact_while_packing) {
    Document doc = microDoc();
    const std::vector<int16_t> start = doc.mesh.micro->disp.toVector();
    UndoRecord a = stroke(doc, 0.0f);
    CHECK(a.micro && a.micro->pending); // (packed off the release: tris final, the journal pending)
    CHECK(!a.micro->tris.empty() && a.micro->tris.size() == a.micro->keys.size());
    CHECK(a.micro->bytes() > 0);
    doc.undo.push(std::move(a));
    const std::vector<int16_t> afterA = doc.mesh.micro->disp.toVector();
    CHECK(afterA != start);
    // A second stroke over the same tiles at once (the first may still be packing).
    doc.undo.push(stroke(doc, 0.1f));
    const std::vector<int16_t> afterB = doc.mesh.micro->disp.toVector();
    CHECK(afterB != afterA);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp.toVector() == afterA);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp.toVector() == start);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp.toVector() == afterA);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp.toVector() == afterB);
}

TEST(micro_freeze_undo_right_after_release_and_a_dropped_step) {
    Document doc = microDoc();
    const std::vector<int16_t> start = doc.mesh.micro->disp.toVector();
    doc.undo.push(stroke(doc, 0.0f, 10));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh)); // (no wait in between: swapWith settles)
    CHECK(doc.mesh.micro->disp.toVector() == start);
    // A step dropped while its packing runs (a quit right after the release): nothing outlives it.
    { UndoRecord r = stroke(doc, -0.2f, 10); }
    Document other = microDoc();
    { UndoRecord r = stroke(other, 0.3f, 10); other.undo.clear(); }
}

TEST(micro_freeze_invariant_widen_keeps_the_surface) {
    Document doc = microDoc();
    MicroMesh& mm = *doc.mesh.micro;
    doc.undo.push(stroke(doc, 0.0f, 4, 0.3f));
    mm.clearDirty();
    const std::vector<int16_t> before = mm.disp.toVector();
    const float stepBefore = mm.step;
    const int div = mm.widen(mm.rangeWorld() * 3.0f);
    CHECK(div > 1);
    CHECK(mm.rescaled && !mm.allDirty); // (the renderer re-sends heights and keeps the shading)
    // Every point within one new step of where it was: the same surface.
    const std::vector<int16_t> after = mm.disp.toVector();
    float worst = 0.0f;
    for (size_t i = 0; i < before.size(); ++i) worst = std::max(worst, std::abs(float(after[i]) * mm.step - float(before[i]) * stepBefore));
    CHECK(worst <= mm.step);
}

TEST(undo_base_revision_skips_micro_only_steps) {
    Document doc = microDoc();
    const uint64_t base0 = doc.undo.baseRevision(), rev0 = doc.undo.revision();
    doc.undo.push(stroke(doc, 0.0f));
    CHECK(doc.undo.revision() != rev0);
    CHECK(doc.undo.baseRevision() == base0);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.undo.baseRevision() == base0);
    UndoRecord moved;
    moved.positions.indices = {0};
    moved.positions.values = {doc.mesh.positions[0]};
    doc.undo.push(std::move(moved));
    CHECK(doc.undo.baseRevision() != base0);
}

// A big Clay stroke moves the base under its micro level, and its last queued dabs land in endStroke, before the level
// stops stroking: the renderer saw a base moved by "anything but a stroke" and shaded the stroke's whole backlog in the
// release frame (core-800m: ~500k triangles, a 2-3 s frame, +1.9 GB kept). The release says the move is the stroke's.
TEST(micro_freeze_stroke_release_base_move_is_the_strokes) {
    Document doc = microDoc();
    Symmetry none;
    none.x = false;
    doc.mesh.clearDirty();
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Clay"));
    for (int i = 0; i < 6; ++i)
        doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Clay, dabAt(glm::vec3(0.1f * float(i), 0.0f, 1.0f), 0.9f), none);
    doc.undo.push(doc.sculptor.endStroke());
    const MicroMesh& mm = *doc.mesh.micro;
    CHECK(doc.mesh.dirtyBegin < doc.mesh.dirtyEnd); // (the base moved: the big-brush base kernel)
    CHECK(!mm.stroking);
    CHECK(mm.strokeEnded);
    doc.mesh.micro->clearDirty(); // (the renderer's sync)
    CHECK(!mm.strokeEnded);
}
