// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// StrokePath / DabQueue (core/StrokePath.h): the stroke that keeps its path when a dab costs more than a frame.
#include "TestHarness.h"

#include "core/StrokePath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace sf;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }

// The loop App::strokeTo used before StrokePath: straight dabs at even spacing, pressure interpolated.
struct Reference {
    float lx = 0, ly = 0, residual = 0;
    void addSample(float x, float y, float p0, float p1, float spacing, std::vector<PathDab>& out) {
        const float dx = x - lx, dy = y - ly;
        const float len = std::sqrt(dx * dx + dy * dy);
        float travelled = spacing - residual;
        while (travelled <= len) {
            const float u = travelled / len;
            out.push_back({lx + dx * u, ly + dy * u, p0 + (p1 - p0) * u});
            travelled += spacing;
        }
        residual = len - (travelled - spacing);
        lx = x;
        ly = y;
    }
};

float lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return float(s >> 8) / float(1 << 24);
}

} // namespace

TEST(stroke_path_straight_matches_the_old_loop) {
    uint32_t seed = 7;
    StrokePath path;
    Reference ref;
    path.reset(10.0f, 20.0f);
    ref.lx = 10.0f;
    ref.ly = 20.0f;
    float x = 10.0f, y = 20.0f, p = 0.5f;
    std::vector<PathDab> a, b;
    for (int i = 0; i < 200; ++i) {
        x += (lcg(seed) - 0.5f) * 60.0f;
        y += (lcg(seed) - 0.5f) * 60.0f;
        const float p1 = lcg(seed);
        const float spacing = 3.0f + lcg(seed) * 12.0f;
        path.addSample(x, y, p, p1, spacing, false, a);
        ref.addSample(x, y, p, p1, spacing, b);
        p = p1;
    }
    CHECK(a.size() == b.size());
    CHECK(a.size() > 50);
    const size_t n = std::min(a.size(), b.size());
    int bad = 0;
    for (size_t i = 0; i < n; ++i)
        if (!near(a[i].x, b[i].x, 0.01f) || !near(a[i].y, b[i].y, 0.01f) || !near(a[i].pressure, b[i].pressure, 1e-4f)) ++bad;
    CHECK(bad == 0);
}

TEST(stroke_path_dabs_are_evenly_spaced) {
    StrokePath path;
    path.reset(0.0f, 0.0f);
    std::vector<PathDab> out;
    path.addSample(500.0f, 0.0f, 1.0f, 1.0f, 12.5f, false, out);
    path.addSample(500.0f, 300.0f, 1.0f, 1.0f, 12.5f, false, out); // a corner: spacing carries over it
    CHECK(out.size() > 60);
    int bad = 0;
    for (size_t i = 1; i < out.size(); ++i) {
        const float d = std::hypot(out[i].x - out[i - 1].x, out[i].y - out[i - 1].y);
        // across the corner the straight distance is shorter than the spacing, never longer
        if (d > 12.5f + 1e-3f || d < 12.5f * 0.7f) ++bad;
    }
    CHECK(bad == 0);
}

TEST(stroke_path_curve_stays_a_curve_when_samples_are_far_apart) {
    // A circle of radius 300 px sampled every 40 degrees: a chord is 205 px long, 18 px off the circle in the middle.
    const float r = 300.0f, step = 40.0f * 3.14159265f / 180.0f;
    auto deviation = [&](bool smooth) {
        StrokePath path;
        path.reset(r, 0.0f);
        std::vector<PathDab> out;
        size_t from = 0;
        for (int k = 1; k <= 8; ++k) {
            path.addSample(r * std::cos(k * step), r * std::sin(k * step), 1.0f, 1.0f, 15.0f, smooth, out);
            if (k == 2) from = out.size(); // the first piece has no earlier point to curve through
        }
        float worst = 0.0f;
        for (size_t i = from; i < out.size(); ++i) worst = std::max(worst, std::abs(std::hypot(out[i].x, out[i].y) - r));
        return worst;
    };
    const float straight = deviation(false), curved = deviation(true);
    CHECK(straight > 15.0f);
    CHECK(curved < 6.0f);
    CHECK(curved < straight * 0.4f);
}

