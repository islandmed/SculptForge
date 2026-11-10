// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dependency-free test harness. Each tests/test_*.cpp file includes this and
// defines TEST(name) { CHECK(...); } cases; tests/test_main.cpp runs them all.
// New test files are picked up by CMake automatically (re-run cmake).
#pragma once

#include "core/Document.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sf::test {

inline int g_failures = 0;
inline int g_checks = 0;

struct TestCase {
    const char* name;
    std::function<void()> fn;
    const char* file; // the tests/test_*.cpp it is in: its area (tests/areas.map [units]), SF_TEST_AREA
};
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Register {
    Register(const char* n, std::function<void()> f, const char* file) { registry().push_back({n, std::move(f), file}); }
};

inline Document makeDoc(Mesh m) {
    Document d;
    d.setMesh(std::move(m));
    return d;
}

// `sculptforge_tests --shard K/N` (tests/test_main.cpp) runs shard K of N; every test is in exactly one of the N
// shards (sf::test::inShard). Shards are balanced by time, not by count: the tests in tests/shard_times.inc (the
// slow ones, seconds measured on the PC; tools/test/shard_times.py writes it) go longest first to the shard with the
// least work so far (the time-ordered greedy pass), after every other test is placed by its name's hash (FNV-1a), so
// a quick test stays in its shard when others are added and the slow tests of one file don't land together (CI runs
// every other shard on one machine, ctest -I K,,N; by registration index that machine got all the slow ones).
inline std::uint32_t shardHash(const char* name) {
    std::uint32_t h = 2166136261u;
    for (const char* c = name; *c; ++c) h = (h ^ std::uint8_t(*c)) * 16777619u;
    return h ^ (h >> 16); // the low bits of FNV-1a alone follow the last character too closely
}
// A shard's expected seconds for each of N shards (tests/test_main.cpp), and inShard from it.
std::vector<double> shardSeconds(int shards);
bool inShard(const char* name, int shard, int shards);
// The area (tests/areas.map [units]) of a test file, "" when the map has no line for it.
std::string areaOfFile(const char* file);

// A wall-clock limit for a speed check: the value given on a developer machine, four times that on a CI runner
// (CI or GITHUB_ACTIONS set: shared, often loaded virtual machines, where Windows runners have flaked at 1.1x), and
// the value itself again with SF_BENCH_LIMITS=1, which is how benchmarks are judged.
inline double timeLimit(double local) {
    const char* strict = std::getenv("SF_BENCH_LIMITS");
    if (strict && std::string(strict) == "1") return local;
    if (std::getenv("CI") || std::getenv("GITHUB_ACTIONS")) return local * 4.0;
    // tools/test/run.py sets SF_TIME_LIMIT_SCALE for a parallel run (ctest -jN): the machine is as loaded as CI's.
    const char* scale = std::getenv("SF_TIME_LIMIT_SCALE");
    const double k = scale ? std::atof(scale) : 0.0;
    return k > 1.0 ? local * k : local;
}

// Tests that open real network connections (WebRTC peers over the machine's interfaces) return early with
// SF_TEST_NO_NETWORK=1: the release package jobs (.github/workflows/packages.yml) set it, because a runner's network
// can stall them (ar_link_sends_the_model_over_webrtc timed out on the Windows runner); CI and local runs keep them.
inline bool networkTestsOff() {
    const char* off = std::getenv("SF_TEST_NO_NETWORK");
    return off && std::string(off) == "1";
}

inline std::string tempPath(const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }

} // namespace sf::test

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++::sf::test::g_checks;                                                         \
        if (!(cond)) {                                                                  \
            ++::sf::test::g_failures;                                                   \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                               \
    } while (0)

#define TEST(name)                                                  \
    static void name();                                             \
    static ::sf::test::Register reg_##name(#name, name, __FILE__);  \
    static void name()

using sf::test::makeDoc;
using sf::test::tempPath;
using sf::test::timeLimit;
