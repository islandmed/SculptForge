// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Texture.h"

#include "core/ThreadPool.h"

#include "core/Undo.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

namespace sf {

namespace {

// --- Colour conversion: 8-bit sRGB <-> linear float through tables ---

struct ColorTables {
    std::array<float, 256> toLinear;
    static constexpr int kSteps = 4096;
    std::array<float, kSteps + 1> toSrgb; // linear 0..1 -> sRGB 0..1
    ColorTables() {
        for (int i = 0; i < 256; ++i) {
            const float x = float(i) / 255.0f;
            toLinear[size_t(i)] = x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
        }
        for (int i = 0; i <= kSteps; ++i) {
            const float x = float(i) / kSteps;
            toSrgb[size_t(i)] = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
        }
    }
};
const ColorTables& tables() {
    static const ColorTables t;
    return t;
}
inline float linearOf(uint8_t c) { return tables().toLinear[c]; }
inline uint8_t srgb8(float linear) {
    const float x = std::clamp(linear, 0.0f, 1.0f) * float(ColorTables::kSteps);
    const int i = std::min(int(x), ColorTables::kSteps - 1);
    const float f = x - float(i);
    const auto& t = tables().toSrgb;
    return uint8_t(std::lround((t[size_t(i)] + (t[size_t(i) + 1] - t[size_t(i)]) * f) * 255.0f));
}
inline uint8_t unit8(float x) { return uint8_t(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); }
inline glm::vec3 linearRgb(const uint8_t* p) { return {linearOf(p[0]), linearOf(p[1]), linearOf(p[2])}; }
inline void putLinear(uint8_t* p, const glm::vec3& c) {
    p[0] = srgb8(c.x);
    p[1] = srgb8(c.y);
    p[2] = srgb8(c.z);
}

// Runs fn(begin, end) over rows [0, n) on several threads for big jobs.
template <class Fn>
void parallelRows(int n, size_t work, Fn fn) {
    const int threads = work < 500000 ? 1 : int(std::clamp<unsigned>(std::thread::hardware_concurrency(), 1u, 16u));
    if (threads <= 1 || n < 2) {
        fn(0, n);
        return;
    }
    std::vector<std::thread> pool;
    const int chunk = (n + threads - 1) / threads;
    for (int t = 0; t < threads; ++t) {
        const int b = t * chunk, e = std::min(n, b + chunk);
        if (b < e) pool.emplace_back([=] { fn(b, e); });
    }
    for (auto& th : pool) th.join();
}

std::atomic<uint64_t> gTextureSerial{1};
std::atomic<uint32_t> gTextureLayerId{1};

// --- Fixed-point triangle setup (1/256 texel) shared by the rasteriser, the gutter and the brush ---

constexpr int64_t kSub = 256;

struct TriSetup {
    glm::i64vec2 p[3];
    int order[3] = {0, 1, 2}; // p[i] is corner order[i] of the caller's triangle
    int64_t area = 0;         // twice the area, > 0 (counter-clockwise after setup)
    bool topLeft[3] = {};     // edge opposite p[i] (p[i+1] -> p[i+2]) owns texels exactly on it
    glm::vec2 t[3];           // corners in texel units (float), caller order
    bool valid = false;

    TriSetup(const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, int w, int h) {
        const glm::vec2 uv[3] = {uv0, uv1, uv2};
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(uv[i].x) || !std::isfinite(uv[i].y)) return;
            t[i] = glm::vec2(uv[i].x * float(w), uv[i].y * float(h));
            // Clamp far-out UVs so the fixed-point maths can't overflow (they rasterise outside the texture anyway).
            const double x = std::clamp(double(uv[i].x), -4.0, 5.0) * w * kSub, y = std::clamp(double(uv[i].y), -4.0, 5.0) * h * kSub;
            p[i] = glm::i64vec2(std::llround(x), std::llround(y));
        }
        area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);
        if (area == 0) return;
        if (area < 0) {
            std::swap(p[1], p[2]);
            std::swap(order[1], order[2]);
            area = -area;
        }
        for (int i = 0; i < 3; ++i) {
            const glm::i64vec2 a = p[(i + 1) % 3], b = p[(i + 2) % 3];
            const int64_t dx = b.x - a.x, dy = b.y - a.y;
            topLeft[i] = dy < 0 || (dy == 0 && dx < 0);
        }
        valid = true;
    }
    // Edge function of the edge opposite corner i at fixed-point point (X, Y): > 0 inside.
    int64_t edge(int i, int64_t X, int64_t Y) const {
        const glm::i64vec2 a = p[(i + 1) % 3], b = p[(i + 2) % 3];
        return (b.x - a.x) * (Y - a.y) - (b.y - a.y) * (X - a.x);
    }
    // Texel range covering the corners, grown by `pad` texels and clipped to [0, w) x [0, h).
    void bounds(int w, int h, int pad, int& x0, int& y0, int& x1, int& y1) const {
        const float minX = std::min({t[0].x, t[1].x, t[2].x}), maxX = std::max({t[0].x, t[1].x, t[2].x});
        const float minY = std::min({t[0].y, t[1].y, t[2].y}), maxY = std::max({t[0].y, t[1].y, t[2].y});
        x0 = std::max(0, int(std::floor(minX - 0.5f)) - pad);
        y0 = std::max(0, int(std::floor(minY - 0.5f)) - pad);
        x1 = std::min(w - 1, int(std::ceil(maxX - 0.5f)) + pad);
        y1 = std::min(h - 1, int(std::ceil(maxY - 0.5f)) + pad);
    }
};

// Closest point of triangle (a, b, c) to p in 2D, as barycentric weights (Ericson, Real-Time Collision Detection).
glm::vec3 closestBarycentric(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    const glm::vec2 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return {1, 0, 0};
    const glm::vec2 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return {0, 1, 0};
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return {1 - v, v, 0};
    }
    const glm::vec2 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return {0, 0, 1};
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return {1 - w, 0, w};
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {0, 1 - w, w};
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom, w = vc * denom;
    return {1 - v - w, v, w};
}

// Visits the texels of one UV triangle inside [cx0, cx1] x [cy0, cy1]: fn(x, y, bary, inside). Inside texels
// follow the exact top-left rule; with pad > 0 the texels within `pad` texels of the triangle that no UV
// triangle covers (tex->coverage) are visited too, with the barycentrics of the nearest triangle point.
template <class Fn>
void visitTriangle(const TriSetup& s, int w, int h, int pad, const PaintTexture* cover, int cx0, int cy0, int cx1, int cy1,
                   Fn&& fn) {
    if (!s.valid) return;
    int x0, y0, x1, y1;
    s.bounds(w, h, pad, x0, y0, x1, y1);
    x0 = std::max(x0, cx0);
    y0 = std::max(y0, cy0);
    x1 = std::min(x1, cx1);
    y1 = std::min(y1, cy1);
    if (x0 > x1 || y0 > y1) return;
    const double inv = 1.0 / double(s.area);
    const float pad2 = float(pad) * float(pad);
    for (int y = y0; y <= y1; ++y) {
        const int64_t Y = int64_t(y) * kSub + kSub / 2;
        for (int x = x0; x <= x1; ++x) {
            const int64_t X = int64_t(x) * kSub + kSub / 2;
            int64_t e[3];
            bool inside = true;
            for (int i = 0; i < 3 && inside; ++i) {
                e[i] = s.edge(i, X, Y);
                inside = e[i] > 0 || (e[i] == 0 && s.topLeft[i]);
            }
            if (inside) {
                float b[3];
                for (int i = 0; i < 3; ++i) b[s.order[i]] = float(double(s.edge(i, X, Y)) * inv);
                fn(x, y, glm::vec3(b[0], b[1], b[2]), true);
                continue;
            }
            if (pad <= 0 || (cover && cover->covered(x, y))) continue;
            const glm::vec2 c(float(x) + 0.5f, float(y) + 0.5f);
            const glm::vec3 b = closestBarycentric(c, s.t[0], s.t[1], s.t[2]);
            const glm::vec2 q = s.t[0] * b.x + s.t[1] * b.y + s.t[2] * b.z;
            const glm::vec2 d = q - c;
            if (glm::dot(d, d) > pad2) continue;
            fn(x, y, b, false);
        }
    }
}

uint64_t uvFingerprint(const Mesh& m) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t(m.uvs.size()) * 0x9E3779B97F4A7C15ull);
    if (m.uvs.empty()) return h | 1;
    const size_t n = m.uvs.size(), step = std::max<size_t>(1, n / 4096);
    for (size_t i = 0; i < n; i += step) {
        uint32_t bits[2];
        std::memcpy(bits, &m.uvs[i], sizeof bits);
        h = (h ^ bits[0]) * 1099511628211ull;
        h = (h ^ bits[1]) * 1099511628211ull;
    }
    return h | 1; // never 0 ("no coverage")
}

bool validTexLayer(const PaintTexture& t, int layer) { return layer >= -1 && layer < int(t.layers.size()); }

void ensureStack(PaintTexture& t) {
    if (!t.layers.empty()) return;
    t.base = t.pixels;
    t.activeLayer = -1;
}

