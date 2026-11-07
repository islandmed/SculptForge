// Resident micro code (billion-ideas prototype, standalone, headless).
// Claim under test: a billion-micro-triangle .sfdetail can stay RESIDENT ON THE GPU, compressed, in a form a
// GL 3.3 vertex shader decodes per point in O(1) texel fetches (random access, no recursion, no CPU inflate).
// Layout per tile (level L, here 7): the level L-2 grid decoded (int16, as the coarse grid is today), plus every
// level-L point's residual against the integer upsample of that grid, in fixed-width blocks of 32 points
// (per block a 4-bit width; per 8 blocks a 16-bit word offset). Lossless; verified by decoding every point.
// Build: g++ -O2 -std=c++20 -pthread tools/billion/resident_codec.cpp -lzstd -o resident_codec
// Run:   resident_codec [file.sfdetail] [threads] [mid levels below L, default 2]
#include <zstd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

static uint32_t idx(uint32_t i, uint32_t j, uint32_t n) { return j * (n + 1) - j * (j - 1) / 2 + i; }
template <class T> static T get(const uint8_t* b, size_t at) { T v; std::memcpy(&v, b + at, sizeof v); return v; }

// FocusDetail.cpp's row-delta decode and integer upsample, copied so the tool stands alone.
static void deltaDecode(int16_t* v, uint32_t n) {
    uint32_t p = 0;
    for (uint32_t j = 0; j <= n; ++j)
        for (uint32_t i = 0; i + j <= n; ++i, ++p) {
            const int prev = p == 0 ? 0 : i > 0 ? v[p - 1] : v[idx(0, j - 1, n)];
            v[p] = int16_t(uint16_t(uint16_t(v[p]) + uint16_t(prev)));
        }
}
// The predicted value of point (i, j) of an n grid from the cn grid `c` (exact at its points, linear between).
static inline int predict(const int16_t* c, uint32_t cn, uint32_t n, uint32_t i, uint32_t j) {
    const int s = int(n / cn); int sh = 0; while ((1 << sh) < s) ++sh;
    const uint32_t I = i >> sh, J = j >> sh; const int fi = int(i) & (s - 1), fj = int(j) & (s - 1);
    int sum;
    if (fi + fj <= s) sum = (s - fi - fj) * c[idx(I, J, cn)] + (fi ? fi * c[idx(I + 1, J, cn)] : 0) + (fj ? fj * c[idx(I, J + 1, cn)] : 0);
    else sum = (fi + fj - s) * c[idx(I + 1, J + 1, cn)] + (s - fi) * c[idx(I, J + 1, cn)] + (s - fj) * c[idx(I + 1, J, cn)];
    const int q = sum + s / 2;
    return q >= 0 ? q >> sh : -((-q + s - 1) >> sh);
}
static inline uint32_t zz(int v) { return (uint32_t(v) << 1) ^ uint32_t(v >> 31); }
static inline int unzz(uint32_t u) { return int(u >> 1) ^ -int(u & 1); }

constexpr uint32_t kBlock = 32, kGroup = 8;

struct Encoded {           // one tile
    std::vector<int16_t> mid;      // level L-2 grid, decoded
    std::vector<uint8_t> widths;   // per block, 0..16 (4 bits on the GPU; 16 stored as 15+escape in a real format)
    std::vector<uint16_t> groupOff;// per group of 8 blocks: payload word offset
    std::vector<uint32_t> words;   // payload
};

