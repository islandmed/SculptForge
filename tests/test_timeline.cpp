// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Sculpt Timeline (core/Timeline.h): seeking, branches, re-tune, the .sfhist sidecar. SF_BENCH_LIMITS=1 adds
// timing checks on a large mesh.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/Commands.h"
#include "core/Masking.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Timeline.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <random>

using namespace sf;

namespace {

// One Standard stroke of `dabs` dabs from `a` towards `b` on the unit sphere.
void stroke(Document& d, glm::vec3 a, glm::vec3 b, float radius = 0.3f, int dabs = 4, BrushType t = BrushType::Standard) {
    d.sculptor.beginStroke(d.mesh, brushName(t));
    for (int i = 0; i < dabs; ++i) {
        DabParams p;
        const glm::vec3 dir = glm::normalize(glm::mix(a, b, dabs > 1 ? float(i) / float(dabs - 1) : 0.0f));
        p.center = dir;
        p.viewDir = -dir;
        // On the surface as it is now (earlier strokes may have moved it off the unit sphere), as the app picks.
        if (RayHit hit; d.bvh.raycast(d.mesh, dir * 4.0f, -dir, hit)) p.center = hit.position;
        p.radius = radius;
        p.strength = 0.6f;
        d.sculptor.dab(d.mesh, d.bvh, t, p, Symmetry{});
    }
    d.undo.push(d.sculptor.endStroke());
}

glm::vec3 randomDir(std::mt19937& rng) {
    std::normal_distribution<float> n;
    return glm::normalize(glm::vec3(n(rng), n(rng), n(rng)));
}

float maxDiff(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return 1e30f;
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, glm::length(a[i] - b[i]));
    return m;
}

bool benchLimits() {
    const char* e = std::getenv("SF_BENCH_LIMITS");
    return e && std::string(e) == "1";
}

} // namespace

TEST(timeline_seek_matches_undo_history) {
    Document d = makeDoc(makeIcosphere(4));
    auto t = Timeline::attach(d);
    t->checkpointEvery = 3;
    std::mt19937 rng(7);
    std::vector<std::vector<glm::vec3>> states{d.mesh.positions};
    for (int i = 0; i < 14; ++i) {
        stroke(d, randomDir(rng), randomDir(rng));
        t->sync(d); // as the app does every frame
        states.push_back(d.mesh.positions);
    }
    CHECK(t->path().size() == 15);
    CHECK(t->cursor() == 14);
    CHECK(t->checkpointCount() >= 4); // steps 0, 3, 6, 9, 12
    // Jump around: every state must come back exactly, whichever way it is reached.
    for (size_t target : {0u, 14u, 7u, 1u, 13u, 6u, 6u, 0u, 9u, 2u, 14u}) {
        CHECK(t->seek(d, target));
        CHECK(t->cursor() == target);
        CHECK(maxDiff(d.mesh.positions, states[target]) == 0.0f);
    }
    // The undo stack still works as before after the timeline moved steps around.
    CHECK(t->seek(d, 14));
    for (int k = 13; k >= 10; --k) {
        CHECK(d.undo.undo(d.mesh, d.bvh));
        CHECK(maxDiff(d.mesh.positions, states[size_t(k)]) == 0.0f);
    }
    CHECK(d.undo.redo(d.mesh, d.bvh));
    CHECK(maxDiff(d.mesh.positions, states[11]) == 0.0f);
    CHECK(t->cursor() == 11);
    // A checkpoint jump is used for a long seek on a run of strokes.
    CHECK(t->seek(d, 0));
    CHECK(t->seek(d, 13));
    CHECK(maxDiff(d.mesh.positions, states[13]) == 0.0f);
    // The normals follow the positions (compared with a full recompute).
    std::vector<glm::vec3> normals = d.mesh.normals;
    d.mesh.recomputeAllNormals();
    CHECK(maxDiff(normals, d.mesh.normals) < 1e-5f);
}