void dropStackIfEmpty(PaintTexture& t) {
    if (!t.layers.empty()) return;
    if (t.base.size() == t.pixels.size()) t.pixels = t.base;
    std::vector<uint8_t>().swap(t.base);
    t.activeLayer = -1;
    syncLayerChannels(t); // drops the channel composites
    t.markAllDirty();
}

static_assert(kTileImageTile == kTextureTile, "material channel tiles line up with the texture's undo tiles");

// Writes paint of weight `w` into one texel of a material channel (n components of `bits`), starting from
// `orig`: Base mixes towards `val` (erase: towards the channel's default); a layer paints "source over" onto its
// coverage (component n), erase lowers the coverage. Normals are renormalised.
inline void paintChannelTexel(uint8_t* dst, const uint8_t* orig, int n, int bits, bool layer, const float* val,
                              const float* def, float w, bool erase, bool normal) {
    const int m = layer ? n + 1 : n;
    float o[5], v[5];
    if (bits == 16) {
        for (int k = 0; k < m; ++k) {
            uint16_t u;
            std::memcpy(&u, orig + k * 2, 2);
            o[k] = float(u) * (1.0f / 65535.0f);
        }
    } else {
        for (int k = 0; k < m; ++k) o[k] = float(orig[k]) * (1.0f / 255.0f);
    }
    if (!layer) {
        const float* to = erase ? def : val;
        for (int k = 0; k < n; ++k) v[k] = o[k] + (to[k] - o[k]) * w;
    } else if (erase) {
        for (int k = 0; k < n; ++k) v[k] = o[k];
        v[n] = o[n] * (1.0f - w);
    } else {
        const float a = o[n], na = a + w * (1.0f - a);
        for (int k = 0; k < n; ++k) v[k] = na > 0.0f ? (o[k] * (a * (1.0f - w)) + val[k] * w) / na : val[k];
        v[n] = na;
    }
    if (normal && n == 3) {
        glm::vec3 d(v[0] * 2.0f - 1.0f, v[1] * 2.0f - 1.0f, v[2] * 2.0f - 1.0f);
        const float len = glm::length(d);
        d = len > 1e-6f ? d / len : glm::vec3(0, 0, 1);
        v[0] = d.x * 0.5f + 0.5f, v[1] = d.y * 0.5f + 0.5f, v[2] = d.z * 0.5f + 0.5f;
    }
    if (bits == 16) {
        for (int k = 0; k < m; ++k) {
            const uint16_t u = uint16_t(std::clamp(v[k], 0.0f, 1.0f) * 65535.0f + 0.5f);
            std::memcpy(dst + k * 2, &u, 2);
        }
    } else {
        for (int k = 0; k < m; ++k) dst[k] = uint8_t(std::clamp(v[k], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
}

// A layer channel image made from a Base channel image (opaque coverage), for duplicating Base.
TileImage layerImageFromBase(const TileImage& base) {
    TileImage out;
    if (!base.valid()) return out;
    const int n = base.components(), bits = base.bits();
    std::array<uint16_t, 4> fill = base.fill();
    if (n < 4) fill[size_t(n)] = bits == 16 ? 65535 : 255;
    out.reset(base.width(), base.height(), n + 1, bits, fill);
    for (size_t ti = 0; ti < base.tileCount(); ++ti) {
        const TileImage::Tile* t = base.tile(ti);
        if (!t) continue; // stays the opaque fill
        const int x0 = int(ti % size_t(base.tilesX())) * kTileImageTile, y0 = int(ti / size_t(base.tilesX())) * kTileImageTile;
        for (int y = y0; y < std::min(y0 + kTileImageTile, base.height()); ++y)
            for (int x = x0; x < std::min(x0 + kTileImageTile, base.width()); ++x) {
                float v[5] = {0, 0, 0, 0, 0};
                base.values(x, y, v);
                v[n] = 1.0f;
                out.setValues(x, y, v);
            }
        out.compactTile(ti);
    }
    return out;
}

// A layer channel merged onto a Base channel image (the lowest layer merging down).
void mergeChannelIntoBase(TileImage& base, const TileImage& layer, float opacity, uint8_t blend, bool normal) {
    if (!base.valid() || !layer.valid()) return;
    const int n = base.components();
    for (size_t ti = 0; ti < layer.tileCount(); ++ti) {
        if (!layer.tile(ti)) continue;
        const int x0 = int(ti % size_t(layer.tilesX())) * kTileImageTile, y0 = int(ti / size_t(layer.tilesX())) * kTileImageTile;
        for (int y = y0; y < std::min(y0 + kTileImageTile, base.height()); ++y)
            for (int x = x0; x < std::min(x0 + kTileImageTile, base.width()); ++x) {
                float v[4] = {0, 0, 0, 0}, s[5] = {0, 0, 0, 0, 0};
                base.values(x, y, v);
                layer.values(x, y, s);
                const float a = std::min(s[n] * opacity, 1.0f);
                if (a <= 0.0f) continue;
                if (normal && n == 3) {
                    const glm::vec3 b = glm::normalize(decodeNormal(v[0], v[1], v[2]) + glm::vec3(0, 0, 1e-6f));
                    const glm::vec3 d = glm::normalize(decodeNormal(s[0], s[1], s[2]) + glm::vec3(0, 0, 1e-6f));
                    const glm::vec3 r = blend == kBlendNormalCombine ? normalCombine(b, d) : d;
                    const glm::vec3 mixed = glm::normalize(b * (1.0f - a) + r * a + glm::vec3(0, 0, 1e-6f));
                    v[0] = mixed.x * 0.5f + 0.5f, v[1] = mixed.y * 0.5f + 0.5f, v[2] = mixed.z * 0.5f + 0.5f;
                } else {
                    const BlendMode mode = blend < kBlendModeCount ? BlendMode(blend) : BlendMode::Normal;
                    const glm::vec3 bc = n == 1 ? glm::vec3(v[0]) : glm::vec3(v[0], v[1], v[2]);
                    const glm::vec3 sc = n == 1 ? glm::vec3(s[0]) : glm::vec3(s[0], s[1], s[2]);
                    const glm::vec3 o = glm::mix(bc, glm::clamp(blendColors(mode, bc, sc), 0.0f, 1.0f), a);
                    for (int k = 0; k < n; ++k) v[k] = o[k];
                }
                base.setValues(x, y, v);
            }
        base.compactTile(ti);
    }
}

// A layer channel merged into the layer channel below it (keeping coverage, as the colour merge does).
void mergeChannelIntoLayer(TileImage& low, float lowOpacity, const TileImage& up, float upOpacity, uint8_t blend, bool normal) {
    if (!low.valid() || !up.valid()) return;
    const int n = low.components() - 1;
    for (size_t ti = 0; ti < low.tileCount(); ++ti) {
        if (!up.tile(ti) && (!low.tile(ti) || lowOpacity == 1.0f)) continue;
        const int x0 = int(ti % size_t(low.tilesX())) * kTileImageTile, y0 = int(ti / size_t(low.tilesX())) * kTileImageTile;
        for (int y = y0; y < std::min(y0 + kTileImageTile, low.height()); ++y)
            for (int x = x0; x < std::min(x0 + kTileImageTile, low.width()); ++x) {
                float d[5] = {0, 0, 0, 0, 0}, s[5] = {0, 0, 0, 0, 0};
                low.values(x, y, d);
                up.values(x, y, s);
                const float ab = std::min(d[n] * lowOpacity, 1.0f), as = std::min(s[n] * upOpacity, 1.0f);
                if (as <= 0.0f) {
                    if (lowOpacity != 1.0f) {
                        d[n] = ab;
                        low.setValues(x, y, d);
                    }
                    continue;
                }
                const float ao = as + ab * (1.0f - as);
                float mixed[3];
                if (normal && n == 3) {
                    const glm::vec3 b = glm::normalize(decodeNormal(d[0], d[1], d[2]) + glm::vec3(0, 0, 1e-6f));
                    const glm::vec3 t = glm::normalize(decodeNormal(s[0], s[1], s[2]) + glm::vec3(0, 0, 1e-6f));
                    const glm::vec3 r = blend == kBlendNormalCombine ? normalCombine(b, t) : t;
                    mixed[0] = r.x * 0.5f + 0.5f, mixed[1] = r.y * 0.5f + 0.5f, mixed[2] = r.z * 0.5f + 0.5f;
                } else {
                    const BlendMode mode = blend < kBlendModeCount ? BlendMode(blend) : BlendMode::Normal;
                    const glm::vec3 bc = n == 1 ? glm::vec3(d[0]) : glm::vec3(d[0], d[1], d[2]);
                    const glm::vec3 sc = n == 1 ? glm::vec3(s[0]) : glm::vec3(s[0], s[1], s[2]);
                    const glm::vec3 o = glm::clamp(blendColors(mode, bc, sc), 0.0f, 1.0f);
                    mixed[0] = o.x, mixed[1] = o.y, mixed[2] = o.z;
                }
                for (int k = 0; k < n; ++k) d[k] = (as * (1.0f - ab) * s[k] + as * ab * mixed[k] + (1.0f - as) * ab * d[k]) / ao;
                d[n] = ao;
                low.setValues(x, y, d);
            }
        low.compactTile(ti);
    }
}

// One texel of the composite: Base and the visible layers in display (sRGB) space, as compositePaintVertex.
inline void compositeTexel(const PaintTexture& t, size_t i, uint8_t* out) {
    const uint8_t* b = &t.base[i * 4];
    glm::vec3 c(0.0f);
    bool blended = false;
    for (const TextureLayer& l : t.layers) {
        if (!l.visible || !l.channels[0].enabled) continue; // channels[BaseColor]: the layer shows its colour
        const uint8_t* s = &l.rgba[i * 4];
        const float a = std::min(float(s[3]) / 255.0f * l.opacity, 1.0f);
        if (a <= 0.0f) continue;
        if (!blended) c = glm::vec3(b[0], b[1], b[2]) / 255.0f;
        blended = true;
        const glm::vec3 src = glm::vec3(s[0], s[1], s[2]) / 255.0f;
        c = glm::mix(c, glm::clamp(blendColors(l.blend, c, src), 0.0f, 1.0f), a);
    }
    if (!blended) {
        out[0] = b[0], out[1] = b[1], out[2] = b[2];
    } else {
        out[0] = unit8(c.x), out[1] = unit8(c.y), out[2] = unit8(c.z);
    }
    out[3] = 255;
}

// Writes paint of weight `w` into texel `px` of the target (Base / plain when `layer` < 0), starting from `orig`.
inline void paintTexel(uint8_t* px, const uint8_t* orig, bool layer, const glm::vec3& color, float w, bool erase) {
    if (!layer) {
        putLinear(px, glm::mix(linearRgb(orig), erase ? glm::vec3(1.0f) : color, w));
        px[3] = 255;
        return;
    }
    const float a = float(orig[3]) / 255.0f;
    if (erase) {
        std::memcpy(px, orig, 3);
        px[3] = unit8(a * (1.0f - w));
        return;
    }
    // "Source over" onto the layer's own coverage (as paintVertex).
    const float na = a + w * (1.0f - a);
    putLinear(px, na > 0.0f ? (linearRgb(orig) * (a * (1.0f - w)) + color * w) / na : color);
    px[3] = unit8(na);
}


// --- Packed tiles for undo (PackedTexels in core/Undo.h) ---

int tileCount(int size) { return (size + kTextureTile - 1) / kTextureTile; }

// Appends a cw x ch tile whose rows start `stride` bytes apart: one RGBA value when every texel is the same.
void appendTile(PackedTexels& p, const uint8_t* src, size_t stride, int cw, int ch) {
    bool uniform = true;
    for (int y = 0; y < ch && uniform; ++y) {
        const uint8_t* row = src + size_t(y) * stride;
        for (int x = 0; x < cw; ++x)
            if (std::memcmp(row + size_t(x) * 4, src, 4) != 0) {
                uniform = false;
                break;
            }
    }
    if (cw > 0 && ch > 0) {
        if (uniform) {
            p.data.insert(p.data.end(), src, src + 4);
        } else {
            for (int y = 0; y < ch; ++y)
                p.data.insert(p.data.end(), src + size_t(y) * stride, src + size_t(y) * stride + size_t(cw) * 4);
        }
    }
    p.offsets.push_back(uint32_t(p.data.size()));
}

// Writes tile i of `p` (cw x ch texels) to `dst` (rows `stride` bytes apart).
void writeTile(const PackedTexels& p, size_t i, uint8_t* dst, size_t stride, int cw, int ch) {
    const uint8_t* src = &p.data[p.offsets[i]];
    const size_t n = p.offsets[i + 1] - p.offsets[i];
    for (int y = 0; y < ch; ++y) {
        uint8_t* row = dst + size_t(y) * stride;
        if (n == 4) {
            for (int x = 0; x < cw; ++x) std::memcpy(row + size_t(x) * 4, src, 4);
        } else if (n == size_t(cw) * size_t(ch) * 4) {
            std::memcpy(row, src + size_t(y) * size_t(cw) * 4, size_t(cw) * 4);
        }
    }
}

// The texel arrays of a texture: Base (or the plain texture) first, then the layers.
size_t planeCount(const PaintTexture& t) { return 1 + t.layers.size(); }
const std::vector<uint8_t>& plane(const PaintTexture& t, size_t i) {
    return i == 0 ? (t.layers.empty() ? t.pixels : t.base) : t.layers[i - 1].rgba;
}
uint32_t planeId(const PaintTexture& t, size_t i) { return i == 0 ? 0 : t.layers[i - 1].id; }
// The array of `ref` with the same id and size as array `i` of `t`, or null.
const std::vector<uint8_t>* matchingPlane(const PaintTexture& t, size_t i, const PaintTexture* ref) {
    if (!ref || ref->width != t.width || ref->height != t.height) return nullptr;
    for (size_t j = 0; j < planeCount(*ref); ++j)
        if (planeId(*ref, j) == planeId(t, i)) {
            const std::vector<uint8_t>& r = plane(*ref, j);
            return r.size() == t.texelCount() * 4 ? &r : nullptr;
        }
    return nullptr;
}

// `t` without its texels (size, settings, layer names...), plus in `planes` the tiles of each of its arrays
// that differ from the matching array of `ref` (all tiles where there is none). Null for no texture.
std::shared_ptr<PaintTexture> packTexture(const PaintTexture* t, const PaintTexture* ref, std::vector<PackedTexels>& planes) {
    planes.clear();
    if (!t) return nullptr;
    auto shell = std::make_shared<PaintTexture>();
    shell->width = t->width;
    shell->height = t->height;
    shell->activeLayer = t->activeLayer;
    shell->channels = t->channels; // material channels: copy-on-write tiles, shared until either side edits them
    shell->heightNormalStrength = t->heightNormalStrength;
    shell->layers.reserve(t->layers.size());
    for (const TextureLayer& l : t->layers) {
        TextureLayer s;
        s.id = l.id;
        s.name = l.name;
        s.opacity = l.opacity;
        s.blend = l.blend;
        s.visible = l.visible;
        s.locked = l.locked;
        s.channels = l.channels;
        shell->layers.push_back(std::move(s));
    }
    const int tilesX = tileCount(t->width), tilesY = tileCount(t->height);
    const size_t stride = size_t(t->width) * 4;
    planes.resize(planeCount(*t));
    for (size_t i = 0; i < planes.size(); ++i) {
        PackedTexels& p = planes[i];
        p.id = planeId(*t, i);
        p.offsets.push_back(0);
        const std::vector<uint8_t>& src = plane(*t, i);
        if (src.size() != t->texelCount() * 4) continue;
        const std::vector<uint8_t>* r = matchingPlane(*t, i, ref);
        for (int ty = 0; ty < tilesY; ++ty)
            for (int tx = 0; tx < tilesX; ++tx) {
                const int cw = std::min(kTextureTile, t->width - tx * kTextureTile), ch = std::min(kTextureTile, t->height - ty * kTextureTile);
                const size_t at = size_t(ty) * kTextureTile * stride + size_t(tx) * kTextureTile * 4;
                if (r) {
                    bool same = true;
                    for (int y = 0; y < ch && same; ++y)
                        same = std::memcmp(&src[at + size_t(y) * stride], &(*r)[at + size_t(y) * stride], size_t(cw) * 4) == 0;
                    if (same) continue;
                }
                p.tiles.push_back(uint32_t(ty * tilesX + tx));
                appendTile(p, &src[at], stride, cw, ch);
            }
    }
    return shell;
}

// The texture packTexture(t, ref) described, rebuilt from `shell`, `planes` and the same `ref`.
std::shared_ptr<PaintTexture> unpackTexture(const PaintTexture& shell, const std::vector<PackedTexels>& planes, const PaintTexture* ref) {
    auto t = std::make_shared<PaintTexture>();
    t->width = shell.width;
    t->height = shell.height;
    t->serial = gTextureSerial++;
    t->activeLayer = shell.activeLayer;
    t->channels = shell.channels;
    t->heightNormalStrength = shell.heightNormalStrength;
    t->layers = shell.layers;
    const size_t n = t->texelCount() * 4, stride = size_t(t->width) * 4;
    const int tilesX = tileCount(t->width);
    for (size_t i = 0; i < planeCount(*t) && i < planes.size(); ++i) {
        std::vector<uint8_t>& dst = i == 0 ? (t->layers.empty() ? t->pixels : t->base) : t->layers[i - 1].rgba;
        if (const std::vector<uint8_t>* r = matchingPlane(shell, i, ref); r && r->size() == n) dst = *r;
        else dst.assign(n, 255);
        const PackedTexels& p = planes[i];
        for (size_t k = 0; k < p.tiles.size(); ++k) {
            const int tx = int(p.tiles[k] % uint32_t(tilesX)) * kTextureTile, ty = int(p.tiles[k] / uint32_t(tilesX)) * kTextureTile;
            const int cw = std::min(kTextureTile, t->width - tx), ch = std::min(kTextureTile, t->height - ty);
            if (cw > 0 && ch > 0) writeTile(p, k, &dst[size_t(ty) * stride + size_t(tx) * 4], stride, cw, ch);
        }
    }
    if (!t->layers.empty()) {
        t->pixels.resize(n);
        compositeTextureAll(*t);
    }
    if (ref && ref->width == t->width && ref->height == t->height) { // same UVs: the coverage still holds
        t->coverage = ref->coverage;
        t->coverageUvs = ref->coverageUvs;
    }
    t->markAllDirty();
    return t;
}
} // namespace

// --- PaintTexture ---

uint32_t newTextureLayerId() { return gTextureLayerId++; }

bool validTextureSize(int size) { return size == 1024 || size == 2048 || size == 4096 || size == 8192; }

void PaintTexture::markDirty(int x0, int y0, int x1, int y1) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, width), y1 = std::min(y1, height);
    if (x0 >= x1 || y0 >= y1) return;
    const int tilesX = tileCount(width);
    dirtyTiles.resize(size_t(tilesX) * size_t(tileCount(height)), 0);
    for (int ty = y0 / kTextureTile; ty <= (y1 - 1) / kTextureTile; ++ty)
        std::memset(&dirtyTiles[size_t(ty) * size_t(tilesX) + size_t(x0 / kTextureTile)], 1,
                    size_t((x1 - 1) / kTextureTile - x0 / kTextureTile + 1));
    if (dirtyMin.x >= dirtyMax.x) {
        dirtyMin = {x0, y0};
        dirtyMax = {x1, y1};
    } else {
        dirtyMin = glm::min(dirtyMin, glm::ivec2(x0, y0));
        dirtyMax = glm::max(dirtyMax, glm::ivec2(x1, y1));
    }
    ++revision;
}

