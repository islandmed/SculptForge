// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Masks on micro levels, for every brush that moves the surface (a user report: eyes masked on a Micro 3 sphere,
// Move still pushed them). Geometric invariants (GeomInvariants.h): a fully masked sample never moves (bit-identical),
// a partly masked one moves at most (1 - mask) of what it would unmasked; the same for the mask the base mesh carries.
#include "TestHarness.h"

#include "GeomInvariants.h"

#include "core/Benchmark.h"
#include "core/Micro.h"

#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

Document microDoc(int level, size_t baseTriangles = 20000) {
    Document doc = makeDoc(makeBenchSphere(baseTriangles));
    std::string err;
    CHECK(microDivide(doc.mesh, level, err));
    return doc;
}

const glm::vec3 kFront(0.0f, 0.0f, 1.0f);
// A grab's five steps each round the 16-bit displacement (half a step either way, the masked and the free run apart).
constexpr float kQuantisation = 6.0f;

DabParams dabAt(const glm::vec3& c, float r, float strength = 1.0f) {
    DabParams p;
    p.center = c;
    p.radius = r;
    p.strength = strength;
    p.viewDir = glm::vec3(0, 0, -1);
    return p;
}

// A patch in front, fully masked at its core and ramping off: two overlapping dabs.
void maskFront(Document& doc) {
    Symmetry sym;
    sym.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Mask"));
    for (int i = 0; i < 3; ++i) doc.sculptor.dabMask(doc.mesh, doc.bvh, dabAt(kFront, 0.22f), sym);
    doc.sculptor.dabMask(doc.mesh, doc.bvh, dabAt(glm::normalize(glm::vec3(0.2f, 0.0f, 1.0f)), 0.3f, 0.5f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(doc.mesh.micro->hasMask());
}

// One stroke of `type` across the masked patch, the way the app drives it: Move is a grab (anchored at the start),
// every other brush a row of dabs with the pen's travel in strokeDelta.
void stroke(Document& doc, BrushType type) {
    Symmetry sym;
    sym.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, brushName(type)));
    if (type == BrushType::Move) {
        doc.sculptor.beginGrab(doc.mesh, doc.bvh, dabAt(kFront, 0.4f), sym);
        for (int i = 0; i < 5; ++i) doc.sculptor.grab(doc.mesh, doc.bvh, glm::vec3(0.03f, 0.0f, 0.02f));
    } else {
        for (int k = 0; k < 9; ++k) {
            const float x = -0.2f + 0.05f * float(k);
            DabParams p = dabAt(glm::normalize(glm::vec3(x, 0.0f, 1.0f)), 0.3f);
            p.strokeDelta = glm::vec3(0.05f, 0.0f, 0.0f);
            doc.sculptor.dab(doc.mesh, doc.bvh, type, p, sym);
        }
    }
    doc.undo.push(doc.sculptor.endStroke());
}

bool finite(const geom::MicroSnapshot& s) {
    for (const glm::vec3& p : s.points)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    return true;
}

// The brushes the stroke() driver runs on a micro level (Cloth simulates vertices and Morph needs grid shapes: neither
// does anything there, which the sweep also holds: they must not move a masked sample either).
std::vector<BrushType> allBrushes() {
    std::vector<BrushType> v;
    for (int t = 0; t < int(BrushType::Count); ++t) v.push_back(BrushType(t));
    return v;
}

} // namespace

TEST(micro_mask_every_brush_holds_masked_samples) {
    // The sweep: every brush in the table, whatever path it takes (the grid, the base cage below it, a Kelvinlet), at
    // micro levels 1..3. Grab-type brushes (Move, Snake Hook, Nudge, the Kelvinlets, Detail Clone, Vdm) and the rest alike.
    for (int level : {1, 2, 3}) {
        for (BrushType type : allBrushes()) {
            Document doc = microDoc(level);
            maskFront(doc);
            const geom::MicroSnapshot before = geom::microSnapshot(doc.mesh, kFront);
            stroke(doc, type);
            const geom::MicroSnapshot after = geom::microSnapshot(doc.mesh, kFront);
            size_t full = 0;
            const size_t moved = geom::maskedMoved(doc.mesh, before, after, &full);
            std::printf("  micro %d  %-12s fully masked samples %zu, moved %zu\n", level, brushName(type), full, moved);
            CHECK(full > 0);
            CHECK(finite(after));
            if (moved != 0) std::printf("    FAIL: %s level %d moved %zu masked samples\n", brushName(type), level, moved);
            CHECK(moved == 0);
        }
    }
}