TEST(timeline_walks_steps_that_are_not_strokes) {
    Document d = makeDoc(makeIcosphere(3));
    CommandRegistry reg;
    auto t = Timeline::attach(d);
    t->checkpointEvery = 2;
    std::vector<std::vector<glm::vec3>> states{d.mesh.positions};
    std::vector<size_t> masked{0};
    stroke(d, {0, 0, 1}, {0, 1, 0});
    t->sync(d);
    states.push_back(d.mesh.positions);
    CHECK(reg.execute(d, "mask_all").ok); // a mask step between strokes: walked by the undo stack
    t->sync(d);
    states.push_back(d.mesh.positions);
    CHECK(reg.execute(d, "mask_clear").ok);
    t->sync(d);
    states.push_back(d.mesh.positions);
    stroke(d, {1, 0, 0}, {0, 0, -1});
    t->sync(d);
    states.push_back(d.mesh.positions);
    CHECK(t->path().size() == 5);
    CHECK(t->node(t->path()[2]).kind == Timeline::Kind::Mask);
    CHECK(t->seek(d, 0));
    CHECK(maxDiff(d.mesh.positions, states[0]) == 0.0f);
    CHECK(t->seek(d, 2));
    CHECK(!d.mesh.mask.empty() && d.mesh.mask[0] > 0.5f); // mask_all is shown
    CHECK(t->seek(d, 4));
    CHECK(maxDiff(d.mesh.positions, states[4]) == 0.0f);
    CHECK(d.mesh.mask.empty() || d.mesh.mask[0] < 0.5f);
    CHECK(t->lastSeek().walked >= 1);
}

TEST(timeline_branches_keep_the_old_continuation) {
    Document d = makeDoc(makeIcosphere(4));
    auto t = Timeline::attach(d);
    stroke(d, {0, 0, 1}, {0, 0.5f, 1});
    stroke(d, {1, 0, 0}, {1, 0.5f, 0});
    stroke(d, {0, 1, 0}, {0.5f, 1, 0});
    t->sync(d);
    const std::vector<glm::vec3> oldTip = d.mesh.positions;
    const uint32_t oldTipId = t->cursorNode();
    CHECK(t->seek(d, 1)); // back to after the first stroke ...
    stroke(d, {0, -1, 0}, {0, -1, 0.5f}); // ... and sculpt something else: a branch
    t->sync(d);
    const std::vector<glm::vec3> newTip = d.mesh.positions;
    CHECK(t->path().size() == 3);
    CHECK(t->branches().size() == 1);
    CHECK(t->branches()[0].from == 1);
    CHECK(t->branches()[0].length == 2);
    CHECK(t->branches()[0].tip == oldTipId);
    CHECK(!d.undo.canRedo()); // the undo stack sees only the active branch
    // Select the old continuation: it comes back exactly, and the new one becomes the alternative.
    std::string e;
    CHECK(t->select(d, oldTipId, &e));
    CHECK(maxDiff(d.mesh.positions, oldTip) == 0.0f);
    CHECK(t->path().size() == 4);
    CHECK(t->branches().size() == 1);
    CHECK(t->branches()[0].length == 1);
    // ... and back again, through the command.
    CommandRegistry reg;
    const uint32_t newTipId = t->branches()[0].tip;
    CHECK(reg.execute(d, "timeline_select", {{"node", double(newTipId)}}).ok);
    CHECK(maxDiff(d.mesh.positions, newTip) == 0.0f);
    CHECK(reg.execute(d, "timeline_info", {{"list", true}}).ok);
    // Undo then a new edit with the plain undo stack also keeps the redo steps as a branch.
    CHECK(d.undo.undo(d.mesh, d.bvh));
    stroke(d, {0, 0, -1}, {0.3f, 0, -1});
    t->sync(d);
    CHECK(t->branches().size() == 2);
}

