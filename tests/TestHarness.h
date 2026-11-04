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
};
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Register {
    Register(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline Document makeDoc(Mesh m) {
    Document d;
    d.setMesh(std::move(m));
    return d;
}

// `sculptforge_tests --shard K/N` (tests/test_main.cpp) runs the tests whose name hashes (FNV-1a) to K modulo N: every
// test is in exactly one of the N shards, a test stays in its shard when others are added, and the slow tests of one
// file (registered next to each other) don't all land in the same shards, as they did by registration index: CI
// runs every other shard on one machine (ctest -I K,,N), and that machine got all the slow ones.
inline std::uint32_t shardHash(const char* name) {
    std::uint32_t h = 2166136261u;
    for (const char* c = name; *c; ++c) h = (h ^ std::uint8_t(*c)) * 16777619u;
    return h ^ (h >> 16); // the low bits of FNV-1a alone follow the last character too closely
}
inline bool inShard(const char* name, int shard, int shards) {
    return shards <= 1 || shardHash(name) % std::uint32_t(shards) == std::uint32_t(shard);
}

// A wall-clock limit for a speed check: the value given on a developer machine, four times that on a CI runner
// (CI or GITHUB_ACTIONS set: shared, often loaded virtual machines, where Windows runners have flaked at 1.1x), and
// the value itself again with SF_BENCH_LIMITS=1, which is how benchmarks are judged.
inline double timeLimit(double local) {
    const char* strict = std::getenv("SF_BENCH_LIMITS");
    if (strict && std::string(strict) == "1") return local;
    return std::getenv("CI") || std::getenv("GITHUB_ACTIONS") ? local * 4.0 : local;
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
    static ::sf::test::Register reg_##name(#name, name);            \
    static void name()

using sf::test::makeDoc;
using sf::test::tempPath;
using sf::test::timeLimit;
