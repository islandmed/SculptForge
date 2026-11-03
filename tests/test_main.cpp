// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs every TEST registered by the tests/test_*.cpp files (with arguments: only the tests whose names contain
// one of them, e.g. `sculptforge_tests qremesh_guide`). `--shard K/N` runs only shard K (0-based) of N, so the
// suite can run as N processes at once (CMake registers sculptforge_tests_0of16 ... for ctest -j); every test is
// in exactly one shard (sf::test::inShard).
#include "TestHarness.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

int main(int argc, char** argv) {
    using namespace sf::test;
    int shard = 0, shards = 1;
    std::vector<const char*> filters;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shard") == 0 && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d/%d", &shard, &shards) != 2 || shards < 1 || shard < 0 || shard >= shards) {
                std::printf("--shard wants K/N with 0 <= K < N, got %s\n", argv[i]);
                return 2;
            }
        } else {
            filters.push_back(argv[i]);
        }
    }
    int ran = 0;
    for (std::size_t index = 0; index < registry().size(); ++index) {
        const auto& t = registry()[index];
        if (!inShard(t.name, shard, shards)) continue;
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
