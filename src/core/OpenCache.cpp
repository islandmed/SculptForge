// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/OpenCache.h"

#include "core/Micro.h"
#include "core/ThreadPool.h"

#include <atomic>
#include <cstring>

namespace sf {

size_t openCacheMinTriangles = 8'000'000;
OpenCacheUse lastOpenCacheUse;

namespace {

constexpr uint64_t kMul = 0x9E3779B97F4A7C15ull;

inline uint64_t mix(uint64_t h) {
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 29;
    return h;
}

// One block: four independent lanes (no dependency chain from one word to the next), then folded.
uint64_t hashBlock(const uint8_t* p, size_t n, uint64_t seed) {
    uint64_t h[4] = {seed ^ 0x243F6A8885A308D3ull, seed ^ 0x13198A2E03707344ull, seed ^ 0xA4093822299F31D0ull,
                     seed ^ 0x082EFA98EC4E6C89ull};
    size_t i = 0;
    for (; i + 32 <= n; i += 32)
        for (int k = 0; k < 4; ++k) {
            uint64_t w;
            std::memcpy(&w, p + i + size_t(k) * 8, 8);
            h[k] = (h[k] ^ w) * kMul;
            h[k] ^= h[k] >> 32;
        }
    uint64_t r = mix(h[0] ^ mix(h[1] + 1) ^ mix(h[2] + 2) ^ mix(h[3] + 3));
    for (; i < n; i += 8) {
        uint64_t w = 0;
        std::memcpy(&w, p + i, std::min<size_t>(8, n - i));
        r = mix((r ^ w) * kMul);
    }
    return mix(r ^ n);
}

} // namespace

uint64_t openCacheHash(const void* data, size_t bytes) {
    constexpr size_t kBlock = size_t(1) << 20;
    const size_t blocks = (bytes + kBlock - 1) / kBlock;
    std::vector<uint64_t> part(blocks);
    const auto* p = static_cast<const uint8_t*>(data);
    poolFor(blocks, [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) part[k] = hashBlock(p + k * kBlock, std::min(kBlock, bytes - k * kBlock), k);
    }, 1);
    uint64_t h = mix(bytes ^ kMul);
    for (uint64_t v : part) h = mix((h ^ v) * kMul);
    return h;
}

uint64_t openCacheKey(const Mesh& mesh) {
    return mix(openCacheHash(mesh.triangles.data(), mesh.triangles.size() * sizeof(glm::uvec3)) ^ (uint64_t(mesh.vertexCount()) * kMul));
}

bool microAdoptEdgeNeighbors(const Mesh& mesh, std::vector<uint32_t>& out, std::vector<uint32_t>& saved) {
    const size_t nt = mesh.triangleCount();
    if (saved.size() != nt * 3) return false;
    std::atomic<bool> ok = true;
    poolFor(nt, [&](size_t b, size_t e) {
        for (size_t t = b; t < e && ok.load(std::memory_order_relaxed); ++t) {
            const glm::uvec3 tr = mesh.triangles[t];
            for (int k = 0; k < 3; ++k) {
                const uint32_t nb = saved[t * 3 + size_t(k)];
                if (nb == MicroMesh::kNone) continue;
                const uint32_t u = nb >> 2, f = nb & 3;
                if (u >= nt || f > 2 || u == t) {
                    ok = false;
                    break;
                }
                const glm::uvec3 o = mesh.triangles[u];
                const uint32_t a = tr[k], c = tr[(k + 1) % 3], p = o[int(f)], q = o[int(f + 1) % 3];
                if (!((p == a && q == c) || (p == c && q == a))) {
                    ok = false;
                    break;
                }
            }
        }
    }, 1 << 14);
    if (!ok) return false;
    out = std::move(saved);
    std::vector<uint32_t>().swap(saved);
    return true;
}

} // namespace sf
