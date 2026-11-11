// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AtomicRef<T>: atomic operations on a plain integer in a vector, for the parallel brush passes that mark
// vertices and BVH leaves. std::atomic_ref where the standard library has it; Apple's libc++ and the Android NDK's
// (r27) don't yet, so there the GCC/Clang __atomic builtins do the same (relaxed order only, all these passes need;
// any trivially copyable T, floats too). Code uses AtomicRef, never std::atomic_ref directly.
#pragma once

#include <atomic>

namespace sf {

#if defined(__cpp_lib_atomic_ref)
template <typename T> using AtomicRef = std::atomic_ref<T>;
#else
template <typename T> class AtomicRef {
public:
    explicit AtomicRef(T& v) : p_(&v) {}
    T load(std::memory_order = std::memory_order_seq_cst) const {
        T r;
        __atomic_load(p_, &r, __ATOMIC_RELAXED);
        return r;
    }
    void store(T v, std::memory_order = std::memory_order_seq_cst) const { __atomic_store(p_, &v, __ATOMIC_RELAXED); }
    T exchange(T v, std::memory_order = std::memory_order_seq_cst) const {
        T r;
        __atomic_exchange(p_, &v, &r, __ATOMIC_RELAXED);
        return r;
    }
    T fetch_or(T v, std::memory_order = std::memory_order_seq_cst) const { return __atomic_fetch_or(p_, v, __ATOMIC_RELAXED); }
    bool compare_exchange_weak(T& expected, T desired, std::memory_order = std::memory_order_seq_cst) const {
        return __atomic_compare_exchange(p_, &expected, &desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    }

private:
    T* p_;
};
#endif

} // namespace sf
