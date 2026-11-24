// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/CreatureSkin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace sf {

namespace {

constexpr float kTau = 6.2831853f;

float sat(float t) { return std::clamp(t, 0.0f, 1.0f); }
float smooth01(float t) {
    t = sat(t);
    return t * t * (3.0f - 2.0f * t);
}
float mix(float a, float b, float t) { return a + (b - a) * t; }

int wrap(int i, int period) {
    const int m = i % period;
    return m < 0 ? m + period : m;
}

uint32_t hash(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}
float hash01(uint32_t h) { return float(h & 0xffffu) / 65535.0f; }

// Gradient noise (about -1..1) whose lattice repeats every `period` cells; quintic fade, so no grid creases show
// at close range the way value noise's do.
float gnoise(float x, float y, int period, uint32_t seed) {
    static const float kGrad[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7071f, 0.7071f}, {-0.7071f, 0.7071f},
                                      {0.7071f, -0.7071f}, {-0.7071f, -0.7071f}};
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx), iy = int(fy);
    const float tx = x - fx, ty = y - fy;
    auto dot = [&](int dx, int dy) {
        const float* g = kGrad[hash(wrap(ix + dx, period), wrap(iy + dy, period), seed) & 7u];
        return g[0] * (tx - float(dx)) + g[1] * (ty - float(dy));
    };
    auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    const float u = fade(tx), v = fade(ty);
    return 1.4f * mix(mix(dot(0, 0), dot(1, 0), u), mix(dot(0, 1), dot(1, 1), u), v);
}

// fBm of periodic octaves (each doubles the frequency and the period, so the sum repeats too); about -1..1.
float fbm(float x, float y, int period, uint32_t seed, int octaves = 4, float gain = 0.5f) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += gnoise(x + 0.31f * float(o), y + 0.67f * float(o), period, seed + uint32_t(o) * 101u) * amp;
        norm += amp;
        x *= 2.0f;
        y *= 2.0f;
        period *= 2;
        amp *= gain;
    }
    return sum / norm;
}

// Worley cells on a lattice that repeats every px x py cells. f1: distance to the nearest point; edge: distance
// to the nearest cell border (the true border, not f2 - f1, so creases and grooves keep an even width); id: the
// nearest cell's hash.
struct Cells {
    float f1 = 1e9f, edge = 1e9f;
    uint32_t id = 0;
};
Cells cells(float x, float y, int px, int py, uint32_t seed, float jitter = 1.0f) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const float fx = x - float(ix), fy = y - float(iy);
    auto point = [&](int gx, int gy, float& ox, float& oy) {
        const uint32_t h = hash(wrap(gx, px), wrap(gy, py), seed);
        ox = float(gx - ix) + 0.5f + jitter * (hash01(h) - 0.5f) - fx;
        oy = float(gy - iy) + 0.5f + jitter * (hash01(h >> 16) - 0.5f) - fy;
        return h;
    };
    Cells c;
    float best = 1e9f, mx = 0.0f, my = 0.0f;
    int bx = ix, by = iy;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            float ox, oy;
            const uint32_t h = point(ix + dx, iy + dy, ox, oy);
            const float d = ox * ox + oy * oy;
            if (d < best) best = d, mx = ox, my = oy, bx = ix + dx, by = iy + dy, c.id = h * 0x9e3779b9u;
        }
    c.f1 = std::sqrt(best);
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;
            float ox, oy;
            point(bx + dx, by + dy, ox, oy);
            const float lx = ox - mx, ly = oy - my, l2 = lx * lx + ly * ly;
            if (l2 < 1e-8f) continue;
            c.edge = std::min(c.edge, ((mx + ox) * lx + (my + oy) * ly) * 0.5f / std::sqrt(l2));
        }
    return c;
}

// 0 on a cell's border, 1 at its point: a dome profile that is the same for big and small cells.
float cellDome(const Cells& c) { return sat(c.edge / std::max(c.edge + c.f1, 1e-6f) * 2.0f); }

