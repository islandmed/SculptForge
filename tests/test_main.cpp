// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs every TEST registered by the tests/test_*.cpp files (with arguments: only the tests whose names contain
// one of them, e.g. `sculptforge_tests qremesh_guide`). `--shard K/N` runs only shard K (0-based) of N, so the
// suite can run as N processes at once (CMake registers sculptforge_tests_0of16 ... for ctest -j); every test is
// in exactly one shard (sf::test::inShard). Tests named nightly_* (benchmarks, stress, huge meshes) run only with
// `--nightly` (ctest entry sculptforge_tests_nightly, label nightly: T3 of docs/testing-flow.md), never in a shard.
// `--area a,b` (or SF_TEST_AREA=a,b) runs only the cases of those areas: a case's area is its file's line in
// tests/areas.map [units]; a file the map doesn't cover always runs. `--list-areas` prints "file<TAB>area" per file.
#include "TestHarness.h"

#include "core/Keychain.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <sstream>
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

// A glob with `*` only (matches anything, '/' too), as in tests/areas.map.
bool globMatch(const char* g, const char* s) {
    if (!*g) return !*s;
    if (*g == '*') return globMatch(g + 1, s) || (*s && globMatch(g, s + 1));
    return *g == *s && globMatch(g + 1, s + 1);
}

// tests/areas.map [units]: "test_x   area" lines, first match wins. Empty when the map can't be read.
const std::vector<std::pair<std::string, std::string>>& unitAreas() {
    static const auto rules = [] {
        std::vector<std::pair<std::string, std::string>> r;
        const char* env = std::getenv("SF_TEST_AREAS_MAP");
        std::ifstream in(env ? std::string(env) : std::string(SF_TEST_DATA_DIR) + "/../areas.map");
        std::string line, section;
        while (std::getline(in, line)) {
            if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
            std::istringstream ls(line);
            std::string a, b;
            if (!(ls >> a)) continue;
            if (a.front() == '[') {
                section = a;
                continue;
            }
            if (section == "[units]" && (ls >> b)) r.emplace_back(a, b);
        }
        return r;
    }();
    return rules;
}

} // namespace

std::string areaOfFile(const char* file) {
    std::string stem = file;
    if (const auto slash = stem.find_last_of("/\\"); slash != std::string::npos) stem.erase(0, slash + 1);
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".cpp") == 0) stem.erase(stem.size() - 4);
    for (const auto& [glob, area] : unitAreas())
        if (globMatch(glob.c_str(), stem.c_str())) return area;
    return "";
}

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
    std::string areas = std::getenv("SF_TEST_AREA") ? std::getenv("SF_TEST_AREA") : "";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shard") == 0 && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d/%d", &shard, &shards) != 2 || shards < 1 || shard < 0 || shard >= shards) {
                std::printf("--shard wants K/N with 0 <= K < N, got %s\n", argv[i]);
                return 2;
            }
        } else if (std::strcmp(argv[i], "--nightly") == 0) {
            nightly = true;
        } else if (std::strcmp(argv[i], "--area") == 0 && i + 1 < argc) {
            areas = argv[++i];
        } else if (std::strcmp(argv[i], "--list-areas") == 0) {
            std::map<std::string, std::string> files;
            for (const auto& t : registry()) files[t.file] = areaOfFile(t.file);
            for (const auto& [f, a] : files) std::printf("%s\t%s\n", f.c_str(), a.empty() ? "-" : a.c_str());
            return 0;
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
        if (!areas.empty()) {
            const std::string a = areaOfFile(t.file);
            if (!a.empty() && ("," + areas + ",").find("," + a + ",") == std::string::npos) continue;
        }
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
