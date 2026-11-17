// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeomInvariants.h"
#include "TestHarness.h"
#include "core/BVH.h"
#include "core/Benchmark.h"
#include "core/Micro.h"
#include "core/MicroHistory.h"
#include "core/BrushAlpha.h"
#include "core/Document.h"
#include "core/Sculpt.h"
#include "core/Undo.h"
#include <random>
#include <vector>

using namespace sf;

namespace {

struct Scene {
    Mesh mesh;
    BVH bvh;
    std::vector<int16_t> start;
};

Scene scene() {
    Scene s;
    s.mesh = makeBenchSphere(3000);
    std::string err;
    CHECK(microDivide(s.mesh, 4, err));
    s.bvh.build(s.mesh);
    s.start = s.mesh.micro->disp.toVector();
    return s;
}

// A stroke of 8 dabs near `around`, with one of several brushes (Smooth and Clay read the surface they land on).
std::vector<MicroDab> stroke(std::mt19937& rng, const Mesh& m, const glm::vec3& around) {
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const BrushType types[5] = {BrushType::Standard, BrushType::Clay, BrushType::Smooth, BrushType::Inflate, BrushType::Flatten};
    const BrushType type = types[int(U(rng) * 5.0f) % 5];
    const glm::vec3 c0 = glm::normalize(around + (glm::vec3(U(rng), U(rng), U(rng)) - 0.5f) * 0.4f);
    const glm::vec3 t = glm::normalize(glm::cross(c0, glm::vec3(0.3f, 1.0f, 0.2f)));
    std::vector<MicroDab> dabs;
    for (int d = 0; d < 8; ++d) {
        MicroDab dab;
        dab.type = type;
        dab.center = glm::normalize(c0 + t * (float(d) * 0.03f));
        dab.viewDir = -dab.center;
        dab.alphaUp = t, dab.strokeDir = t;
        dab.radius = 0.15f, dab.strength = 0.3f + 0.4f * U(rng), dab.invert = U(rng) < 0.3f;
        dabs.push_back(dab);
    }
    (void)m;
    return dabs;
}

// Every editable stroke's dabs again from the start, in order (baked strokes can't be: callers keep them all editable).
std::vector<int16_t> fromScratch(const Scene& s, const MicroHistory& h, const std::vector<uint32_t>& ids) {
    Mesh m = makeBenchSphere(3000);
    std::string err;
    microDivide(m, 4, err);
    std::copy(s.start.begin(), s.start.end(), m.micro->disp.data());
    MicroHistory again;
    again.bakeDepth = 1 << 30;
    for (uint32_t id : ids) again.add(m, s.bvh, h.find(id)->dabs);
    return m.micro->disp.toVector();
}

} // namespace

TEST(micro_history_edits_equal_a_replay_from_scratch) {
    Scene s = scene();
    MicroHistory h;
    h.bakeDepth = 1 << 30;
    std::mt19937 rng(4);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const glm::vec3 around(0.0f, 0.0f, 1.0f); // overlapping strokes on one side
    std::vector<uint32_t> ids;
    for (int i = 0; i < 24; ++i) ids.push_back(h.add(s.mesh, s.bvh, stroke(rng, s.mesh, around)));
    for (int e = 0; e < 16; ++e) {
        const size_t k = size_t(U(rng) * float(ids.size())) % ids.size();
        MicroReplayStats st;
        if (U(rng) < 0.25f) {
            CHECK(h.remove(s.mesh, s.bvh, ids[k], &st));
            ids.erase(ids.begin() + long(k));
        } else {
            std::vector<MicroDab> dabs = h.find(ids[k])->dabs;
            const glm::vec3 shift = (glm::vec3(U(rng), U(rng), U(rng)) - 0.5f) * 0.1f;
            for (MicroDab& d : dabs) d.center = glm::normalize(d.center + shift), d.strength *= 0.5f + U(rng);
            CHECK(h.edit(s.mesh, s.bvh, ids[k], dabs, &st));
            CHECK(st.replayed >= 1);
        }
    }
    CHECK(s.mesh.micro->disp.toVector() == fromScratch(s, h, ids));
}

TEST(micro_history_bakes_past_the_depth) {
    Scene s = scene();
    MicroHistory h;
    h.bakeDepth = 3;
    std::mt19937 rng(8);
    std::vector<uint32_t> ids;
    for (int i = 0; i < 10; ++i) ids.push_back(h.add(s.mesh, s.bvh, stroke(rng, s.mesh, glm::vec3(1.0f, 0.0f, 0.0f))));
    CHECK(h.find(ids.front())->baked);   // 9 overlapping strokes on it
    CHECK(!h.find(ids.back())->baked);   // the top one stays editable
    std::vector<MicroDab> top = h.find(ids.back())->dabs;
    CHECK(!h.edit(s.mesh, s.bvh, ids.front(), top));
    CHECK(h.editable() < h.size());
    // an editable stroke never has a baked one above it that it meets
    for (size_t i = 0; i < ids.size(); ++i) {
        const MicroRecord* r = h.find(ids[i]);
        if (r->baked) continue;
        for (size_t j = i + 1; j < ids.size(); ++j) CHECK(!h.find(ids[j])->baked);
    }
}