// Overlapping scales in offset rows, each hanging from its base below and rising to a free tip above (so a stroke
// with Follow stroke direction, drawn head to tail, points the tips back along the body). Lower rows lie over the
// row above, as real scales overlap. `p`: 1.2 diamond .. 2 oval; `keel`: a ridge down the middle.
float imbricate(float x, float y, int cols, int rows, float halfW, float halfH, float p, float keel, uint32_t seed,
                float rings = 0.0f) {
    const float X = x * float(cols), Y = y * float(rows);
    const int r0 = int(std::floor(Y));
    float h = 0.0f;
    for (int r = r0 - 2; r <= r0 + 2; ++r) {
        const float off = (wrap(r, rows) & 1) ? 0.5f : 0.0f;
        const int c0 = int(std::floor(X - off));
        for (int c = c0 - 1; c <= c0 + 1; ++c) {
            const uint32_t id = hash(wrap(c, cols), wrap(r, rows), seed);
            const float cx = float(c) + 0.5f + off + 0.08f * (hash01(id) - 0.5f);
            const float cy = float(r) + 0.5f + 0.08f * (hash01(id >> 16) - 0.5f);
            const float dx = (X - cx) / halfW, dy = (Y - cy) / halfH;
            const float d = std::pow(std::pow(std::abs(dx), p) + std::pow(std::abs(dy), p), 1.0f / p);
            if (d >= 1.0f) continue;
            const float t = sat(0.5f - 0.5f * dy); // 0 at the base, 1 at the tip
            // The free edge rolls over onto the scale below in a short bevel (a hard step would alias on a micro grid).
            const float cover = smooth01((1.0f - d) / 0.16f);
            float s = 0.25f + 0.75f * std::sqrt(t);
            if (keel > 0.0f) s += keel * (1.0f - smooth01(std::abs(dx) / 0.14f)) * smooth01(t / 0.6f);
            if (rings > 0.0f) s += rings * std::sin(d * 24.0f) * t;
            s = s * (0.82f + 0.18f * hash01(id >> 8)) * (0.35f + 0.65f * cover);
            h = mix(h, s, cover); // (later rows lie over earlier ones)
        }
    }
    return h;
}

// Fine lines across the tile with the wave vector (kx, ky) (whole numbers: they repeat), wobbled by `warp`
// (in line spacings), about `width` of a spacing wide (wider and narrower along them), broken into lengths of
// about 1/breaks of the tile; `keep` (-1..1) more or less of them.
float lines(float x, float y, float kx, float ky, float warp, float width, uint32_t seed, int breaks = 6, float keep = 0.25f) {
    const float ph = kx * x + ky * y + warp;
    const float d = std::abs(ph - std::round(ph));
    const float b = float(breaks);
    const float present = smooth01((fbm(x * b, y * b, breaks, seed, 2) + keep) / 0.5f);
    const float w = width * (0.6f + 0.8f * (0.5f + 0.5f * fbm(x * b * 2.0f, y * b * 2.0f, breaks * 2, seed + 7, 2)));
    return (1.0f - smooth01(d / w)) * present;
}

// A crease along cell borders whose width and depth wander along it (so a net of them doesn't read as grout).
float crease(const Cells& c, float width, float x, float y, int period, uint32_t seed) {
    const float n = 0.5f + 0.5f * fbm(x * float(period), y * float(period), period, seed, 2);
    const float w = width * (0.55f + 0.9f * n);
    return std::pow(sat(1.0f - c.edge / w), 1.4f) * smooth01((n - 0.15f) / 0.35f);
}

// A domed tubercle of radius r round a cell's point, ringed by a groove; `in` is how far inside it is (0..1).
float tubercle(const Cells& c, float r, float& in) {
    in = smooth01((r + 0.03f - c.f1) / 0.05f);
    const float dome = std::sqrt(sat(1.0f - (c.f1 / r) * (c.f1 / r)));
    const float groove = 1.0f - smooth01(std::abs(c.f1 - r) / 0.045f);
    return 0.3f + 0.55f * dome * dome - 0.45f * groove; // (a low cone, not a white coin)
}

// Pores: one per cell where hash < density, radius r0 .. r0 + rv of a cell, deeper in the middle.
float pores(const Cells& c, float density, float r0, float rv, float sharp) {
    if (hash01(c.id) >= density) return 0.0f;
    const float r = r0 + rv * hash01(c.id >> 9);
    return std::pow(sat(1.0f - c.f1 / r), sharp) * (0.6f + 0.4f * hash01(c.id >> 18));
}

// --- Hide wrinkles -------------------------------------------------------------------------------------------------

