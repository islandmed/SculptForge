// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The test runner itself: CI runs sculptforge_tests as several shards at once (--shard K/N), which must
// together run every test exactly once.
#include "TestHarness.h"

#include <vector>

TEST(test_shards_cover_every_test_once) {
    using sf::test::inShard;
    const std::size_t count = sf::test::registry().size();
    CHECK(count > 100); // the whole suite is registered
    for (int shards = 1; shards <= 9; ++shards) {
        std::vector<int> runs(count + 17, 0); // a few more than registered: the rule must not depend on the count
        for (int k = 0; k < shards; ++k)
            for (std::size_t i = 0; i < runs.size(); ++i)
                if (inShard(i, k, shards)) ++runs[i];
        bool once = true;
        for (int r : runs) once = once && r == 1;
        CHECK(once);
    }
    // Shards stay balanced: with 4 shards none gets more than a quarter (rounded up) of the tests.
    for (int k = 0; k < 4; ++k) {
        std::size_t n = 0;
        for (std::size_t i = 0; i < count; ++i) n += inShard(i, k, 4) ? 1 : 0;
        CHECK(n <= (count + 3) / 4);
    }
}