TEST(timeline_retune_scales_one_stroke) {
    Document d = makeDoc(makeIcosphere(5));
    auto t = Timeline::attach(d);
    const std::vector<glm::vec3> start = d.mesh.positions;
    stroke(d, {0, 0, 1}, {0, 0.3f, 1}, 0.25f);   // A (front)
    t->sync(d);
    const uint32_t a = t->cursorNode();
    stroke(d, {0, 0, -1}, {0, 0.3f, -1}, 0.25f); // B (back): doesn't overlap A
    t->sync(d);
    // B alone, for comparison.
    Document only = makeDoc(makeIcosphere(5));
    stroke(only, {0, 0, -1}, {0, 0.3f, -1}, 0.25f);
    std::string why;
    CHECK(t->canRetune(a, &why));
    CHECK(t->retune(d, a, 0.0f)); // A gone: exactly B alone where they don't overlap
    CHECK(maxDiff(d.mesh.positions, only.mesh.positions) < 1e-5f);
    CHECK(t->retune(d, a, 2.0f)); // twice as strong
    const glm::vec3 front = d.mesh.positions[0];
    (void)front;
    float maxFront = 0.0f;
    for (const glm::vec3& p : d.mesh.positions)
        if (p.z > 0.5f) maxFront = std::max(maxFront, glm::length(p));
    CHECK(t->retune(d, a, 1.0f));
    float maxFront1 = 0.0f;
    for (const glm::vec3& p : d.mesh.positions)
        if (p.z > 0.5f) maxFront1 = std::max(maxFront1, glm::length(p));
    CHECK(maxFront - 1.0f > 1.8f * (maxFront1 - 1.0f));
    // Seeking still reproduces consistent states with the new strength: start, then back to the end.
    CHECK(t->seek(d, 0));
    CHECK(maxDiff(d.mesh.positions, start) < 1e-6f);
    CHECK(t->retune(d, a, 0.0f)); // re-tuning a stroke ahead of the cursor changes only the stored steps
    CHECK(maxDiff(d.mesh.positions, start) < 1e-6f);
    CHECK(t->seek(d, 2));
    CHECK(maxDiff(d.mesh.positions, only.mesh.positions) < 1e-5f);
    // An overlapping later stroke keeps its displacement on top (the documented approximation): re-tuning to the
    // same strength twice is the identity.
    stroke(d, {0, 0.1f, 1}, {0, 0.2f, 1}, 0.3f); // overlaps A
    t->sync(d);
    const std::vector<glm::vec3> before = d.mesh.positions;
    CHECK(t->retune(d, a, 1.5f));
    CHECK(maxDiff(d.mesh.positions, before) > 1e-4f);
    CHECK(t->retune(d, a, 0.0f));
    CHECK(maxDiff(d.mesh.positions, before) < 1e-5f);
    CHECK(!t->canRetune(t->path()[0], &why)); // the start isn't a stroke
}

TEST(timeline_history_sidecar_round_trip) {
    Document d = makeDoc(makeIcosphere(4));
    CommandRegistry reg;
    auto t = Timeline::attach(d);
    std::vector<std::vector<glm::vec3>> states{d.mesh.positions};
    for (int i = 0; i < 5; ++i) {
        stroke(d, glm::vec3(std::cos(float(i)), std::sin(float(i)), 0.3f), glm::vec3(std::cos(float(i) + 0.4f), std::sin(float(i) + 0.4f), 0.3f));
        t->sync(d);
        states.push_back(d.mesh.positions);
    }
    CHECK(t->seek(d, 2));
    stroke(d, {0, 0, 1}, {0, 0.2f, 1}); // a branch
    t->sync(d);
    const std::vector<glm::vec3> branchTip = d.mesh.positions;
    CHECK(t->retune(d, t->path()[1], 0.5f));
    const std::vector<glm::vec3> saved = d.mesh.positions;
    const std::string dir = tempPath("sf_timeline_test");
    std::filesystem::create_directories(dir);
    const std::string project = dir + "/history.sfp";
    std::string err;
    CHECK(saveProject(project, d, err));
    CHECK(saveHistorySidecar(d, project, &err));
    CHECK(std::filesystem::exists(dir + "/history.sfhist"));

    Document e;
    CHECK(loadProject(project, e, err));
    CHECK(maxDiff(e.mesh.positions, saved) == 0.0f);
    CHECK(loadHistorySidecar(e, project, &err) == 1);
    Timeline* u = Timeline::of(e.undo);
    CHECK(u != nullptr);
    if (!u) return;
    CHECK(u->path().size() == 4);
    CHECK(u->cursor() == 3);
    CHECK(u->branches().size() == 1);
    CHECK(u->node(u->path()[1]).strength == 0.5f);
    CHECK(maxDiff(e.mesh.positions, branchTip) > 0.0f); // (re-tuned since)
    CHECK(u->seek(e, 0));
    CHECK(maxDiff(e.mesh.positions, states[0]) < 1e-6f);
    CHECK(u->seek(e, 3));
    CHECK(maxDiff(e.mesh.positions, saved) < 1e-6f);
    CHECK(u->select(e, u->branches()[0].tip, &err));
    CHECK(e.undo.canUndo());
    // A sidecar for another mesh is refused, leaving the history alone.
    Document other = makeDoc(makeIcosphere(4));
    CHECK(loadHistorySidecar(other, project, &err) == 0);
    CHECK(!other.undo.canUndo());
    // Damaged files are refused.
    std::vector<uint8_t> blob;
    CHECK(u->save(blob, e.mesh));
    for (size_t cut : {size_t(9), blob.size() / 2, blob.size() - 1}) {
        std::vector<uint8_t> part(blob.begin(), blob.begin() + long(cut));
        Document f;
        CHECK(loadProject(project, f, err));
        // (loadProject read the saved state; the blob belongs to `e`'s state, which may differ: either way no crash)
        CHECK(!Timeline::load(f, part, &err));
    }
    std::filesystem::remove_all(dir);
}

