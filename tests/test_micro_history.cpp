// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"
#include "core/BVH.h"
#include "core/Benchmark.h"
#include "core/Micro.h"
#include "core/MicroHistory.h"
#include "core/Sculpt.h"
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
    CHECK(!h.edit(s.mesh, s.bvh, ids.front(), h.find(ids.back())->dabs));
    CHECK(h.editable() < h.size());
    // an editable stroke never has a baked one above it that it meets
    for (size_t i = 0; i < ids.size(); ++i) {
        const MicroRecord* r = h.find(ids[i]);
        if (r->baked) continue;
        for (size_t j = i + 1; j < ids.size(); ++j) CHECK(!h.find(ids[j])->baked);
    }
}
