// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// parallelFor: splits [0, n) into chunks run on the shared pool's thread count (the calling one included). For coarse jobs
// (QRemesh's field smoothing, ProjectAll); small ranges run inline.
#pragma once

#include "core/ThreadPool.h"

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace sf {

// The shared pool's size: every hardware thread up to 16, or the count the user set (Preferences > Performance).
inline int parallelThreads() { return ThreadPool::shared().threads(); }

// Calls fn(begin, end) on disjoint chunks covering [0, n); runs inline when n < minParallel.
template <class F>
void parallelFor(size_t n, F&& fn, size_t minParallel = 20000) {
    if (n == 0) return;
    const size_t threads = n < minParallel ? 1 : std::min<size_t>(size_t(parallelThreads()), (n + 255) / 256);
    if (threads <= 1) {
        fn(size_t(0), n);
        return;
    }
    // More chunks than threads so uneven work balances; threads take chunks in turn.
    const size_t chunks = threads * 4, step = (n + chunks - 1) / chunks;
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    auto work = [&](size_t t) {
        for (size_t c = t; c < chunks; c += threads) {
            const size_t b = c * step, e = std::min(n, b + step);
            if (b < e) fn(b, e);
        }
    };
    for (size_t t = 1; t < threads; ++t) pool.emplace_back(work, t);
    work(0);
    for (auto& th : pool) th.join();
}

} // namespace sf