TEST(micro_mask_stroke_still_moves_the_free_surface) {
    // The mask holds a patch, not the whole mesh: the brushes that work on micro levels (and Move, and the Kelvinlets)
    // still reshape the unmasked surface beside it, so a fix can't pass by doing nothing. (The Kelvin brushes: below.)
    for (BrushType type : {BrushType::Standard, BrushType::Clay, BrushType::Inflate, BrushType::Move}) {
        Document doc = microDoc(3);
        maskFront(doc);
        const geom::MicroSnapshot before = geom::microSnapshot(doc.mesh, kFront);
        stroke(doc, type);
        const geom::MicroSnapshot after = geom::microSnapshot(doc.mesh, kFront);
        const MicroMesh& mm = *doc.mesh.micro;
        size_t freeMoved = 0;
        for (size_t k = 0; k < before.tris.size(); ++k)
            for (uint32_t s = 0; s < mm.samples; ++s)
                if (mm.mask[size_t(before.tris[k]) * mm.samples + s] == 0 &&
                    glm::distance(before.points[k * mm.samples + s], after.points[k * mm.samples + s]) > 1e-4f)
                    ++freeMoved;
        std::printf("  %-12s free samples moved %zu\n", brushName(type), freeMoved);
        CHECK(freeMoved > 0);
    }
}

TEST(micro_mask_move_scales_partial_masks) {
    // Move on a masked micro level: the move at a partly masked sample is at most (1 - mask) of the unmasked move, both
    // runs along the same rule (Move along normals is the grid's, which scales per sample).
    Document masked = microDoc(3), free = microDoc(3);
    masked.sculptor.microMoveAlongNormals = free.sculptor.microMoveAlongNormals = true;
    maskFront(masked);
    const geom::MicroSnapshot before = geom::microSnapshot(masked.mesh, kFront);
    stroke(free, BrushType::Move);
    stroke(masked, BrushType::Move);
    const geom::MicroSnapshot ref = geom::microSnapshot(free.mesh, kFront), after = geom::microSnapshot(masked.mesh, kFront);
    int atMask = -1;
    const float over = geom::maskedOvershoot(masked.mesh, before, after, ref, &atMask);
    std::printf("  Move along normals: worst excess over (1 - mask) x unmasked move %.2e (at mask %d, step %.2e)\n", over, atMask,
                masked.mesh.micro->step);
    CHECK(over < masked.mesh.micro->step * kQuantisation);
}

TEST(micro_mask_move_default_follows_the_painted_mask) {
    // The default Move (the base cage below the micro level) over a mask takes the grid's per-point rule for the stroke: the
    // cage holds nothing finer than its triangles (artist-qa: a faceted patch wider than the painted mask, a hard step at its
    // edge, folds between two masks). So the held area IS the painted mask, with its soft falloff: every unmasked sample
    // moves exactly as the unmasked stroke moves it, the partly masked ones at most (1 - mask) of it, the masked none.
    Document masked = microDoc(3), free = microDoc(3);
    free.sculptor.microMoveAlongNormals = true; // (the per-point rule, without a mask)
    maskFront(masked);
    CHECK(!masked.sculptor.microMoveAlongNormals);
    const geom::MicroSnapshot before = geom::microSnapshot(masked.mesh, kFront);
    stroke(free, BrushType::Move);
    stroke(masked, BrushType::Move);
    const geom::MicroSnapshot ref = geom::microSnapshot(free.mesh, kFront), after = geom::microSnapshot(masked.mesh, kFront);
    size_t full = 0;
    CHECK(geom::maskedMoved(masked.mesh, before, after, &full) == 0);
    CHECK(full > 0);
    int atMask = -1;
    const float over = geom::maskedOvershoot(masked.mesh, before, after, ref, &atMask);
    const MicroMesh& mm = *masked.mesh.micro;
    float worstFree = 0.0f;
    for (size_t k = 0; k < before.tris.size(); ++k)
        for (uint32_t s = 0; s < mm.samples; ++s)
            if (mm.mask[size_t(before.tris[k]) * mm.samples + s] == 0)
                worstFree = std::max(worstFree, glm::distance(after.points[k * mm.samples + s], ref.points[k * mm.samples + s]));
    std::printf("  Move: worst excess over (1 - mask) x unmasked move %.2e; unmasked samples off the unmasked stroke by %.2e\n", over, worstFree);
    CHECK(over < mm.step * kQuantisation);
    CHECK(worstFree < mm.step * kQuantisation);
}

