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
    // Thinned to the limit, the newest dab stays.
    DabQueue q;
    for (int i = 0; i < 30; ++i) q.push(PathDab{float(i), 0.0f, 1.0f});
    q.thin(strokeBacklogLimit(0.045));
    CHECK(q.size() <= 4 && !q.empty());
    PathDab last{};
    while (!q.empty()) last = q.pop();
    CHECK(last.x == 29.0f);
}