TEST(stroke_path_curve_ends_on_the_sample_and_keeps_the_spacing) {
    StrokePath path;
    path.reset(0.0f, 0.0f);
    std::vector<PathDab> out;
    path.addSample(100.0f, 0.0f, 0.0f, 0.5f, 10.0f, true, out);
    path.addSample(180.0f, 90.0f, 0.5f, 1.0f, 10.0f, true, out);
    path.addSample(200.0f, 250.0f, 1.0f, 0.2f, 10.0f, true, out);
    CHECK(!out.empty());
    // the last dab is within one spacing of the last sample
    CHECK(std::hypot(out.back().x - 200.0f, out.back().y - 250.0f) <= 10.0f + 1e-3f);
    // pressure stays within what the samples carried
    for (const PathDab& d : out) CHECK(d.pressure >= -1e-4f && d.pressure <= 1.0f + 1e-4f);
}

TEST(stroke_path_hairpin_turn_does_not_loop) {
    StrokePath path;
    path.reset(0.0f, 0.0f);
    std::vector<PathDab> out;
    path.addSample(200.0f, 0.0f, 1.0f, 1.0f, 8.0f, true, out);
    path.addSample(0.0f, 10.0f, 1.0f, 1.0f, 8.0f, true, out); // straight back
    for (const PathDab& d : out) {
        CHECK(std::isfinite(d.x) && std::isfinite(d.y));
        CHECK(d.x > -1.0f && d.x < 201.0f);
        CHECK(d.y > -1.0f && d.y < 11.0f);
    }
}

TEST(stroke_path_dense_samples_barely_change) {
    // At ordinary speeds (gaps under two spacings) smoothing does nothing at all.
    uint32_t seed = 3;
    StrokePath s, t;
    s.reset(0, 0);
    t.reset(0, 0);
    std::vector<PathDab> a, b;
    float x = 0, y = 0;
    for (int i = 0; i < 300; ++i) {
        x += 3.0f + lcg(seed) * 4.0f;
        y += (lcg(seed) - 0.5f) * 6.0f;
        s.addSample(x, y, 1.0f, 1.0f, 10.0f, true, a);
        t.addSample(x, y, 1.0f, 1.0f, 10.0f, false, b);
    }
    CHECK(a.size() == b.size());
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) CHECK(near(a[i].x, b[i].x) && near(a[i].y, b[i].y));
}

TEST(dab_queue_keeps_order_and_thins_from_the_old_end) {
    DabQueue q;
    for (int i = 0; i < 100; ++i) q.push({float(i), 0.0f, 1.0f});
    CHECK(q.thin(0) == 0 && q.size() == 100); // 0: never
    const size_t dropped = q.thin(24);
    CHECK(q.size() <= 24 && q.size() > 0);
    CHECK(dropped == 100 - q.size());
    float last = -1.0f, newest = 0.0f;
    bool ordered = true;
    while (!q.empty()) {
        const PathDab d = q.pop();
        if (d.x <= last) ordered = false;
        last = d.x;
        newest = d.x;
    }
    CHECK(ordered);
    CHECK(near(newest, 99.0f)); // the newest dab always stays
}

TEST(dab_queue_under_the_limit_is_untouched) {
    DabQueue q;
    for (int i = 0; i < 10; ++i) q.push({float(i), 0.0f, 1.0f});
    CHECK(q.thin(24) == 0);
    CHECK(q.size() == 10);
    DabQueue one;
    one.push({1.0f, 1.0f, 1.0f});
    CHECK(one.thin(1) == 0 && one.size() == 1);
}

