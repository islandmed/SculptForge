// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A tester's report on a micro level (80K base triangles, a 5M micro level), Keep Strokes Editable on: Move moved the
// surface and it snapped back at the release; Tool > Masking > Adjust (Invert, Blur, ...) did nothing. The promises,
// with the option off and on: a Move keeps exactly what the last drag left (the cage path, the grid's path with a mask
// near, and Move along normals), through the strokes that follow; Adjust changes the mask the micro level brushes and
// draws with (its own per grid point), with an undo step that brings the old one back.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Commands.h"
#include "core/Micro.h"
#include "core/MicroHistory.h"

#include <algorithm>
#include <vector>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

Document microDoc(bool keep) {
    Document doc = makeDoc(makeBenchSphere(80000));
    std::string err;
    CHECK(microDivide(doc.mesh, 3, err));
    doc.sculptor.keepEditable = keep;
    return doc;
}

DabParams dabAt(const glm::vec3& c, float r, float strength = 0.5f) {
    DabParams p;
    p.center = glm::normalize(c);
    p.radius = r;
    p.strength = strength;
    p.viewDir = -p.center;
    return p;
}

Symmetry noSymmetry() {
    Symmetry s;
    s.x = false;
    return s;
}

struct State {
    std::vector<glm::vec3> positions;
    std::vector<int16_t> disp;
    bool operator==(const State&) const = default;
};
State state(const Mesh& m) { return {m.positions, m.micro->disp.toVector()}; }

void standardStroke(Document& doc, glm::vec3 at) {
    Sculptor& sc = doc.sculptor;
    CHECK(sc.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < 6; ++i)
        sc.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(at + glm::vec3(0.03f * float(i), 0.0f, 0.0f), 0.12f), noSymmetry());
    doc.undo.push(sc.endStroke());
}

enum class MoveKind { Cage, MaskNear, AlongNormals };

// A stroke, then a Move over it; the Move's result must stay what the last drag left.
void moveKeeps(bool keep, MoveKind kind) {
    Document doc = microDoc(keep);
    Sculptor& sc = doc.sculptor;
    standardStroke(doc, glm::vec3(-0.05f, 0.0f, 1.0f)); // (brushes work: a kept stroke under the Move)
    if (kind == MoveKind::MaskNear) { // a mask beside the Move: the grid's per-point rule
        CHECK(sc.beginStroke(doc.mesh, "Mask"));
        sc.dabMask(doc.mesh, doc.bvh, dabAt(glm::vec3(0.25f, 0.0f, 1.0f), 0.08f, 1.0f), noSymmetry());
        doc.undo.push(sc.endStroke());
        CHECK(doc.mesh.micro->hasMask());
    }
    sc.microMoveAlongNormals = kind == MoveKind::AlongNormals;
    const State before = state(doc.mesh);
    CHECK(sc.beginStroke(doc.mesh, "Move"));
    sc.beginGrab(doc.mesh, doc.bvh, dabAt(glm::vec3(0.0f, 0.0f, 1.0f), 0.25f, 1.0f), noSymmetry());
    for (int i = 0; i < 5; ++i) sc.grab(doc.mesh, doc.bvh, glm::vec3(0.01f, 0.0f, 0.02f));
    const State dragged = state(doc.mesh);
    CHECK(!(dragged == before)); // (it moved while dragging)
    doc.undo.push(sc.endStroke());
    CHECK(state(doc.mesh) == dragged); // the release keeps it
    standardStroke(doc, glm::vec3(0.0f, 0.0f, -1.0f)); // a stroke elsewhere (records, syncs the history)
    standardStroke(doc, glm::vec3(0.0f, 0.02f, 1.0f)); // and one over the moved place
    const State after = state(doc.mesh);
    CHECK(!(after.positions == before.positions && after.disp == before.disp));
    // Undo the two strokes: the Move is still there, exactly.
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(state(doc.mesh) == dragged);
    sc.microMoveAlongNormals = false;
}

