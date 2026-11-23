// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Move with Front faces only (a user report, 0.13.12: Move under View falloff dragged the far side of a one-piece head,
// and Connected only can't help there). The invariant: with Front faces only on, no vertex (no micro sample) whose
// normal faces away from the view moves; the rest of the grab is what it was without it, bit for bit; off, the far
// side goes with it under View falloff (the guide's "through the model": a zero above is the gate, not a missed stroke).
#include "GeomInvariants.h"
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Micro.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

const glm::vec3 kView(0.0f, 0.0f, -1.0f); // looking down -Z at the sphere's +Z face

DabParams grabAt(const glm::vec3& c, float radius, bool frontOnly, bool viewFalloff) {
    DabParams p;
    p.center = c;
    p.viewDir = kView;
    p.radius = radius;
    p.strength = 0.8f;
    p.viewFalloff = viewFalloff;
    p.frontFacesOnly = frontOnly;
    return p;
}

void drag(Document& d, const DabParams& p, const Symmetry& sym) {
    CHECK(d.sculptor.beginStroke(d.mesh, "Move"));
    d.sculptor.beginGrab(d.mesh, d.bvh, p, sym);
    for (int i = 0; i < 5; ++i) d.sculptor.grab(d.mesh, d.bvh, glm::vec3(0.03f, 0.01f, 0.02f));
    d.undo.push(d.sculptor.endStroke());
}

struct Moved {
    size_t back = 0, front = 0;
    bool frontSame = true; // every front vertex ended where the unrestricted grab left it
};

} // namespace

TEST(move_front_faces_only_leaves_back_facing_vertices_alone) {
    for (bool mirrored : {false, true}) {
        const Symmetry sym{mirrored, false, false};
        const glm::vec3 c = glm::normalize(glm::vec3(0.1f, 0.05f, 1.0f));
        Document off = makeDoc(makeIcosphere(6, 1.0f)), on = makeDoc(makeIcosphere(6, 1.0f));
        const std::vector<glm::vec3> start = off.mesh.positions;
        const std::vector<glm::vec3> nrm = off.mesh.normals;
        drag(off, grabAt(c, 1.3f, false, true), sym);
        drag(on, grabAt(c, 1.3f, true, true), sym);
        size_t backOff = 0, backOn = 0, frontOn = 0, frontDiffers = 0;
        for (size_t v = 0; v < start.size(); ++v) {
            const bool back = glm::dot(nrm[v], kView) > 0.0f;
            const bool movedOff = off.mesh.positions[v] != start[v], movedOn = on.mesh.positions[v] != start[v];
            if (back) {
                backOff += movedOff;
                backOn += movedOn;
            } else {
                frontOn += movedOn;
                frontDiffers += on.mesh.positions[v] != off.mesh.positions[v];
            }
        }
        std::printf("  Move, View falloff%s: back-facing vertices moved %zu off / %zu on; front moved %zu, differs %zu\n",
                    mirrored ? ", X mirror" : "", backOff, backOn, frontOn, frontDiffers);
        CHECK(backOff > 100); // (the control: without it the far side goes along)
        CHECK(backOn == 0);
        CHECK(frontOn > 100);
        CHECK(frontDiffers == 0); // (the near side is the same grab, bit for bit)
    }
}

TEST(move_front_faces_only_off_is_the_old_grab) {
    // The flag is the only thing that changes the result: a default DabParams and an explicit false give the same bits,
    // with and without View falloff.
    for (bool viewFalloff : {false, true}) {
        Document a = makeDoc(makeIcosphere(5, 1.0f)), b = makeDoc(makeIcosphere(5, 1.0f));
        DabParams p = grabAt(glm::vec3(0.0f, 0.0f, 1.0f), 0.9f, false, viewFalloff);
        drag(a, p, Symmetry{false, false, false});
        DabParams q;
        q.center = p.center, q.viewDir = p.viewDir, q.radius = p.radius, q.strength = p.strength, q.viewFalloff = viewFalloff;
        drag(b, q, Symmetry{false, false, false});
        CHECK(a.mesh.positions == b.mesh.positions && a.mesh.normals == b.mesh.normals);
    }
}

namespace {

// A micro level the grab runs on: Move along the normals (the grid) or on the cage below it.
Moved microRun(bool alongNormals, bool frontOnly, std::vector<glm::vec3>* grid = nullptr) {
    Document doc = makeDoc(makeBenchSphere(20000));
    std::string err;
    CHECK(microDivide(doc.mesh, 2, err));
    doc.sculptor.microMoveAlongNormals = alongNormals;
    const geom::MicroSnapshot before = geom::microSnapshot(doc.mesh, glm::vec3(0, 0, 1), -1.1f);
    drag(doc, grabAt(glm::vec3(0.0f, 0.0f, 1.0f), 2.1f, frontOnly, true) /* (the grid reaches by distance: out to the far pole) */, Symmetry{false, false, false});
    const geom::MicroSnapshot after = geom::microSnapshot(doc.mesh, glm::vec3(0, 0, 1), -1.1f);
    Moved r;
    for (size_t i = 0; i < before.points.size(); ++i) {
        const glm::vec3 d = glm::normalize(before.points[i]);
        const bool moved = glm::distance(before.points[i], after.points[i]) > 1e-6f;
        if (d.z < -0.15f) r.back += moved;
        else if (d.z > 0.15f) r.front += moved;
    }
    if (grid) *grid = after.points;
    return r;
}

} // namespace

TEST(move_front_faces_only_holds_on_a_micro_level) {
    for (bool alongNormals : {true, false}) {
        std::vector<glm::vec3> offPts, onPts;
        const Moved off = microRun(alongNormals, false, &offPts), on = microRun(alongNormals, true, &onPts);
        // (the same grab for the near side; the band at the silhouette follows its moved neighbours' frames, so it is left out)
        size_t frontDiffers = 0;
        CHECK(offPts.size() == onPts.size());
        for (size_t i = 0; i < offPts.size() && i < onPts.size(); ++i)
            if (glm::normalize(offPts[i]).z > 0.3f && offPts[i] != onPts[i]) ++frontDiffers;
        std::printf("  Micro Move (%s): back samples moved %zu off / %zu on; front moved %zu, differs %zu\n",
                    alongNormals ? "along normals" : "cage", off.back, on.back, on.front, frontDiffers);
        CHECK(off.back > 0);
        CHECK(on.back == 0);
        CHECK(on.front > 0);
        CHECK(frontDiffers == 0);
    }
}