static void encode(const int16_t* full, uint32_t n, uint32_t mn, Encoded& e) {
    const uint32_t S = (n + 1) * (n + 2) / 2, ms = n / mn;
    e.mid.resize((mn + 1) * (mn + 2) / 2);
    for (uint32_t J = 0; J <= mn; ++J) for (uint32_t I = 0; I + J <= mn; ++I) e.mid[idx(I, J, mn)] = full[idx(I * ms, J * ms, n)];
    std::vector<uint32_t> r(S);
    uint32_t p = 0;
    for (uint32_t j = 0; j <= n; ++j) for (uint32_t i = 0; i + j <= n; ++i, ++p) r[p] = zz(full[p] - predict(e.mid.data(), mn, n, i, j));
    const uint32_t blocks = (S + kBlock - 1) / kBlock;
    e.widths.assign(blocks, 0); e.groupOff.clear(); e.words.clear();
    uint64_t bitpos = 0; // within the tile, word aligned per group
    for (uint32_t b = 0; b < blocks; ++b) {
        if (b % kGroup == 0) { bitpos = (bitpos + 31) & ~uint64_t(31); e.groupOff.push_back(uint16_t(bitpos / 32)); }
        uint32_t mx = 0; for (uint32_t k = b * kBlock; k < std::min(S, (b + 1) * kBlock); ++k) mx |= r[k];
        uint32_t w = 0; while (w < 32 && (mx >> w)) ++w;
        e.widths[b] = uint8_t(w);
        for (uint32_t k = b * kBlock; k < (b + 1) * kBlock; ++k) {
            const uint32_t v = k < S ? r[k] : 0;
            for (uint32_t t = 0; t < w; ++t, ++bitpos) {
                if (bitpos / 32 >= e.words.size()) e.words.push_back(0);
                if ((v >> t) & 1) e.words[bitpos / 32] |= 1u << (bitpos % 32);
            }
        }
    }
}

