// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AtomicRef (core/AtomicRef.h): the parallel brush passes mark vertices and BVH leaves through it. std::atomic_ref
// where the library has it, the GCC/Clang builtin version on Apple and the Android NDK; both must keep the promise
// the passes rely on: concurrent marks of the same element are exact (never lost, never torn).
#include "TestHarness.h"

#include "core/AtomicRef.h"

#include <cstdint>
#include <thread>
#include <vector>

using namespace sf;

namespace {

template <template <typename> class Ref> void markInvariant() {
    constexpr int kThreads = 4, kElements = 2048;
    // Every thread claims every element with exchange: exactly one thread wins each.
    std::vector<uint32_t> stamp(kElements, 0);
    std::vector<int> wins(kThreads, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
            for (int i = 0; i < kElements; ++i)
                if (Ref<uint32_t>(stamp[size_t(i)]).exchange(7u, std::memory_order_relaxed) == 0u) ++wins[size_t(t)];
        });
    for (auto& th : threads) th.join();
    int total = 0;
    for (const int w : wins) total += w;
    CHECK(total == kElements);
    for (const uint32_t v : stamp) CHECK(v == 7u);

    // Bits set from several threads all survive.
    std::vector<uint32_t> bits(kElements, 0);
    threads.clear();
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
            for (int i = 0; i < kElements; ++i) Ref<uint32_t>(bits[size_t(i)]).fetch_or(1u << t, std::memory_order_relaxed);
        });
    for (auto& th : threads) th.join();
    for (const uint32_t v : bits) CHECK(v == 0xFu);

    // A float is stored whole.
    float f = 0.0f;
    Ref<float>(f).store(1.25f, std::memory_order_relaxed);
    CHECK(Ref<float>(f).load(std::memory_order_relaxed) == 1.25f);
}

} // namespace

TEST(atomic_ref_invariant_marks_are_exact) {
    markInvariant<AtomicRef>(); // what the build uses
#if defined(__cpp_lib_atomic_ref) && (defined(__GNUC__) || defined(__clang__))
    markInvariant<AtomicRefBuiltin>(); // the fallback, even where std::atomic_ref exists
#endif
}
