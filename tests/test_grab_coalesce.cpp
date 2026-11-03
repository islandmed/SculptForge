// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Move once a frame (App::queueGrab / flushGrab): the positions move with every pointer sample (grabMove), the
// normals and the BVH once a frame (grabFinish); the mesh comes out as a whole grab per sample left it, bit for bit.
#include "TestHarness.h"

#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <cmath>

using namespace sf;

TEST(grab_once_a_frame_matches_per_sample) {
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.radius = 0.6f;
    p.strength = 0.5f;
    const Symmetry sym{true, false, false}; // (a mirror too)
    // A wiggly pointer path: 240 samples, applied one by one or in frames of 4-9.
    std::vector<glm::vec3> deltas;
    for (int i = 0; i < 240; ++i)
        deltas.push_back(glm::vec3(0.004f * std::sin(0.11f * float(i)), 0.003f * std::cos(0.07f * float(i)), 0.002f));
    auto run = [&](bool perSample) {
        Document d = makeDoc(makeIcosphere(5));
        d.sculptor.beginStroke(d.mesh, "Move");
        d.sculptor.beginGrab(d.mesh, d.bvh, p, sym);
        int inFrame = 0;
        for (size_t i = 0; i < deltas.size(); ++i) {
            if (perSample) {
                d.sculptor.grab(d.mesh, d.bvh, deltas[i]);
                continue;
            }
            d.sculptor.grabMove(d.mesh, deltas[i]);
            if (++inFrame == 4 + int(i % 6) || i + 1 == deltas.size()) {
                d.sculptor.grabFinish(d.mesh, d.bvh);
                inFrame = 0;
            }
        }
        d.sculptor.endStroke();
        return d.mesh;
    };
    const Mesh a = run(true), b = run(false);
    CHECK(a.positions.size() == b.positions.size());
    float worst = 0.0f, worstNormal = 0.0f, moved = 0.0f;
    const Mesh start = makeIcosphere(5);
    for (size_t v = 0; v < a.positions.size(); ++v) {
        worst = std::max(worst, glm::length(a.positions[v] - b.positions[v]));
        worstNormal = std::max(worstNormal, glm::length(a.normals[v] - b.normals[v]));
        moved = std::max(moved, glm::length(a.positions[v] - start.positions[v]));
    }
    std::printf("  grab per sample vs once a frame: positions within %.2g, normals within %.2g (moved up to %.3f)\n",
                double(worst), double(worstNormal), double(moved));
    CHECK(moved > 0.1f); // (the stroke did move the surface)
    CHECK(worst == 0.0f && worstNormal == 0.0f);
    CHECK(a.positions == b.positions && a.normals == b.normals);
}