TEST(timeline_follows_the_undo_budget_and_clear) {
    Document d = makeDoc(makeIcosphere(4));
    auto t = Timeline::attach(d);
    stroke(d, {0, 0, 1}, {0, 0.3f, 1});
    t->sync(d);
    d.undo.byteBudget = d.undo.undoBytes() + 1; // room for about one more step
    stroke(d, {1, 0, 0}, {1, 0.3f, 0});
    stroke(d, {0, 1, 0}, {0.3f, 1, 0});
    t->sync(d);
    CHECK(t->path().size() == d.undo.undoCount() + 1);
    CHECK(t->cursor() == d.undo.undoCount());
    CHECK(t->seek(d, 0));
    CHECK(t->seek(d, t->path().size() - 1));
    d.setMesh(makeIcosphere(3)); // a new scene clears the history
    CHECK(Timeline::of(d.undo) == t.get());
    CHECK(t->path().size() == 1);
    // SubTools: each history keeps its own timeline.
    stroke(d, {0, 0, 1}, {0, 0.3f, 1});
    t->sync(d);
    d.addSubtool(makeIcosphere(3), "Other");
    CHECK(Timeline::of(d.undo) == nullptr);
    auto t2 = Timeline::attach(d);
    stroke(d, {0, 0, 1}, {0, 0.3f, 1});
    stroke(d, {0, 1, 0}, {0, 1, 0.3f});
    d.selectSubtool(0);
    CHECK(Timeline::of(d.undo) == t.get());
    CHECK(t->path().size() == 2);
    d.selectSubtool(1);
    CHECK(Timeline::of(d.undo) == t2.get());
    CHECK(t2->path().size() == 3);
    CHECK(t2->seek(d, 0));
}


