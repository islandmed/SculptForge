// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The undo history's byte budget follows the computer's memory (#202, slice 1): a quarter of the total, never below
// 256 MB, never past the free memory beyond the margin; the oldest steps that don't fit are counted so the status
// line can say so, and undo / redo of what is left restores exactly.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/MemoryGuard.h"
#include "core/Undo.h"

#include <algorithm>

using namespace sf;

namespace {

constexpr size_t kMB = size_t(1) << 20, kGiB = size_t(1) << 30;

// Both memory overrides for the length of a test.
struct Machine {
    Machine(size_t totalBytes, size_t freeBytes) {
        setTotalMemoryOverride(totalBytes);
        setAvailableMemoryOverride(freeBytes);
    }
    ~Machine() {
        setTotalMemoryOverride(0);
        setAvailableMemoryOverride(0);
    }
};

// A whole-mesh step (what Decimate and Divide push): the record keeps the mesh as it was, then every vertex moves by
// `shift`, so each state of the document is told apart by its positions.
void pushWholeMeshStep(Document& d, float shift) {
    UndoRecord r;
    r.label = "Whole mesh";
    r.mesh = std::make_unique<Mesh>(d.mesh);
    for (auto& p : d.mesh.positions) p.x += shift;
    d.undo.push(std::move(r));
}

} // namespace

TEST(undo_budget_follows_total_and_free_memory) {
    const size_t margin = memoryMargin(0);
    CHECK(margin == 512 * kMB);
    // The floor tier: 8 GB with plenty free is a quarter of it; 2 GB free leaves 1.5 GB past the margin.
    CHECK(undoBudgetFor(8 * kGiB, 6 * kGiB, 0) == 2 * kGiB);
    CHECK(undoBudgetFor(8 * kGiB, 2 * kGiB, 0) == 2 * kGiB - margin);
    // A studio box keeps far more than the old fixed 1 GiB.
    CHECK(undoBudgetFor(64 * kGiB, 50 * kGiB, 0) == 16 * kGiB);
    // Little free: the floor, not zero.
    CHECK(undoBudgetFor(8 * kGiB, 600 * kMB, 0) == 256 * kMB);
    CHECK(undoBudgetFor(8 * kGiB, 100 * kMB, 0) == 256 * kMB);
    // Short of the margin the history gives back the shortfall (here 212 MB of its 1 GiB).
    CHECK(undoBudgetFor(8 * kGiB, 300 * kMB, 1 * kGiB) == 1 * kGiB - 212 * kMB);
    // What the history already holds is room it has: free memory counts without it.
    CHECK(undoBudgetFor(8 * kGiB, 600 * kMB, 1 * kGiB) == 1 * kGiB + 600 * kMB - margin);
    // A small machine: a quarter of 2 GB is the floor.
    CHECK(undoBudgetFor(2 * kGiB, 1 * kGiB, 0) == 512 * kMB);
    // Unknown free memory is the fixed 1 GiB of before; an unknown total caps at 1 GiB.
    CHECK(undoBudgetFor(8 * kGiB, 0, 0) == kGiB);
    CHECK(undoBudgetFor(64 * kGiB, 0, 5 * kGiB) == kGiB);
    CHECK(undoBudgetFor(0, 6 * kGiB, 0) == kGiB);
}

TEST(undo_budget_reads_the_machine_figures) {
    UndoStack u;
    {
        Machine floorTier(8 * kGiB, 2 * kGiB); // SF_HW_SIMULATE=floor with SF_AVAILABLE_MEMORY_MB=2048
        CHECK(totalMemoryBytes() == 8 * kGiB);
        CHECK(u.budget() == 2 * kGiB - memoryMargin(0));
    }
    {
        Machine studio(64 * kGiB, 48 * kGiB);
        CHECK(u.budget() == 16 * kGiB);
    }
    u.byteBudget = 123 * kMB; // a fixed budget (Figure Pose's history, tests) wins
    CHECK(u.budget() == 123 * kMB);
}

TEST(undo_budget_drops_oldest_steps_with_a_count_and_restores_exactly) {
    Machine tight(8 * kGiB, 300 * kMB); // 212 MB short of the margin: the budget is the 256 MB floor until the history is bigger
    Document d = makeDoc(makeBenchSphere(150000));
    std::vector<std::vector<glm::vec3>> states{d.mesh.positions}; // [k] = the document after k pushes
    pushWholeMeshStep(d, 0.01f);
    states.push_back(d.mesh.positions);
    const size_t step = d.undo.lastUndo()->budgetBytes();
    CHECK(step > 4 * kMB && step < 200 * kMB);
    CHECK(!d.undo.takeNotice().any());

    const size_t pushes = 2 * (256 * kMB) / step + 4; // well past the floor
    for (size_t i = 2; i <= pushes; ++i) {
        pushWholeMeshStep(d, 0.01f * float(i));
        states.push_back(d.mesh.positions);
    }
    const UndoStack::Notice n = d.undo.takeNotice();
    CHECK(n.dropped > 0);
    CHECK(n.dropped == pushes - d.undo.undoCount()); // every removed step counted
    CHECK(!n.lastStepOver);
    CHECK(d.undo.undoBytes() < pushes * step);
    CHECK(d.undo.droppedTotal() == n.dropped);
    CHECK(!d.undo.takeNotice().any()); // said once

    // Undo all the way: each step lands on the exact state before it, and stops where the history ends.
    const size_t kept = d.undo.undoCount();
    for (size_t i = 0; i < kept; ++i) CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(!d.undo.canUndo());
    CHECK(d.mesh.positions == states[pushes - kept]);
    for (size_t i = 0; i < kept; ++i) CHECK(d.undo.redo(d.mesh, d.bvh));
    CHECK(d.mesh.positions == states[pushes]);
    CHECK(!d.undo.takeNotice().any()); // undo and redo drop nothing
}