size_t microMasked(const Mesh& m, int above = 127) {
    return size_t(std::count_if(m.micro->mask.begin(), m.micro->mask.end(), [&](uint8_t v) { return v > above; }));
}
size_t microSoft(const Mesh& m) {
    return size_t(std::count_if(m.micro->mask.begin(), m.micro->mask.end(), [](uint8_t v) { return v > 2 && v < 253; }));
}

void maskAdjust(bool keep) {
    Document doc = microDoc(keep);
    Sculptor& sc = doc.sculptor;
    standardStroke(doc, glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(sc.beginStroke(doc.mesh, "Mask"));
    for (int i = 0; i < 3; ++i) sc.dabMask(doc.mesh, doc.bvh, dabAt(glm::vec3(0.0f, 0.0f, 1.0f), 0.2f, 1.0f), noSymmetry());
    doc.undo.push(sc.endStroke());
    const MicroMesh& mm = *doc.mesh.micro;
    CHECK(mm.hasMask());
    const std::vector<uint8_t> painted = mm.mask;
    const size_t masked = microMasked(doc.mesh), total = mm.mask.size();
    CHECK(masked > 0 && masked < total / 4);

    // Invert: masked becomes free and free masked, per grid point.
    CHECK(registry().execute(doc, "mask_invert").ok);
    CHECK(doc.mesh.micro->hasMask());
    bool exact = true;
    for (size_t i = 0; i < total; ++i) exact = exact && doc.mesh.micro->mask[i] == uint8_t(255 - painted[i]);
    CHECK(exact);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(doc.mesh.micro->mask == painted);

    // Blur softens the edge: more partly masked points, the masked area about the same.
    const size_t soft = microSoft(doc.mesh);
    CHECK(registry().execute(doc, "mask_blur", {{"iterations", 2.0}}).ok);
    CHECK(microSoft(doc.mesh) > soft);
    CHECK(doc.mesh.micro->mask != painted);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(doc.mesh.micro->mask == painted);

    // Sharpen tightens it; grow and shrink move the edge out and in.
    CHECK(registry().execute(doc, "mask_sharpen", {{"iterations", 4.0}}).ok);
    CHECK(microSoft(doc.mesh) < soft);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(registry().execute(doc, "mask_grow").ok);
    CHECK(microMasked(doc.mesh) > masked);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(registry().execute(doc, "mask_shrink").ok);
    CHECK(microMasked(doc.mesh) < masked);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(doc.mesh.micro->mask == painted);

    // Clear frees every point (undo brings the mask back), All masks every one.
    CHECK(registry().execute(doc, "mask_clear").ok);
    CHECK(!doc.mesh.micro->hasMask() || microMasked(doc.mesh, 0) == 0);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(doc.mesh.micro->mask == painted);
    CHECK(registry().execute(doc, "mask_all").ok);
    CHECK(microMasked(doc.mesh) == total);
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(doc.mesh.micro->mask == painted);

    // The mask Adjust made is the one brushes use: inverted, the painted spot is free and Standard raises it.
    CHECK(registry().execute(doc, "mask_invert").ok);
    const std::vector<int16_t> grid = doc.mesh.micro->disp.toVector();
    standardStroke(doc, glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(doc.mesh.micro->disp.toVector() != grid);
}

// Every copy of a grid point on a shared edge or corner holds one value (the mask is watertight, as the heights are).
bool seamsAgree(const Mesh& m) {
    const MicroMesh& mm = *m.micro;
    const uint32_t n = mm.n, S = mm.samples;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        for (int ed = 0; ed < 3; ++ed) {
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(ed)];
            if (nb == MicroMesh::kNone) continue;
            const uint32_t u = nb >> 2;
            const int f = int(nb & 3);
            const bool reversed = m.triangles[u][f] != m.triangles[t][ed];
            for (uint32_t k = 0; k <= n; ++k) {
                uint32_t i, j, i2, j2;
                MicroMesh::edgePoint(ed, k, n, i, j);
                MicroMesh::edgePoint(f, reversed ? n - k : k, n, i2, j2);
                if (mm.mask[size_t(t) * S + MicroMesh::index(i, j, n)] != mm.mask[size_t(u) * S + MicroMesh::index(i2, j2, n)]) return false;
            }
        }
    return true;
}

} // namespace

