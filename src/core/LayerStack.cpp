// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/LayerStack.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/MeshMaps.h"
#include "core/Polypaint.h"
#include "core/Texture.h"
#include "core/ThreadPool.h"
#include "core/Undo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <sstream>

namespace sf {

namespace {

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Gradient noise in 3D (Perlin's improved noise with hashed gradients), about -1..1.
inline float gradDot(uint32_t h, const glm::vec3& d) {
    switch (h % 12u) {
    case 0: return d.x + d.y;
    case 1: return -d.x + d.y;
    case 2: return d.x - d.y;
    case 3: return -d.x - d.y;
    case 4: return d.x + d.z;
    case 5: return -d.x + d.z;
    case 6: return d.x - d.z;
    case 7: return -d.x - d.z;
    case 8: return d.y + d.z;
    case 9: return -d.y + d.z;
    case 10: return d.y - d.z;
    default: return -d.y - d.z;
    }
}
float gradientNoise(const glm::vec3& p, uint32_t seed) {
    const glm::vec3 fl = glm::floor(p);
    const glm::ivec3 i(fl);
    const glm::vec3 f = p - fl;
    const glm::vec3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    auto h = [&](int dx, int dy, int dz) {
        return hash32(uint32_t(i.x + dx) * 73856093u ^ uint32_t(i.y + dy) * 19349663u ^ uint32_t(i.z + dz) * 83492791u ^ seed);
    };
    auto g = [&](int dx, int dy, int dz) { return gradDot(h(dx, dy, dz), f - glm::vec3(float(dx), float(dy), float(dz))); };
    const float x00 = glm::mix(g(0, 0, 0), g(1, 0, 0), u.x), x10 = glm::mix(g(0, 1, 0), g(1, 1, 0), u.x);
    const float x01 = glm::mix(g(0, 0, 1), g(1, 0, 1), u.x), x11 = glm::mix(g(0, 1, 1), g(1, 1, 1), u.x);
    return glm::mix(glm::mix(x00, x10, u.y), glm::mix(x01, x11, u.y), u.z);
}
// Fractal sum, 0..1 (0.5 on average).
float fbm(const glm::vec3& p, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    glm::vec3 q = p;
    for (int o = 0; o < std::clamp(octaves, 1, 8); ++o) {
        sum += amp * gradientNoise(q, seed + uint32_t(o) * 1013u);
        norm += amp;
        amp *= 0.5f;
        q = q * 2.02f + glm::vec3(17.3f, 9.1f, 4.7f);
    }
    return std::clamp(0.5f + 0.75f * sum / norm, 0.0f, 1.0f);
}

float blendMask(MaskBlend b, float a, float v) {
    switch (b) {
    case MaskBlend::Replace: return v;
    case MaskBlend::Multiply: return a * v;
    case MaskBlend::Add: return a + v;
    case MaskBlend::Subtract: return a - v;
    case MaskBlend::Min: return std::min(a, v);
    case MaskBlend::Max: return std::max(a, v);
    case MaskBlend::Screen: return 1.0f - (1.0f - a) * (1.0f - v);
    }
    return v;
}

inline float smooth(float e0, float e1, float x) {
    if (e1 <= e0) return x >= e0 ? 1.0f : 0.0f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct EvalContext {
    const PaintTexture& tex;
    const TextureLayer& layer;
    const MeshMapSet* maps = nullptr;
    glm::vec3 boxMin{0.0f}, boxSize{1.0f};
    float diagonal = 1.0f;

    float map(MeshMapKind k, int x, int y, float fallback) const {
        return maps && maps->has(k) ? maps->value(k, x, y) : fallback;
    }
    glm::vec3 vec(MeshMapKind k, int x, int y, glm::vec3 fallback) const {
        return maps && maps->has(k) ? maps->vector(k, x, y) : fallback;
    }
    // The texel's point in object space (from the position map), for noise.
    glm::vec3 point(int x, int y) const {
        if (maps && maps->has(MeshMapKind::Position)) return boxMin + maps->vector(MeshMapKind::Position, x, y) * boxSize;
        return glm::vec3(float(x) / float(tex.width), float(y) / float(tex.height), 0.0f) * diagonal; // (no maps: UV space)
    }
    float noise(const MaskEffect& e, int x, int y) const {
        const float size = std::max(e.scale, 1e-4f) * diagonal;
        const float n = fbm(point(x, y) / size, e.octaves, e.seed * 2654435761u + 7u);
        return std::clamp(0.5f + (n - 0.5f) * e.contrast * 1.6f, 0.0f, 1.0f);
    }
};

// A generator's value at texel (x, y), 0..1.
float generatorValue(const MaskEffect& e, const EvalContext& c, int x, int y) {
    float s = 0.0f;
    switch (e.generator) {
    case GeneratorKind::EdgeWear: // (square root: gently rounded edges count too, not only sharp ones)
        s = std::sqrt(std::clamp((c.map(MeshMapKind::Curvature, x, y, 0.5f) - 0.5f) * 2.0f, 0.0f, 1.0f));
        break;
    case GeneratorKind::Dirt:
        s = std::clamp((1.0f - c.map(MeshMapKind::AmbientOcclusion, x, y, 1.0f)) * 1.6f +
                           std::max(0.0f, 0.5f - c.map(MeshMapKind::Curvature, x, y, 0.5f)) * 1.2f,
                       0.0f, 1.0f);
        break;
    case GeneratorKind::Gradient: {
        const glm::vec3 d = glm::length(e.direction) > 1e-6f ? glm::normalize(e.direction) : glm::vec3(0, 1, 0);
        const glm::vec3 p = c.maps && c.maps->has(MeshMapKind::Position) ? c.maps->vector(MeshMapKind::Position, x, y) : glm::vec3(0.5f);
        // Along the direction across the box: 0 at one end, 1 at the other.
        const glm::vec3 ad = glm::abs(d);
        s = std::clamp(glm::dot(p - 0.5f, d) / std::max(0.5f * (ad.x + ad.y + ad.z), 1e-6f) * 0.5f + 0.5f, 0.0f, 1.0f);
        break;
    }
    case GeneratorKind::Thickness: s = 1.0f - c.map(MeshMapKind::Thickness, x, y, 1.0f); break;
    case GeneratorKind::LightDirection: {
        const glm::vec3 d = glm::length(e.direction) > 1e-6f ? glm::normalize(e.direction) : glm::vec3(0, 1, 0);
        s = glm::dot(c.vec(MeshMapKind::WorldNormal, x, y, glm::vec3(0, 1, 0)), d) * 0.5f + 0.5f;
        break;
    }
    }
    if (e.breakup > 0.0f) s += e.breakup * (c.noise(e, x, y) - 0.5f);
    // Level: how much of the range is covered; softness: the width of the transition.
    const float t = 1.0f - std::clamp(e.level, 0.0f, 1.0f), soft = std::max(e.softness, 0.0f) * 0.5f;
    return smooth(t - soft, t + soft + 1e-4f, s);
}

float levels(const MaskEffect& e, float v) {
    const float span = std::max(e.inHigh - e.inLow, 1e-4f);
    v = std::clamp((v - e.inLow) / span, 0.0f, 1.0f);
    v = std::pow(v, 1.0f / std::max(e.gamma, 0.01f));
    return e.outLow + v * (e.outHigh - e.outLow);
}

bool covered(const PaintTexture& t, int x, int y) {
    return t.coverage.size() == (t.texelCount() + 7) / 8 && t.covered(x, y);
}

// One tile of a Fill layer's mask: `out` gets kTextureTile^2 values (rows of the tile, clipped texels unused).
void tileMask(const EvalContext& c, const FillLayer& fill, int tx0, int ty0, std::vector<float>& out) {
    const int pad = fill.blurPad();
    const int W = kTextureTile + 2 * pad;
    const int rx0 = tx0 - pad, ry0 = ty0 - pad;
    std::vector<float> m(size_t(W) * size_t(W), 1.0f), tmp;
    auto clampX = [&](int x) { return std::clamp(x, 0, c.tex.width - 1); };
    auto clampY = [&](int y) { return std::clamp(y, 0, c.tex.height - 1); };
    bool first = true;
    for (const MaskEffect& e : fill.effects) {
        if (!e.enabled || e.opacity <= 0.0f) continue;
        const float op = std::clamp(e.opacity, 0.0f, 1.0f);
        const MaskBlend blend = first && e.kind != MaskEffectKind::Levels && e.kind != MaskEffectKind::Blur &&
                                        e.kind != MaskEffectKind::Invert ? MaskBlend::Replace : e.blend;
        switch (e.kind) {
        case MaskEffectKind::Generator:
        case MaskEffectKind::Noise:
        case MaskEffectKind::Paint:
        case MaskEffectKind::Fill:
            for (int ly = 0; ly < W; ++ly)
                for (int lx = 0; lx < W; ++lx) {
                    const int x = clampX(rx0 + lx), y = clampY(ry0 + ly);
                    float v = 0.0f;
                    if (e.kind == MaskEffectKind::Generator) v = generatorValue(e, c, x, y);
                    else if (e.kind == MaskEffectKind::Noise) v = c.noise(e, x, y);
                    else if (e.kind == MaskEffectKind::Fill) v = std::clamp(e.value, 0.0f, 1.0f);
                    else v = c.layer.mask.size() == c.tex.texelCount() * 4
                                 ? float(c.layer.mask[(size_t(y) * size_t(c.tex.width) + size_t(x)) * 4 + 3]) / 255.0f
                                 : 0.0f;
                    float& a = m[size_t(ly) * size_t(W) + size_t(lx)];
                    a = glm::mix(a, std::clamp(blendMask(blend, a, v), 0.0f, 1.0f), op);
                }
            first = false;
            break;
        case MaskEffectKind::Levels:
        case MaskEffectKind::Invert:
            for (float& a : m) a = glm::mix(a, e.kind == MaskEffectKind::Invert ? 1.0f - a : std::clamp(levels(e, a), 0.0f, 1.0f), op);
            break;
        case MaskEffectKind::Blur: {
            // Two box passes (close to a Gaussian), each separable, over covered texels only so the mask doesn't
            // bleed in from empty texture between UV islands.
            const int r = std::clamp(int(std::lround(e.radius)), 0, 64);
            if (r == 0) break;
            std::vector<float> w(m.size());
            for (int ly = 0; ly < W; ++ly)
                for (int lx = 0; lx < W; ++lx)
                    w[size_t(ly) * size_t(W) + size_t(lx)] = covered(c.tex, clampX(rx0 + lx), clampY(ry0 + ly)) ? 1.0f : 0.0f;
            std::vector<float> orig = m;
            tmp.assign(m.size(), 0.0f);
            std::vector<float> tw(m.size(), 0.0f);
            for (int pass = 0; pass < 2; ++pass) {
                for (int axis = 0; axis < 2; ++axis) {
                    for (int ly = 0; ly < W; ++ly)
                        for (int lx = 0; lx < W; ++lx) {
                            float s = 0.0f, ws = 0.0f;
                            for (int k = -r; k <= r; ++k) {
                                const int sx = axis == 0 ? lx + k : lx, sy = axis == 0 ? ly : ly + k;
                                if (sx < 0 || sy < 0 || sx >= W || sy >= W) continue;
                                const size_t j = size_t(sy) * size_t(W) + size_t(sx);
                                s += m[j] * w[j];
                                ws += w[j];
                            }
                            const size_t i = size_t(ly) * size_t(W) + size_t(lx);
                            tmp[i] = ws > 0.0f ? s / ws : m[i];
                            tw[i] = ws > 0.0f ? 1.0f : w[i];
                        }
                    m.swap(tmp);
                    w.swap(tw);
                }
            }
            for (size_t i = 0; i < m.size(); ++i) m[i] = glm::mix(orig[i], m[i], op);
            break;
        }
        }
    }
    out.resize(size_t(kTextureTile) * kTextureTile);
    for (int ly = 0; ly < kTextureTile; ++ly)
        for (int lx = 0; lx < kTextureTile; ++lx)
            out[size_t(ly) * kTextureTile + size_t(lx)] = std::clamp(m[size_t(ly + pad) * size_t(W) + size_t(lx + pad)], 0.0f, 1.0f);
}

EvalContext contextFor(const PaintTexture& tex, const TextureLayer& layer) {
    EvalContext c{tex, layer};
    const MeshMapSet* maps = tex.meshMaps.get();
    if (maps && maps->width == tex.width && maps->height == tex.height) {
        c.maps = maps;
        c.boxMin = maps->boundsMin;
        c.boxSize = maps->boundsMax - maps->boundsMin;
        c.diagonal = maps->diagonal;
    }
    return c;
}

std::vector<uint32_t> allTiles(const PaintTexture& t) {
    const size_t n = size_t((t.width + kTextureTile - 1) / kTextureTile) * size_t((t.height + kTextureTile - 1) / kTextureTile);
    std::vector<uint32_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = uint32_t(i);
    return v;
}

// The tiles plus those within `pad` texels around them (blur reads its neighbours).
std::vector<uint32_t> growTiles(const PaintTexture& t, const std::vector<uint32_t>& tiles, int pad) {
    if (pad <= 0) return tiles;
    const int tilesX = (t.width + kTextureTile - 1) / kTextureTile, tilesY = (t.height + kTextureTile - 1) / kTextureTile;
    const int grow = (pad + kTextureTile - 1) / kTextureTile;
    std::vector<uint8_t> on(size_t(tilesX) * size_t(tilesY), 0);
    for (uint32_t ti : tiles) {
        const int tx = int(ti % uint32_t(tilesX)), ty = int(ti / uint32_t(tilesX));
        for (int y = std::max(0, ty - grow); y <= std::min(tilesY - 1, ty + grow); ++y)
            for (int x = std::max(0, tx - grow); x <= std::min(tilesX - 1, tx + grow); ++x) on[size_t(y) * size_t(tilesX) + size_t(x)] = 1;
    }
    std::vector<uint32_t> out;
    for (size_t i = 0; i < on.size(); ++i)
        if (on[i]) out.push_back(uint32_t(i));
    return out;
}

MeshMapRequester g_requester;
std::mutex g_requesterMutex;

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const char* maskEffectId(MaskEffectKind k) {
    static const char* ids[] = {"generator", "noise", "paint", "levels", "blur", "invert", "fill"};
    return ids[std::min(int(k), kMaskEffectKindCount - 1)];
}
const char* maskEffectLabel(MaskEffectKind k) {
    static const char* labels[] = {"Generator", "Noise", "Paint", "Levels", "Blur", "Invert", "Fill"};
    return labels[std::min(int(k), kMaskEffectKindCount - 1)];
}
const char* generatorId(GeneratorKind g) {
    static const char* ids[] = {"edge_wear", "dirt", "gradient", "thickness", "light_direction"};
    return ids[std::min(int(g), kGeneratorKindCount - 1)];
}
const char* generatorLabel(GeneratorKind g) {
    static const char* labels[] = {"Edge Wear", "Dirt", "Gradient", "Thickness", "Light Direction"};
    return labels[std::min(int(g), kGeneratorKindCount - 1)];
}
const char* maskBlendId(MaskBlend b) {
    static const char* ids[] = {"replace", "multiply", "add", "subtract", "min", "max", "screen"};
    return ids[std::min(int(b), kMaskBlendCount - 1)];
}

namespace {
std::string lower(const std::string& s) {
    std::string o;
    for (char c : s) o += c == '-' || c == ' ' ? '_' : char(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
} // namespace

bool parseMaskEffect(const std::string& text, MaskEffectKind& out) {
    const std::string s = lower(text);
    for (int i = 0; i < kMaskEffectKindCount; ++i)
        if (s == maskEffectId(MaskEffectKind(i))) return out = MaskEffectKind(i), true;
    return false;
}
bool parseGenerator(const std::string& text, GeneratorKind& out) {
    std::string s = lower(text);
    if (s == "edges" || s == "edge") s = "edge_wear";
    if (s == "light" || s == "dust" || s == "top") s = "light_direction";
    if (s == "grime" || s == "occlusion") s = "dirt";
    for (int i = 0; i < kGeneratorKindCount; ++i)
        if (s == generatorId(GeneratorKind(i))) return out = GeneratorKind(i), true;
    return false;
}
bool parseMaskBlend(const std::string& text, MaskBlend& out) {
    const std::string s = lower(text);
    for (int i = 0; i < kMaskBlendCount; ++i)
        if (s == maskBlendId(MaskBlend(i))) return out = MaskBlend(i), true;
    return false;
}

// ---------------------------------------------------------------------------
// Fill layers
// ---------------------------------------------------------------------------

uint32_t MaskEffect::mapsNeeded() const {
    uint32_t m = 0;
    const bool noise = kind == MaskEffectKind::Noise || (kind == MaskEffectKind::Generator && breakup > 0.0f);
    if (noise) m |= meshMapBit(MeshMapKind::Position);
    if (kind == MaskEffectKind::Generator) switch (generator) {
        case GeneratorKind::EdgeWear: m |= meshMapBit(MeshMapKind::Curvature); break;
        case GeneratorKind::Dirt: m |= meshMapBit(MeshMapKind::AmbientOcclusion) | meshMapBit(MeshMapKind::Curvature); break;
        case GeneratorKind::Gradient: m |= meshMapBit(MeshMapKind::Position); break;
        case GeneratorKind::Thickness: m |= meshMapBit(MeshMapKind::Thickness); break;
        case GeneratorKind::LightDirection: m |= meshMapBit(MeshMapKind::WorldNormal); break;
        }
    if (kind == MaskEffectKind::Blur) m |= 0; // (coverage only)
    return m;
}

uint32_t FillLayer::mapsNeeded() const {
    uint32_t m = 0;
    for (const MaskEffect& e : effects)
        if (e.enabled) m |= e.mapsNeeded();
    return m;
}
bool FillLayer::needsMaps() const { return mapsNeeded() != 0; }
bool FillLayer::hasPaint() const {
    return std::any_of(effects.begin(), effects.end(), [](const MaskEffect& e) { return e.enabled && e.kind == MaskEffectKind::Paint; });
}
int FillLayer::blurPad() const {
    int pad = 0;
    for (const MaskEffect& e : effects)
        if (e.enabled && e.kind == MaskEffectKind::Blur) pad += 2 * std::clamp(int(std::lround(e.radius)), 0, 64);
    return pad;
}

bool isFillLayer(const PaintTexture& tex, int layer) {
    return layer >= 0 && layer < int(tex.layers.size()) && tex.layers[size_t(layer)].fill != nullptr;
}

uint32_t fillMapsNeeded(const PaintTexture& tex) {
    uint32_t m = 0;
    for (const TextureLayer& l : tex.layers)
        if (l.fill) m |= l.fill->mapsNeeded();
    return m;
}

void evaluateFillLayer(const Mesh& mesh, PaintTexture& tex, int layer, const std::vector<uint32_t>* tilesIn) {
    if (!isFillLayer(tex, layer) || tex.width <= 0) return;
    ensureCoverage(mesh, tex);
    syncLayerChannels(tex);
    TextureLayer& l = tex.layers[size_t(layer)];
    const FillLayer& fill = *l.fill;
    const std::vector<uint32_t> tiles = tilesIn ? *tilesIn : allTiles(tex);
    const int tilesX = (tex.width + kTextureTile - 1) / kTextureTile;
    const size_t n = tex.texelCount() * 4;
    if (l.rgba.size() != n) l.rgba.assign(n, 0);
    // The material: the colour as stored (sRGB) and each channel's encoded value.
    const glm::vec3 srgb = linearToSrgb(glm::clamp(fill.material.color, 0.0f, 1.0f));
    const bool colour = fill.material.channels & channelBit(Channel::BaseColor);
    l.channels[size_t(Channel::BaseColor)].enabled = colour;
    struct Target {
        int c;
        TileImage* img;
        std::array<float, 4> value;
        int comps, bits;
        std::vector<uint8_t*> tiles;
    };
    std::vector<Target> targets;
    for (int c = 1; c < kChannelCount; ++c) {
        TileImage& img = l.channels[size_t(c)].image;
        if (!channelEnabled(tex, Channel(c)) || !img.valid()) continue;
        if (!(fill.material.channels & (1u << c))) { // not this layer's: transparent
            for (uint32_t ti : tiles) img.fillTile(ti, img.fill());
            continue;
        }
        l.channels[size_t(c)].enabled = true;
        Target t{c, &img, fill.material.encoded(Channel(c)), img.components(), img.bits(), {}};
        for (uint32_t ti : tiles) t.tiles.push_back(img.writableTile(ti)); // (serially: not thread-safe per tile)
        targets.push_back(std::move(t));
    }
    const EvalContext ctx = contextFor(tex, l);
    poolFor(tiles.size(), [&](size_t b, size_t e) {
        std::vector<float> mask;
        for (size_t k = b; k < e; ++k) {
            const uint32_t ti = tiles[k];
            const int x0 = int(ti % uint32_t(tilesX)) * kTextureTile, y0 = int(ti / uint32_t(tilesX)) * kTextureTile;
            tileMask(ctx, fill, x0, y0, mask);
            for (int ly = 0; ly < kTextureTile; ++ly) {
                const int y = y0 + ly;
                for (int lx = 0; lx < kTextureTile; ++lx) {
                    const int x = x0 + lx;
                    const float a = mask[size_t(ly) * kTextureTile + size_t(lx)];
                    if (x < tex.width && y < tex.height) {
                        uint8_t* px = &l.rgba[(size_t(y) * size_t(tex.width) + size_t(x)) * 4];
                        for (int i = 0; i < 3; ++i) px[i] = uint8_t(std::lround(srgb[i] * 255.0f));
                        px[3] = colour ? uint8_t(std::lround(a * 255.0f)) : 0;
                    }
                    for (const Target& t : targets) {
                        const size_t li = size_t(ly) * kTileImageTile + size_t(lx);
                        const float maxv = t.bits == 16 ? 65535.0f : 255.0f;
                        for (int i = 0; i < t.comps; ++i) {
                            const float v = i == t.comps - 1 ? a : t.value[size_t(i)];
                            const uint32_t u = uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * maxv));
                            if (t.bits == 16) reinterpret_cast<uint16_t*>(t.tiles[k])[li * size_t(t.comps) + size_t(i)] = uint16_t(u);
                            else t.tiles[k][li * size_t(t.comps) + size_t(i)] = uint8_t(u);
                        }
                    }
                }
            }
        }
    }, 1, 2);
    l.fill->mapsRevision = ctx.maps ? ctx.maps->revision : 0;
}

void evaluateFillLayers(const Mesh& mesh, PaintTexture& tex, const std::vector<uint32_t>* tilesIn, bool onlyPainted) {
    bool any = false;
    int pad = 0;
    for (size_t i = 0; i < tex.layers.size(); ++i)
        if (const auto& f = tex.layers[i].fill; f && (!onlyPainted || f->hasPaint())) any = true, pad = std::max(pad, f->blurPad());
    if (!any) return;
    const std::vector<uint32_t> tiles = tilesIn ? growTiles(tex, *tilesIn, pad) : allTiles(tex);
    for (size_t i = 0; i < tex.layers.size(); ++i)
        if (const auto& f = tex.layers[i].fill; f && (!onlyPainted || f->hasPaint())) evaluateFillLayer(mesh, tex, int(i), &tiles);
    const int tilesX = (tex.width + kTextureTile - 1) / kTextureTile;
    if (!tilesIn) {
        compositeTextureAll(tex);
        return;
    }
    for (uint32_t ti : tiles) {
        const int x0 = int(ti % uint32_t(tilesX)) * kTextureTile, y0 = int(ti / uint32_t(tilesX)) * kTextureTile;
        compositeTexture(tex, x0, y0, x0 + kTextureTile, y0 + kTextureTile);
    }
}

bool updateFillLayers(const Mesh& mesh, PaintTexture& tex) {
    if (tex.fillRevision == tex.revision) return false;
    bool painted = false;
    for (const TextureLayer& l : tex.layers) painted = painted || (l.fill && l.fill->hasPaint());
    if (!painted) {
        tex.fillRevision = tex.revision;
        return false;
    }
    const int tilesX = (tex.width + kTextureTile - 1) / kTextureTile, tilesY = (tex.height + kTextureTile - 1) / kTextureTile;
    std::vector<uint32_t> tiles;
    for (int ty = 0; ty < tilesY; ++ty)
        for (int tx = 0; tx < tilesX; ++tx)
            if (tex.tileDirty(tx, ty)) tiles.push_back(uint32_t(ty * tilesX + tx));
    if (!tiles.empty() && tiles.size() < size_t(tilesX * tilesY)) evaluateFillLayers(mesh, tex, &tiles, true);
    else if (!tiles.empty()) evaluateFillLayers(mesh, tex, nullptr, true);
    tex.fillRevision = tex.revision;
    return !tiles.empty();
}

float fillMaskAt(const Mesh&, const PaintTexture& tex, int layer, int x, int y) {
    if (!isFillLayer(tex, layer)) return 0.0f;
    const TextureLayer& l = tex.layers[size_t(layer)];
    std::vector<float> mask;
    const int x0 = (x / kTextureTile) * kTextureTile, y0 = (y / kTextureTile) * kTextureTile;
    tileMask(contextFor(tex, l), *l.fill, x0, y0, mask);
    return mask[size_t(y - y0) * kTextureTile + size_t(x - x0)];
}

int addFillLayer(Mesh& mesh, PaintTexture& tex, std::string name, FillLayer fill, int above) {
    const int i = addTextureLayer(tex, name.empty() ? "Fill" : name, above);
    TextureLayer& l = tex.layers[size_t(i)];
    l.fill = std::make_shared<FillLayer>(std::move(fill));
    l.mask.assign(tex.texelCount() * 4, 0); // the painted mask starts empty (coverage 0)
    for (size_t k = 0; k < l.mask.size(); k += 4) l.mask[k] = l.mask[k + 1] = l.mask[k + 2] = 255;
    evaluateFillLayer(mesh, tex, i);
    compositeTextureAll(tex);
    return i;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

namespace {
Json vec3Json(const glm::vec3& v) {
    Json a = Json::array();
    for (int i = 0; i < 3; ++i) a.push(double(v[i]));
    return a;
}
bool readVec3(const Json& j, glm::vec3& v) {
    if (!j.isArray() || j.size() != 3) return false;
    for (int i = 0; i < 3; ++i)
        if (!j[size_t(i)].isNumber()) return false;
    for (int i = 0; i < 3; ++i) v[i] = float(j[size_t(i)].asNumber());
    return true;
}
float num(const Json& j, const char* key, float def, float lo, float hi) {
    const Json& v = j[key];
    return v.isNumber() && std::isfinite(v.asNumber()) ? std::clamp(float(v.asNumber()), lo, hi) : def;
}
} // namespace

Json maskEffectJson(const MaskEffect& e) {
    Json j = Json::object();
    j.set("kind", maskEffectId(e.kind));
    j.set("blend", maskBlendId(e.blend));
    j.set("opacity", double(e.opacity));
    if (!e.enabled) j.set("enabled", false);
    switch (e.kind) {
    case MaskEffectKind::Generator:
        j.set("generator", generatorId(e.generator));
        j.set("level", double(e.level));
        j.set("softness", double(e.softness));
        j.set("breakup", double(e.breakup));
        if (e.generator == GeneratorKind::Gradient || e.generator == GeneratorKind::LightDirection)
            j.set("direction", vec3Json(e.direction));
        [[fallthrough]];
    case MaskEffectKind::Noise:
        j.set("scale", double(e.scale));
        j.set("octaves", e.octaves);
        j.set("seed", double(e.seed));
        j.set("contrast", double(e.contrast));
        break;
    case MaskEffectKind::Levels:
        j.set("in_low", double(e.inLow));
        j.set("in_high", double(e.inHigh));
        j.set("gamma", double(e.gamma));
        j.set("out_low", double(e.outLow));
        j.set("out_high", double(e.outHigh));
        break;
    case MaskEffectKind::Blur: j.set("radius", double(e.radius)); break;
    case MaskEffectKind::Fill: j.set("value", double(e.value)); break;
    default: break;
    }
    return j;
}

bool maskEffectFromJson(const Json& j, MaskEffect& e, std::string& error) {
    e = MaskEffect();
    if (!j.isObject() || !parseMaskEffect(j["kind"].isString() ? j["kind"].asString() : std::string(), e.kind)) {
        error = "A mask effect needs a kind (generator, noise, paint, levels, blur, invert or fill)";
        return false;
    }
    if (j["blend"].isString() && !parseMaskBlend(j["blend"].asString(), e.blend)) {
        error = "Unknown mask blend '" + j["blend"].asString() + "'";
        return false;
    }
    if (e.kind == MaskEffectKind::Paint && !j["blend"].isString()) e.blend = MaskBlend::Add;
    e.opacity = num(j, "opacity", 1.0f, 0.0f, 1.0f);
    e.enabled = j["enabled"].asBool(true);
    if (j["generator"].isString() && !parseGenerator(j["generator"].asString(), e.generator)) {
        error = "Unknown generator '" + j["generator"].asString() + "'";
        return false;
    }
    e.level = num(j, "level", e.level, 0.0f, 1.0f);
    e.softness = num(j, "softness", e.softness, 0.0f, 1.0f);
    e.breakup = num(j, "breakup", e.breakup, 0.0f, 2.0f);
    readVec3(j["direction"], e.direction);
    e.scale = num(j, "scale", e.scale, 0.0005f, 10.0f);
    e.octaves = int(num(j, "octaves", float(e.octaves), 1.0f, 8.0f));
    e.seed = uint32_t(num(j, "seed", float(e.seed), 0.0f, 1e6f));
    e.contrast = num(j, "contrast", e.contrast, 0.0f, 10.0f);
    e.inLow = num(j, "in_low", e.inLow, 0.0f, 1.0f);
    e.inHigh = num(j, "in_high", e.inHigh, 0.0f, 1.0f);
    e.gamma = num(j, "gamma", e.gamma, 0.01f, 10.0f);
    e.outLow = num(j, "out_low", e.outLow, 0.0f, 1.0f);
    e.outHigh = num(j, "out_high", e.outHigh, 0.0f, 1.0f);
    e.radius = num(j, "radius", e.radius, 0.0f, 64.0f);
    e.value = num(j, "value", e.value, 0.0f, 1.0f);
    return true;
}

Json fillLayerJson(const FillLayer& f) {
    Json j = Json::object();
    Json mat = Json::object();
    const BrushMaterial& m = f.material;
    Json channels = Json::array();
    for (int c = 0; c < kChannelCount; ++c)
        if (m.channels & (1u << c)) channels.push(channelFormat(c).id);
    mat.set("channels", std::move(channels));
    mat.set("color", vec3Json(linearToSrgb(glm::clamp(m.color, 0.0f, 1.0f))));
    mat.set("roughness", double(m.roughness));
    mat.set("metallic", double(m.metallic));
    mat.set("normal", vec3Json(m.normal));
    mat.set("height", double(m.height));
    mat.set("ao", double(m.ao));
    mat.set("emissive", vec3Json(linearToSrgb(glm::clamp(m.emissive, 0.0f, 1.0f))));
    mat.set("opacity", double(m.opacity));
    j.set("material", std::move(mat));
    Json effects = Json::array();
    for (const MaskEffect& e : f.effects) effects.push(maskEffectJson(e));
    j.set("mask", std::move(effects));
    return j;
}

bool fillLayerFromJson(const Json& j, FillLayer& out, std::string& error) {
    out = FillLayer();
    if (!j.isObject()) {
        error = "A Fill layer must be an object";
        return false;
    }
    const Json& mat = j["material"];
    BrushMaterial& m = out.material;
    m.channels = 0;
    if (mat.isObject()) {
        for (const Json& c : mat["channels"].items()) {
            Channel ch;
            if (!c.isString() || !parseChannel(c.asString(), ch)) {
                error = "Unknown channel in a Fill layer";
                return false;
            }
            m.channels |= channelBit(ch);
        }
        glm::vec3 v;
        if (readVec3(mat["color"], v)) m.color = srgbToLinear(glm::clamp(v, 0.0f, 1.0f));
        m.roughness = num(mat, "roughness", m.roughness, 0.0f, 1.0f);
        m.metallic = num(mat, "metallic", m.metallic, 0.0f, 1.0f);
        if (readVec3(mat["normal"], v) && glm::length(v) > 1e-6f) m.normal = glm::normalize(v);
        m.height = num(mat, "height", m.height, 0.0f, 1.0f);
        m.ao = num(mat, "ao", m.ao, 0.0f, 1.0f);
        if (readVec3(mat["emissive"], v)) m.emissive = srgbToLinear(glm::clamp(v, 0.0f, 1.0f));
        m.opacity = num(mat, "opacity", m.opacity, 0.0f, 1.0f);
    }
    if (!m.channels) m.channels = channelBit(Channel::BaseColor);
    for (const Json& e : j["mask"].items()) {
        MaskEffect effect;
        if (!maskEffectFromJson(e, effect, error)) return false;
        out.effects.push_back(effect);
    }
    if (out.effects.size() > 32) {
        error = "A Fill layer has at most 32 mask effects";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Mesh Maps for Fill layers
// ---------------------------------------------------------------------------

void setMeshMapRequester(MeshMapRequester requester) {
    std::lock_guard lock(g_requesterMutex);
    g_requester = std::move(requester);
}

bool ensureFillMeshMaps(Document& doc, std::string& error) {
    Mesh& m = doc.mesh;
    if (!hasTexture(m)) return true;
    const PaintTexture& t = *m.texture;
    const uint32_t need = fillMapsNeeded(t);
    if (!need) return true;
    const MeshMapSet* have = t.meshMaps.get();
    if (have && have->matches(m, t.width, t.height) && (have->kinds & need) == need) return true;
    {
        std::lock_guard lock(g_requesterMutex);
        if (g_requester) {
            g_requester(doc);
            return false;
        }
    }
    auto set = std::make_shared<MeshMapSet>();
    MeshMapSettings st;
    st.aoRays = 24;
    st.thicknessRays = 12;
    if (have) st = have->settings, set->revision = have->revision, set->live = have->live;
    const uint32_t kinds = need | (have ? have->kinds : 0u) | meshMapBit(MeshMapKind::Position);
    if (!bakeMeshMaps(m, t.width, t.height, kinds, st, *set, error)) return false;
    mutableTexture(m).meshMaps = std::move(set);
    return true;
}

void notifyMeshMapsChanged(Document&, Mesh& mesh, const std::vector<uint32_t>* tiles) {
    if (!hasTexture(mesh)) return;
    bool any = false;
    for (const TextureLayer& l : mesh.texture->layers) any = any || (l.fill && l.fill->needsMaps());
    if (!any) return;
    PaintTexture& t = mutableTexture(mesh);
    evaluateFillLayers(mesh, t, tiles);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kNoTextureFill = "The Part has no texture: Tool > Texture Map > New Texture";

// The layer a command names (layer=<name> / index=<i>), else the active one; -2 with `error` when none fits.
int fillLayerArg(const PaintTexture& t, const Args& a, std::string& error) {
    int i = t.activeLayer;
    if (a.contains("layer")) {
        i = findTextureLayer(t, argString(a, "layer"));
        if (i < 0) {
            error = "No layer called '" + argString(a, "layer") + "'";
            return -2;
        }
    } else if (a.contains("index")) {
        i = int(argNumber(a, "index", -1));
    }
    if (!isFillLayer(t, i)) {
        error = "Not a Fill layer (add one with fill_layer action=add)";
        return -2;
    }
    return i;
}

// Material arguments onto `m`; `channels` gets the channels named or given a value. False + error on a bad value.
bool materialArgs(const Args& a, BrushMaterial& m, std::string& error) {
    uint32_t given = 0;
    if (a.contains("color")) {
        if (!parseColor(argString(a, "color"), m.color)) return error = "Unknown colour '" + argString(a, "color") + "'", false;
        given |= channelBit(Channel::BaseColor);
    }
    auto value = [&](const char* key, float& v, Channel c) {
        if (!a.contains(key)) return;
        v = std::clamp(float(argNumber(a, key, v)), 0.0f, 1.0f);
        given |= channelBit(c);
    };
    value("roughness", m.roughness, Channel::Roughness);
    value("metallic", m.metallic, Channel::Metallic);
    value("height", m.height, Channel::Height);
    value("ao", m.ao, Channel::AO);
    value("opacity", m.opacity, Channel::Opacity);
    if (a.contains("emissive")) {
        if (!parseColor(argString(a, "emissive"), m.emissive)) return error = "Unknown emissive colour", false;
        given |= channelBit(Channel::Emissive);
    }
    if (a.contains("channels")) {
        uint32_t ch = 0;
        std::stringstream ss(argString(a, "channels"));
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (item.empty()) continue;
            Channel c;
            if (!parseChannel(item, c)) return error = "Unknown channel '" + item + "'", false;
            ch |= channelBit(c);
        }
        m.channels = ch;
    } else if (given) {
        m.channels |= given;
    }
    return true;
}

// Mask effect arguments onto `e`.
bool effectArgs(const Args& a, MaskEffect& e, std::string& error) {
    if (a.contains("kind") && !parseMaskEffect(argString(a, "kind"), e.kind))
        return error = "Unknown mask effect '" + argString(a, "kind") + "' (generator, noise, paint, levels, blur, invert, fill)", false;
    if (a.contains("generator")) {
        if (!parseGenerator(argString(a, "generator"), e.generator))
            return error = "Unknown generator '" + argString(a, "generator") +
                           "' (edge_wear, dirt, gradient, thickness, light_direction)",
                   false;
        if (!a.contains("kind")) e.kind = MaskEffectKind::Generator;
    }
    if (a.contains("blend") && !parseMaskBlend(argString(a, "blend"), e.blend))
        return error = "Unknown blend (replace, multiply, add, subtract, min, max, screen)", false;
    auto f = [&](const char* key, float& v, float lo, float hi) {
        if (a.contains(key)) v = std::clamp(float(argNumber(a, key, v)), lo, hi);
    };
    f("mask_opacity", e.opacity, 0.0f, 1.0f);
    f("level", e.level, 0.0f, 1.0f);
    f("softness", e.softness, 0.0f, 1.0f);
    f("breakup", e.breakup, 0.0f, 2.0f);
    f("scale", e.scale, 0.0005f, 10.0f);
    f("contrast", e.contrast, 0.0f, 10.0f);
    f("in_low", e.inLow, 0.0f, 1.0f);
    f("in_high", e.inHigh, 0.0f, 1.0f);
    f("gamma", e.gamma, 0.01f, 10.0f);
    f("out_low", e.outLow, 0.0f, 1.0f);
    f("out_high", e.outHigh, 0.0f, 1.0f);
    f("radius", e.radius, 0.0f, 64.0f);
    f("value", e.value, 0.0f, 1.0f);
    if (a.contains("octaves")) e.octaves = std::clamp(int(argNumber(a, "octaves", e.octaves)), 1, 8);
    if (a.contains("seed")) e.seed = uint32_t(std::clamp(argNumber(a, "seed", e.seed), 0.0, 1e6));
    if (a.contains("enabled")) e.enabled = argBool(a, "enabled", true);
    if (a.contains("direction")) {
        glm::vec3 d;
        if (std::sscanf(argString(a, "direction").c_str(), "%f,%f,%f", &d.x, &d.y, &d.z) != 3 || glm::length(d) < 1e-6f)
            return error = "direction is x,y,z (e.g. 0,1,0)", false;
        e.direction = glm::normalize(d);
    }
    return true;
}

std::string describeFill(const PaintTexture& t, int i) {
    const FillLayer& f = *t.layers[size_t(i)].fill;
    std::string s = "Fill layer '" + t.layers[size_t(i)].name + "':";
    for (int c = 0; c < kChannelCount; ++c)
        if (f.material.channels & (1u << c)) s += std::string(" ") + channelFormat(c).id;
    s += "; mask:";
    if (f.effects.empty()) s += " full";
    for (size_t k = 0; k < f.effects.size(); ++k) {
        const MaskEffect& e = f.effects[k];
        s += std::string(k ? "," : "") + " " + (e.kind == MaskEffectKind::Generator ? generatorId(e.generator) : maskEffectId(e.kind));
        if (k) s += std::string(" (") + maskBlendId(e.blend) + ")";
        if (!e.enabled) s += " [off]";
    }
    return s;
}

// The edit as one undo step: settings changed by `edit`, the layer evaluated again (Mesh Maps baked first when
// it needs them and they aren't there; the app bakes them in the background and evaluates when they arrive).
CommandResult editFill(Document& doc, const Args& a, const std::string& label,
                       const std::function<bool(PaintTexture&, int&, std::string&)>& edit) {
    Mesh& m = doc.mesh;
    if (!hasTexture(m)) return {false, kNoTextureFill};
    UndoRecord r = captureTexture(m, label);
    PaintTexture& t = *m.texture;
    int layer = -1;
    std::string err;
    if (!edit(t, layer, err)) {
        m.texture = r.texture->texture; // (nothing changed: put the original back)
        return {false, err};
    }
    std::string note;
    if (!ensureFillMeshMaps(doc, err)) {
        if (!err.empty()) note = " (Mesh Maps: " + err + ")";
        else note = " (baking Mesh Maps...)";
    }
    evaluateFillLayer(m, *m.texture, layer);
    compositeTextureAll(*m.texture);
    finishTextureUndo(r, m);
    doc.undo.push(std::move(r));
    doc.modified = true;
    return {true, describeFill(*m.texture, layer) + note};
}

} // namespace

void registerLayerStackCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    reg.add({"fill_layer",
             "Fill layers: a material laid over the whole Part through a mask of effects (generators reading the Mesh "
             "Maps: edge_wear, dirt, gradient, thickness, light_direction; noise in object space; the painted mask; "
             "levels, blur, invert, fill). add makes one (with a generator, or full), set changes its material, effect "
             "adds or edits a mask effect (effect=<index>), remove_effect drops one, info describes it. Undoable; the "
             "layer re-evaluates when its settings, its painted mask or the Mesh Maps change.",
             {{"action", T::String, "add (default), set, effect, remove_effect, info.", false},
              {"name", T::String, "add: the layer's name.", false},
              {"layer", T::String, "The Fill layer by name (default: the active layer).", false},
              {"index", T::Number, "The Fill layer by index.", false},
              {"channels", T::String, "Channels it lays down, comma list (default: those given a value).", false},
              {"color", T::String, "Base colour (#rrggbb, r,g,b or a name).", false},
              {"roughness", T::Number, "0-1.", false},
              {"metallic", T::Number, "0-1.", false},
              {"height", T::Number, "0-1 (0.5 = flat).", false},
              {"ao", T::Number, "0-1.", false},
              {"emissive", T::String, "Emissive colour.", false},
              {"opacity", T::Number, "Material opacity channel, 0-1.", false},
              {"effect", T::Number, "effect / remove_effect: which mask effect (0 = top; default: a new one at the end).", false},
              {"kind", T::String, "Mask effect: generator, noise, paint, levels, blur, invert, fill.", false},
              {"generator", T::String, "edge_wear, dirt, gradient, thickness, light_direction.", false},
              {"blend", T::String, "How the effect combines: replace, multiply (default), add, subtract, min, max, screen.", false},
              {"mask_opacity", T::Number, "The effect's opacity, 0-1.", false},
              {"level", T::Number, "Generator: how much it covers, 0-1.", false},
              {"softness", T::Number, "Generator: transition width, 0-1.", false},
              {"breakup", T::Number, "Generator: noise break-up, 0-2.", false},
              {"scale", T::Number, "Noise feature size as a fraction of the Part (default 0.05).", false},
              {"octaves", T::Number, "Noise octaves, 1-8.", false},
              {"seed", T::Number, "Noise seed.", false},
              {"contrast", T::Number, "Noise contrast.", false},
              {"direction", T::String, "Gradient / light direction x,y,z.", false},
              {"in_low", T::Number, "Levels.", false},
              {"in_high", T::Number, "Levels.", false},
              {"gamma", T::Number, "Levels.", false},
              {"out_low", T::Number, "Levels.", false},
              {"out_high", T::Number, "Levels.", false},
              {"radius", T::Number, "Blur radius in texels.", false},
              {"value", T::Number, "Fill value, 0-1.", false},
              {"enabled", T::Bool, "The effect is on.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string action = argString(a, "action", "add");
                 Mesh& m = doc.mesh;
                 if (!hasTexture(m)) return {false, kNoTextureFill};
                 if (action == "info") {
                     std::string err;
                     const int i = fillLayerArg(*m.texture, a, err);
                     return i < 0 ? CommandResult{false, err} : CommandResult{true, describeFill(*m.texture, i)};
                 }
                 if (action == "add")
                     return editFill(doc, a, "New Fill Layer", [&](PaintTexture& t, int& layer, std::string& err) {
                         FillLayer f;
                         f.material.channels = 0;
                         if (!materialArgs(a, f.material, err)) return false;
                         if (!f.material.channels) f.material.channels = channelBit(Channel::BaseColor);
                         // Channels the texture doesn't have yet are turned on.
                         for (int c = 1; c < kChannelCount; ++c)
                             if (f.material.channels & (1u << c)) setChannelEnabled(t, Channel(c), true);
                         MaskEffect e;
                         if (a.contains("generator") || a.contains("kind")) {
                             if (!effectArgs(a, e, err)) return false;
                             if (e.kind == MaskEffectKind::Paint && !a.contains("blend")) e.blend = MaskBlend::Add;
                         } else {
                             e.kind = MaskEffectKind::Fill;
                         }
                         f.effects.push_back(e);
                         const std::string name = argString(a, "name", e.kind == MaskEffectKind::Generator ? generatorLabel(e.generator) : "Fill");
                         const int above = t.layers.empty() ? -2 : t.activeLayer;
                         layer = addTextureLayer(t, uniqueTextureLayerName(t, name), above);
                         t.layers[size_t(layer)].fill = std::make_shared<FillLayer>(std::move(f));
                         t.layers[size_t(layer)].mask.assign(t.texelCount() * 4, 0);
                         for (size_t k = 0; k < t.layers[size_t(layer)].mask.size(); k += 4)
                             t.layers[size_t(layer)].mask[k] = t.layers[size_t(layer)].mask[k + 1] = t.layers[size_t(layer)].mask[k + 2] = 255;
                         return true;
                     });
                 if (action == "set")
                     return editFill(doc, a, "Fill Layer", [&](PaintTexture& t, int& layer, std::string& err) {
                         layer = fillLayerArg(t, a, err);
                         if (layer < 0) return false;
                         auto f = std::make_shared<FillLayer>(*t.layers[size_t(layer)].fill);
                         if (!materialArgs(a, f->material, err)) return false;
                         for (int c = 1; c < kChannelCount; ++c)
                             if (f->material.channels & (1u << c)) setChannelEnabled(t, Channel(c), true);
                         t.layers[size_t(layer)].fill = std::move(f);
                         return true;
                     });
                 if (action == "effect" || action == "remove_effect")
                     return editFill(doc, a, "Fill Mask", [&](PaintTexture& t, int& layer, std::string& err) {
                         layer = fillLayerArg(t, a, err);
                         if (layer < 0) return false;
                         auto f = std::make_shared<FillLayer>(*t.layers[size_t(layer)].fill);
                         const int n = int(f->effects.size());
                         const int k = a.contains("effect") ? int(argNumber(a, "effect", -1)) : -1;
                         if (action == "remove_effect") {
                             if (k < 0 || k >= n) return err = "effect must be 0 to " + std::to_string(n - 1), false;
                             f->effects.erase(f->effects.begin() + k);
                         } else if (k >= 0 && k < n) {
                             if (!effectArgs(a, f->effects[size_t(k)], err)) return false;
                         } else if (k == -1 || k == n) {
                             MaskEffect e;
                             if (!effectArgs(a, e, err)) return false;
                             if (e.kind == MaskEffectKind::Paint && !a.contains("blend")) e.blend = MaskBlend::Add;
                             if (f->effects.size() >= 32) return err = "A Fill layer has at most 32 mask effects", false;
                             f->effects.push_back(e);
                         } else {
                             return err = "effect must be 0 to " + std::to_string(n), false;
                         }
                         t.layers[size_t(layer)].fill = std::move(f);
                         return true;
                     });
                 return {false, "action must be add, set, effect, remove_effect or info"};
             }});
}

} // namespace sf