TEST(stroke_backlog_keeps_a_heavy_brush_near_the_pointer) {
    // About 50 ms of dabs may wait: a 45 ms dab on a 21M-triangle mesh keeps the floor of 4 (0.9.6 kept 24, a
    // second behind the pointer), a cheap dab keeps hundreds so a fast small brush keeps its spacing.
    CHECK(strokeBacklogLimit(0.045) == 4);
    CHECK(strokeBacklogLimit(0.0001) == 500);
    CHECK(strokeBacklogLimit(0.0) == 4);        // not measured yet
    CHECK(strokeBacklogLimit(0.045, 0) == 0);   // SF_STROKE_BACKLOG=0: never thinned
    CHECK(strokeBacklogLimit(0.0001, 10) == 10); // a fixed limit
    // A floor in time: slow dabs keep fewer waiting (at least one), quick ones the usual floor.
    CHECK(strokeBacklogLimit(0.07, -1, 0.05, 4, 0.15) == 2);  // a 240 px dab on a 28M scan: two waiting, not four
    CHECK(strokeBacklogLimit(0.04, -1, 0.05, 4, 0.15) == 3);
    CHECK(strokeBacklogLimit(0.02, -1, 0.05, 4, 0.15) == 4);
    CHECK(strokeBacklogLimit(0.0001, -1, 0.05, 4, 0.15) == 500);
    CHECK(strokeBacklogLimit(0.0, -1, 0.05, 4, 0.15) == 4);
    CHECK(strokeBacklogLimit(0.2, -1, 0.05, 4, 0.15) == 1);    // slower still: one
    // Thinned to the limit, the newest dab stays.
    DabQueue q;
    for (int i = 0; i < 30; ++i) q.push(PathDab{float(i), 0.0f, 1.0f});
    q.thin(strokeBacklogLimit(0.045));
    CHECK(q.size() <= 4 && !q.empty());
    PathDab last{};
    while (!q.empty()) last = q.pop();
    CHECK(last.x == 29.0f);
}

namespace {

// One stroke as the app runs it: a 240 Hz pen's samples (timestamped) reach strokeTo once a frame, the pacer
// decides, the queue is thinned past the stall margin (App::paceDabs), and a worker places queued dabs at
// `cost` seconds each until the next frame (the solver thread). `frames` are the frame times; pacer = false is
// the frame-time thinning of 0.11.8 (DabQueue::thin at the backlog limit after each sample).
struct Replay {
    std::vector<PathDab> placed;
    std::vector<float> surface; // a stand-in mesh: heights on a grid, each dab adding its falloff x pressure
};

// `jitter`: what each placed dab really costs, as a factor of `cost` by its index (a real machine's noise).
Replay replayStroke(const std::vector<double>& frames, double cost, bool pacer, double (*jitter)(size_t) = nullptr) {
    constexpr double kHz = 240.0, kSeconds = 1.2;
    constexpr float kSpacing = 36.0f; // a 240 px brush at 0.15
    auto sampleAt = [](double t) { // an S-curve across the canvas, pressure rising then falling
        const double u = t / kSeconds;
        return PathDab{float(200.0 + 900.0 * u), float(400.0 + 200.0 * std::sin(6.2831853 * u)), float(0.3 + 0.7 * std::sin(3.14159265 * u))};
    };
    StrokePath path;
    DabPacer pace;
    DabQueue queue;
    Replay r;
    PathDab prev = sampleAt(0.0);
    path.reset(prev.x, prev.y);
    pace.add(prev, 0.0);
    double measured = 0.0, workerFree = 0.0, lastT = 0.0;
    size_t sent = 1;
    std::vector<PathDab> dabs, kept;
    auto decide = [&](bool finish) { // App::paceDabs
        const size_t limit = strokeBacklogLimit(pace.cost(), -1, 0.05, 4, 0.15);
        kept.clear();
        if (finish) pace.finish(limit, kept);
        else pace.decide(limit, kept);
        for (const PathDab& d : kept) queue.push(d);
        if (!finish && measured > 0.0) queue.thin(strokeBacklogLimit(measured, -1, 0.05, 4, 0.15) + size_t(0.3 / measured));
    };
    if (pacer) decide(false);
    else queue.push(prev);
    for (size_t f = 0; f < frames.size(); ++f) {
        const double now = frames[f];
        for (; double(sent) / kHz <= std::min(now, kSeconds); ++sent) { // the samples since the last frame
            const double t = double(sent) / kHz;
            const PathDab s = sampleAt(t);
            dabs.clear();
            path.addSample(s.x, s.y, prev.pressure, s.pressure, kSpacing, true, dabs);
            for (const PathDab& d : dabs) {
                if (pacer) pace.add(d, lastT + (t - lastT) * d.at);
                else queue.push(d);
            }
            if (pacer) decide(false);
            else queue.thin(measured > 0.0 ? strokeBacklogLimit(measured, -1, 0.05, 4, 0.15) : 4);
            prev = s;
            lastT = t;
        }
        if (now >= kSeconds && sent > size_t(kSeconds * kHz)) { // released: every dab left
            if (pacer) decide(true);
            while (!queue.empty()) r.placed.push_back(queue.pop());
            break;
        }
        if (pacer) decide(false); // (the frame loop: dabs that waited for the first cost)
        // the worker, handed dabs this frame, places them until the next frame begins
        const double until = f + 1 < frames.size() ? frames[f + 1] : now;
        workerFree = std::max(workerFree, now);
        while (!queue.empty() && workerFree < until) {
            const double c = cost * (jitter ? jitter(r.placed.size()) : 1.0);
            r.placed.push_back(queue.pop());
            workerFree += c;
            measured = measured > 0.0 ? measured * 0.8 + c * 0.2 : c;
            pace.measure(c);
        }
    }
    r.surface.assign(64 * 32, 0.0f);
    for (const PathDab& d : r.placed)
        for (int j = 0; j < 32; ++j)
            for (int i = 0; i < 64; ++i) {
                const float dx = (float(i) * 20.0f - d.x) / 120.0f, dy = (float(j) * 25.0f - d.y) / 120.0f;
                const float q = 1.0f - (dx * dx + dy * dy);
                if (q > 0.0f) r.surface[size_t(j * 64 + i)] += q * q * d.pressure;
            }
    return r;
}

std::vector<double> framesAt(double hz, double stallAt = -1.0, double stall = 0.0) {
    std::vector<double> t;
    for (double now = 0.0; now < 1.5; now += 1.0 / hz) {
        if (stallAt >= 0.0 && now >= stallAt && stall > 0.0) {
            now += stall;
            stall = 0.0;
        }
        t.push_back(now);
    }
    t.push_back(1.5);
    return t;
}

} // namespace

