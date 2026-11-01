// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small persistent thread pool for the per-dab work of brushes (kernels, normals, BVH refits), where
// starting threads for every dab (core/Parallel.h) would cost more than the work.
//
// poolFor() splits [0, n) into fixed-size blocks: which block a thread takes varies, but the blocks
// themselves, and so any per-block partial results combined in block order, don't depend on the thread
// count, so results are deterministic. Small ranges run inline on the calling thread.
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace sf {

class ThreadPool {
public:
    explicit ThreadPool(int threads);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // The pool brushes use: one thread per hardware thread (up to 16), the caller included.
    static ThreadPool& shared();
    // Threads for shared() (the hardware profile's, core/HardwareProfile.h), honoured if called before its first
    // use; 0 = automatic. Returns false when the pool already exists (the count applies at the next start).
    static bool setSharedThreadCount(int threads);

    int threads() const { return int(workers_.size()) + 1; }
    // Serial: every run() executes inline on the calling thread (tests compare results against this).
    void setSerial(bool serial) { serial_ = serial; }
    bool serial() const { return serial_; }
    // Calls job(i) for every i in [0, count), spread over the pool and the calling thread; returns when all
    // are done. Runs inline when called from inside a job, while another thread is using the pool, or when
    // count <= 1.
    void run(size_t count, const std::function<void(size_t)>& job);

private:
    // One run(): workers join it under mutex_ while it is current, and run() waits until every job is done and
    // every worker that joined has left, so a late worker can never touch a finished run.
    struct Run {
        const std::function<void(size_t)>* job = nullptr;
        size_t count = 0;
        std::atomic<size_t> next{0};
        size_t finished = 0; // guarded by mutex_
        int users = 0;       // workers inside, guarded by mutex_
    };
    void workerLoop();
    size_t drain(Run& run);

    std::vector<std::thread> workers_;
    std::mutex runMutex_; // one run() at a time
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    Run* current_ = nullptr;
    std::atomic<uint64_t> generation_{0};
    bool stop_ = false;
    std::atomic<bool> serial_{false};
};

// Calls fn(begin, end) for the blocks [b * block, min(n, (b + 1) * block)) of [0, n) on the shared pool.
// Each block is one call, so per-block partial results indexed by begin / block are deterministic. Fewer than
// `minBlocks` blocks run inline, in order (waking threads would cost more than the work).
template <class F>
void poolFor(size_t n, F&& fn, size_t block = 4096, size_t minBlocks = 4) {
    if (n == 0) return;
    block = std::max<size_t>(block, 1);
    const size_t blocks = (n + block - 1) / block;
    if (blocks < std::max<size_t>(minBlocks, 2)) {
        for (size_t b = 0; b < blocks; ++b) fn(b * block, std::min(n, (b + 1) * block));
        return;
    }
    const std::function<void(size_t)> job = [&](size_t b) { fn(b * block, std::min(n, (b + 1) * block)); };
    ThreadPool::shared().run(blocks, job);
}

} // namespace sf
