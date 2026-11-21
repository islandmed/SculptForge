// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// dabSampleIndex (core/BenchLag.h): which pointer sample a dab belongs to, for Help > Benchmark's lag.
#include "TestHarness.h"

#include "core/BenchLag.h"

#include <cmath>
#include <vector>

using namespace sf;

TEST(bench_dab_sample_two_laps_over_the_same_spot_never_read_shorter) {
    // Help > Benchmark's lag (#184): a path fitted small to the model goes round twice over the same circle. A dab on
    // the first lap belongs to the first lap's sample; the nearest sample of all was the second lap's (a later one),
    // and the lag read a whole lap short.
    struct P { float x, y; };
    std::vector<P> s;
    const int lap = 96;
    for (int k = 0; k < 2 * lap; ++k) {
        const float a = 6.2831853f * float(k) / float(lap);
        s.push_back({100.0f + 50.0f * std::cos(a), 100.0f + 50.0f * std::sin(a)});
    }
    auto pos = [&](size_t i) { return s[i]; };
    // Halfway between samples 10 and 11 of the first lap (on the chord), and exactly on sample 30.
    const P mid{(s[10].x + s[11].x) * 0.5f, (s[10].y + s[11].y) * 0.5f};
    const size_t i = dabSampleIndex(s.size(), 0, mid.x, mid.y, pos);
    CHECK(i == 10 || i == 11);
    CHECK(dabSampleIndex(s.size(), 0, s[30].x, s[30].y, pos) == 30);
    // Matched from the last dab's sample on: a dab after the first lap is on the second.
    CHECK(dabSampleIndex(s.size(), size_t(lap + 5), s[30].x, s[30].y, pos) == size_t(lap + 30));
    // Off the path (a dab on a curve between samples): the nearest sample, as before.
    CHECK(dabSampleIndex(s.size(), 0, 100.0f, 100.0f + 60.0f, pos) >= size_t(lap)); // (the nearest, latest of equals)
}
