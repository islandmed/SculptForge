// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fused batches (Sculptor::beginBatch, core/SculptBatch.cpp) are batch-boundary invariant: one recorded dab list gives
// the same surface, bit for bit, whether the solver placed it a dab at a time, in batches of 2, 8 or 32, in one batch,
// or in batches of whatever size the clock gave it (the app sizes them to the measured time, so a stroke must not
// depend on it). Found by the S8 parity run: with Fast Big Brushes on, the same list left ~9.9M of 14M vertices
// different from run to run.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BrushRecipe.h"
#include "core/Sculpt.h"
#include "core/Sha256.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace sf;

namespace {

const Mesh& testHead() {
    static const Mesh head = makeBenchHead(1'500'000); // ~750k vertices: a 1.1 radius holds well over kAdaptiveMinVertices
    return head;
}

// The dab list: picked on the unmoved head (so every run replays the same centres), 0.15 radius apart.
std::vector<DabParams> dabList(const Mesh& head, float radius, int count, float strength) {
    Document d = test::makeDoc(head);
    std::vector<DabParams> out;
    for (int i = 0; i < count; ++i) {
        DabParams p;
        const float t = -0.5f + float(i) * 0.15f * radius;
        p.center = glm::vec3(t, 0.1f, 1.0f);
        if (RayHit hit; d.bvh.raycast(d.mesh, glm::vec3(t, 0.1f, 5.0f), glm::vec3(0, 0, -1), hit)) {
            p.center = hit.position;
            p.pickNormal = glm::normalize(hit.normal);
        }
        p.viewDir = p.pickDir = glm::vec3(0, 0, -1);
        p.pickedAfter = 0; // (all picked on the unmoved head: each dab's centre is carried along by the dabs before it)
        p.radius = radius;
        p.strength = strength * (0.6f + 0.4f * float(i % 3) / 2.0f); // (varied pressure)
        p.strokeDelta = glm::vec3(0.15f * radius, 0, 0);
        out.push_back(p);
    }
    return out;
}

struct Result {
    std::string positions;
    double perDab = 0.0; // vertices a dab reached
};

// The list placed with `plan`'s batch sizes in turn (0: the whole stroke in one batch; empty: no batches at all, the
// dab-by-dab path), the way the app runs a stroke: catching up the normals behind the brush after each batch.
Result place(const Mesh& head, const std::vector<DabParams>& list, BrushType type, const char* recipe, bool symmetry,
             const std::vector<int>& plan) {
    Document d = test::makeDoc(head);
    d.sculptor.adaptiveDabs = true;
    d.sculptor.beginStroke(d.mesh, recipe ? recipe : brushName(type));
    const BrushRecipe* r = nullptr;
    BrushRecipe found;
    if (recipe)
        for (const BrushRecipe& b : builtinBrushRecipes())
            if (b.name == recipe) found = b, r = &found;
    const Symmetry sym{symmetry, false, false};
    size_t next = 0, k = 0;
    bool open = false;
    for (size_t i = 0; i < list.size(); ++i) {
        if (!plan.empty() && i == next) {
            if (open) {
                d.sculptor.endBatch(d.mesh, d.bvh);
                d.sculptor.catchUpBehind(d.mesh, d.bvh, 0.35);
            }
            d.sculptor.beginBatch();
            open = true;
            const int size = plan[k++ % plan.size()];
            next = size > 0 ? i + size_t(size) : list.size();
        }
        if (r) d.sculptor.dabRecipe(d.mesh, d.bvh, *r, list[i], sym);
        else d.sculptor.dab(d.mesh, d.bvh, type, list[i], sym);
    }
    if (open) d.sculptor.endBatch(d.mesh, d.bvh);
    d.sculptor.flushDraft(d.mesh, d.bvh);
    Result out;
    out.perDab = double(d.sculptor.timings.vertices) / double(std::max<uint64_t>(1, d.sculptor.timings.dabs));
    d.sculptor.endStroke();
    out.positions = sha256Hex(d.mesh.positions.data(), d.mesh.positions.size() * sizeof(glm::vec3));
    return out;
}

// Every batch plan gives the first one's surface.
void invariant(BrushType type, const char* recipe, bool symmetry, int dabs = 12) {
    const Mesh& head = testHead();
    const std::vector<DabParams> list = dabList(head, 1.1f, dabs, 0.4f);
    const std::vector<std::vector<int>> plans = {{1}, {1}, {2}, {8}, {32}, {0}, {3, 1, 5, 2}, {7, 2, 4, 1, 6}};
    const Result base = place(head, list, type, recipe, symmetry, plans.front());
    const bool fused = recipe == nullptr && type != BrushType::ClayBlock && type != BrushType::Smooth;
    if (fused) CHECK(base.perDab > double(Sculptor::kAdaptiveMinVertices)); // (on the fused path at all)
    for (size_t i = 1; i < plans.size(); ++i) {
        const Result other = place(head, list, type, recipe, symmetry, plans[i]);
        if (other.positions != base.positions)
            std::printf("  %s%s: batches %d... differ from batches of 1 (%.12s vs %.12s)\n", recipe ? recipe : brushName(type),
                        symmetry ? " (X symmetry)" : "", plans[i].front(), other.positions.c_str(), base.positions.c_str());
        CHECK(other.positions == base.positions);
    }
}

} // namespace

TEST(fused_batches_are_invariant_standard) {
    invariant(BrushType::Standard, nullptr, false);
    invariant(BrushType::Standard, nullptr, true);
}
TEST(fused_batches_are_invariant_clay) {
    invariant(BrushType::Clay, nullptr, false);
    invariant(BrushType::Clay, nullptr, true);
}
TEST(fused_batches_are_invariant_flatten_and_creases) {
    invariant(BrushType::Flatten, nullptr, true);
    invariant(BrushType::Crease, nullptr, false);
    invariant(BrushType::Pinch, nullptr, true);
    invariant(BrushType::SharpCrease, nullptr, false);
}
TEST(fused_batches_are_invariant_exact_brushes) {
    // (Clay Block, Crisp Flatten and Smooth always take the exact path: batching must leave them alone.)
    invariant(BrushType::ClayBlock, nullptr, false, 6);
    invariant(BrushType::ClayBlock, nullptr, true, 6);
    invariant(BrushType::Smooth, nullptr, false, 6);
    invariant(BrushType::Standard, "Crisp Flatten", false, 6);
}
