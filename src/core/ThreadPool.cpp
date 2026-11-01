// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ThreadPool.h"

namespace sf {

namespace {
thread_local bool insidePoolJob = false;
}

ThreadPool::ThreadPool(int threads) {
    for (int i = 1; i < threads; ++i) workers_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    for (auto& t : workers_) t.join();
}

namespace {
std::atomic<int> g_sharedThreads{0};
std::atomic<bool> g_sharedMade{false};
} // namespace

bool ThreadPool::setSharedThreadCount(int threads) {
    g_sharedThreads.store(std::clamp(threads, 0, 64));
    return !g_sharedMade.load();
}

ThreadPool& ThreadPool::shared() {
    static ThreadPool pool([] {
        g_sharedMade.store(true);
        const int wanted = g_sharedThreads.load();
        return wanted > 0 ? wanted : int(std::clamp(std::thread::hardware_concurrency(), 1u, 16u));
    }());
    return pool;
}

// Runs jobs of `run` until none are left; returns how many this thread ran.
size_t ThreadPool::drain(Run& run) {
    const bool wasInside = insidePoolJob;
    insidePoolJob = true;
    size_t ran = 0;
    for (size_t i; (i = run.next.fetch_add(1)) < run.count; ++ran) (*run.job)(i);
    insidePoolJob = wasInside;
    return ran;
}

void ThreadPool::workerLoop() {
    uint64_t seen = 0;
    std::unique_lock lock(mutex_);
    for (;;) {
        // Brush strokes run one job after another: spin a little before sleeping, so the next dab's job
        // doesn't wait for the thread to wake up.
        lock.unlock();
        for (int i = 0; i < 1000 && generation_.load(std::memory_order_relaxed) == seen; ++i) std::this_thread::yield();
        lock.lock();
        wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
        if (stop_) return;
        seen = generation_;
        Run* run = current_;
        if (!run) continue;
        ++run->users;
        lock.unlock();
        const size_t ran = drain(*run);
        lock.lock();
        run->finished += ran;
        if (--run->users == 0 && run->finished == run->count) done_.notify_all();
    }
}

void ThreadPool::run(size_t count, const std::function<void(size_t)>& job) {
    if (count == 0) return;
    std::unique_lock runLock(runMutex_, std::defer_lock);
    if (count == 1 || workers_.empty() || serial_ || insidePoolJob || !runLock.try_lock()) {
        for (size_t i = 0; i < count; ++i) job(i);
        return;
    }
    Run run;
    run.job = &job;
    run.count = count;
    {
        std::lock_guard lock(mutex_);
        current_ = &run;
        ++generation_;
    }
    wake_.notify_all();
    const size_t ran = drain(run);
    std::unique_lock lock(mutex_);
    run.finished += ran;
    done_.wait(lock, [&] { return run.users == 0 && run.finished == run.count; });
    current_ = nullptr;
}

} // namespace sf