void PaintTexture::markAllDirty() {
    ++revision;
    dirtyFrom = revision; // older uploads are stale: views re-upload everything
    dirtyMin = {0, 0};
    dirtyMax = {width, height};
    dirtyTiles.assign(size_t(tileCount(width)) * size_t(tileCount(height)), 1);
}

bool PaintTexture::tileDirty(int tx, int ty) const {
    const size_t i = size_t(ty) * size_t(tileCount(width)) + size_t(tx);
    return i < dirtyTiles.size() && dirtyTiles[i];
}

void PaintTexture::clearDirty() {
    dirtyFrom = revision;
    dirtyMin = dirtyMax = glm::ivec2(0);
    std::fill(dirtyTiles.begin(), dirtyTiles.end(), uint8_t(0));
}

size_t textureUploadTexels(const PaintTexture& t) {
    size_t n = 0;
    for (int ty = 0; ty < tileCount(t.height); ++ty)
        for (int tx = 0; tx < tileCount(t.width); ++tx)
            if (t.tileDirty(tx, ty))
                n += size_t(std::min(kTextureTile, t.width - tx * kTextureTile)) *
                     size_t(std::min(kTextureTile, t.height - ty * kTextureTile));
    return n;
}

std::vector<uint8_t>& PaintTexture::target(int layer) {
    if (layers.empty()) return pixels;
    if (layer < 0 || layer >= int(layers.size())) return base;
    return layers[size_t(layer)].rgba;
}

