// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Move once a frame (App::queueGrab / flushGrab): the positions move with every pointer sample (grabMove), the
// normals and the BVH once a frame (grabFinish); the mesh comes out as a whole grab per sample left it, bit for bit.
#include "GeomInvariants.h"
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>
#include <span>

using namespace sf;

namespace {
const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}
} // namespace

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

// The app's loop goes further (App::flushGrab): a frame's samples move the region in one pass (Sculptor::grabMoves),
// each vertex taking them in order: the same bits as one grabMove per sample, with a mask, a sculpt layer, symmetry.
TEST(grab_moves_in_one_pass_match_per_sample) {
    DabParams p;
    p.center = glm::vec3(0, 0, 1);
    p.radius = 0.9f;
    p.strength = 0.5f;
    std::vector<glm::vec3> deltas;
    for (int i = 0; i < 240; ++i)
        deltas.push_back(glm::vec3(0.004f * std::sin(0.11f * float(i)), 0.003f * std::cos(0.07f * float(i)), 0.002f));
    for (int setup = 0; setup < 4; ++setup) { // plain, mirrored, masked, into a sculpt layer
        const Symmetry sym{setup == 1, false, false};
        auto start = [&] {
            Mesh m = makeIcosphere(5);
            if (setup == 2) {
                m.ensureMask();
                for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = std::clamp(m.positions[v].x * 2.0f + 0.5f, 0.0f, 1.0f);
            }
            if (setup == 3) {
                m.addLayer("Detail");
                m.setLayerWeight(0, 0.5f);
            }
            return m;
        };
        auto run = [&](bool batched) {
            Document d = makeDoc(start());
            d.sculptor.beginStroke(d.mesh, "Move", setup == 3 ? 0 : -1);
            d.sculptor.beginGrab(d.mesh, d.bvh, p, sym);
            for (size_t i = 0; i < deltas.size();) {
                const size_t n = std::min(deltas.size() - i, size_t(1 + i % 9)); // frames of 1-9 samples
                if (batched) d.sculptor.grabMoves(d.mesh, std::span(deltas).subspan(i, n));
                else
                    for (size_t k = 0; k < n; ++k) d.sculptor.grabMove(d.mesh, deltas[i + k]);
                d.sculptor.grabFinish(d.mesh, d.bvh);
                i += n;
            }
            Mesh moved = d.mesh;
            d.undo.push(d.sculptor.endStroke());
            CHECK(registry().execute(d, "undo", {}).ok); // (the stroke's record: the region's start positions)
            const Mesh s0 = start();
            CHECK(d.mesh.positions == s0.positions);
            if (setup == 3) CHECK(d.mesh.layers[0].delta == s0.layers[0].delta);
            return moved;
        };
        const Mesh a = run(false), b = run(true), s = start();
        float moved = 0.0f;
        for (size_t v = 0; v < a.positions.size(); ++v) moved = std::max(moved, glm::length(a.positions[v] - s.positions[v]));
        CHECK(moved > 0.1f);
        CHECK(a.positions == b.positions && a.normals == b.normals);
        if (setup == 3) CHECK(a.layers[0].delta == b.layers[0].delta);
    }
}

// The promise on a dense curved mesh (tests/GeomInvariants.h): a Move drag from the stroke start, in frames of 1-9
// pointer samples (grabMoves), across the X mirror plane (vertices in both runs): no spikes or steps at the falloff
// edge, nothing moves past the grab's reach, no faces fold, the two halves stay mirror images, nothing non-finite.
TEST(move_invariant_dense_sphere_mirror_drag) {
    using namespace sf::geom;
    Document d = denseSphere();
    const std::vector<glm::vec3> before = d.mesh.positions;
    const float edge = meanEdge(denseSphereFixture().mesh);
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.06f, 0.25f, 0.97f)); // (the mirror's region overlaps this one)
    p.viewDir = glm::vec3(0, 0, -1);
    p.radius = 0.3f;
    p.strength = 0.5f;
    d.sculptor.beginStroke(d.mesh, "Move");
    d.sculptor.beginGrab(d.mesh, d.bvh, p, Symmetry{true, false, false});
    std::vector<glm::vec3> deltas;
    for (int i = 0; i < 180; ++i)
        deltas.push_back(glm::vec3(0.0006f * std::sin(0.05f * float(i)), 0.0008f, 0.0012f * std::cos(0.03f * float(i))));
    for (size_t i = 0; i < deltas.size();) {
        const size_t n = std::min(deltas.size() - i, size_t(1 + i % 9));
        d.sculptor.grabMoves(d.mesh, std::span(deltas).subspan(i, n));
        d.sculptor.grabFinish(d.mesh, d.bvh);
        i += n;
    }
    d.undo.push(d.sculptor.endStroke());
    const glm::vec3 mirrored(-p.center.x, p.center.y, p.center.z);
    const std::vector<glm::vec3> centers = {p.center, mirrored};
    const float moved = maxMove(before, d.mesh), outside = maxMoveOutside(before, d.mesh, centers, 1.05f * p.radius);
    const float sym = symmetryError(d.mesh, mirrorMapX(denseSphereFixture().mesh));
    const size_t nan = nonFinite(d.mesh), inverted = invertedFaces(before, d.mesh);
    const EdgeReport e = edgeOutliers(d.mesh, before, centers, p.radius, edge);
    std::printf("  Move across the mirror on %zu triangles: moved %.4f (edge %.4f), outside %.2g, mirror %.2g, inside out %zu, "
                "edge umbrella %.3f (%d ring verts, %d spikes)\n",
                d.mesh.triangleCount(), double(moved), double(edge), double(outside), double(sym), inverted, double(e.worst), e.ring,
                e.spikes);
    CHECK(moved > 0.1f);
    CHECK(nan == 0);
    CHECK(outside == 0.0f);
    CHECK(sym < 1e-5f);
    CHECK(inverted == 0);
    CHECK(e.ring > 100 && e.spikes == 0);
}