// --- P2: the live sculpt path (Sculptor::keepEditable) ---

namespace {

DabParams dabAt(const glm::vec3& c, float r, float strength = 0.5f) {
    DabParams p;
    p.center = glm::normalize(c);
    p.radius = r;
    p.strength = strength;
    p.viewDir = -p.center;
    return p;
}

Document inkDoc() {
    Document doc = makeDoc(makeBenchSphere(6000));
    std::string err;
    CHECK(microDivide(doc.mesh, 4, err));
    doc.sculptor.microBaseDabs = false; // (the grid itself, as on a dense base)
    doc.sculptor.keepEditable = true;
    return doc;
}

std::vector<int16_t> grid(const Mesh& m) { return m.micro->disp.toVector(); }
std::vector<uint8_t> maskOf(const Mesh& m) { return m.micro->mask; }

// Live strokes of every kind the history keeps: Standard with an alpha (mirrored), a recipe with a pattern, the mask
// brush, Clay over the mask, Move along the normals; each pushed on the Part's history like the app does.
void liveStrokes(Document& doc, const BrushAlpha& alpha, const BrushRecipe& recipe) {
    Symmetry mirror; // (X on)
    Symmetry none;
    none.x = false;
    Sculptor& sc = doc.sculptor;
    CHECK(sc.beginStroke(doc.mesh, "Alpha"));
    for (int i = 0; i < 8; ++i) {
        DabParams p = dabAt(glm::vec3(0.4f + 0.03f * float(i), 0.1f, 1.0f), 0.18f, 0.6f);
        p.alpha = &alpha;
        p.alphaUp = glm::vec3(0, 1, 0);
        p.pressure = 0.5f + 0.06f * float(i);
        sc.dab(doc.mesh, doc.bvh, BrushType::Standard, p, mirror);
    }
    doc.undo.push(sc.endStroke());
    CHECK((sc.lastKeptStroke() != 0) == sc.keepEditable);
    CHECK(sc.beginStroke(doc.mesh, "Recipe"));
    for (int i = 0; i < 6; ++i) sc.dabRecipe(doc.mesh, doc.bvh, recipe, dabAt(glm::vec3(0.45f, 0.05f + 0.04f * float(i), 1.0f), 0.2f), none);
    doc.undo.push(sc.endStroke());
    CHECK((sc.lastKeptStroke() != 0) == sc.keepEditable);
    CHECK(sc.beginStroke(doc.mesh, "Mask"));
    for (int i = 0; i < 4; ++i) sc.dabMask(doc.mesh, doc.bvh, dabAt(glm::vec3(0.5f, 0.1f * float(i), 1.0f), 0.15f, 0.8f), none);
    doc.undo.push(sc.endStroke());
    CHECK((sc.lastKeptStroke() != 0) == sc.keepEditable);
    CHECK(sc.beginStroke(doc.mesh, "Clay"));
    for (int i = 0; i < 8; ++i) sc.dab(doc.mesh, doc.bvh, BrushType::Clay, dabAt(glm::vec3(0.35f + 0.04f * float(i), 0.08f, 1.0f), 0.2f), none);
    doc.undo.push(sc.endStroke());
    CHECK((sc.lastKeptStroke() != 0) == sc.keepEditable);
    sc.microMoveAlongNormals = true;
    CHECK(sc.beginStroke(doc.mesh, "Move"));
    sc.beginGrab(doc.mesh, doc.bvh, dabAt(glm::vec3(0.42f, 0.12f, 1.0f), 0.2f), mirror);
    for (int i = 1; i <= 4; ++i) sc.grab(doc.mesh, doc.bvh, glm::normalize(glm::vec3(0.42f, 0.12f, 1.0f)) * (0.01f * float(i)));
    doc.undo.push(sc.endStroke());
    CHECK((sc.lastKeptStroke() != 0) == sc.keepEditable);
    sc.microMoveAlongNormals = false;
}

BrushRecipe patterned() {
    BrushRecipe r;
    r.depth = 0.6f;
    r.pattern = BrushRecipe::Pattern::Scales;
    r.patternAmount = 0.8f;
    r.patternScale = 0.4f;
    return r;
}

// The kept strokes again from the start, on a fresh copy of the scene: what an exact replay must give.
void replayAll(const MicroHistory& h, std::vector<int16_t>& disp, std::vector<uint8_t>& mask) {
    Document fresh = makeDoc(makeBenchSphere(6000));
    std::string err;
    CHECK(microDivide(fresh.mesh, 4, err));
    MicroHistory again;
    again.bakeDepth = 1 << 30;
    for (const MicroRecord& r : h.records()) {
        CHECK(!r.baked);
        again.add(fresh.mesh, fresh.bvh, r.dabs, r.ink);
    }
    disp = grid(fresh.mesh), mask = maskOf(fresh.mesh);
}

} // namespace

