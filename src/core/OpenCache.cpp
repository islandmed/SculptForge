// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/OpenCache.h"

#include "core/Micro.h"
#include "core/ThreadPool.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

namespace sf {

size_t openCacheMinTriangles = 8'000'000;
OpenCacheUse lastOpenCacheUse;

bool openCacheEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("SF_OPEN_CACHE");
        return !(e && std::string(e) == "0");
    }();
    return on;
}

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

std::vector<uint8_t> openCachePack(const std::vector<uint32_t>& values) {
    const size_t blocks = (values.size() + kOpenCacheBlock - 1) / kOpenCacheBlock;
    std::vector<std::vector<uint8_t>> parts(blocks);
    poolFor(blocks, [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            std::vector<uint8_t>& o = parts[k];
            const size_t v0 = k * kOpenCacheBlock, v1 = std::min(values.size(), v0 + kOpenCacheBlock);
            o.reserve((v1 - v0) * 2);
            uint32_t prev = 0;
            for (size_t i = v0; i < v1; ++i) {
                const int32_t d = int32_t(values[i] - prev);
                uint32_t z = (uint32_t(d) << 1) ^ uint32_t(d >> 31);
                prev = values[i];
                while (z >= 0x80) o.push_back(uint8_t(z | 0x80)), z >>= 7;
                o.push_back(uint8_t(z));
            }
        }
    }, 1);
    const size_t header = 8 + (blocks + 1) * 8;
    size_t total = header;
    for (const auto& p : parts) total += p.size();
    std::vector<uint8_t> out(total);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) out[at + size_t(i)] = uint8_t(v >> (8 * i)); };
    auto put64 = [&](size_t at, uint64_t v) { for (int i = 0; i < 8; ++i) out[at + size_t(i)] = uint8_t(v >> (8 * i)); };
    put32(0, kOpenCacheBlock);
    put32(4, uint32_t(blocks));
    uint64_t at = 0;
    for (size_t k = 0; k < blocks; ++k) {
        put64(8 + k * 8, at);
        std::memcpy(out.data() + header + at, parts[k].data(), parts[k].size());
        at += parts[k].size();
    }
    put64(8 + blocks * 8, at);
    return out;
}

bool openCacheUnpack(const uint8_t* data, size_t n, size_t count, std::vector<uint32_t>& out) {
    auto get32 = [&](size_t at) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(data[at + size_t(i)]) << (8 * i); return v; };
    auto get64 = [&](size_t at) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t(data[at + size_t(i)]) << (8 * i); return v; };
    if (n < 8 || get32(0) != kOpenCacheBlock) return false;
    const size_t blocks = get32(4);
    if (blocks != (count + kOpenCacheBlock - 1) / kOpenCacheBlock || (n - 8) / 8 < blocks + 1) return false;
    const size_t header = 8 + (blocks + 1) * 8;
    std::vector<uint64_t> off(blocks + 1);
    for (size_t k = 0; k <= blocks; ++k) off[k] = get64(8 + k * 8);
    if (off[0] != 0 || off[blocks] != n - header) return false;
    for (size_t k = 0; k < blocks; ++k)
        if (off[k + 1] < off[k]) return false;
    out.resize(count);
    std::atomic<bool> ok = true;
    const uint8_t* body = data + header;
    poolFor(blocks, [&](size_t b, size_t e) {
        for (size_t k = b; k < e && ok.load(std::memory_order_relaxed); ++k) {
            const uint8_t* p = body + off[k];
            const uint8_t* const end = body + off[k + 1];
            const size_t v0 = k * kOpenCacheBlock, v1 = std::min(count, v0 + kOpenCacheBlock);
            uint32_t prev = 0;
            for (size_t i = v0; i < v1; ++i) {
                uint32_t z = 0;
                for (int shift = 0;; shift += 7) {
                    if (p == end || shift > 28 || (shift == 28 && *p > 0x0F)) {
                        ok = false;
                        break;
                    }
                    const uint8_t c = *p++;
                    z |= uint32_t(c & 0x7F) << shift;
                    if (!(c & 0x80)) break;
                }
                if (!ok) break;
                prev += uint32_t(int32_t(z >> 1) ^ -int32_t(z & 1));
                out[i] = prev;
            }
            if (p != end) ok = false;
        }
    }, 1);
    if (!ok) out.clear();
    return ok;
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