size_t PaintTexture::bytes() const {
    size_t n = pixels.size() + base.size() + coverage.size() + sizeof(PaintTexture);
    for (const auto& l : layers) n += l.rgba.size() + sizeof(TextureLayer);
    return n + channelBytes(*this);
}

std::shared_ptr<PaintTexture> makeTexture(int width, int height) {
    auto t = std::make_shared<PaintTexture>();
    t->width = std::max(width, 1);
    t->height = std::max(height, 1);
    t->serial = gTextureSerial++;
    t->pixels.assign(t->texelCount() * 4, 255);
    t->markAllDirty();
    return t;
}

std::shared_ptr<PaintTexture> cloneTexture(const PaintTexture& tex) {
    auto t = std::make_shared<PaintTexture>(tex);
    t->serial = gTextureSerial++;
    t->markAllDirty();
    return t;
}

bool hasTexture(const Mesh& m) {
    return m.texture && m.texture->width > 0 && m.hasUvs() && m.uvs.size() == m.triangleCount() * 3 &&
           m.texture->pixels.size() == m.texture->texelCount() * 4;
}

bool paintsTexture(const Mesh& m) { return m.paintToTexture && hasTexture(m); }

PaintTexture& mutableTexture(Mesh& m) {
    if (m.texture.use_count() > 1) m.texture = cloneTexture(*m.texture);
    return *m.texture;
}

void ensureCoverage(const Mesh& m, PaintTexture& tex, bool force) {
    const uint64_t fp = uvFingerprint(m);
    if (!force && tex.coverageUvs == fp && tex.coverage.size() == (tex.texelCount() + 7) / 8) return;
    tex.coverage.assign((tex.texelCount() + 7) / 8, 0);
    tex.coverageUvs = fp;
    if (m.uvs.size() != m.triangleCount() * 3) return;
    const int w = tex.width, h = tex.height;
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        const TriSetup s(m.uvs[t * 3], m.uvs[t * 3 + 1], m.uvs[t * 3 + 2], w, h);
        visitTriangle(s, w, h, 0, nullptr, 0, 0, w - 1, h - 1, [&](int x, int y, const glm::vec3&, bool) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            tex.coverage[i >> 3] |= uint8_t(1u << (i & 7));
        });
    }
}

void rasterizeUvTriangle(const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, int width, int height,
                         const TexelFn& fn) {
    if (width <= 0 || height <= 0) return;
    const TriSetup s(uv0, uv1, uv2, width, height);
    visitTriangle(s, width, height, 0, nullptr, 0, 0, width - 1, height - 1,
                  [&](int x, int y, const glm::vec3& b, bool) { fn(x, y, b.x, b.y, b.z); });
}

// --- Compositing and sampling ---

void compositeTexture(PaintTexture& t, int x0, int y0, int x1, int y1) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, t.width), y1 = std::min(y1, t.height);
    if (x0 >= x1 || y0 >= y1) return;
    if (!t.layers.empty() && t.base.size() == t.pixels.size()) {
        parallelRows(y1 - y0, size_t(x1 - x0) * size_t(y1 - y0) * (t.layers.size() + 1), [&](int b, int e) {
            for (int y = y0 + b; y < y0 + e; ++y)
                for (int x = x0; x < x1; ++x) {
                    const size_t i = size_t(y) * size_t(t.width) + size_t(x);
                    compositeTexel(t, i, &t.pixels[i * 4]);
                }
        });
        compositeChannels(t, x0, y0, x1, y1);
    }
    t.markDirty(x0, y0, x1, y1);
}

void compositeTextureAll(PaintTexture& t) {
    compositeTexture(t, 0, 0, t.width, t.height);
    t.markAllDirty();
}

glm::vec3 sampleTexture(const PaintTexture& t, const glm::vec2& uv) {
    if (t.width <= 0 || t.pixels.size() != t.texelCount() * 4) return glm::vec3(1.0f);
    const float fx = std::clamp(uv.x, 0.0f, 1.0f) * float(t.width) - 0.5f;
    const float fy = std::clamp(uv.y, 0.0f, 1.0f) * float(t.height) - 0.5f;
    const int x0 = std::clamp(int(std::floor(fx)), 0, t.width - 1), y0 = std::clamp(int(std::floor(fy)), 0, t.height - 1);
    const int x1 = std::min(x0 + 1, t.width - 1), y1 = std::min(y0 + 1, t.height - 1);
    const float ax = std::clamp(fx - float(x0), 0.0f, 1.0f), ay = std::clamp(fy - float(y0), 0.0f, 1.0f);
    auto at = [&](int x, int y) { return linearRgb(&t.pixels[(size_t(y) * size_t(t.width) + size_t(x)) * 4]); };
    return glm::mix(glm::mix(at(x0, y0), at(x1, y0), ax), glm::mix(at(x0, y1), at(x1, y1), ax), ay);
}

glm::vec3 sampleTextureAt(const Mesh& m, uint32_t tri, const glm::vec3& point) {
    if (!hasTexture(m) || tri >= m.triangleCount()) return glm::vec3(1.0f);
    const glm::uvec3& t = m.triangles[tri];
    const glm::vec3 a = m.positions[t.x], b = m.positions[t.y], c = m.positions[t.z];
    const glm::vec3 v0 = b - a, v1 = c - a, v2 = point - a;
    const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
    const float den = d00 * d11 - d01 * d01;
    float v = 0.0f, w = 0.0f;
    if (std::abs(den) > 1e-20f) {
        v = (d11 * d20 - d01 * d21) / den;
        w = (d00 * d21 - d01 * d20) / den;
    }
    const glm::vec2 uv = m.uvs[size_t(tri) * 3] * (1.0f - v - w) + m.uvs[size_t(tri) * 3 + 1] * v + m.uvs[size_t(tri) * 3 + 2] * w;
    return sampleTexture(*m.texture, uv);
}

// --- Brush ---

bool TexturePainter::begin(Mesh& mesh, uint32_t channels) {
    active_ = false;
    tiles_.clear();
    dab_ = 0;
    if (!paintsTexture(mesh)) return false;
    const PaintTexture& t = *mesh.texture;
    layer_ = t.layers.empty() ? -1 : std::clamp(t.activeLayer, -1, int(t.layers.size()) - 1);
    if (!textureLayerEditable(t, layer_)) return false;
    channels_ = channels & enabledChannelMask(t);
    if (!channels_) return false;
    PaintTexture& tex = mutableTexture(mesh);
    syncLayerChannels(tex); // every layer has an image for every enabled channel
    ensureCoverage(mesh, tex);
    tilesX_ = (tex.width + kTextureTile - 1) / kTextureTile;
    tilesY_ = (tex.height + kTextureTile - 1) / kTextureTile;
    tiles_.resize(size_t(tilesX_) * size_t(tilesY_));
    tilePtr_ = std::make_unique<std::atomic<Tile*>[]>(tiles_.size());
    for (size_t i = 0; i < tiles_.size(); ++i) tilePtr_[i].store(nullptr, std::memory_order_relaxed);
    active_ = true;
    return true;
}

