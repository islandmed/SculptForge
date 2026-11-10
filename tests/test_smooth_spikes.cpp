// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shift-smooth on an Extract-made Part: a spike on either layer of the shell must come down
// under a Smooth stroke's dabs placed as the app places them (every 0.15 of the radius along the path).
#include "TestHarness.h"

#include "core/ExtractMirror.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

constexpr Symmetry kNoSym{false, false, false};

// How far v sits off its neighbours' mean, along its normal.
float spikeHeight(const Mesh& m, uint32_t v) {
    glm::vec3 mean(0.0f);
    int n = 0;
    for (uint32_t w : m.neighbors(v)) mean += m.positions[w], ++n;
    return n ? std::abs(glm::dot(m.positions[v] - mean / float(n), m.normals[v])) : 0.0f;
}

} // namespace

TEST(smooth_reduces_spikes_on_both_layers_of_an_extract) {
    for (bool keepVolume : {false, true})
        for (bool frontOnly : {false, true}) {
            Mesh src = makeIcosphere(5);
            src.mask.assign(src.vertexCount(), 0.0f);
            for (uint32_t v = 0; v < src.vertexCount(); ++v)
                if (glm::normalize(src.positions[v]).z > 0.6f) src.mask[v] = 1.0f;
            std::string err;
            Mesh ex = extractShell(src, ExtractOptions(), err);
            CHECK(ex.vertexCount() > 0);
            ex.recomputeAllNormals();
            // A spike on the outer layer and one on the inner layer, each near the cap's pole.
            const uint32_t np = uint32_t(ex.vertexCount() / 2);
            uint32_t outer = 0;
            float best = 1e9f;
            for (uint32_t v = 0; v < np; ++v) {
                const float d = glm::distance(ex.positions[v], glm::vec3(0, 0, 1));
                if (d < best) best = d, outer = v;
            }
            const uint32_t inner = outer + np;
            const float edge = glm::distance(ex.positions[outer], ex.positions[ex.neighbors(outer)[0]]);
            ex.positions[outer] += ex.normals[outer] * (3.0f * edge);
            ex.positions[inner] -= ex.normals[inner] * (3.0f * edge);
            ex.recomputeAllNormals();
            Document d = makeDoc(std::move(ex));
            const float o0 = spikeHeight(d.mesh, outer), i0 = spikeHeight(d.mesh, inner);

            const float radius = 0.2f, spacing = 0.15f * radius;
            d.sculptor.beginStroke(d.mesh, "Smooth");
            glm::vec3 prev(0.0f);
            for (float x = -0.35f; x <= 0.35f; x += spacing) {
                const glm::vec3 c = glm::normalize(glm::vec3(x, 0.0f, 1.0f));
                DabParams p;
                p.center = c;
                p.viewDir = -c;
                p.radius = radius;
                p.strength = 0.5f;
                p.keepVolume = keepVolume;
                p.frontFacesOnly = frontOnly;
                p.strokeDelta = c - prev;
                prev = c;
                d.sculptor.dab(d.mesh, d.bvh, BrushType::Smooth, p, kNoSym);
            }
            d.undo.push(d.sculptor.endStroke());
            d.mesh.recomputeAllNormals();
            std::printf("smooth keepVolume=%d frontOnly=%d: outer spike %.4f -> %.4f, inner %.4f -> %.4f\n", keepVolume,
                        frontOnly, o0, spikeHeight(d.mesh, outer), i0, spikeHeight(d.mesh, inner));
            std::fflush(stdout);
            const float o1 = spikeHeight(d.mesh, outer), i1 = spikeHeight(d.mesh, inner);
            if (!keepVolume && !frontOnly) { // (the defaults: one stroke takes both spikes down by well over half)
                CHECK(o1 < 0.4f * o0);
                CHECK(i1 < 0.4f * i0);
            } else if (!frontOnly) { // keep volume: gentler, but both layers come down
                CHECK(o1 < 0.7f * o0);
                CHECK(i1 < 0.7f * i0);
            } else { // Front Faces Only never reaches the inner layer (opt-in; the Brush panel says so)
                CHECK(o1 < 0.7f * o0);
                CHECK(i1 > 0.99f * i0);
            }
        }
}
