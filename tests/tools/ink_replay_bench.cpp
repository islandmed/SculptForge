// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Forge Ink as the default stroke path, option A (exact replay): every Micro stroke keeps its dabs and its undo tiles.
// Editing stroke k = undo the strokes from the top down to k that reach the changed region (the region growing with each
// one that does), then replay k (edited) and those above it, in order, with today's kernel. Prints the edit ms against
// how deep k is, and checks the result against replaying every stroke from scratch.
//   sf_ink_replay_bench [millions=20] [strokes=200] [radius=0.06] [check=1]
#include "core/BVH.h"
#include "core/Benchmark.h"
#include "core/Micro.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

using namespace sf;
using Clock = std::chrono::steady_clock;

namespace {

struct Dab { glm::vec3 c, n, t; };
struct Rec {
    std::vector<Dab> dabs;
    std::unique_ptr<MicroUndo> undo;
    std::vector<uint32_t> tiles; // sorted
};

double ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }

void run(Mesh& m, const BVH& bvh, Rec& rec, float r, float s) {
    MicroStroke st;
    st.begin(m);
    for (const Dab& d : rec.dabs) st.dab(m, bvh, BrushType::Standard, d.c, -d.n, r, s, false, 0.0f, d.t, false, 0.0f, nullptr, {}, d.t);
    rec.undo = st.end(m);
    rec.tiles = rec.undo ? rec.undo->tris : std::vector<uint32_t>{};
    std::sort(rec.tiles.begin(), rec.tiles.end());
}

bool meets(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) return true;
        a[i] < b[j] ? ++i : ++j;
    }
    return false;
}

void merge(std::vector<uint32_t>& into, const std::vector<uint32_t>& add) {
    std::vector<uint32_t> out;
    std::set_union(into.begin(), into.end(), add.begin(), add.end(), std::back_inserter(out));
    into.swap(out);
}

// The base triangles the dabs of `rec` can reach (before running them).
std::vector<uint32_t> reach(const Mesh& m, const BVH& bvh, const Rec& rec, float r) {
    std::vector<uint32_t> all, one;
    for (const Dab& d : rec.dabs) {
        one.clear();
        microQueryTriangles(m, bvh, d.c, r * 1.5f, one);
        merge(all, (std::sort(one.begin(), one.end()), one));
    }
    return all;
}

} // namespace

int main(int argc, char** argv) {
    const int millions = argc > 1 ? std::atoi(argv[1]) : 20;
    const int n = argc > 2 ? std::atoi(argv[2]) : 200;
    const float r = argc > 3 ? float(std::atof(argv[3])) : 0.06f;
    const bool check = argc > 4 ? std::atoi(argv[4]) != 0 : true;
    const float s = 0.5f;

    Mesh m = makeCore200mMesh(size_t(millions) * 1000000 / 1024, 5);
    if (!m.micro) return 1;
    BVH bvh;
    bvh.build(m);
    const std::vector<int16_t> start = m.micro->disp.toVector();
    const float step0 = m.micro->step;

    // Strokes clustered on part of the head (as a sculpting session works an area), so they overlap.
    std::mt19937 rng(31);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const glm::vec3 focus = m.positions[size_t(U(rng) * float(m.positions.size())) % m.positions.size()];
    std::vector<uint32_t> near;
    for (uint32_t v = 0; v < m.positions.size(); ++v)
        if (glm::distance(m.positions[v], focus) < 0.5f) near.push_back(v);
    std::vector<Rec> recs(static_cast<size_t>(n));
    auto t0 = Clock::now();
    for (Rec& rec : recs) {
        const uint32_t v = near[size_t(U(rng) * float(near.size())) % near.size()];
        const glm::vec3 nn = glm::normalize(m.normals[v]);
        glm::vec3 t = glm::normalize(glm::cross(nn, std::abs(nn.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
        const float a = U(rng) * 6.283f;
        t = t * std::cos(a) + glm::cross(nn, t) * std::sin(a);
        for (int d = 0; d < 20; ++d) rec.dabs.push_back({m.positions[v] + t * (float(d) * r * 0.15f), nn, t});
        run(m, bvh, rec, r, s);
    }
    const double strokeMs = ms(t0) / n;
    std::printf("ink-replay: %zuM micro tris, %d strokes x 20 dabs (r %.3g, within 0.5 of one point), %.1f ms a stroke\n",
                m.micro->microTriangles() / 1000000, n, double(r), strokeMs);

    for (int depth : {0, 5, 20, 50, n / 2, n - 1}) {
        if (depth >= n) continue;
        const size_t k = size_t(n - 1 - depth);
        Rec edited;
        const glm::vec3 shift = recs[k].dabs[0].t * (r * 0.5f);
        for (Dab d : recs[k].dabs) d.c += shift, edited.dabs.push_back(d);
        t0 = Clock::now();
        std::vector<uint32_t> changed = recs[k].tiles;
        merge(changed, reach(m, bvh, edited, r));
        std::vector<size_t> above;
        for (size_t j = k + 1; j < recs.size(); ++j)
            if (meets(recs[j].tiles, changed)) above.push_back(j), merge(changed, recs[j].tiles);
        for (size_t q = above.size(); q-- > 0;)
            if (recs[above[q]].undo) recs[above[q]].undo->swapWith(m);
        if (recs[k].undo) recs[k].undo->swapWith(m);
        recs[k].dabs = edited.dabs;
        run(m, bvh, recs[k], r, s);
        for (size_t j : above) run(m, bvh, recs[j], r, s);
        const double editMs = ms(t0);
        std::printf("ink-replay: edit at depth %3d: %7.1f ms, %zu strokes replayed above it, %zu tiles in the region\n", depth, editMs,
                    above.size() + 1, changed.size());
    }
    if (m.micro->step != step0) std::printf("ink-replay: NOTE the level widened (step %.3g -> %.3g)\n", double(step0), double(m.micro->step));

    if (check) { // every stroke again from the start, in order: must equal the incremental result
        Mesh ref = makeCore200mMesh(size_t(millions) * 1000000 / 1024, 5);
        std::copy(start.begin(), start.end(), ref.micro->disp.data());
        for (Rec& rec : recs) {
            Rec copy;
            copy.dabs = rec.dabs;
            run(ref, bvh, copy, r, s);
        }
        size_t differ = 0;
        float worst = 0.0f;
        for (size_t i = 0; i < start.size(); ++i) {
            const float a = float(m.micro->disp.data()[i]) * m.micro->step, b = float(ref.micro->disp.data()[i]) * ref.micro->step;
            if (a != b) ++differ, worst = std::max(worst, std::abs(a - b));
        }
        std::printf("ink-replay: vs replay from scratch: %zu points differ, worst %.3g world (%.3f px at 540 px/unit)\n", differ,
                    double(worst), double(worst * 540.0f));
    }
    return 0;
}