TileImage* TexturePainter::channelTarget(PaintTexture& tex, int c) const {
    if (c <= 0 || c >= kChannelCount || !(channels_ & (1u << c)) || !channelEnabled(tex, Channel(c))) return nullptr;
    if (layer_ >= 0) {
        if (layer_ >= int(tex.layers.size())) return nullptr;
        TileImage& img = tex.layers[size_t(layer_)].channels[size_t(c)].image;
        return img.valid() && img.width() == tex.width && img.height() == tex.height ? &img : nullptr;
    }
    return &tex.channels[size_t(c)].base;
}

TexturePainter::Tile& TexturePainter::tile(uint32_t index, const std::vector<uint8_t>& target, int width, int height,
                                           PaintTexture& tex) {
    if (Tile* made = tilePtr_[index].load(std::memory_order_acquire)) return *made;
    std::lock_guard<std::mutex> lock(tileMutex_);
    std::unique_ptr<Tile>& slot = tiles_[index];
    if (slot) return *slot;
    slot = std::make_unique<Tile>();
    Tile& t = *slot;
    constexpr size_t n = size_t(kTextureTile) * kTextureTile;
    t.original.assign(n * 4, 0);
    t.coverage.assign(n, 0.0f);
    t.stamp.assign(n, 0);
    const int tx = int(index % uint32_t(tilesX_)) * kTextureTile, ty = int(index / uint32_t(tilesX_)) * kTextureTile;
    const int cw = std::min(kTextureTile, width - tx), ch = std::min(kTextureTile, height - ty);
    for (int y = 0; y < ch; ++y)
        std::memcpy(&t.original[size_t(y) * kTextureTile * 4], &target[(size_t(ty + y) * size_t(width) + size_t(tx)) * 4],
                    size_t(cw) * 4);
    // The material channels: keep the old tile for undo and make the image's tile writable.
    for (int c = 1; c < kChannelCount; ++c) {
        TileImage* img = channelTarget(tex, c);
        if (!img) continue;
        ChannelTile& ct = t.channels[size_t(c)];
        ct.original = img->tilePtr(index);
        if (ct.original && !ct.original->uniform) {
            ct.origTexels = ct.original->data.data();
            ct.origStride = img->texelBytes();
        } else {
            img->texel(tx, ty, ct.origValue); // the uniform value or the fill, as one raw texel
            ct.origTexels = ct.origValue;
            ct.origStride = 0;
        }
        ct.texels = img->writableTile(index);
    }
    tilePtr_[index].store(&t, std::memory_order_release);
    return t;
}

// A dab in two passes: (1) per mirror, the triangles under the brush and the texel box each can reach, in a
// fixed order; (2) the texture's 16-row bands in parallel, each visiting its triangles in that same order. A
// texel lies in one band, so it sees exactly the writes it would get serially: the result doesn't depend on
// the thread count.
size_t TexturePainter::dab(Mesh& mesh, const BVH& bvh, const DabParams& p, const glm::vec3& color, Symmetry sym) {
    BrushMaterial m;
    m.color = color;
    m.channels = channelBit(Channel::BaseColor);
    return dab(mesh, bvh, p, m, sym);
}