// What the vertex shader would do for point (i, j): 3 mid fetches + 1 width word + 1 group offset + 1-2 payload words.
static inline int decodePoint(const Encoded& e, uint32_t n, uint32_t mn, uint32_t i, uint32_t j) {
    const uint32_t p = idx(i, j, n), b = p / kBlock, g = b / kGroup;
    uint32_t bit = uint32_t(e.groupOff[g]) * 32;
    for (uint32_t k = g * kGroup; k < b; ++k) bit += e.widths[k] * kBlock; // 8 nibbles = one uint word on the GPU
    const uint32_t w = e.widths[b];
    uint32_t v = 0;
    if (w) {
        bit += (p % kBlock) * w;
        const uint64_t lo = e.words[bit / 32], hi = bit / 32 + 1 < e.words.size() ? e.words[bit / 32 + 1] : 0;
        v = uint32_t(((hi << 32 | lo) >> (bit % 32)) & ((1ull << w) - 1));
    }
    return predict(e.mid.data(), mn, n, i, j) + unzz(v);
}

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : std::string(getenv("HOME")) + "/.cache/sculptforge/standard-1b-v1.sfdetail";
    const unsigned threads = argc > 2 ? unsigned(atoi(argv[2])) : std::max(1u, std::thread::hardware_concurrency() / 2);
    const int fd = open(path.c_str(), O_RDONLY); struct stat st{};
    if (fd < 0 || fstat(fd, &st) != 0) { std::fprintf(stderr, "can't open %s\n", path.c_str()); return 1; }
    const uint8_t* b = (const uint8_t*)mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (std::memcmp(b, "SFDETAIL", 8) != 0 || get<uint32_t>(b, 12) != 1) { std::fprintf(stderr, "need a zstd .sfdetail v1\n"); return 1; }
    const uint32_t level = get<uint32_t>(b, 16), T = get<uint32_t>(b, 24);
    const uint64_t dirOff = get<uint64_t>(b, 56);
    const uint32_t n = 1u << level, S = (n + 1) * (n + 2) / 2, mn = n >> (argc > 3 ? atoi(argv[3]) : 2), MS = (mn + 1) * (mn + 2) / 2;
    std::printf("file %s: %u tiles, level %u, %u points a tile, %.3fG micro triangles, %.1f MB on disk (zstd)\n",
                path.c_str(), T, level, S, double(T) * n * n / 1e9, st.st_size / 1e6);

    std::atomic<uint32_t> next{0};
    std::atomic<uint64_t> payloadBits{0}, widthBits{0}, groupBits{0}, midBytes{0}, mismatches{0}, hist[17]{};
    std::atomic<uint64_t> encNs{0}, decNs{0}, decPoints{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back([&] {
        std::vector<int16_t> full(S); Encoded e; ZSTD_DCtx* dctx = ZSTD_createDCtx();
        uint64_t pb = 0, wb = 0, gb = 0, mb = 0, mm = 0, en = 0, de = 0, dp = 0, h[17]{};
        for (uint32_t tile; (tile = next.fetch_add(1)) < T;) {
            const uint8_t* d = b + dirOff + size_t(tile) * 24;
            const uint64_t off = get<uint64_t>(d, 0); const uint32_t bytes = get<uint32_t>(d, 8);
            if (ZSTD_decompressDCtx(dctx, full.data(), S * 2, b + off, bytes) != S * 2) { ++mm; continue; }
            deltaDecode(full.data(), n);
            auto t0 = std::chrono::steady_clock::now();
            encode(full.data(), n, mn, e);
            auto t1 = std::chrono::steady_clock::now();
            int64_t chk = 0; uint32_t p = 0;
            for (uint32_t j = 0; j <= n; ++j) for (uint32_t i = 0; i + j <= n; ++i, ++p) { const int v = decodePoint(e, n, mn, i, j); chk += v; mm += v != full[p]; }
            auto t2 = std::chrono::steady_clock::now();
            if (chk == 0x7fffffffffff) std::puts("");
            en += (t1 - t0).count(); de += (t2 - t1).count(); dp += S;
            for (uint8_t w : e.widths) { pb += uint64_t(w) * kBlock; ++h[std::min<int>(w, 16)]; }
            wb += e.widths.size() * 4; gb += e.groupOff.size() * 16; mb += MS * 2;
        }
        payloadBits += pb; widthBits += wb; groupBits += gb; midBytes += mb; mismatches += mm; encNs += en; decNs += de; decPoints += dp;
        for (int k = 0; k <= 16; ++k) hist[k] += h[k];
        ZSTD_freeDCtx(dctx);
    });
    auto w0 = std::chrono::steady_clock::now();
    for (auto& th : pool) th.join();
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
    const double tris = double(T) * n * n, pts = double(T) * S;
    const double resid = (payloadBits + widthBits + groupBits) / 8.0, mid = double(midBytes);
    std::printf("lossless: %s (%llu mismatches)\n", mismatches ? "NO" : "yes", (unsigned long long)mismatches.load());
    std::printf("mid grid (level %u, decoded int16): %.1f MB = %.4f B/tri\n", level - (argc > 3 ? atoi(argv[3]) : 2), mid / 1e6, mid / tris);
    std::printf("residual blocks: payload %.1f MB + widths %.1f MB + offsets %.1f MB = %.1f MB = %.3f bits/point\n",
                payloadBits / 8e6, widthBits / 8e6, groupBits / 8e6, resid / 1e6, resid * 8 / pts);
    std::printf("RESIDENT TOTAL: %.1f MB = %.4f B/micro-tri (today's renderer: 6 B/point = %.0f MB; zstd file %.1f MB, not random access)\n",
                (mid + resid) / 1e6, (mid + resid) / tris, pts * 6 / 1e6, st.st_size / 1e6);
    std::printf("block widths:"); for (int k = 0; k <= 16; ++k) if (hist[k]) std::printf(" %d:%.1f%%", k, 100.0 * hist[k] / (pts / kBlock)); std::puts("");
    std::printf("encode %.1f ns/point (one thread), decode per point random access %.1f ns/point (one thread, scalar CPU emulation)\n",
                double(encNs) / pts, double(decNs) / double(decPoints));
    std::printf("wall %.1f s on %u threads (zstd inflate + encode + verify every point)\n", wall, threads);
}