TEST(micro_ink_live_strokes_replay_byte_identical) {
    BrushAlpha alpha;
    CHECK(BrushAlpha::builtin("noise", alpha));
    const BrushRecipe recipe = patterned();
    Document doc = inkDoc();
    liveStrokes(doc, alpha, recipe);
    MicroHistory* h = microHistory(doc.mesh, false);
    CHECK(h && h->editable() == 5);
    CHECK(h->records()[0].ink.alpha && h->records()[1].ink.pattern != 0 && h->records()[2].ink.maskBrush);
    std::vector<int16_t> disp;
    std::vector<uint8_t> mask;
    replayAll(*h, disp, mask);
    CHECK(disp == grid(doc.mesh)); // alpha, recipe, mask and Move strokes replay exactly as they ran live
    CHECK(mask == maskOf(doc.mesh));

    // Recording changes nothing: the same strokes with keepEditable off leave the same bytes.
    Document plain = inkDoc();
    plain.sculptor.keepEditable = false;
    liveStrokes(plain, alpha, recipe);
    CHECK(grid(plain.mesh) == grid(doc.mesh));
    CHECK(!microHistory(plain.mesh, false));

    // Edit the bottom (alpha) stroke: everything above it replays; equal to a from-scratch run of the edited list.
    std::vector<MicroDab> dabs = h->records()[0].dabs;
    for (MicroDab& d : dabs) d.center = glm::normalize(d.center + glm::vec3(0.0f, 0.03f, 0.0f));
    MicroReplayStats st;
    std::string why;
    CHECK(h->edit(doc.mesh, doc.bvh, h->records()[0].id, dabs, &st, &why));
    CHECK(st.replayed >= 4);
    replayAll(*h, disp, mask);
    CHECK(disp == grid(doc.mesh));
    CHECK(mask == maskOf(doc.mesh));
}

TEST(micro_ink_shares_undo_and_undo_redo_is_byte_identical) {
    BrushAlpha alpha;
    CHECK(BrushAlpha::builtin("noise", alpha));
    Document doc = inkDoc();
    const std::vector<int16_t> start = grid(doc.mesh);
    liveStrokes(doc, alpha, patterned());
    MicroHistory* h = microHistory(doc.mesh, false);
    CHECK(h->records()[0].undo.get() == doc.undo.undoStep(4)->micro.get()); // one set of tiles, not two
    const std::vector<int16_t> after = grid(doc.mesh);
    const std::vector<uint8_t> afterMask = maskOf(doc.mesh);

    // An edit as an undo step, then undo it: exactly the grids before it; redo: exactly the edited ones.
    UndoRecord e;
    e.label = "Edit stroke";
    e.ink = std::make_shared<MicroInkStep>();
    e.ink->op = MicroInkStep::Op::Edit;
    e.ink->id = h->records()[1].id;
    e.ink->dabs = h->records()[1].dabs;
    for (MicroDab& d : e.ink->dabs) d.strength *= 0.4f;
    CHECK(e.ink->swapWith(doc.mesh, doc.bvh));
    doc.undo.push(std::move(e));
    const std::vector<int16_t> edited = grid(doc.mesh);
    CHECK(edited != after);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(grid(doc.mesh) == after && maskOf(doc.mesh) == afterMask);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(grid(doc.mesh) == edited);

    // A removal as an undo step; undo puts the stroke back in its place.
    UndoRecord r;
    r.label = "Remove stroke";
    r.ink = std::make_shared<MicroInkStep>();
    r.ink->op = MicroInkStep::Op::Remove;
    r.ink->id = h->records()[0].id;
    r.ink->out = false;
    CHECK(r.ink->swapWith(doc.mesh, doc.bvh));
    doc.undo.push(std::move(r));
    CHECK(h->size() == 4);
    const std::vector<int16_t> removedGrid = grid(doc.mesh);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(h->size() == 5 && grid(doc.mesh) == edited);

    // Undo everything (the strokes' shared tiles were rewritten by the replays): back to the start, byte for byte.
    while (doc.undo.canUndo()) CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(grid(doc.mesh) == start);
    CHECK(!doc.mesh.micro->hasMask());
    while (doc.undo.canRedo()) CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(grid(doc.mesh) == removedGrid);

    // Undo the removal, the edit and the Move stroke, then a new stroke: Move leaves the history with its redo step.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh) && doc.undo.undo(doc.mesh, doc.bvh) && doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(h->size() == 5 && h->editable() == 4);
    Symmetry none;
    none.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(-0.5f, 0.0f, 1.0f), 0.2f), none);
    doc.undo.push(doc.sculptor.endStroke());
    h->sync();
    CHECK(h->size() == 5 && h->editable() == 5); // (4 kept + the new one)
}