size_t TexturePainter::dab(Mesh& mesh, const BVH& bvh, const DabParams& p, const BrushMaterial& material, Symmetry sym) {
    if (!active_ || p.radius <= 0.0f || !hasTexture(mesh)) return 0;
    PaintTexture& tex = mutableTexture(mesh);
    if (!textureLayerEditable(tex, layer_) || (layer_ >= 0 && layer_ >= int(tex.layers.size()))) return 0;
    std::vector<uint8_t>& target = tex.target(layer_);
    const bool onLayer = layer_ >= 0;
    const glm::vec3 color = material.color;
    const uint32_t painted = channels_ & material.channels;
    const bool paintColor = painted & 1u;
    // The material channels this dab writes, with the brush's value and the channel's default (for erasing).
    struct ChanJob {
        int c, n, bits, tb;
        bool normal;
        float val[4], def[4];
    };
    std::vector<ChanJob> jobs;
    for (int c = 1; c < kChannelCount; ++c) {
        if (!(painted & (1u << c)) || !channelTarget(tex, c)) continue;
        const ChannelFormat& f = channelFormat(c);
        ChanJob j{c, f.components, f.bits, f.components * (f.bits == 16 ? 2 : 1) + (onLayer ? (f.bits == 16 ? 2 : 1) : 0),
                  c == int(Channel::Normal), {}, {}};
        const std::array<float, 4> v = material.encoded(Channel(c));
        for (int k = 0; k < 4; ++k) j.val[k] = v[size_t(k)], j.def[k] = f.defaultValue[size_t(k)];
        jobs.push_back(j);
    }
    if (!paintColor && jobs.empty()) return 0;
    const int w = tex.width, h = tex.height;
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    if (s <= 0.0f) return 0;
    const bool useAlpha = p.alpha && !p.alpha->empty();
    const bool masked = mesh.hasMask();
    const bool normals = mesh.normals.size() == mesh.vertexCount();
    const float r = p.radius, r2 = r * r;

    struct Mirror {
        glm::vec3 center, viewDir, aUp, aRight;
        uint32_t stamp;
    };
    std::vector<Mirror> mirrors;
    dabTris_.clear();
    for (const glm::vec3& m : Sculptor::mirrors(sym)) {
        Mirror mi;
        mi.center = p.center * m;
        mi.viewDir = p.viewDir * m;
        // The alpha's frame on the view plane (as recipe brushes orient it).
        glm::vec3 aUp = p.alphaUp * m;
        aUp -= mi.viewDir * glm::dot(aUp, mi.viewDir);
        mi.aUp = glm::length(aUp) > 1e-6f ? glm::normalize(aUp) : glm::vec3(0, 1, 0);
        mi.aRight = glm::cross(mi.aUp, -mi.viewDir);
        mi.stamp = ++dab_;
        const int mirror = int(mirrors.size());
        mirrors.push_back(mi);
        bvh.queryTriangles(mesh, mi.center, r, tris_);
        for (uint32_t ti : tris_) {
            if (mesh.triangleHidden(ti)) continue;
            const glm::uvec3& tv = mesh.triangles[ti];
            const TriSetup su(mesh.uvs[size_t(ti) * 3], mesh.uvs[size_t(ti) * 3 + 1], mesh.uvs[size_t(ti) * 3 + 2], w, h);
            if (!su.valid) continue;
            const glm::vec3 P0 = mesh.positions[tv.x], P1 = mesh.positions[tv.y], P2 = mesh.positions[tv.z];
            // Only the texels whose 3D point can be inside the sphere: the affine texel -> 3D map turns the
            // sphere into an ellipse in texel space; clip to its bounding box.
            const glm::vec2 e1 = su.t[1] - su.t[0], e2 = su.t[2] - su.t[0];
            const float det = e1.x * e2.y - e1.y * e2.x;
            if (std::abs(det) < 1e-12f) continue;
            // A = [P1-P0, P2-P0] * inverse([e1 e2]): columns are d(3D)/dx and d(3D)/dy.
            const glm::vec3 q1 = P1 - P0, q2 = P2 - P0;
            const glm::vec3 Ax = (q1 * e2.y - q2 * e1.y) / det, Ay = (q2 * e1.x - q1 * e2.x) / det;
            const float mxx = glm::dot(Ax, Ax), mxy = glm::dot(Ax, Ay), myy = glm::dot(Ay, Ay);
            const float mdet = mxx * myy - mxy * mxy;
            DabTri d{ti, mirror, 0, 0, w - 1, h - 1};
            if (mdet > 1e-30f) {
                const glm::vec3 rel = mi.center - P0;
                const float bx = glm::dot(Ax, rel), by = glm::dot(Ay, rel);
                const float dx = (myy * bx - mxy * by) / mdet, dy = (mxx * by - mxy * bx) / mdet;
                const glm::vec3 closest = P0 + Ax * dx + Ay * dy;
                const float k = r2 - glm::dot(closest - mi.center, closest - mi.center);
                if (k < 0.0f) continue;
                const float hx = std::sqrt(k * myy / mdet), hy = std::sqrt(k * mxx / mdet);
                const glm::vec2 c = su.t[0] + glm::vec2(dx, dy);
                d.x0 = int(std::floor(c.x - hx)) - kTextureGutter - 1;
                d.x1 = int(std::ceil(c.x + hx)) + kTextureGutter + 1;
                d.y0 = int(std::floor(c.y - hy)) - kTextureGutter - 1;
                d.y1 = int(std::ceil(c.y + hy)) + kTextureGutter + 1;
            }
            // The triangle's own texel box (with the gutter) limits it too.
            int bx0, by0, bx1, by1;
            su.bounds(w, h, kTextureGutter, bx0, by0, bx1, by1);
            d.x0 = std::max(d.x0, bx0), d.y0 = std::max(d.y0, by0), d.x1 = std::min(d.x1, bx1), d.y1 = std::min(d.y1, by1);
            if (d.x0 > d.x1 || d.y0 > d.y1) continue;
            dabTris_.push_back(d);
        }
    }
    if (dabTris_.empty()) return 0;

    // Bucket the triangles by band (keeping their order), then paint the bands that have any.
    constexpr int kBandRows = 16; // small enough that one dab makes plenty of jobs
    const int bandCount = (h + kBandRows - 1) / kBandRows;
    bandTris_.resize(size_t(bandCount));
    for (auto& b : bandTris_) b.clear();
    for (uint32_t i = 0; i < uint32_t(dabTris_.size()); ++i)
        for (int band = dabTris_[i].y0 / kBandRows; band <= dabTris_[i].y1 / kBandRows; ++band)
            bandTris_[size_t(band)].push_back(i);
    std::vector<int> bands;
    for (int band = 0; band < bandCount; ++band)
        if (!bandTris_[size_t(band)].empty()) bands.push_back(band);
    struct BandResult {
        glm::ivec2 lo{INT32_MAX}, hi{INT32_MIN};
        size_t changed = 0;
    };
    std::vector<BandResult> results(bands.size());

    ThreadPool::shared().run(bands.size(), [&](size_t job) {
        const int band = bands[job];
        const int by0 = band * kBandRows, by1 = std::min(h, by0 + kBandRows) - 1;
        BandResult& res = results[job];
        for (uint32_t di : bandTris_[size_t(band)]) {
            const DabTri& d = dabTris_[di];
            const Mirror& mi = mirrors[size_t(d.mirror)];
            const glm::uvec3& tv = mesh.triangles[d.tri];
            const TriSetup su(mesh.uvs[size_t(d.tri) * 3], mesh.uvs[size_t(d.tri) * 3 + 1], mesh.uvs[size_t(d.tri) * 3 + 2], w, h);
            const glm::vec3 P0 = mesh.positions[tv.x], P1 = mesh.positions[tv.y], P2 = mesh.positions[tv.z];
            const float m0 = masked ? mesh.mask[tv.x] : 0.0f, m1 = masked ? mesh.mask[tv.y] : 0.0f,
                        m2 = masked ? mesh.mask[tv.z] : 0.0f;
            const glm::vec3 N0 = normals ? mesh.normals[tv.x] : glm::vec3(0), N1 = normals ? mesh.normals[tv.y] : glm::vec3(0),
                            N2 = normals ? mesh.normals[tv.z] : glm::vec3(0);
            const glm::vec3 faceN = glm::cross(P1 - P0, P2 - P0);
            Tile* cached = nullptr;
            uint32_t cachedIndex = UINT32_MAX;
            visitTriangle(su, w, h, kTextureGutter, &tex, d.x0, std::max(d.y0, by0), d.x1, std::min(d.y1, by1),
                          [&](int x, int y, const glm::vec3& b, bool) {
                const glm::vec3 P = P0 * b.x + P1 * b.y + P2 * b.z;
                const glm::vec3 o = P - mi.center;
                const float d2 = glm::dot(o, o);
                if (d2 >= r2) return;
                // Front faces only: never paint through to the far side.
                const glm::vec3 N = normals ? N0 * b.x + N1 * b.y + N2 * b.z : faceN;
                if (glm::dot(N, mi.viewDir) > 0.0f) return;
                const float f = useAlpha ? p.alpha->sample(glm::dot(o, mi.aRight) / r, glm::dot(o, mi.aUp) / r)
                                         : brushFalloff(std::sqrt(d2), r, p.focal);
                const float wgt = f * s * (1.0f - std::clamp(m0 * b.x + m1 * b.y + m2 * b.z, 0.0f, 1.0f));
                if (wgt <= 0.0f) return;
                const uint32_t ti2 = uint32_t(y / kTextureTile) * uint32_t(tilesX_) + uint32_t(x / kTextureTile);
                if (ti2 != cachedIndex) {
                    cached = &tile(ti2, target, w, h, tex);
                    cachedIndex = ti2;
                }
                Tile& t = *cached;
                const size_t li = size_t(y % kTextureTile) * kTextureTile + size_t(x % kTextureTile);
                if (t.stamp[li] == mi.stamp) return; // one write per texel per dab (shared edges, overlapping UVs)
                t.stamp[li] = mi.stamp;
                float& acc = t.coverage[li];
                acc += std::min(wgt, 1.0f) * (1.0f - acc);
                if (paintColor)
                    paintTexel(&target[(size_t(y) * size_t(w) + size_t(x)) * 4], &t.original[li * 4], onLayer, color, acc, p.invert);
                for (const ChanJob& j : jobs) {
                    const ChannelTile& ct = t.channels[size_t(j.c)];
                    paintChannelTexel(ct.texels + li * size_t(j.tb), ct.origTexels + li * size_t(ct.origStride), j.n, j.bits,
                                      onLayer, j.val, j.def, acc, p.invert, j.normal);
                }
                res.lo = glm::min(res.lo, glm::ivec2(x, y));
                res.hi = glm::max(res.hi, glm::ivec2(x + 1, y + 1));
                ++res.changed;
            });
        }
        // With layers, the band recomposites its own changed rows (in parallel with the other bands).
        if (paintColor && res.changed && !tex.layers.empty() && tex.base.size() == tex.pixels.size())
            for (int y = res.lo.y; y < res.hi.y; ++y)
                for (int x = res.lo.x; x < res.hi.x; ++x) {
                    const size_t i = size_t(y) * size_t(w) + size_t(x);
                    compositeTexel(tex, i, &tex.pixels[i * 4]);
                }
    });

    size_t changed = 0;
    for (const BandResult& res : results)
        if (res.changed) {
            changed += res.changed;
            if (!tex.layers.empty())
                for (const ChanJob& j : jobs) compositeChannel(tex, Channel(j.c), res.lo.x, res.lo.y, res.hi.x, res.hi.y);
            tex.markDirty(res.lo.x, res.lo.y, res.hi.x, res.hi.y);
        }
    return changed;
}

void TexturePainter::end(UndoRecord& record) {
    if (active_) {
        auto u = std::make_unique<TextureTileUndo>();
        u->layer = layer_;
        u->texels.offsets.push_back(0);
        bool any = false;
        if (channels_ & 1u)
            for (uint32_t i = 0; i < uint32_t(tiles_.size()); ++i)
                if (tiles_[i]) {
                    u->texels.tiles.push_back(i);
                    appendTile(u->texels, tiles_[i]->original.data(), size_t(kTextureTile) * 4, kTextureTile, kTextureTile);
                    any = true;
                }
        // Material channels: one entry per (layer, channel) holding the tiles before the stroke.
        for (int c = 1; c < kChannelCount; ++c) {
            if (!(channels_ & (1u << c))) continue;
            TextureChannelTiles ct;
            ct.layer = layer_;
            ct.channel = uint8_t(c);
            for (uint32_t i = 0; i < uint32_t(tiles_.size()); ++i)
                if (tiles_[i] && tiles_[i]->channels[size_t(c)].texels) {
                    ct.tiles.push_back(i);
                    ct.old.push_back(tiles_[i]->channels[size_t(c)].original);
                }
            if (!ct.tiles.empty()) {
                u->channels.push_back(std::move(ct));
                any = true;
            }
        }
        if (any) record.textureTiles = std::move(u);
    }
    active_ = false;
    tiles_.clear();
    tilePtr_.reset();
    std::vector<uint32_t>().swap(tris_);
    std::vector<DabTri>().swap(dabTris_);
    bandTris_.clear();
}

size_t TextureChannelTiles::bytes() const {
    size_t n = tiles.size() * sizeof(uint32_t) + old.size() * sizeof(old[0]);
    for (const auto& t : old)
        if (t) n += sizeof(*t) + t->data.size(); // shared with nothing once the image has moved on
    return n;
}

size_t TextureTileUndo::bytes() const {
    size_t n = texels.bytes();
    for (const auto& c : channels) n += c.bytes();
    return n;
}