TEST(stroke_dabs_do_not_depend_on_the_frame_rate) {
    // dab-normals: one scripted stroke placed 32-56 dabs from run to run (the frame-time thinning). Now the same
    // input gives the same dabs at 30, 60 and 144 Hz and through a 250 ms stall, for slow dabs (70 ms: a 240 px
    // brush on a 28M scan, floor tier, which skips dabs) and quick ones (2 ms, which keep every dab).
    const float diag = std::hypot(1920.0f, 1080.0f);
    for (double cost : {0.07, 0.002}) {
        const Replay ref = replayStroke(framesAt(60.0), cost, true);
        CHECK(ref.placed.size() > 10);
        for (const std::vector<double>& frames : {framesAt(30.0), framesAt(144.0), framesAt(60.0, 0.5, 0.25)}) {
            const Replay r = replayStroke(frames, cost, true);
            CHECK(r.placed.size() == ref.placed.size());
            if (r.placed.size() != ref.placed.size()) continue;
            float worst = 0.0f, worstPressure = 0.0f, worstHeight = 0.0f;
            for (size_t i = 0; i < r.placed.size(); ++i) {
                worst = std::max(worst, std::hypot(r.placed[i].x - ref.placed[i].x, r.placed[i].y - ref.placed[i].y));
                worstPressure = std::max(worstPressure, std::abs(r.placed[i].pressure - ref.placed[i].pressure));
            }
            for (size_t i = 0; i < r.surface.size(); ++i) worstHeight = std::max(worstHeight, std::abs(r.surface[i] - ref.surface[i]));
            CHECK(worst <= 1e-4f * diag);
            CHECK(worstPressure <= 1e-5f);
            CHECK(worstHeight <= 1e-5f);
        }
    }
    // The slow brush skips dabs (it stays within the backlog limit of the pointer), the quick one keeps them all.
    const Replay slow = replayStroke(framesAt(60.0), 0.07, true), quick = replayStroke(framesAt(60.0), 0.002, true);
    CHECK(slow.placed.size() < quick.placed.size());
    // ... and the frame-time thinning it replaces gave a different stroke at each frame rate (other dabs kept: the
    // same count, a different volume) and fewer dabs through a stall.
    auto volume = [](const Replay& r) {
        double v = 0.0;
        for (float h : r.surface) v += h;
        return v;
    };
    const Replay at30 = replayStroke(framesAt(30.0), 0.07, false), at144 = replayStroke(framesAt(144.0), 0.07, false);
    CHECK(std::abs(volume(at30) - volume(at144)) > 1.0);
    CHECK(replayStroke(framesAt(60.0, 0.5, 0.25), 0.07, false).placed.size() < replayStroke(framesAt(60.0), 0.07, false).placed.size());
}