float elephantHide(float x, float y) {
    const float wx = fbm(x * 3.0f, y * 3.0f, 3, 201, 3), wy = fbm(x * 3.0f + 17.0f, y * 3.0f + 5.0f, 3, 203, 3);
    const float X = x + 0.03f * wx, Y = y + 0.03f * wy;
    const Cells big = cells(X * 6.0f, Y * 9.0f, 6, 9, 205, 0.9f);
    const Cells mid = cells(X * 14.0f + 0.3f * wx, Y * 20.0f, 14, 20, 207, 0.95f);
    const Cells fine = cells(X * 24.0f + 0.35f * fbm(x * 12.0f, y * 12.0f, 12, 219, 2), Y * 28.0f, 24, 28, 209, 1.0f);
    const float w1 = 0.05f + 0.05f * (fbm(x * 5.0f, y * 5.0f, 5, 211, 2) * 0.5f + 0.5f);
    const float c1 = std::pow(sat(1.0f - big.edge / w1), 1.6f);
    const float c2 = crease(mid, 0.1f, X, Y, 9, 215) * (0.6f + 0.4f * hash01(mid.id));
    const float c3 = crease(fine, 0.13f, X, Y, 17, 217);
    const float pad = std::sqrt(sat(big.edge / 0.45f)), pad2 = std::sqrt(sat(mid.edge / 0.4f));
    const float grain = fbm(x * 60.0f, y * 60.0f, 60, 213, 2);
    return 0.55f * pad + 0.25f * pad2 + 0.04f * grain - 0.9f * c1 - 0.45f * c2 - 0.15f * c3;
}

float hideFolds(float x, float y) {
    constexpr int kRows = 11;
    const float warp = fbm(x * 3.0f, y * 3.0f, 3, 221, 3);
    const float Y = y * float(kRows) + 0.9f * warp + 0.35f * std::sin(kTau * (2.0f * x + y) + 1.3f);
    const float row = std::floor(Y), f = Y - row;
    const float amp = 0.6f + 0.4f * hash01(hash(wrap(int(row), kRows), 0, 223));
    const float keep = smooth01((fbm(x * 5.0f, y * 5.0f, 5, 225, 2) + 0.35f) / 0.5f); // folds fade out here and there
    // A crease at each row line: steep above it, a long rounded roll below.
    const float roll = smooth01(f / 0.22f) * smooth01((1.0f - f) / 0.07f) * (1.0f - 0.45f * smooth01((f - 0.3f) / 0.7f));
    const float fold = mix(0.5f, roll, keep * amp);
    // Short creases across the folds, one cell per fold, wandering and fading.
    const Cells cr = cells(x * 22.0f + 0.6f * warp + 0.25f * fbm(x * 20.0f, y * 20.0f, 20, 226, 2), Y, 22, kRows, 227, 0.9f);
    const float cross = crease(cr, 0.07f, x, y, 9, 228) * smooth01(fbm(x * 8.0f, y * 8.0f, 8, 230, 2) / 0.4f);
    return fold - 0.22f * cross + 0.05f * fbm(x * 80.0f, y * 80.0f, 80, 229, 2);
}

float dinosaurHide(float x, float y) {
    const float n = fbm(x * 4.0f + 0.5f * fbm(x * 2.0f, y * 2.0f, 2, 231, 2), y * 4.0f, 4, 233, 4);
    const float fold = std::pow(1.0f - std::min(std::abs(n) * 1.7f, 1.0f), 3.0f); // meandering deep folds
    const float broad = 0.5f + 0.5f * fbm(x * 3.0f, y * 3.0f, 3, 235, 2);
    const Cells pb = cells(x * 30.0f + 0.6f * n, y * 30.0f, 30, 30, 237, 0.85f);
    const float pebble = std::pow(cellDome(pb), 0.7f) * smooth01(pb.edge / 0.1f) * (0.7f + 0.3f * hash01(pb.id));
    // Larger feature scales here and there among the pebbles, each in its own groove.
    const float w = 0.25f * fbm(x * 20.0f, y * 20.0f, 20, 238, 2);
    const Cells fs = cells(x * 11.0f + w, y * 11.0f + w, 11, 11, 239, 0.8f);
    float in = 0.0f;
    const float feature = tubercle(fs, 0.26f + 0.1f * hash01(fs.id >> 4), in);
    const float m = (fs.id % 3u == 0u) ? in : 0.0f;
    const float skin = mix(pebble * 0.6f, feature, m) - ((fs.id % 3u == 0u) ? 0.3f * (1.0f - smooth01(std::abs(fs.f1 - 0.3f) / 0.05f)) * (1.0f - m) : 0.0f);
    return 0.5f * skin + 0.3f * broad - 0.75f * fold * (0.6f + 0.4f * broad);
}