void TextureTileUndo::swapWith(Mesh& mesh) {
    if (!mesh.texture) return;
    PaintTexture& t = mutableTexture(mesh);
    // Material channels: swap the tile pointers with the image's (the inverse step).
    for (TextureChannelTiles& ct : channels) {
        const Channel c = Channel(ct.channel);
        if (ct.channel == 0 || ct.channel >= kChannelCount || !channelEnabled(t, c)) continue;
        if (ct.layer >= 0) syncLayerChannels(t);
        TileImage* img = ct.layer < 0 ? &t.channels[ct.channel].base
                         : ct.layer < int(t.layers.size()) ? &t.layers[size_t(ct.layer)].channels[ct.channel].image
                                                          : nullptr;
        if (!img || !img->valid()) continue;
        for (size_t i = 0; i < ct.tiles.size() && i < ct.old.size(); ++i) {
            if (ct.tiles[i] >= img->tileCount()) continue;
            TileImage::TilePtr now = img->tilePtr(ct.tiles[i]);
            img->setTilePtr(ct.tiles[i], std::move(ct.old[i]));
            ct.old[i] = std::move(now);
            const int tx = int(ct.tiles[i] % uint32_t(img->tilesX())) * kTextureTile, ty = int(ct.tiles[i] / uint32_t(img->tilesX())) * kTextureTile;
            if (!t.layers.empty()) compositeChannel(t, c, tx, ty, tx + kTextureTile, ty + kTextureTile);
            t.markDirty(tx, ty, tx + kTextureTile, ty + kTextureTile);
        }
    }
    if (texels.tiles.empty()) return;
    if (!validTexLayer(t, layer) || (layer >= 0 && t.layers.empty())) return;
    std::vector<uint8_t>& target = t.target(layer);
    if (target.size() != t.texelCount() * 4) return;
    const int tilesX = tileCount(t.width);
    PackedTexels now; // what the tiles hold now: the inverse step
    now.id = texels.id;
    now.tiles = texels.tiles;
    now.offsets.push_back(0);
    for (size_t i = 0; i < texels.tiles.size(); ++i) {
        const int tx = int(texels.tiles[i] % uint32_t(tilesX)) * kTextureTile, ty = int(texels.tiles[i] / uint32_t(tilesX)) * kTextureTile;
        const int cw = std::min(kTextureTile, t.width - tx), ch = std::min(kTextureTile, t.height - ty);
        uint8_t* at = &target[(size_t(ty) * size_t(t.width) + size_t(tx)) * 4];
        appendTile(now, at, size_t(t.width) * 4, cw, ch);
        if (cw <= 0 || ch <= 0) continue;
        writeTile(texels, i, at, size_t(t.width) * 4, cw, ch);
        if (!t.layers.empty()) compositeTexture(t, tx, ty, tx + cw, ty + ch);
        else t.markDirty(tx, ty, tx + cw, ty + ch);
    }
    texels = std::move(now);
}

void TextureStackUndo::commit(const Mesh& mesh) {
    if (committed) return;
    const std::shared_ptr<PaintTexture> whole = std::move(texture);
    texture = packTexture(whole.get(), mesh.texture.get(), planes);
    committed = true;
}

void TextureStackUndo::swapWith(Mesh& mesh) {
    commit(mesh);
    std::shared_ptr<PaintTexture> restored = texture ? unpackTexture(*texture, planes, mesh.texture.get()) : nullptr;
    std::vector<PackedTexels> inverse; // the mesh's texture now, relative to the one put back
    texture = packTexture(mesh.texture.get(), restored.get(), inverse);
    planes = std::move(inverse);
    mesh.texture = std::move(restored);
    std::swap(paintToTexture, mesh.paintToTexture);
    mesh.facesDirty = true; // the renderer keeps per-triangle UVs only while there is a texture
}

size_t TextureStackUndo::bytes() const {
    if (!texture) return 0;
    if (!committed) return texture->bytes();
    size_t n = sizeof(PaintTexture);
    for (const auto& l : texture->layers) {
        n += sizeof(TextureLayer) + l.name.size();
        for (int c = 1; c < kChannelCount; ++c) n += l.channels[size_t(c)].image.uniqueBytes();
    }
    for (int c = 1; c < kChannelCount; ++c)
        n += texture->channels[size_t(c)].base.uniqueBytes() + texture->channels[size_t(c)].composite.uniqueBytes();
    for (const auto& p : planes) n += p.bytes() + sizeof(PackedTexels);
    return n;
}

UndoRecord captureTexture(Mesh& mesh, std::string label) {
    UndoRecord r;
    r.label = std::move(label);
    r.texture = std::make_unique<TextureStackUndo>();
    r.texture->texture = mesh.texture;
    r.texture->paintToTexture = mesh.paintToTexture;
    if (mesh.texture) mesh.texture = cloneTexture(*mesh.texture);
    return r;
}

void finishTextureUndo(UndoRecord& record, const Mesh& mesh) {
    if (record.texture) record.texture->commit(mesh);
}

// --- Whole-texture operations ---

void bakeTexels(const Mesh& m, PaintTexture& tex, const BakeFn& fn, bool skipHidden) {
    if (m.uvs.size() != m.triangleCount() * 3 || tex.width <= 0) return;
    ensureCoverage(m, tex);
    const int w = tex.width, h = tex.height;
    std::vector<uint8_t> written((tex.texelCount() + 7) / 8, 0);
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        if (skipHidden && m.triangleHidden(t)) continue;
        const TriSetup s(m.uvs[size_t(t) * 3], m.uvs[size_t(t) * 3 + 1], m.uvs[size_t(t) * 3 + 2], w, h);
        visitTriangle(s, w, h, kTextureGutter, &tex, 0, 0, w - 1, h - 1, [&](int x, int y, const glm::vec3& b, bool) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            if (written[i >> 3] & (1u << (i & 7))) return;
            written[i >> 3] |= uint8_t(1u << (i & 7));
            fn(t, b.x, b.y, b.z, x, y);
        });
    }
}

size_t textureFill(Mesh& m, const glm::vec3& color, const std::vector<float>& vw) {
    if (!hasTexture(m) || vw.size() != m.vertexCount()) return 0;
    PaintTexture& tex = mutableTexture(m);
    const int layer = tex.layers.empty() ? -1 : tex.activeLayer;
    if (!textureLayerEditable(tex, layer)) return 0;
    std::vector<uint8_t>& target = tex.target(layer);
    const bool onLayer = layer >= 0;
    size_t changed = 0;
    bakeTexels(m, tex, [&](uint32_t t, float b0, float b1, float b2, int x, int y) {
        const glm::uvec3& tv = m.triangles[t];
        const float wgt = std::clamp(vw[tv.x] * b0 + vw[tv.y] * b1 + vw[tv.z] * b2, 0.0f, 1.0f);
        if (wgt <= 0.0f) return;
        uint8_t* px = &target[(size_t(y) * size_t(tex.width) + size_t(x)) * 4];
        uint8_t orig[4];
        std::memcpy(orig, px, 4);
        paintTexel(px, orig, onLayer, color, wgt, false);
        ++changed;
    });
    compositeTextureAll(tex);
    return changed;
}

void polypaintToTexture(Mesh& m) {
    if (!hasTexture(m)) return;
    PaintTexture& tex = mutableTexture(m);
    const int layer = tex.layers.empty() ? -1 : tex.activeLayer;
    std::vector<uint8_t>& target = tex.target(layer);
    bakeTexels(m, tex, [&](uint32_t t, float b0, float b1, float b2, int x, int y) {
        const glm::uvec3& tv = m.triangles[t];
        const glm::vec3 c = m.colorAt(tv.x) * b0 + m.colorAt(tv.y) * b1 + m.colorAt(tv.z) * b2;
        uint8_t* px = &target[(size_t(y) * size_t(tex.width) + size_t(x)) * 4];
        putLinear(px, c);
        px[3] = 255;
    }, false);
    compositeTextureAll(tex);
}

void textureToPolypaint(Mesh& m) {
    if (!hasTexture(m)) return;
    const size_t nv = m.vertexCount();
    std::vector<glm::vec3> sum(nv, glm::vec3(0.0f));
    std::vector<uint32_t> count(nv, 0);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const uint32_t v = m.triangles[t][c];
            sum[v] += sampleTexture(*m.texture, m.uvs[t * 3 + size_t(c)]);
            ++count[v];
        }
    m.ensureColors();
    for (uint32_t v = 0; v < nv; ++v)
        if (count[v]) paintVertex(m, v, sum[v] / float(count[v]), 1.0f);
    m.markAllDirty();
}

Image textureImage(const PaintTexture& t) {
    Image img;
    if (t.width <= 0 || t.pixels.size() != t.texelCount() * 4) return img;
    img.width = t.width;
    img.height = t.height;
    img.rgba.resize(t.pixels.size());
    const size_t row = size_t(t.width) * 4;
    for (int y = 0; y < t.height; ++y) // bottom-up texture rows -> top-down image rows
        std::memcpy(&img.rgba[size_t(t.height - 1 - y) * row], &t.pixels[size_t(y) * row], row);
    return img;
}

bool loadTextureImage(PaintTexture& t, const Image& img, std::string& error) {
    if (img.empty() || img.rgba.size() != size_t(img.width) * size_t(img.height) * 4) {
        error = "The image is empty";
        return false;
    }
    if (img.width > 16384 || img.height > 16384) {
        error = "The image is larger than 16384 pixels";
        return false;
    }
    const uint32_t channels = enabledChannelMask(t);
    t.width = img.width;
    t.height = img.height;
    t.layers.clear();
    t.base.clear();
    t.activeLayer = -1;
    // The material channels start again at their defaults at the new size.
    for (int c = 1; c < kChannelCount; ++c) {
        t.channels[size_t(c)] = ChannelPlane();
        if (channels & (1u << c)) setChannelEnabled(t, Channel(c), true);
    }
    t.coverage.clear();
    t.coverageUvs = 0;
    t.pixels.resize(img.rgba.size());
    const size_t row = size_t(t.width) * 4;
    for (int y = 0; y < t.height; ++y) std::memcpy(&t.pixels[size_t(y) * row], &img.rgba[size_t(t.height - 1 - y) * row], row);
    for (size_t i = 3; i < t.pixels.size(); i += 4) t.pixels[i] = 255; // the paint is opaque
    t.markAllDirty();
    return true;
}

// --- Layer stack ---