TEST(stroke_pacer_keeps_the_last_dab) {
    // Skipped dabs never include the stroke's end: finish keeps the last dab (the stroke ends where the pointer did).
    DabPacer p;
    std::vector<PathDab> out;
    for (size_t i = 0; i < DabPacer::kCalibrationDabs; ++i) p.measure(0.1);
    for (int i = 0; i < 10; ++i) p.add(PathDab{float(i), 0.0f, 1.0f}, 0.001 * i);
    p.finish(1, out);
    CHECK(!out.empty() && out.front().x == 0.0f && out.back().x == 9.0f);
    CHECK(out.size() < 10);
    // Until the first dabs' cost is known only they are decided (kept); limit 0 keeps every dab.
    DabPacer q;
    out.clear();
    for (int i = 0; i < 8; ++i) q.add(PathDab{float(i), 0.0f, 1.0f}, 0.01 * i);
    q.decide(2, out);
    CHECK(out.size() == DabPacer::kCalibrationDabs && q.pending() == 8 - DabPacer::kCalibrationDabs);
    q.decide(0, out);
    CHECK(out.size() == 8);
}

TEST(stroke_cost_is_rounded_so_noise_does_not_move_dabs) {
    // Half-octave steps: costs a little apart agree, a step apart don't.
    CHECK(std::abs(DabPacer::roundCost(0.07) - 0.0625) < 1e-12);
    CHECK(DabPacer::roundCost(0.066) == DabPacer::roundCost(0.074));
    CHECK(DabPacer::roundCost(0.05) != DabPacer::roundCost(0.07));
    CHECK(DabPacer::roundCost(0.0) == 0.0);
    // The median of the first dabs: one slow outlier (a cold cache) doesn't move it.
    DabPacer p;
    p.measure(0.30);
    p.measure(0.070, 3);
    CHECK(p.cost() == DabPacer::roundCost(0.070));
    p.measure(1.0); // (fixed for the stroke)
    CHECK(p.cost() == DabPacer::roundCost(0.070));
    // One stroke, two machines' worth of noise in what each dab costs, the same rounding step: the same dabs, at
    // 30 and 144 Hz too. (A live cost moved skipped dabs with every difference: the bench's 32-56 dabs.)
    auto a = [](size_t i) { return 1.0 + 0.06 * std::sin(double(i) * 1.7); };
    auto b = [](size_t i) { return 0.95 + 0.08 * std::cos(double(i) * 0.9); };
    const Replay ra = replayStroke(framesAt(30.0), 0.07, true, a), rb = replayStroke(framesAt(144.0), 0.07, true, b);
    CHECK(ra.placed.size() == rb.placed.size() && ra.placed.size() > 10);
    bool same = ra.placed.size() == rb.placed.size();
    for (size_t i = 0; same && i < ra.placed.size(); ++i)
        same = ra.placed[i].x == rb.placed[i].x && ra.placed[i].y == rb.placed[i].y && ra.placed[i].pressure == rb.placed[i].pressure;
    CHECK(same);
}
