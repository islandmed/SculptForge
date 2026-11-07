// Real-scan stand-in for resident_codec (billion-ideas prototype, standalone, headless).
// No level-7 micro asset from a real scan exists yet, so this renders a dense scan's depth orthographically
// (one pixel = one micro spacing), cuts it into level-7 triangular tiles (129 points an edge, heights in steps of
// 1/32 micro spacing, as microDivide's base edge / 4096 at level 7), minus each tile's plane through its corners
// (the part a base triangle carries), and writes a minimal zstd .sfdetail v1 that resident_codec reads.
// Tiles touching the background or overflowing int16 are skipped (counted).
// Build: g++ -O2 -std=c++20 -pthread tools/billion/scan_tiles.cpp -lzstd -o scan_tiles
// Run:   scan_tiles scan.ply out.sfdetail [pixels on the long side, default 8192] [view: 0 front, 1 side]
#include <zstd.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

static uint32_t idx(uint32_t i, uint32_t j, uint32_t n) { return j * (n + 1) - j * (j - 1) / 2 + i; }
static float bef(const uint8_t* p) { uint32_t u = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; float f; std::memcpy(&f, &u, 4); return f; }
static int32_t bei(const uint8_t* p) { return int32_t(uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]); }

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "scan_tiles scan.ply out.sfdetail [px] [view]\n"); return 1; }
    const int R = argc > 3 ? atoi(argv[3]) : 8192, view = argc > 4 ? atoi(argv[4]) : 0;
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 1;
    char line[256]; size_t V = 0, F = 0; bool be = false;
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "element vertex %zu", &V) == 1) {}
        else if (std::sscanf(line, "element face %zu", &F) == 1) {}
        else if (std::strstr(line, "binary_big_endian")) be = true;
        else if (!std::strncmp(line, "end_header", 10)) break;
    }
    if (!be || !V || !F) { std::fprintf(stderr, "need a binary big-endian ply (x y z float; uchar int list)\n"); return 1; }
    std::vector<float> P(V * 3); std::vector<uint8_t> buf(V * 12);
    if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) return 1;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t k = 0; k < V * 3; ++k) { P[k] = bef(&buf[k * 4]); lo[k % 3] = std::min(lo[k % 3], P[k]); hi[k % 3] = std::max(hi[k % 3], P[k]); }
    buf.clear(); buf.shrink_to_fit();
    // view axes: screen x, screen y, depth
    const int ax = view == 0 ? 0 : 1, ay = 2, az = view == 0 ? 1 : 0;
    // Lucy stands along z; use the largest extent as the long side
    const float ext = std::max(hi[ax] - lo[ax], hi[ay] - lo[ay]), s = (R - 1) / ext;
    const int W = int((hi[ax] - lo[ax]) * s) + 2, H = int((hi[ay] - lo[ay]) * s) + 2;
    std::vector<float> Z(size_t(W) * H, -std::numeric_limits<float>::infinity());
    std::vector<uint8_t> fb(13);
    for (size_t t = 0; t < F; ++t) {
        if (std::fread(fb.data(), 1, 13, f) != 13 || fb[0] != 3) { std::fprintf(stderr, "bad face %zu\n", t); return 1; }
        float x[3], y[3], z[3];
        for (int c = 0; c < 3; ++c) {
            const size_t v = size_t(bei(&fb[1 + 4 * c]));
            x[c] = (P[v * 3 + ax] - lo[ax]) * s; y[c] = (P[v * 3 + ay] - lo[ay]) * s; z[c] = (P[v * 3 + az] - lo[az]) * s;
        }
        const float area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
        if (std::fabs(area) < 1e-12f) continue;
        const int x0 = std::max(0, int(std::floor(std::min({x[0], x[1], x[2]})))), x1 = std::min(W - 1, int(std::ceil(std::max({x[0], x[1], x[2]}))));
        const int y0 = std::max(0, int(std::floor(std::min({y[0], y[1], y[2]})))), y1 = std::min(H - 1, int(std::ceil(std::max({y[0], y[1], y[2]}))));
        for (int py = y0; py <= y1; ++py) for (int px = x0; px <= x1; ++px) {
            const float w0 = ((x[1] - px) * (y[2] - py) - (x[2] - px) * (y[1] - py)) / area;
            const float w1 = ((x[2] - px) * (y[0] - py) - (x[0] - px) * (y[2] - py)) / area;
            const float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const float d = w0 * z[0] + w1 * z[1] + w2 * z[2]; // nearer = larger
            float& zz = Z[size_t(py) * W + px];
            if (d > zz) zz = d;
        }
    }
    std::fclose(f); P.clear(); P.shrink_to_fit();
    // Tiles: equilateral triangles of 128 pixels an edge, alternating up and down, in rows.
    const uint32_t L = 7, n = 1u << L, S = (n + 1) * (n + 2) / 2;
    const float step = 1.0f / 32.0f, h = std::sqrt(3.0f) / 2;
    auto depth = [&](float px, float py, bool& ok) {
        const int ix = int(px), iy = int(py);
        if (ix < 0 || iy < 0 || ix + 1 >= W || iy + 1 >= H) { ok = false; return 0.0f; }
        const float fx = px - ix, fy = py - iy;
        const float a = Z[size_t(iy) * W + ix], b = Z[size_t(iy) * W + ix + 1], c = Z[size_t(iy + 1) * W + ix], d = Z[size_t(iy + 1) * W + ix + 1];
        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(d)) { ok = false; return 0.0f; }
        return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
    };
    std::FILE* o = std::fopen(argv[2], "wb");
    std::vector<uint8_t> header(128, 0); std::fwrite(header.data(), 1, 128, o);
    struct E { uint64_t off; uint32_t bytes; uint16_t range, pad; float err; uint32_t pad2; };
    std::vector<E> dir; uint64_t pos = 128; size_t skipBg = 0, skipRange = 0;
    std::vector<float> g(S); std::vector<int16_t> v(S), d(S); std::vector<uint8_t> out(ZSTD_compressBound(S * 2));
    for (int row = 0; (row + 1) * n * h < H; ++row)
        for (int col = 0; (col / 2 + 1) * float(n) + n / 2 < W; ++col) {
            const bool up = col % 2 == 0;
            const float bx = float(col / 2) * n + (up ? 0 : n / 2.0f), by = float(row) * n * h;
            bool ok = true;
            uint32_t p = 0;
            for (uint32_t j = 0; j <= n && ok; ++j) for (uint32_t i = 0; i + j <= n; ++i, ++p) {
                const float px = up ? bx + i + j * 0.5f : bx + i + j * 0.5f, py = up ? by + j * h : by + (n - j) * h;
                g[p] = depth(px, py, ok);
                if (!ok) break;
            }
            if (!ok) { ++skipBg; continue; }
            const float c0 = g[idx(0, 0, n)], c1 = g[idx(n, 0, n)], c2 = g[idx(0, n, n)];
            int mx = 0; p = 0;
            for (uint32_t j = 0; j <= n; ++j) for (uint32_t i = 0; i + j <= n; ++i, ++p) {
                const float plane = c0 + (c1 - c0) * (float(i) / n) + (c2 - c0) * (float(j) / n);
                const long q = std::lround((g[p] - plane) / step);
                mx = std::max<long>(mx, std::labs(q)); v[p] = int16_t(std::clamp<long>(q, -32767, 32767));
            }
            if (mx > 32767) { ++skipRange; continue; }
            p = 0; // row delta, as FocusDetail
            for (uint32_t j = 0; j <= n; ++j) for (uint32_t i = 0; i + j <= n; ++i, ++p) {
                const int prev = p == 0 ? 0 : i > 0 ? v[p - 1] : v[idx(0, j - 1, n)];
                d[p] = int16_t(uint16_t(uint16_t(v[p]) - uint16_t(prev)));
            }
            const size_t r = ZSTD_compress(out.data(), out.size(), d.data(), S * 2, 3);
            std::fwrite(out.data(), 1, r, o);
            dir.push_back({pos, uint32_t(r), uint16_t(mx), 0, 0, 0}); pos += r;
        }
    const uint64_t dirOff = pos;
    std::fwrite(dir.data(), sizeof(E), dir.size(), o);
    auto put32 = [&](size_t at, uint32_t x) { std::memcpy(&header[at], &x, 4); };
    auto put64 = [&](size_t at, uint64_t x) { std::memcpy(&header[at], &x, 8); };
    std::memcpy(header.data(), "SFDETAIL", 8); put32(8, 1); put32(12, 1); put32(16, L); put32(20, 3); put32(24, uint32_t(dir.size()));
    put32(28, 1); std::memcpy(&header[32], &step, 4); put32(36, 1024); put64(56, dirOff); put64(64, dir.size() * sizeof(E));
    std::fseek(o, 0, SEEK_SET); std::fwrite(header.data(), 1, 128, o); std::fclose(o);
    std::printf("depth %dx%d, %zu tiles written (%.1fM micro triangles), skipped %zu background, %zu range\n",
                W, H, dir.size(), dir.size() * double(n * n) / 1e6, skipBg, skipRange);
}