// Large meshes with a long history. Always runs small; SF_BENCH_LIMITS=1 runs SF_TIMELINE_MILLIONS (default 5)
// with SF_TIMELINE_STROKES strokes (default 64; a third of them big) and checks the times. SF_TIMELINE_UNDO_GB
// raises the undo and timeline budgets (default: the app's 1 GiB undo, 1.5 GiB timeline extras) to keep them all.
TEST(timeline_seek_large_mesh_timing) {
    const bool limits = benchLimits();
    auto env = [](const char* k, double fallback) { return std::getenv(k) ? std::atof(std::getenv(k)) : fallback; };
    const double millions = limits ? env("SF_TIMELINE_MILLIONS", 5.0) : 0.3;
    const int strokes = limits ? int(env("SF_TIMELINE_STROKES", 64)) : 12;
    using C = std::chrono::steady_clock;
    auto ms = [](C::time_point a) { return std::chrono::duration<double, std::milli>(C::now() - a).count(); };
    const size_t rss0 = residentBytes();
    auto c0 = C::now();
    Document d = makeDoc(makeBenchSphere(size_t(millions * 1e6)));
    const double buildMs = ms(c0);
    auto t = Timeline::attach(d);
    if (const double gb = env("SF_TIMELINE_UNDO_GB", 0.0); gb > 0) {
        d.undo.byteBudget = size_t(gb * double(1u << 30));
        t->branchBudget = size_t(gb * double(1u << 30));
    }
    const size_t rssMesh = residentBytes();
    std::mt19937 rng(3);
    double strokeMs = 0.0, syncMs = 0.0;
    size_t touchedSum = 0, touchedMax = 0;
    for (int i = 0; i < strokes; ++i) {
        const bool big = i % 3 == 2; // every third stroke a big one (a sixth of the sphere and more)
        const glm::vec3 a = randomDir(rng);
        const glm::vec3 b = glm::normalize(a + randomDir(rng) * (big ? 0.9f : 0.35f));
        c0 = C::now();
        const size_t before = d.undo.undoCount();
        stroke(d, a, b, big ? 0.45f : 0.12f, big ? 8 : 10);
        if (std::getenv("SF_TIMELINE_DEBUG") && d.undo.undoCount() == before)
            std::printf("    stroke %d (%s) changed nothing: a %.2f %.2f %.2f b %.2f %.2f %.2f\n", i, big ? "big" : "small", a.x, a.y, a.z, b.x, b.y, b.z);
        strokeMs += ms(c0);
        c0 = C::now();
        t->sync(d);
        syncMs += ms(c0);
        touchedSum += t->node(t->cursorNode()).touched;
        touchedMax = std::max<size_t>(touchedMax, t->node(t->cursorNode()).touched);
    }
    const size_t last = t->path().size() - 1; // the undo budget may have dropped the oldest strokes
    const std::vector<glm::vec3> end = d.mesh.positions;
    // Scrubbing one step at a time back over the last 40 steps, as dragging the scrubber slowly does.
    std::vector<double> stepMs;
    for (size_t i = last; i > 0 && i + 40 > last; --i) {
        c0 = C::now();
        CHECK(t->seek(d, i - 1));
        stepMs.push_back(ms(c0));
    }
    // Long jumps: to the start, to the end, and between random points (a fast drag or a click).
    c0 = C::now();
    CHECK(t->seek(d, 0));
    const double toStart = ms(c0);
    const bool startCk = t->lastSeek().usedCheckpoint;
    c0 = C::now();
    CHECK(t->seek(d, last));
    const double toEnd = ms(c0);
    const bool endCk = t->lastSeek().usedCheckpoint;
    CHECK(maxDiff(d.mesh.positions, end) == 0.0f);
    std::vector<double> jumpMs;
    int jumpsCk = 0;
    std::uniform_int_distribution<size_t> pick(0, last);
    for (int k = 0; k < 20; ++k) {
        const size_t to = pick(rng);
        c0 = C::now();
        CHECK(t->seek(d, to));
        jumpMs.push_back(ms(c0));
        jumpsCk += t->lastSeek().usedCheckpoint;
    }
    CHECK(t->seek(d, last));
    CHECK(maxDiff(d.mesh.positions, end) == 0.0f);
    auto pct = [](std::vector<double> v, double q) {
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
    };
    std::printf("  timeline %.1fM tris (%.1fM verts), %d strokes (avg %zu, max %zu verts; %zu kept by the undo budget): "
                "build %.0f ms, strokes avg %.0f ms, capture avg %.1f ms\n"
                "    scrub 1 step: p50 %.1f ms, max %.1f ms | to start %.0f ms (%s) | to end %.0f ms (%s) | random jumps p50 %.0f ms, "
                "max %.0f ms (%d of 20 from checkpoints)\n"
                "    memory: mesh RSS %.2f GB, undo %.2f GB, timeline extras %.2f GB, checkpoints %zu = %.2f GB (every %zu steps), "
                "process RSS %.2f GB\n",
                double(d.mesh.triangleCount()) / 1e6, double(d.mesh.vertexCount()) / 1e6, strokes, touchedSum / size_t(strokes),
                touchedMax, last, buildMs, strokeMs / strokes, syncMs / strokes, pct(stepMs, 0.5), pct(stepMs, 1.0), toStart,
                startCk ? "checkpoint" : "walked", toEnd, endCk ? "checkpoint" : "walked", pct(jumpMs, 0.5), pct(jumpMs, 1.0), jumpsCk,
                double(rssMesh - std::min(rssMesh, rss0)) / double(1u << 30), double(d.undo.undoBytes()) / double(1u << 30),
                double(t->extraBytes()) / double(1u << 30), t->checkpointCount(), double(t->checkpointBytes()) / double(1u << 30),
                t->checkpointEvery, double(residentBytes()) / double(1u << 30));
    if (limits) { // generous: other work on the machine shares the cores
        CHECK(pct(stepMs, 0.5) < 50.0);
        CHECK(toStart < 1500.0);
        CHECK(toEnd < 1500.0);
    }
}