TEST(kse_move_cage_keeps_its_result_off) { moveKeeps(false, MoveKind::Cage); }
TEST(kse_move_cage_keeps_its_result_on) { moveKeeps(true, MoveKind::Cage); }
TEST(kse_move_mask_near_keeps_its_result_off) { moveKeeps(false, MoveKind::MaskNear); }
TEST(kse_move_mask_near_keeps_its_result_on) { moveKeeps(true, MoveKind::MaskNear); }
TEST(kse_move_along_normals_keeps_its_result_off) { moveKeeps(false, MoveKind::AlongNormals); }
TEST(kse_move_along_normals_keeps_its_result_on) { moveKeeps(true, MoveKind::AlongNormals); }
TEST(kse_mask_adjust_on_micro_off) { maskAdjust(false); }
TEST(kse_mask_adjust_on_micro_on) { maskAdjust(true); }

TEST(kse_mask_adjust_geom_invariants) {
    Document doc = microDoc(false);
    Sculptor& sc = doc.sculptor;
    CHECK(sc.beginStroke(doc.mesh, "Mask"));
    for (int i = 0; i < 4; ++i) sc.dabMask(doc.mesh, doc.bvh, dabAt(glm::vec3(0.1f * float(i), 0.05f, 1.0f), 0.15f, 0.7f), noSymmetry());
    doc.undo.push(sc.endStroke());
    CHECK(seamsAgree(doc.mesh));
    const std::vector<uint8_t> painted = doc.mesh.micro->mask;
    const uint8_t lo = *std::min_element(painted.begin(), painted.end()), hi = *std::max_element(painted.begin(), painted.end());
    // Invert twice is exact.
    CHECK(registry().execute(doc, "mask_invert").ok);
    CHECK(registry().execute(doc, "mask_invert").ok);
    CHECK(doc.mesh.micro->mask == painted);
    // Blur stays inside the painted range and keeps the seams closed.
    CHECK(registry().execute(doc, "mask_blur", {{"iterations", 3.0}}).ok);
    const std::vector<uint8_t>& b = doc.mesh.micro->mask;
    CHECK(*std::min_element(b.begin(), b.end()) >= lo && *std::max_element(b.begin(), b.end()) <= hi);
    CHECK(seamsAgree(doc.mesh));
    CHECK(registry().execute(doc, "undo").ok);
    // Grow never lowers a point, shrink never raises one; both keep the seams closed.
    CHECK(registry().execute(doc, "mask_grow").ok);
    bool up = true;
    for (size_t i = 0; i < painted.size(); ++i) up = up && doc.mesh.micro->mask[i] >= painted[i];
    CHECK(up);
    CHECK(seamsAgree(doc.mesh));
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(registry().execute(doc, "mask_shrink").ok);
    bool down = true;
    for (size_t i = 0; i < painted.size(); ++i) down = down && doc.mesh.micro->mask[i] <= painted[i];
    CHECK(down);
    CHECK(seamsAgree(doc.mesh));
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(doc.mesh.micro->mask == painted);
    // Redo brings the op back exactly.
    CHECK(registry().execute(doc, "mask_blur").ok);
    const std::vector<uint8_t> blurred = doc.mesh.micro->mask;
    CHECK(registry().execute(doc, "undo").ok);
    CHECK(registry().execute(doc, "redo").ok);
    CHECK(doc.mesh.micro->mask == blurred);
}