TEST(micro_ink_bakes_what_outside_writes_reach) {
    BrushAlpha alpha;
    CHECK(BrushAlpha::builtin("noise", alpha));
    Document doc = inkDoc();
    liveStrokes(doc, alpha, patterned());
    MicroHistory* h = microHistory(doc.mesh, false);
    const uint32_t id = h->records()[3].id; // (Clay)
    // A stroke the history doesn't keep (keepEditable off) over the same place bakes what it meets.
    doc.sculptor.keepEditable = false;
    Symmetry none;
    none.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0.45f, 0.1f, 1.0f), 0.2f), none);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(h->find(id)->baked);
    std::vector<MicroDab> dabs;
    CHECK(!h->edit(doc.mesh, doc.bvh, id, dabs));
    // A write nobody told the history about (a script poking the grid): the next edit there refuses and bakes.
    doc.sculptor.keepEditable = true;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(-0.5f, 0.0f, 1.0f), 0.2f), none);
    doc.undo.push(doc.sculptor.endStroke());
    const uint32_t far = doc.sculptor.lastKeptStroke();
    CHECK(far != 0);
    const MicroRecord* fr = h->find(far);
    mutableMicro(doc.mesh).disp[size_t(fr->tiles[fr->tiles.size() / 2]) * doc.mesh.micro->samples + 1] += 7;
    dabs = fr->dabs;
    std::string why;
    CHECK(!h->edit(doc.mesh, doc.bvh, far, dabs, nullptr, &why));
    CHECK(why.find("outside") != std::string::npos);
    CHECK(h->find(far)->baked);
}

// Geometric invariants of an edit: moving a kept stroke under a mask keeps every fully masked sample bit-identical,
// leaves no non-finite point, and moving it back gives the bytes it started from (an edit is exact both ways).
TEST(micro_ink_edit_geom_invariants) {
    Document doc = inkDoc();
    Symmetry none;
    none.x = false;
    Sculptor& sc = doc.sculptor;
    CHECK(sc.beginStroke(doc.mesh, "Mask"));
    for (int i = 0; i < 6; ++i) sc.dabMask(doc.mesh, doc.bvh, dabAt(glm::vec3(0.0f, 0.0f, 1.0f), 0.25f, 1.0f), none);
    doc.undo.push(sc.endStroke());
    const geom::MicroSnapshot masked = geom::microSnapshot(doc.mesh, glm::vec3(0, 0, 1));
    CHECK(sc.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < 10; ++i)
        sc.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(-0.3f + 0.06f * float(i), 0.05f, 1.0f), 0.2f, 0.7f), none);
    doc.undo.push(sc.endStroke());
    const uint32_t id = sc.lastKeptStroke();
    CHECK(id != 0);
    const std::vector<int16_t> before = grid(doc.mesh);
    MicroHistory* h = microHistory(doc.mesh, false);
    std::vector<MicroDab> moved = h->find(id)->dabs, back;
    for (MicroDab& d : moved) d.center = glm::normalize(d.center + glm::vec3(0.0f, 0.12f, 0.0f)), d.strength *= 1.5f;
    CHECK(h->edit(doc.mesh, doc.bvh, id, moved));
    back = moved; // (now the old dabs)
    const geom::MicroSnapshot after = geom::microSnapshot(doc.mesh, glm::vec3(0, 0, 1));
    size_t compared = 0;
    CHECK(geom::maskedMoved(doc.mesh, masked, after, &compared) == 0);
    CHECK(compared > 1000);
    size_t bad = 0;
    for (const glm::vec3& p : after.points) bad += std::isfinite(p.x + p.y + p.z) ? 0 : 1;
    CHECK(bad == 0);
    std::printf("  ink edit under a mask: %zu fully masked samples held\n", compared);
    CHECK(h->edit(doc.mesh, doc.bvh, id, back));
    CHECK(grid(doc.mesh) == before);
}