bool textureLayerEditable(const PaintTexture& t, int layer) {
    if (layer == -1) return true;
    if (!validTexLayer(t, layer)) return false;
    const TextureLayer& l = t.layers[size_t(layer)];
    return !l.locked && l.visible;
}

std::string uniqueTextureLayerName(const PaintTexture& t, const std::string& wanted) {
    const std::string stem = wanted.empty() ? "Layer" : wanted;
    auto used = [&](const std::string& n) { return findTextureLayer(t, n) != -2; };
    if (!wanted.empty() && !used(stem)) return stem;
    for (int i = 2;; ++i) {
        std::string n = stem + " " + std::to_string(i);
        if (!used(n)) return n;
    }
}

int findTextureLayer(const PaintTexture& t, const std::string& name) {
    for (size_t i = 0; i < t.layers.size(); ++i)
        if (t.layers[i].name == name) return int(i);
    return name == "Base" ? -1 : -2;
}

int addTextureLayer(PaintTexture& t, std::string name, int above) {
    ensureStack(t);
    TextureLayer l;
    l.name = uniqueTextureLayerName(t, name.empty() ? "Layer " + std::to_string(t.layers.size() + 1) : name);
    l.rgba.resize(t.texelCount() * 4);
    for (size_t i = 0; i < l.rgba.size(); i += 4) l.rgba[i] = l.rgba[i + 1] = l.rgba[i + 2] = 255, l.rgba[i + 3] = 0;
    if (above == -2) above = int(t.layers.size()) - 1;
    above = std::clamp(above, -1, int(t.layers.size()) - 1);
    t.layers.insert(t.layers.begin() + (above + 1), std::move(l));
    t.activeLayer = above + 1;
    syncLayerChannels(t); // transparent material channels (null tiles: no texel memory)
    compositeChannels(t, 0, 0, t.width, t.height);
    return above + 1;
}

int duplicateTextureLayer(PaintTexture& t, int layer) {
    if (!validTexLayer(t, layer)) return -2;
    ensureStack(t);
    TextureLayer copy;
    if (layer < 0) {
        copy.name = "Base";
        copy.rgba = t.base;
        for (size_t i = 3; i < copy.rgba.size(); i += 4) copy.rgba[i] = 255;
        for (int c = 1; c < kChannelCount; ++c)
            if (channelEnabled(t, Channel(c))) copy.channels[size_t(c)].image = layerImageFromBase(t.channels[size_t(c)].base);
    } else {
        copy = t.layers[size_t(layer)];
        copy.id = newTextureLayerId();
        copy.locked = false;
    }
    copy.name = uniqueTextureLayerName(t, copy.name + " copy");
    t.layers.insert(t.layers.begin() + (layer + 1), std::move(copy));
    syncLayerChannels(t);
    t.activeLayer = layer + 1;
    compositeTextureAll(t);
    return layer + 1;
}

bool removeTextureLayer(PaintTexture& t, int layer) {
    if (layer < 0 || layer >= int(t.layers.size())) return false;
    t.layers.erase(t.layers.begin() + layer);
    t.activeLayer = std::max(std::min(layer, int(t.layers.size()) - 1), -1);
    compositeTextureAll(t);
    dropStackIfEmpty(t);
    return true;
}

bool moveTextureLayer(PaintTexture& t, int layer, int direction) {
    const int to = layer + (direction > 0 ? 1 : -1);
    if (layer < 0 || layer >= int(t.layers.size()) || to < 0 || to >= int(t.layers.size())) return false;
    std::swap(t.layers[size_t(layer)], t.layers[size_t(to)]);
    if (t.activeLayer == layer) t.activeLayer = to;
    else if (t.activeLayer == to) t.activeLayer = layer;
    compositeTextureAll(t);
    return true;
}

bool mergeTextureLayerDown(PaintTexture& t, int layer, std::string& error) {
    if (layer < 0 || layer >= int(t.layers.size())) {
        error = "Choose a layer above Base to merge down";
        return false;
    }
    TextureLayer& up = t.layers[size_t(layer)];
    if (up.locked) {
        error = "Layer '" + up.name + "' is locked";
        return false;
    }
    const size_t n = t.texelCount();
    // Material channels first (the colour merge below changes `low`'s opacity).
    for (int c = 1; c < kChannelCount; ++c) {
        if (!channelEnabled(t, Channel(c))) continue;
        const LayerChannel& uc = up.channels[size_t(c)];
        if (!up.visible || !uc.enabled || !uc.image.valid()) continue;
        const bool normal = c == int(Channel::Normal);
        if (layer == 0) {
            mergeChannelIntoBase(t.channels[size_t(c)].base, uc.image, up.opacity * uc.opacity, uc.blend, normal);
        } else {
            TextureLayer& low = t.layers[size_t(layer - 1)];
            if (low.locked || !low.visible) break; // refused below
            LayerChannel& lc = low.channels[size_t(c)];
            if (!lc.image.valid()) continue;
            mergeChannelIntoLayer(lc.image, low.opacity * lc.opacity, uc.image, up.opacity * uc.opacity, uc.blend, normal);
            lc.opacity = 1.0f;
        }
    }
    if (layer == 0) {
        if (up.visible)
            parallelRows(t.height, n, [&](int b, int e) {
                for (size_t i = size_t(b) * size_t(t.width); i < size_t(e) * size_t(t.width); ++i) {
                    const uint8_t* s = &up.rgba[i * 4];
                    const float a = std::min(float(s[3]) / 255.0f * up.opacity, 1.0f);
                    if (a <= 0.0f) continue;
                    uint8_t* d = &t.base[i * 4];
                    const glm::vec3 c = glm::vec3(d[0], d[1], d[2]) / 255.0f, src = glm::vec3(s[0], s[1], s[2]) / 255.0f;
                    const glm::vec3 o = glm::mix(c, glm::clamp(blendColors(up.blend, c, src), 0.0f, 1.0f), a);
                    d[0] = unit8(o.x), d[1] = unit8(o.y), d[2] = unit8(o.z);
                }
            });
    } else {
        TextureLayer& low = t.layers[size_t(layer - 1)];
        if (low.locked) {
            error = "Layer '" + low.name + "' is locked";
            return false;
        }
        if (!low.visible) {
            error = "Show layer '" + low.name + "' before merging into it";
            return false;
        }
        parallelRows(t.height, n, [&](int b, int e) {
            for (size_t i = size_t(b) * size_t(t.width); i < size_t(e) * size_t(t.width); ++i) {
                uint8_t* d = &low.rgba[i * 4];
                const uint8_t* s = &up.rgba[i * 4];
                const float ab = std::min(float(d[3]) / 255.0f * low.opacity, 1.0f);
                const float as = up.visible ? std::min(float(s[3]) / 255.0f * up.opacity, 1.0f) : 0.0f;
                if (as <= 0.0f) {
                    d[3] = unit8(ab);
                    continue;
                }
                const glm::vec3 cb = glm::vec3(d[0], d[1], d[2]) / 255.0f, cs = glm::vec3(s[0], s[1], s[2]) / 255.0f;
                const glm::vec3 mixed = glm::clamp(blendColors(up.blend, cb, cs), 0.0f, 1.0f);
                const float ao = as + ab * (1.0f - as);
                const glm::vec3 co = (as * (1.0f - ab) * cs + as * ab * mixed + (1.0f - as) * ab * cb) / ao;
                d[0] = unit8(co.x), d[1] = unit8(co.y), d[2] = unit8(co.z), d[3] = unit8(ao);
            }
        });
        low.opacity = 1.0f;
    }
    t.layers.erase(t.layers.begin() + layer);
    t.activeLayer = layer - 1;
    compositeTextureAll(t);
    dropStackIfEmpty(t);
    return true;
}

void flattenTextureLayers(PaintTexture& t) {
    if (t.layers.empty()) return;
    compositeTextureAll(t);
    for (int c = 1; c < kChannelCount; ++c)
        if (channelEnabled(t, Channel(c))) t.channels[size_t(c)].base = channelImage(t, Channel(c));
    t.layers.clear();
    t.base = t.pixels;
    dropStackIfEmpty(t);
}

void clearTextureLayer(PaintTexture& t, int layer) {
    // Material channels go back to their default (Base) or to transparent (a layer) too.
    for (int c = 1; c < kChannelCount; ++c) {
        if (!channelEnabled(t, Channel(c))) continue;
        TileImage* img = layer < 0 ? &t.channels[size_t(c)].base
                         : layer < int(t.layers.size()) ? &t.layers[size_t(layer)].channels[size_t(c)].image
                                                        : nullptr;
        if (img && img->valid()) img->reset(img->width(), img->height(), img->components(), img->bits(), img->fill());
    }
    if (t.layers.empty()) {
        std::fill(t.pixels.begin(), t.pixels.end(), uint8_t(255));
        t.markAllDirty();
        return;
    }
    if (layer < 0) std::fill(t.base.begin(), t.base.end(), uint8_t(255));
    else if (layer < int(t.layers.size())) {
        std::vector<uint8_t>& px = t.layers[size_t(layer)].rgba;
        for (size_t i = 0; i < px.size(); i += 4) px[i] = px[i + 1] = px[i + 2] = 255, px[i + 3] = 0;
    }
    compositeTextureAll(t);
}

} // namespace sf
