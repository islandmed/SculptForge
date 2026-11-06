// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs every TEST registered by the tests/test_*.cpp files (with arguments: only the tests whose names contain
// one of them, e.g. `sculptforge_tests qremesh_guide`). `--shard K/N` runs only shard K (0-based) of N, so the
// suite can run as N processes at once (CMake registers sculptforge_tests_0of16 ... for ctest -j); every test is
// in exactly one shard (sf::test::inShard). Tests named nightly_* (benchmarks, stress, huge meshes) run only with
// `--nightly` (ctest entry sculptforge_tests_nightly, label nightly: T3 of docs/testing-flow.md), never in a shard.
#include "TestHarness.h"

#include "core/Keychain.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace sf::test {
namespace {

struct ShardTime {
    const char* name;
    double seconds;
};
// The slow tests' seconds (tools/test/shard_times.py). Tests not listed count as kUntimedSeconds.
constexpr ShardTime kShardTimes[] = {
#include "shard_times.inc"
};
constexpr double kUntimedSeconds = 0.1;

struct ShardPlan {
    std::unordered_map<std::string, int> shardOf; // the timed tests' shards (the others: by hash)
    std::vector<double> seconds;
};

const ShardPlan& shardPlan(int shards) {
    static std::map<int, ShardPlan> plans;
    auto [it, fresh] = plans.try_emplace(shards);
    ShardPlan& plan = it->second;
    if (!fresh) return plan;
    plan.seconds.assign(std::size_t(shards), 0.0);
    std::unordered_map<std::string, double> timed;
    for (const ShardTime& t : kShardTimes) timed[t.name] = t.seconds;
    std::vector<std::pair<double, const char*>> slow; // (seconds, name) of the registered timed tests
    for (const TestCase& t : registry()) {
        if (std::strncmp(t.name, "nightly_", 8) == 0) continue; // (no shard runs them)
        if (const auto f = timed.find(t.name); f != timed.end()) {
            slow.emplace_back(f->second, t.name);
        } else {
            plan.seconds[shardHash(t.name) % std::uint32_t(shards)] += kUntimedSeconds;
        }
    }
    std::sort(slow.begin(), slow.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : std::strcmp(a.second, b.second) < 0;
    });
    for (const auto& [sec, name] : slow) {
        const auto least = std::min_element(plan.seconds.begin(), plan.seconds.end());
        *least += sec;
        plan.shardOf[name] = int(least - plan.seconds.begin());
    }
    return plan;
}

} // namespace

std::vector<double> shardSeconds(int shards) { return shardPlan(std::max(shards, 1)).seconds; }

bool inShard(const char* name, int shard, int shards) {
    if (shards <= 1) return true;
    const ShardPlan& plan = shardPlan(shards);
    if (const auto f = plan.shardOf.find(name); f != plan.shardOf.end()) return f->second == shard;
    return shardHash(name) % std::uint32_t(shards) == std::uint32_t(shard);
}

} // namespace sf::test

int main(int argc, char** argv) {
    using namespace sf::test;
    sf::keychain::useMemoryForTests(true); // no test reads or writes the user's keychain (AI keys, sign-ins)
    int shard = 0, shards = 1;
    bool nightly = false;
    std::vector<const char*> filters;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shard") == 0 && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d/%d", &shard, &shards) != 2 || shards < 1 || shard < 0 || shard >= shards) {
                std::printf("--shard wants K/N with 0 <= K < N, got %s\n", argv[i]);
                return 2;
            }
        } else if (std::strcmp(argv[i], "--nightly") == 0) {
            nightly = true;
        } else {
            filters.push_back(argv[i]);
        }
    }
    int ran = 0;
    for (std::size_t index = 0; index < registry().size(); ++index) {
        const auto& t = registry()[index];
        if (!inShard(t.name, shard, shards)) continue;
        if ((std::strncmp(t.name, "nightly_", 8) == 0) != nightly) continue;
        bool wanted = filters.empty();
        for (const char* f : filters) wanted = wanted || std::strstr(t.name, f) != nullptr;
        if (!wanted) continue;
        ++ran;
        std::printf("[ RUN] %s\n", t.name);
        std::fflush(stdout); // so a crash still shows which test was running
        int before = g_failures;
        const auto start = std::chrono::steady_clock::now();
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::printf("  EXCEPTION: %s\n", e.what());
        }
        const long long ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s %s (%lld ms)\n", g_failures == before ? "[ OK ]" : "[FAIL]", t.name, ms);
        std::fflush(stdout);
    }
    std::printf("\n%d tests, %d checks, %d failures\n", ran, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
