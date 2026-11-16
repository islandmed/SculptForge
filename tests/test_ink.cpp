// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.h"
#include "core/Benchmark.h"
#include "core/Ink.h"
#include "core/Micro.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace sf;

namespace {

InkSheet smallSheet() {
    InkSheet s;
    s.cols = 96, s.rows = 80, s.tile = 16, s.spacing = 0.01f;
    return s;
}

InkStroke randomStroke(std::mt19937& rng, const InkSheet& s, InkOp op) {
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    InkStroke k;
    k.op = op;
    k.profile = InkProfile(int(U(rng) * 4.0f) % 4);
    glm::vec3 p(U(rng) * s.cols * s.spacing, U(rng) * s.rows * s.spacing, 0.0f);
    for (int i = 0; i < 4; ++i) {
        k.path.push_back({p, 0.02f + 0.06f * U(rng), 0.005f + 0.02f * U(rng)});
        p += glm::vec3((U(rng) - 0.5f) * 0.15f, (U(rng) - 0.5f) * 0.15f, 0.0f);
    }
    return k;
}

float maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float d = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) d = std::max(d, std::abs(a[i] - b[i]));
    return d;
}

// The same strokes, in the same order, evaluated from nothing.
std::vector<float> reference(const InkSheet& s, const InkLayer& layer, const std::vector<uint32_t>& ids) {
    std::vector<float> h;
    auto surf = inkSheetSurface(s, h);
    InkLayer fresh(*surf);
    for (uint32_t id : ids) fresh.add(*layer.find(id));
    return h;
}

} // namespace

TEST(ink_incremental_edits_match_a_rebuild) {
    const InkSheet s = smallSheet();
    std::vector<float> h;
    auto surf = inkSheetSurface(s, h);
    InkLayer layer(*surf);
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    std::vector<uint32_t> ids;
    const InkOp ops[3] = {InkOp::Add, InkOp::Max, InkOp::Min};
    for (int i = 0; i < 30; ++i) ids.push_back(layer.add(randomStroke(rng, s, ops[(i / 4) % 3]))); // runs of 4
    for (int e = 0; e < 120; ++e) {
        const uint32_t id = ids[size_t(U(rng) * float(ids.size())) % ids.size()];
        InkStroke k = *layer.find(id);
        const float what = U(rng);
        if (what < 0.6f) { // move and re-height
            const glm::vec3 d((U(rng) - 0.5f) * 0.2f, (U(rng) - 0.5f) * 0.2f, 0.0f);
            for (InkPoint& p : k.path) p.p += d, p.height *= 0.5f + U(rng);
        } else if (what < 0.75f) {
            k.op = ops[int(U(rng) * 3.0f) % 3];
        } else if (what < 0.85f) {
            k.profile = InkProfile(int(U(rng) * 4.0f) % 4);
        } else {
            layer.remove(id);
            ids.erase(std::find(ids.begin(), ids.end(), id));
            ids.push_back(layer.add(randomStroke(rng, s, ops[int(U(rng) * 3.0f) % 3])));
            continue;
        }
        CHECK(layer.edit(k));
    }
    std::sort(ids.begin(), ids.end()); // ids grow with the order of creation
    CHECK(maxDiff(h, reference(s, layer, ids)) < 1e-5f);
}

TEST(ink_add_edit_evaluates_only_the_edited_stroke) {
    const InkSheet s = smallSheet();
    std::vector<float> h;
    auto surf = inkSheetSurface(s, h);
    InkLayer layer(*surf);
    std::mt19937 rng(3);
    uint32_t last = 0;
    for (int i = 0; i < 200; ++i) last = layer.add(randomStroke(rng, s, InkOp::Add));
    InkStroke k = *layer.find(last);
    for (InkPoint& p : k.path) p.height *= 1.5f;
    InkStats edit, full;
    CHECK(layer.edit(k, &edit));
    layer.rebuild(&full);
    // two contributions (old out, new in) per sample of the tiles it touches, whatever the stack holds
    CHECK(edit.evaluated == 2 * edit.samples);
    CHECK(edit.evaluated * 20 < full.evaluated);
}