// --- Scales --------------------------------------------------------------------------------------------------------

float snakeScales(float x, float y) {
    return imbricate(x, y, 9, 14, 0.56f, 1.05f, 1.25f, 0.14f, 241) + 0.02f * fbm(x * 120.0f, y * 120.0f, 120, 243, 2);
}

float overlapScales(float x, float y) {
    return imbricate(x, y, 8, 12, 0.64f, 0.95f, 2.0f, 0.0f, 245, 0.015f) + 0.02f * fbm(x * 100.0f, y * 100.0f, 100, 247, 2);
}

float pebbleScales(float x, float y) {
    const float w = fbm(x * 4.0f, y * 4.0f, 4, 251, 2);
    const Cells pb = cells(x * 20.0f + 0.4f * w, y * 20.0f, 20, 20, 253, 0.8f);
    const float pebble = std::pow(cellDome(pb), 0.6f) * smooth01(pb.edge / 0.06f) * (0.7f + 0.3f * hash01(pb.id));
    // Rosettes: a big tubercle ringed by the small ones, in about a quarter of the coarse cells.
    const Cells big = cells(x * 7.0f + 0.1f * fbm(x * 21.0f, y * 21.0f, 21, 256, 2), y * 7.0f + 0.1f * w, 7, 7, 255, 0.7f);
    float in = 0.0f;
    const float t = tubercle(big, 0.24f + 0.08f * hash01(big.id >> 4), in);
    const float m = (big.id % 4u == 0u) ? in : 0.0f;
    return mix(pebble * 0.7f, t, m) + 0.03f * fbm(x * 90.0f, y * 90.0f, 90, 257, 2);
}

float scutePlates(float x, float y) {
    constexpr int kRows = 7, kCols = 5;
    // Plates in straight rows and columns, as on a crocodile's back: one row is the period along the stroke, so
    // dabs spaced a whole number of rows apart (the brush's spacing) land plate on plate.
    const float w = fbm(x * 3.0f, y * 3.0f, 3, 261, 3);
    const float Y = y * float(kRows) + 0.04f * w;
    const int row = int(std::floor(Y));
    const float fy = Y - float(row);
    const float X = x * float(kCols) + 0.06f * w;
    const int col = int(std::floor(X));
    const float fx = X - float(col);
    // A rounded square plate with a bevelled rim, a low dome and a keel along its middle.
    const float kGap = 0.03f + 0.02f * hash01(hash(wrap(col, kCols), 0, 263)), kRad = 0.2f;
    const float qx = std::abs(fx - 0.5f) - (0.5f - kGap - kRad), qy = std::abs(fy - 0.5f) - (0.5f - kGap - kRad);
    const float sd = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - kRad;
    const float plate = smooth01(-sd / 0.05f);
    const float dome = 1.0f - 0.8f * ((fx - 0.5f) * (fx - 0.5f) + (fy - 0.5f) * (fy - 0.5f));
    const float keel = 0.2f * (1.0f - smooth01(std::abs(fx - 0.5f - 0.03f * w) / 0.08f)) * smooth01((0.42f - std::abs(fy - 0.5f)) / 0.25f);
    const Cells pt = cells(x * 42.0f, y * 42.0f, 42, 42, 265, 1.0f);
    const float pit = hash01(pt.id) < 0.55f ? 1.0f - smooth01(pt.f1 / 0.25f) : 0.0f;
    // Small granules in the gaps between plates.
    const Cells gr = cells(x * 56.0f, y * 56.0f, 56, 56, 269, 1.0f);
    const float granule = std::sqrt(cellDome(gr)) * 0.25f;
    const float lift = 0.9f + 0.1f * hash01(hash(wrap(col, kCols), wrap(row, kRows), 264));
    return mix(granule, (0.55f * dome + keel - 0.1f * pit) * lift, plate) + 0.04f * fbm(x * 40.0f, y * 40.0f, 40, 267, 2);
}

// --- Skin pores ----------------------------------------------------------------------------------------------------

float poresFine(float x, float y) {
    const float wx = fbm(x * 8.0f, y * 8.0f, 8, 301, 2), wy = fbm(x * 8.0f + 3.0f, y * 8.0f + 9.0f, 8, 303, 2);
    const Cells c = cells(x * 40.0f + 0.25f * wx, y * 40.0f + 0.25f * wy, 40, 40, 305, 1.0f);
    const float density = 0.55f + 0.35f * (fbm(x * 6.0f, y * 6.0f, 6, 307, 2) * 0.5f + 0.5f);
    return pores(c, density, 0.18f, 0.14f, 1.2f) + 0.08f * (fbm(x * 90.0f, y * 90.0f, 90, 309, 2) * 0.5f + 0.5f);
}

