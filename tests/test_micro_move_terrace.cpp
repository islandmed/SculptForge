// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An artist's report on a micro level: a second Move over a masked area left contour rings, black speckles and a
// stepped band. A drag is many small grab events; each rounded its own change to the grid's 16-bit units, so where
// an event moves a point less than half a unit (the falloff's tail, a soft mask edge) the point never moved, and just
// above half a unit it moved a whole one every event: the falloff became a staircase. The promise: a drag in many
// events leaves the same surface as the same drag in one event (to a unit of rounding), and a fully masked grid point
// does not move at all.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Commands.h"
#include "core/Micro.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace sf;

namespace {

Document maskedMicroDoc() {
    Document doc = makeDoc(makeBenchSphere(20000));
    std::string err;
    CHECK(microDivide(doc.mesh, 4, err));
    // A mask beside the Moves (the grid's per-point Move path), with a soft edge across the moved area.
    Sculptor& sc = doc.sculptor;
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.12f, 0.0f, 1.0f)), p.radius = 0.12f, p.strength = 1.0f, p.viewDir = -p.center;
    Symmetry none;
    none.x = false;
    CHECK(sc.beginStroke(doc.mesh, "Mask"));
    sc.dabMask(doc.mesh, doc.bvh, p, none);
    doc.undo.push(sc.endStroke());
    CHECK(doc.mesh.micro->hasMask());
    return doc;
}

// A Move dragged `total` in `events` equal grab events.
void move(Document& doc, const glm::vec3& at, float radius, const glm::vec3& total, int events) {
    Sculptor& sc = doc.sculptor;
    DabParams p;
    p.center = glm::normalize(at), p.radius = radius, p.strength = 1.0f, p.viewDir = -p.center;
    Symmetry none;
    none.x = false;
    CHECK(sc.beginStroke(doc.mesh, "Move"));
    sc.beginGrab(doc.mesh, doc.bvh, p, none);
    for (int i = 0; i < events; ++i) sc.grab(doc.mesh, doc.bvh, total / float(events));
    doc.undo.push(sc.endStroke());
}

void twoMoves(Document& doc, int events) {
    move(doc, glm::vec3(0.0f, 0.0f, 1.0f), 0.3f, glm::vec3(0.0f, 0.0f, 0.06f), events);
    move(doc, glm::vec3(0.05f, 0.03f, 1.0f), 0.25f, glm::vec3(0.0f, 0.0f, 0.04f), events);
}

} // namespace

TEST(micro_move_invariant_many_drag_events_match_one) {
    Document drag = maskedMicroDoc(), once = maskedMicroDoc();
    const std::vector<uint8_t> mask = drag.mesh.micro->mask;
    twoMoves(drag, 200); // (a pen drag: an event every few pixels)
    twoMoves(once, 1);
    const MicroMesh& a = *drag.mesh.micro;
    CHECK(a.step == once.mesh.micro->step); // (same widening)
    const std::vector<int16_t> d = a.disp.toVector(), ref = once.mesh.micro->disp.toVector();
    int worst = 0, moved = 0;
    bool maskedStill = true;
    for (size_t k = 0; k < d.size(); ++k) {
        worst = std::max(worst, std::abs(int(d[k]) - int(ref[k])));
        moved = std::max(moved, std::abs(int(ref[k])));
        if (mask[k] == 255) maskedStill = maskedStill && d[k] == 0; // (the sphere starts smooth: 0 everywhere)
    }
    printf("  step %g (micro edge %g), moved up to %d units, many events vs one: worst %d units\n", double(a.step),
           double(a.baseEdge / float(a.n)), moved, worst);
    CHECK(moved > 100);
    CHECK(worst <= 2);
    CHECK(maskedStill);
}
