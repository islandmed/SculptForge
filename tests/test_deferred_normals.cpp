// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Deferred exact normals (Sculptor::deferNormals / deferDabNormals / takeOverSettle; App::deferNormals): a stroke whose
// last dab's exact normals settle after its end, in batches or at once, gives the mesh the immediate path gives, bit
// for bit (positions and normals); so does a stroke that takes the pending ones over mid-settle (each dab first works
// out those its region lists), and the undo of a deferred stroke.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Sculpt.h"

#include <cstring>

using namespace sf;

namespace {

// `dabs` Clay dabs across the head's face from x0 (0.15 radius apart), X symmetry; the last one deferred when asked.
void strokeAcross(Document& d, float x0, int dabs, bool deferLast) {
    for (int i = 0; i < dabs; ++i) {
        DabParams p;
        const float t = x0 + float(i) * 0.15f * 0.4f;
        p.center = glm::vec3(t, 0.1f, 1.0f);
        if (RayHit hit; d.bvh.raycast(d.mesh, glm::vec3(t, 0.1f, 5.0f), glm::vec3(0, 0, -1), hit)) {
            p.center = hit.position;
            p.pickNormal = glm::normalize(hit.normal);
        }
        p.viewDir = p.pickDir = glm::vec3(0, 0, -1);
        p.radius = 0.4f;
        p.strength = 0.3f;
        p.strokeDelta = glm::vec3(0.06f, 0, 0);
        d.sculptor.deferDabNormals = deferLast && i + 1 == dabs;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Clay, p, Symmetry{true, false, false});
        d.sculptor.deferDabNormals = false;
    }
}

Document exactDoc(const Mesh& head) {
    Document d = makeDoc(head);
    d.sculptor.adaptiveDabs = d.sculptor.analyticNormals = false; // (the floor tier's path: the last dab's exact normals)
    return d;
}

bool sameBits(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::vec3)) == 0;
}

const Mesh& head() {
    static const Mesh m = makeBenchHead(400'000);
    return m;
}

// One stroke, ended deferred and settled in batches of `leaves` (SIZE_MAX: at once, as a consumer's settleNow).
Document deferredStroke(size_t leaves, UndoRecord* record = nullptr) {
    Document d = exactDoc(head());
    d.sculptor.beginStroke(d.mesh, "Clay");
    strokeAcross(d, -0.5f, 8, true);
    d.sculptor.deferNormals = true;
    UndoRecord r = d.sculptor.endStroke();
    CHECK(d.sculptor.settling());
    CHECK(d.sculptor.normalsPending());
    if (leaves == SIZE_MAX) {
        d.sculptor.settleNormals();
    } else {
        while (d.sculptor.normalsPending()) d.sculptor.catchUpNormals(d.mesh, d.bvh, leaves);
        d.sculptor.endSettle();
    }
    CHECK(!d.sculptor.settling());
    if (record) *record = std::move(r);
    return d;
}

Document immediateStroke(UndoRecord* record = nullptr) {
    Document d = exactDoc(head());
    d.sculptor.beginStroke(d.mesh, "Clay");
    strokeAcross(d, -0.5f, 8, false);
    UndoRecord r = d.sculptor.endStroke();
    if (record) *record = std::move(r);
    return d;
}

} // namespace

TEST(deferred_normals_settle_bit_for_bit) {
    const Document now = immediateStroke();
    for (size_t leaves : {size_t(64), size_t(4096), SIZE_MAX}) { // (small batches, frame-sized ones, at once)
        const Document later = deferredStroke(leaves);
        CHECK(sameBits(now.mesh.positions, later.mesh.positions));
        CHECK(sameBits(now.mesh.normals, later.mesh.normals));
    }
}

TEST(deferred_normals_wait_until_settled) {
    // Before the settle the last dab's region still has the normals it had (the deferral did happen).
    const Document now = immediateStroke();
    Document d = exactDoc(head());
    d.sculptor.beginStroke(d.mesh, "Clay");
    strokeAcross(d, -0.5f, 8, true);
    d.sculptor.deferNormals = true;
    d.sculptor.endStroke();
    CHECK(sameBits(now.mesh.positions, d.mesh.positions));
    CHECK(!sameBits(now.mesh.normals, d.mesh.normals));
    d.sculptor.settleNormals();
    CHECK(sameBits(now.mesh.normals, d.mesh.normals));
}

TEST(deferred_normals_taken_over_by_the_next_stroke) {
    // A second stroke over the first one's end (x0 near its last dab) and one far from it, each begun mid-settle with
    // the pending leaves taken over, against the same strokes after a full settle.
    for (float x0 : {-0.1f, 0.45f}) {
        Document settled = exactDoc(head());
        settled.sculptor.beginStroke(settled.mesh, "Clay");
        strokeAcross(settled, -0.5f, 8, true);
        settled.sculptor.deferNormals = true;
        settled.sculptor.endStroke();
        settled.sculptor.settleNormals();
        settled.sculptor.beginStroke(settled.mesh, "Clay");
        strokeAcross(settled, x0, 4, false);
        settled.sculptor.endStroke();

        Document taken = exactDoc(head());
        taken.sculptor.beginStroke(taken.mesh, "Clay");
        strokeAcross(taken, -0.5f, 8, true);
        taken.sculptor.deferNormals = true;
        taken.sculptor.endStroke();
        taken.sculptor.catchUpNormals(taken.mesh, taken.bvh, 256); // (a settle batch or so went by)
        taken.sculptor.takeOverSettle = true;
        taken.sculptor.beginStroke(taken.mesh, "Clay");
        CHECK(!taken.sculptor.settling());
        strokeAcross(taken, x0, 4, true); // (and its own last dab deferred too)
        taken.sculptor.deferNormals = true;
        taken.sculptor.endStroke();
        taken.sculptor.settleNormals();

        CHECK(sameBits(settled.mesh.positions, taken.mesh.positions));
        CHECK(sameBits(settled.mesh.normals, taken.mesh.normals));
    }
}

TEST(deferred_normals_undo_bit_for_bit) {
    UndoRecord a, b;
    Document now = immediateStroke(&a);
    Document later = deferredStroke(SIZE_MAX, &b);
    now.undo.push(std::move(a));
    later.undo.push(std::move(b));
    CHECK(now.undo.undo(now.mesh, now.bvh));
    CHECK(later.undo.undo(later.mesh, later.bvh));
    CHECK(sameBits(now.mesh.positions, later.mesh.positions));
    CHECK(sameBits(now.mesh.normals, later.mesh.normals));
    CHECK(now.undo.redo(now.mesh, now.bvh));
    CHECK(later.undo.redo(later.mesh, later.bvh));
    CHECK(sameBits(now.mesh.positions, later.mesh.positions));
    CHECK(sameBits(now.mesh.normals, later.mesh.normals));
}