float poresClustered(float x, float y) {
    const float cluster = smooth01((fbm(x * 4.0f, y * 4.0f, 4, 311, 3) + 0.1f) / 0.5f);
    const Cells a = cells(x * 22.0f, y * 22.0f, 22, 22, 313, 1.0f);
    const Cells b = cells(x * 52.0f, y * 52.0f, 52, 52, 315, 1.0f);
    return pores(a, 0.2f + 0.7f * cluster, 0.18f, 0.14f, 1.2f) + 0.55f * pores(b, 0.35f + 0.4f * cluster, 0.14f, 0.12f, 1.0f) +
           0.06f * (fbm(x * 80.0f, y * 80.0f, 80, 317, 2) * 0.5f + 0.5f);
}

float poresStretched(float x, float y) {
    const Cells c = cells(x * 20.0f, y * 54.0f, 20, 54, 321, 1.0f); // cells wider than tall: pores drawn out sideways
    const float l = lines(x, y, 0.0f, 34.0f, 0.9f * fbm(x * 3.0f, y * 3.0f, 3, 323, 2), 0.12f, 325, 10, 0.0f);
    return pores(c, 0.75f, 0.2f, 0.15f, 1.2f) + 0.1f * l + 0.05f * (fbm(x * 80.0f, y * 80.0f, 80, 327, 2) * 0.5f + 0.5f);
}

// --- Nose pores ----------------------------------------------------------------------------------------------------

float nosePoresOpen(float x, float y) {
    const float wx = fbm(x * 27.0f, y * 27.0f, 27, 335, 2), wy = fbm(x * 27.0f + 7.0f, y * 27.0f, 27, 337, 2);
    const Cells c = cells(x * 18.0f + 0.07f * wx, y * 18.0f + 0.07f * wy, 18, 18, 331, 0.85f); // (a little ragged)
    const float r = 0.24f + 0.12f * hash01(c.id >> 5);
    const float pore = smooth01((r - c.f1) / (0.45f * r));      // a steep wall
    const float core = 1.0f - smooth01(c.f1 / (0.45f * r));      // and a deeper middle
    const Cells t = cells(x * 44.0f, y * 44.0f, 44, 44, 333, 1.0f);
    return 0.75f * pore * (0.7f + 0.3f * hash01(c.id)) + 0.3f * core + 0.4f * pores(t, 0.4f, 0.12f, 0.08f, 1.0f);
}

float nosePoresDense(float x, float y) {
    const float wx = fbm(x * 52.0f, y * 52.0f, 52, 345, 2), wy = fbm(x * 52.0f + 7.0f, y * 52.0f, 52, 347, 2);
    const Cells c = cells(x * 26.0f + 0.15f * wx, y * 26.0f + 0.15f * wy, 26, 26, 341, 0.7f);
    return pores(c, 0.92f, 0.3f, 0.1f, 0.8f) + 0.06f * (fbm(x * 64.0f, y * 64.0f, 64, 343, 2) * 0.5f + 0.5f);
}

float nosePoresFurrowed(float x, float y) {
    const float w = fbm(x * 8.0f, y * 8.0f, 8, 355, 2);
    const Cells c = cells(x * 22.0f + 0.3f * w, y * 22.0f, 22, 22, 351, 0.9f);
    const float furrow = crease(c, 0.12f, x, y, 9, 357) * smooth01((fbm(x * 9.0f, y * 9.0f, 9, 359, 2) + 0.1f) / 0.4f);
    return pores(c, 0.85f, 0.24f, 0.12f, 1.0f) + 0.16f * furrow + 0.05f * (fbm(x * 60.0f, y * 60.0f, 60, 353, 2) * 0.5f + 0.5f);
}

// --- Fine skin -----------------------------------------------------------------------------------------------------

