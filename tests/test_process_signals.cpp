// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stopping a child process never signals anything but that child (#183: quitting logged the Linux user out): no
// signal for pid 0, -1, 1 or ourselves, none for a child already reaped, and a sentinel in our own process group
// survives a kill of a child that was spawned, killed and spawned again.
#include "TestHarness.h"

#include <chrono>

#if !defined(_WIN32)
#include "ai/Process.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

using namespace sf::ai;

namespace {

// A process in OUR process group that must outlive every ChildProcess::kill below.
struct Sentinel {
    pid_t pid = -1;
    Sentinel() {
        pid = fork();
        if (pid == 0) {
            for (;;) pause();
        }
    }
    bool alive() const {
        int status = 0;
        return pid > 0 && waitpid(pid, &status, WNOHANG) == 0;
    }
    ~Sentinel() {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
    }
};

std::atomic<int> g_hits{0};
void onSignal(int) { g_hits++; }

} // namespace

TEST(process_never_signals_pid_0_minus_1_1_or_self) {
    struct sigaction sa{}, old{}, oldHup{};
    sa.sa_handler = onSignal;
    sigaction(SIGTERM, &sa, &old);
    sigaction(SIGHUP, &sa, &oldHup);
    Sentinel sentinel;
    CHECK(sentinel.alive());
    for (int pid : {0, -1, -2, 1, int(getpid())}) {
        CHECK(!signalChildGroup(pid, SIGTERM));
        CHECK(!signalChildGroup(pid, SIGHUP));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(g_hits.load() == 0); // (not even us)
    CHECK(sentinel.alive());
    sigaction(SIGTERM, &old, nullptr);
    sigaction(SIGHUP, &oldHup, nullptr);
}

TEST(process_kill_of_unstarted_or_finished_child_signals_nothing) {
    Sentinel sentinel;
    {
        ChildProcess never;
        never.kill(0.1); // pid_ was -1: kill(-pid_) would have been kill(1)
        CHECK(!never.started() || !never.running());
    }
    ChildProcess done;
    std::string error;
    CHECK(done.start("true", {}, "", {}, error));
    CHECK(done.wait() == 0);
    done.kill(0.1); // reaped: its pid may be anyone's
    CHECK(!signalChildGroup(-1, SIGTERM));
    CHECK(sentinel.alive());
}

TEST(process_kill_stops_the_child_and_leaves_our_group_alone) {
    Sentinel sentinel;
    for (int round = 0; round < 3; ++round) {
        ChildProcess c;
        std::string error;
        CHECK(c.start("sleep", {"30"}, "", {}, error));
        CHECK(c.running());
        c.kill(0.2);
        CHECK(!c.running());
        CHECK(c.exitCode() == 128 + SIGTERM);
        CHECK(sentinel.alive());
    }
}

TEST(process_signal_reaches_a_child_in_its_own_group_and_what_it_started) {
    Sentinel sentinel;
    ChildProcess c;
    std::string error;
    CHECK(c.start("sh", {"-c", "sleep 30 & wait"}, "", {}, error)); // a grandchild in the child's group
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(c.running());
    c.kill(0.5);
    CHECK(!c.running());
    CHECK(sentinel.alive());
}

TEST(process_signal_to_a_child_in_our_own_group_is_for_the_child_alone) {
    Sentinel other; // pretends to be the child: forked, so it shares our group
    CHECK(getpgid(other.pid) == getpgrp());
    CHECK(signalChildGroup(other.pid, SIGKILL));
    int status = 0;
    CHECK(waitpid(other.pid, &status, 0) == other.pid);
    other.pid = -1;
    CHECK(WIFSIGNALED(status));
    // and we are still here
    CHECK(getpid() > 1);
}
#endif

#include "core/ExitWatchdog.h"

TEST(exit_wait_while_held_ends_when_released_and_never_waits_forever) {
    int polls = 0;
    CHECK(sf::waitWhileHeld([&] { return ++polls < 4; }, 2000, 1));
    CHECK(polls == 4);
    CHECK(sf::waitWhileHeld([] { return false; }, 2000, 1)); // not held: no wait
    int pumped = 0;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!sf::waitWhileHeld([] { return true; }, 60, 5, [&] { ++pumped; })); // stuck key: gives up
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    CHECK(pumped > 1);
}