TEST(ink_bounds_hold_the_relief) {
    const InkSheet s = smallSheet();
    std::vector<float> h;
    auto surf = inkSheetSurface(s, h);
    InkLayer layer(*surf);
    std::mt19937 rng(5);
    const InkOp ops[3] = {InkOp::Add, InkOp::Max, InkOp::Min};
    for (int i = 0; i < 40; ++i) layer.add(randomStroke(rng, s, ops[i % 3]));
    for (uint32_t t = 0; t < surf->tileCount(); ++t) {
        float lo, hi;
        layer.bounds(t, lo, hi);
        if (const auto* r = layer.relief(t))
            for (float v : *r) CHECK(v >= lo - 1e-6f && v <= hi + 1e-6f);
    }
}

TEST(ink_pick_hits_the_relief_and_skips_tiles) {
    const InkSheet s = smallSheet();
    std::vector<float> h;
    auto surf = inkSheetSurface(s, h);
    InkLayer layer(*surf);
    InkStroke k;
    k.path = {{glm::vec3(0.30f, 0.40f, 0.0f), 0.08f, 0.05f}, {glm::vec3(0.60f, 0.40f, 0.0f), 0.08f, 0.05f}};
    layer.add(k);
    glm::vec3 hit;
    size_t tested = 0;
    // straight down onto the ridge's crest: the relief's top there
    CHECK(inkSheetPick(s, layer, h, glm::vec3(0.45f, 0.40f, 1.0f), glm::vec3(0, 0, -1), hit, &tested));
    CHECK(std::abs(hit.z - 0.05f) < 1e-3f);
    CHECK(tested < surf->tileCount());
    // a grazing ray that only the ridge can stop
    CHECK(inkSheetPick(s, layer, h, glm::vec3(0.45f, -0.5f, 0.03f), glm::vec3(0, 1, 0), hit, &tested));
    CHECK(hit.y > 0.32f && hit.y < 0.40f);
}

TEST(ink_rebake_updates_only_the_edited_tiles) {
    const InkSheet s = smallSheet();
    std::vector<float> h;
    auto surf = inkSheetSurface(s, h);
    InkLayer layer(*surf);
    std::mt19937 rng(9);
    std::vector<uint32_t> ids;
    for (int i = 0; i < 25; ++i) ids.push_back(layer.add(randomStroke(rng, s, InkOp::Add)));
    Mesh mesh = inkSheetMesh(s, h);
    std::vector<uint64_t> baked;
    CHECK(inkRebake(s, layer, h, mesh, baked) > 0); // (the first bake records every inked tile)
    InkStroke k = *layer.find(ids[3]);
    for (InkPoint& p : k.path) p.p.x += 0.05f;
    InkStats st;
    layer.edit(k, &st);
    const size_t rebaked = inkRebake(s, layer, h, mesh, baked);
    CHECK(rebaked == st.tiles);
    CHECK(rebaked < surf->tileCount());
    const Mesh full = inkSheetMesh(s, h);
    float worst = 0.0f;
    for (size_t i = 0; i < mesh.positions.size(); ++i) worst = std::max(worst, glm::distance(mesh.positions[i], full.positions[i]));
    CHECK(worst < 1e-6f);
}

TEST(ink_on_a_micro_level_is_removable) {
    Document doc = makeDoc(makeBenchSphere(2000));
    std::string err;
    CHECK(microDivide(doc.mesh, 3, err));
    MicroMesh& mm = *doc.mesh.micro;
    const std::vector<int16_t> before = mm.disp.toVector();
    auto surf = inkMicroSurface(doc.mesh);
    CHECK(surf != nullptr);
    InkLayer layer(*surf);
    InkStroke k;
    const glm::vec3 a = doc.mesh.positions[doc.mesh.triangles[0][0]];
    k.path = {{a, 0.15f, 0.02f}, {a + glm::vec3(0.0f, 0.1f, 0.0f), 0.15f, 0.02f}};
    InkStats st;
    const uint32_t id = layer.add(k, &st);
    CHECK(st.tiles > 0 && st.tiles < mm.baseTriangles);
    CHECK(mm.disp.toVector() != before);
    layer.remove(id);
    CHECK(mm.disp.toVector() == before); // the sculpt under the ink is untouched
}