TEST(micro_base_mask_holds_micro_samples) {
    // The mask the base mesh carries (mask by region, invert, Mask All...): the micro samples inside a fully masked base
    // triangle don't move either, whichever brush moves them.
    for (BrushType type : {BrushType::Standard, BrushType::Clay, BrushType::Inflate, BrushType::Smooth, BrushType::Flatten,
                           BrushType::Move, BrushType::KelvinGrab}) {
        Document doc = microDoc(3);
        doc.sculptor.microBaseDabs = false;
        Mesh& m = doc.mesh;
        std::vector<float>& mask = m.ensureMask();
        for (size_t v = 0; v < m.positions.size(); ++v)
            if (glm::dot(glm::normalize(m.positions[v]), kFront) > 0.97f) mask[v] = 1.0f;
        const geom::MicroSnapshot before = geom::microSnapshot(m, kFront);
        stroke(doc, type);
        const geom::MicroSnapshot after = geom::microSnapshot(m, kFront);
        const MicroMesh& mm = *m.micro;
        size_t checked = 0, moved = 0;
        for (size_t k = 0; k < before.tris.size(); ++k) {
            const glm::uvec3 tr = m.triangles[before.tris[k]];
            if (mask[tr.x] < 1.0f || mask[tr.y] < 1.0f || mask[tr.z] < 1.0f) continue;
            uint32_t s = 0;
            for (uint32_t j = 0; j <= mm.n; ++j)
                for (uint32_t i = 0; i + j <= mm.n; ++i, ++s) {
                    if (i == 0 || j == 0 || i + j == mm.n) continue; // (edges are shared with free triangles)
                    ++checked;
                    if (glm::distance(before.points[k * mm.samples + s], after.points[k * mm.samples + s]) != 0.0f) ++moved;
                }
        }
        std::printf("  base mask  %-12s interior samples %zu, moved %zu\n", brushName(type), checked, moved);
        CHECK(checked > 0);
        CHECK(moved == 0);
    }
}

TEST(micro_mask_kelvin_brushes_leave_a_mask_alone_and_work_elsewhere) {
    // The Kelvin brushes move the base cage, which can't hold a mask finer than its triangles (held two rings out they tore
    // the surface at the mask's edge: artist-qa, 199 spikes). Where a mask is near they do nothing, like the other sideways
    // brushes on a micro level; away from it they work as ever.
    const glm::vec3 back(0.0f, 0.0f, -1.0f);
    for (BrushType type : {BrushType::KelvinGrab, BrushType::KelvinTwist, BrushType::KelvinScale, BrushType::KelvinSnake,
                           BrushType::DetailClone}) {
        Document doc = microDoc(3);
        maskFront(doc);
        const geom::MicroSnapshot nearBefore = geom::microSnapshot(doc.mesh, kFront), farBefore = geom::microSnapshot(doc.mesh, back);
        stroke(doc, type); // (across the mask)
        const geom::MicroSnapshot nearAfter = geom::microSnapshot(doc.mesh, kFront);
        CHECK(nearAfter.points == nearBefore.points);
        Symmetry sym;
        sym.x = false;
        doc.sculptor.beginStroke(doc.mesh, brushName(type));
        for (int k = 0; k < 9; ++k) {
            DabParams p = dabAt(glm::normalize(glm::vec3(-0.2f + 0.05f * float(k), 0.0f, -1.0f)), 0.3f);
            p.strokeDelta = glm::vec3(0.05f, 0.0f, 0.0f);
            doc.sculptor.dab(doc.mesh, doc.bvh, type, p, sym);
        }
        doc.undo.push(doc.sculptor.endStroke());
        const geom::MicroSnapshot farAfter = geom::microSnapshot(doc.mesh, back);
        size_t moved = 0;
        for (size_t i = 0; i < farBefore.points.size(); ++i) moved += farBefore.points[i] != farAfter.points[i];
        std::printf("  %-12s near the mask: untouched; on the far side: %zu points moved\n", brushName(type), moved);
        CHECK(moved > 0);
    }
}