TEST(undo_budget_follows_free_memory_as_steps_are_pushed) {
    Document d = makeDoc(makeBenchSphere(150000));
    Machine roomy(64 * kGiB, 40 * kGiB);
    pushWholeMeshStep(d, 0.01f);
    const size_t step = d.undo.lastUndo()->budgetBytes();
    const size_t n = 400 * kMB / step + 1; // about 400 MB of history, all of it kept while memory is free
    for (size_t i = 2; i <= n; ++i) pushWholeMeshStep(d, 0.01f * float(i));
    CHECK(d.undo.undoCount() == n);
    CHECK(!d.undo.takeNotice().any());
    // Other programs take the memory: 100 MB free is 412 MB short of the margin. The next push gives that back, down to the floor.
    setAvailableMemoryOverride(100 * kMB);
    pushWholeMeshStep(d, 1.0f);
    const UndoStack::Notice note = d.undo.takeNotice();
    CHECK(note.dropped > 0);
    CHECK(d.undo.undoBytes() <= 256 * kMB);
    CHECK(d.undo.canUndo());
}

TEST(undo_budget_keeps_a_whole_mesh_step_that_alone_is_over) {
    Document d = makeDoc(makeBenchSphere(60000));
    d.undo.byteBudget = 1 * kMB;
    const std::vector<glm::vec3> start = d.mesh.positions;
    pushWholeMeshStep(d, 0.1f);
    CHECK(d.undo.undoCount() == 1); // kept, though it is far over
    UndoStack::Notice n = d.undo.takeNotice();
    CHECK(n.dropped == 0 && n.lastStepOver);
    pushWholeMeshStep(d, 0.2f);
    n = d.undo.takeNotice();
    CHECK(d.undo.undoCount() == 1);
    CHECK(n.dropped == 1);
    CHECK(n.lastStepOver);
    pushWholeMeshStep(d, 0.3f);
    n = d.undo.takeNotice();
    CHECK(n.dropped == 1);
    CHECK(d.undo.undo(d.mesh, d.bvh)); // the last step still undoes exactly
    std::vector<glm::vec3> before = start;
    for (auto& p : before) p.x += 0.1f;
    for (auto& p : before) p.x += 0.2f; // (the state after two pushes)
    CHECK(d.mesh.positions == before);
}

// Three Parts: their histories share one budget (the floor tier's 8 GB with little free), so together they hold what one
// history may, the oldest step of any Part goes first, and each Part still undoes exactly.
TEST(undo_budget_is_one_pool_for_all_parts) {
    Machine tight(8 * kGiB, 300 * kMB); // the 256 MB floor
    Document d = makeDoc(makeBenchSphere(150000));
    d.addPart(makeBenchSphere(150000), "B");
    d.addPart(makeBenchSphere(150000), "C");
    CHECK(d.partCount() == 3);
    pushWholeMeshStep(d, 0.01f); // (part 2 is active: measure one step)
    const size_t step = d.undo.lastUndo()->budgetBytes();
    d.undo.clear();
    CHECK(step > 4 * kMB && step < 100 * kMB);

    std::vector<std::vector<std::vector<glm::vec3>>> states(3); // [part][k] = the Part after k of its pushes
    for (int p = 0; p < 3; ++p) {
        d.selectPart(p);
        states[size_t(p)].push_back(d.mesh.positions);
    }
    const size_t rounds = (2 * 256 * kMB) / step / 3 + 3; // twice the pool, in turns across the Parts
    for (size_t k = 1; k <= rounds; ++k)
        for (int p = 0; p < 3; ++p) {
            d.selectPart(p);
            pushWholeMeshStep(d, 0.01f * float(k) + 0.001f * float(p));
            states[size_t(p)].push_back(d.mesh.positions);
        }
    size_t held = 0, dropped = 0;
    std::vector<size_t> kept(3);
    for (int p = 0; p < 3; ++p) {
        const UndoStack& u = d.partUndo(p);
        held += u.undoBytes();
        kept[size_t(p)] = u.undoCount();
        dropped += d.partUndo(p).takeNotice().dropped;
    }
    CHECK(held <= 256 * kMB);                      // one pool, not three
    CHECK(dropped == 3 * rounds - (kept[0] + kept[1] + kept[2])); // every removed step counted, on whichever Part lost it
    CHECK(dropped > 0);
    // The oldest went first, in turn: the Parts' remaining histories differ by at most one step.
    const auto [lo, hi] = std::minmax_element(kept.begin(), kept.end());
    CHECK(*hi - *lo <= 1);
    // Each Part undoes all the way to the exact state it had before its oldest kept step.
    for (int p = 0; p < 3; ++p) {
        d.selectPart(p);
        const size_t n = d.undo.undoCount();
        for (size_t i = 0; i < n; ++i) CHECK(d.undo.undo(d.mesh, d.bvh));
        CHECK(d.mesh.positions == states[size_t(p)][rounds - n]);
    }
}