float skinCrosshatch(float x, float y) {
    // Two main families of short furrows crossing in diamonds, a weaker third, and plateaus between broken up by a
    // faint cell net; every line wobbles, thickens and thins, and stops.
    const float w = 0.8f * fbm(x * 5.0f, y * 5.0f, 5, 361, 3) + 0.25f * fbm(x * 18.0f, y * 18.0f, 18, 362, 2);
    const float a = lines(x, y, 13.0f, 18.0f, w, 0.11f, 363, 9, 0.35f), b = lines(x, y, -13.0f, 18.0f, w, 0.11f, 365, 9, 0.35f);
    const float c = lines(x, y, 20.0f, -5.0f, 0.7f * w, 0.08f, 367, 11, 0.0f);
    const Cells s = cells(x * 28.0f + 0.35f * w, y * 28.0f, 28, 28, 369, 1.0f);
    return std::max({a, b, 0.5f * c}) + 0.18f * crease(s, 0.1f, x, y, 12, 368) + 0.5f * pores(s, 0.12f, 0.12f, 0.06f, 1.0f);
}

float skinFineLines(float x, float y) {
    const float w = 1.2f * fbm(x * 3.0f, y * 3.0f, 3, 371, 3) + 0.3f * fbm(x * 14.0f, y * 14.0f, 14, 373, 2);
    const float depth = 0.55f + 0.45f * (fbm(x * 7.0f, y * 7.0f, 7, 375, 2) * 0.5f + 0.5f);
    // Two interleaved families of slightly different spacing, so the gaps between lines vary.
    const float main = std::max(lines(x, y, 0.0f, 24.0f, w, 0.11f, 377, 6, 0.3f), 0.8f * lines(x, y, 0.0f, 31.0f, 1.1f * w, 0.09f, 378, 7, -0.05f)) * depth;
    const float cross = lines(x, y, 16.0f, 7.0f, 0.5f * w, 0.07f, 379, 10, -0.15f);
    return main + 0.2f * cross + 0.06f * (fbm(x * 80.0f, y * 80.0f, 80, 381, 2) * 0.5f + 0.5f);
}

float skinMicroGrain(float x, float y) {
    const Cells c = cells(x * 60.0f, y * 60.0f, 60, 60, 391, 1.0f);
    const float bump = smooth01(cellDome(c) * 1.4f); // soft round bumps, no polygon rims
    return bump * (0.5f + 0.5f * hash01(c.id)) + 0.35f * fbm(x * 24.0f, y * 24.0f, 24, 393, 2);
}

const CreatureAlpha kCreature[] = {
    {"Elephant Hide", "Thick hide cut by a deep net of creases with finer ones between, for elephants and old skin.", elephantHide},
    {"Hide Folds", "Rings of heavy folds with short cross creases, for trunks, necks and leg joints.", hideFolds},
    {"Dinosaur Hide", "Pebbled hide with bigger feature scales and deep, wandering folds, for dinosaurs and beasts.", dinosaurHide},
    {"Snake Scales", "Overlapping keeled diamond scales in offset rows (Follow stroke, draw head to tail).", snakeScales},
    {"Overlap Scales", "Rounded overlapping scales with fine growth rings, for fish and dragons (Follow stroke).", overlapScales},
    {"Pebble Scales", "Small domed scales packed together with larger rosettes, for lizards and geckos.", pebbleScales},
    {"Scute Plates", "Rows of square keeled plates with pitted tops, for crocodiles and armoured backs.", scutePlates},
    {"Pores Fine", "Dense, tiny pores over fine skin texture, for cheeks and close-ups.", poresFine},
    {"Pores Clustered", "Pores of two sizes gathered in patches, for natural, uneven skin.", poresClustered},
    {"Pores Stretched", "Pores drawn out sideways with faint stretch lines, for foreheads, necks and tight skin.", poresStretched},
    {"Nose Pores Open", "Large, deep, steep-walled pores with small ones between, for the nose and chin.", nosePoresOpen},
    {"Nose Pores Dense", "Close-packed round pores, for the tip and wings of the nose.", nosePoresDense},
    {"Nose Pores Furrowed", "Pores joined by fine furrows, for weathered noses and cheeks.", nosePoresFurrowed},
    {"Skin Crosshatch", "Fine furrows crossing in a diamond net, for the backs of hands and aged skin.", skinCrosshatch},
    {"Skin Fine Lines", "Fine, near-parallel wrinkle lines that break and merge, for eye corners and knuckles.", skinFineLines},
    {"Skin Micro Grain", "A soft stipple of tiny bumps, the last pass of skin texture.", skinMicroGrain},
};

} // namespace

const CreatureAlpha* creatureAlphas(size_t& count) {
    count = std::size(kCreature);
    return kCreature;
}

} // namespace sf
