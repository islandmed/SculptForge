// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The test runner itself: CI runs sculptforge_tests as several shards at once (--shard K/N), which must
// together run every test exactly once.
#include "TestHarness.h"

#include <vector>

TEST(test_shards_cover_every_test_once) {
    using sf::test::inShard;
    const auto& tests = sf::test::registry();
    CHECK(tests.size() > 100); // the whole suite is registered
    for (int shards = 1; shards <= 17; ++shards) {
        bool once = true;
        for (const auto& t : tests) {
            int runs = 0;
            for (int k = 0; k < shards; ++k) runs += inShard(t.name, k, shards) ? 1 : 0;
            once = once && runs == 1;
        }
        CHECK(once);
    }
    // Shards stay balanced by count: with 16 shards (CMakeLists.txt) none gets more than twice its share, and with
    // CI's 2 and 4 machines (every other / every fourth shard) none more than 1.3 times.
    for (int shards : {2, 4, 16}) {
        std::vector<std::size_t> n(std::size_t(shards), 0);
        for (const auto& t : tests)
            for (int k = 0; k < shards; ++k) n[std::size_t(k)] += inShard(t.name, k, shards) ? 1 : 0;
        const double share = double(tests.size()) / shards;
        for (std::size_t c : n) CHECK(double(c) <= share * (shards == 16 ? 2.0 : 1.3));
    }
}
