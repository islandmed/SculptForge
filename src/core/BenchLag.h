// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help > Benchmark's stroke lag (app/ProofMode.cpp): plain floats, tested without a window (tests/test_bench_lag.cpp).
#pragma once

#include <algorithm>
#include <cstddef>

namespace sf {

// Help > Benchmark (app/ProofMode.cpp): which pointer sample a dab belongs to, for how far it trails the pointer. The
// first stretch of the path from sample `from` on (the last dab's) that the dab lies on (within 1.5 px), its nearer end;
// none (a dab placed on a curve between samples): the nearest of the last 2000 samples. The nearest of all matched a
// later lap passing over the same spot (a path fitted small to the model goes round several times) and read the lag
// short (#184). `pos(i)` gives sample i's position (anything with x and y); `count` > 0.
template <class Pos>
size_t dabSampleIndex(size_t count, size_t from, float dx, float dy, Pos pos) {
    for (size_t i = from; i + 1 < count; ++i) {
        const auto a = pos(i), b = pos(i + 1);
        const float ex = float(b.x - a.x), ey = float(b.y - a.y), len2 = ex * ex + ey * ey;
        const float t = len2 > 0.0f ? std::clamp(((dx - float(a.x)) * ex + (dy - float(a.y)) * ey) / len2, 0.0f, 1.0f) : 0.0f;
        const float px = float(a.x) + t * ex - dx, py = float(a.y) + t * ey - dy;
        if (px * px + py * py <= 1.5f * 1.5f) return t < 0.5f ? i : i + 1;
    }
    size_t best = count - 1;
    float bestDist = 1e30f;
    for (size_t i = count; i-- > from && count - i < 2000;) {
        const auto a = pos(i);
        const float ex = float(a.x) - dx, ey = float(a.y) - dy, dist = ex * ex + ey * ey;
        if (dist < bestDist) bestDist = dist, best = i;
    }
    return best;
}

} // namespace sf
