// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ThreadPool.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace sf {

namespace {
thread_local bool insidePoolJob = false;
long long tid() {
#if defined(__linux__)
    return static_cast<long long>(syscall(SYS_gettid));
#else
    return static_cast<long long>(std::hash<std::thread::id>()(std::this_thread::get_id()));
#endif
}
}

LowerThreadPriority::LowerThreadPriority() {
#if defined(_WIN32)
    old_ = GetThreadPriority(GetCurrentThread());
    set_ = old_ != THREAD_PRIORITY_ERROR_RETURN && SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__APPLE__)
    set_ = pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0) == 0; // (std::async: a thread of its own here)
#elif defined(__linux__)
    const id_t tid = static_cast<id_t>(syscall(SYS_gettid)); // (per thread on Linux: its own nice)
    errno = 0;
    old_ = getpriority(PRIO_PROCESS, tid);
    set_ = errno == 0 && setpriority(PRIO_PROCESS, tid, std::min(old_ + 10, 19)) == 0;
#endif
}

LowerThreadPriority::~LowerThreadPriority() {
    if (!set_) return;
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), old_);
#elif defined(__linux__)
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), old_); // (may be refused unprivileged: harmless)
#endif
}

ThreadPool::ThreadPool(int threads) {
    runs_.reserve(kMaxRuns);
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
    g_sharedThreads.store(std::clamp(threads, 0, kMaxThreads));
    return !g_sharedMade.load();
}

int ThreadPool::sharedSizeFor(int requested, int env, unsigned hardware) {
    const int wanted = requested > 0 ? requested : env;
    if (wanted > 0) return std::min(wanted, kMaxThreads);
    return int(std::clamp(hardware, 1u, unsigned(kDefaultMaxThreads)));
}

ThreadPool& ThreadPool::shared() {
    static ThreadPool pool([] {
        g_sharedMade.store(true);
        // SF_THREADS: a parallel test run (tools/test/run.py --threads-per-test) gives each process its share of
        // the cores; ten processes with a pool as big as the machine each spend their time waiting for a core.
        const char* env = std::getenv("SF_THREADS");
        return sharedSizeFor(g_sharedThreads.load(), env ? std::atoi(env) : 0, std::thread::hardware_concurrency());
    }());
    return pool;
}

// Runs jobs of `run` until none are left; returns how many this thread ran. A job's exception stays in run.error
// and no further job starts: thrown through here, it left run() waiting for ever (its Run gone, workers still in it).
size_t ThreadPool::drain(Run& run) {
    const bool wasInside = insidePoolJob;
    insidePoolJob = true;
    size_t ran = 0;
    try {
        for (size_t i; (i = run.next.fetch_add(1)) < run.count; ++ran) (*run.job)(i);
    } catch (...) {
        run.next.store(run.count);
        std::lock_guard lock(mutex_);
        if (!run.error) run.error = std::current_exception();
    }
    insidePoolJob = wasInside;
    return ran;
}

// The run an idle worker joins: the newest with jobs left (a frame's short run, started while a stroke's long one goes on).
ThreadPool::Run* ThreadPool::pickRun() const {
    for (size_t i = runs_.size(); i-- > 0;)
        if (runs_[i]->next.load(std::memory_order_relaxed) < runs_[i]->count) return runs_[i];
    return nullptr;
}

void ThreadPool::workerLoop() {
    uint64_t seen = 0;
    std::unique_lock lock(mutex_);
    for (;;) {
        Run* run = pickRun(); // (another thread's run still has jobs: straight on to it)
        if (!run) {
            // Brush strokes run one job after another: spin a little before sleeping, so the next dab's job
            // doesn't wait for the thread to wake up.
            lock.unlock();
            for (int i = 0; i < 1000 && generation_.load(std::memory_order_relaxed) == seen; ++i) std::this_thread::yield();
            lock.lock();
            wake_.wait(lock, [&] { return stop_ || generation_ != seen || (run = pickRun()) != nullptr; });
            if (stop_) return;
            seen = generation_;
            if (!run) run = pickRun();
            if (!run) continue;
        }
        ++run->users;
        lock.unlock();
        const size_t ran = drain(*run);
        lock.lock();
        run->finished += ran;
        if (--run->users == 0 && (run->finished == run->count || run->error)) done_.notify_all();
    }
}

void ThreadPool::run(size_t count, const std::function<void(size_t)>& job) {
    if (count == 0) return;
    // Another thread's run going on: this one goes beside it (kMaxRuns), and the workers join it as they come free.
    // Before (SF_POOL_SHARED=0), it ran inline: a big brush's last normals on Lucy 28M then took one thread, and its
    // release 0.95 s instead of 0.55 s, as the frame's run or the stroke's had taken the pool first.
    static const bool shared = [] {
        const char* e = std::getenv("SF_POOL_SHARED");
        return !e || *e != '0';
    }();
    std::unique_lock runLock(runMutex_, std::defer_lock);
    bool serialRun = count == 1 || workers_.empty() || serial_ || insidePoolJob;
    if (!serialRun && !shared) serialRun = !runLock.try_lock();
    Run run;
    run.job = &job;
    run.count = count;
    if (!serialRun && shared) {
        std::lock_guard lock(mutex_);
        if (runs_.size() < kMaxRuns) runs_.push_back(&run), ++generation_;
        else serialRun = true;
    }
    if (serialRun) {
        // SF_POOL_LOG=1: the runs that went inline because other threads held the pool, from 2 ms.
        static const bool log = std::getenv("SF_POOL_LOG") != nullptr;
        const bool contended = log && count > 1 && !workers_.empty() && !serial_ && !insidePoolJob;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < count; ++i) job(i);
        if (contended) {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (ms >= 2.0) std::fprintf(stderr, "pool: busy, %zu jobs inline on thread %lld: %.1f ms\n", count, tid(), ms);
        }
        return;
    }
    if (!shared) {
        std::lock_guard lock(mutex_);
        runs_.push_back(&run);
        ++generation_;
    }
    wake_.notify_all();
    const size_t ran = drain(run);
    std::unique_lock lock(mutex_);
    run.finished += ran;
    done_.wait(lock, [&] { return run.users == 0 && (run.finished == run.count || run.error); });
    runs_.erase(std::find(runs_.begin(), runs_.end(), &run));
    if (run.error) std::rethrow_exception(run.error);
}

} // namespace sf
