// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TextureChannels.h"

#include "core/Document.h"
#include "core/Mesh.h"
#include "core/Polypaint.h"
#include "core/Texture.h"
#include "core/ThreadPool.h"
#include "core/Undo.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sf {

namespace {

const ChannelFormat kFormats[kChannelCount] = {
    {"base_color", "Base Color", "BC", 4, 8, true, {1, 1, 1, 1}},
    {"roughness", "Roughness", "R", 1, 8, false, {0.65f, 0, 0, 0}},
    {"metallic", "Metallic", "M", 1, 8, false, {0, 0, 0, 0}},
    {"normal", "Normal", "N", 3, 8, false, {0.5f, 0.5f, 1.0f, 0}},
    {"height", "Height", "H", 1, 16, false, {0.5f, 0, 0, 0}},
    {"ao", "Ambient Occlusion", "AO", 1, 8, false, {1, 0, 0, 0}},
    {"emissive", "Emissive", "E", 3, 8, true, {0, 0, 0, 0}},
    {"opacity", "Opacity", "Op", 1, 8, false, {1, 0, 0, 0}},
};

std::string squash(const std::string& s) {
    std::string out;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

float srgbToLinear(float x) { return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); }
float linearToSrgbValue(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

// Format of a layer's channel image: the channel's components plus coverage.
void resetLayerImage(TileImage& img, const PaintTexture& tex, Channel c) {
    const ChannelFormat& f = channelFormat(c);
    const auto d = f.defaultValue;
    std::array<uint16_t, 4> fill = TileImage::toUnits(f.bits, d[0], d[1], d[2], d[3]);
    fill[size_t(f.components)] = 0; // transparent
    img.reset(tex.width, tex.height, f.components + 1, f.bits, fill);
}

void resetBaseImage(TileImage& img, const PaintTexture& tex, Channel c) {
    const ChannelFormat& f = channelFormat(c);
    const auto d = f.defaultValue;
    img.reset(tex.width, tex.height, f.components, f.bits, TileImage::toUnits(f.bits, d[0], d[1], d[2], d[3]));
}

inline void readUnits(const uint8_t* p, int comps, int bits, float* out) {
    if (bits == 16) {
        for (int k = 0; k < comps; ++k) {
            uint16_t v;
            std::memcpy(&v, p + k * 2, 2);
            out[k] = float(v) * (1.0f / 65535.0f);
        }
    } else {
        for (int k = 0; k < comps; ++k) out[k] = float(p[k]) * (1.0f / 255.0f);
    }
}
inline void writeUnits(uint8_t* p, int comps, int bits, const float* v) {
    if (bits == 16) {
        for (int k = 0; k < comps; ++k) {
            const uint16_t u = uint16_t(std::lround(std::clamp(v[k], 0.0f, 1.0f) * 65535.0f));
            std::memcpy(p + k * 2, &u, 2);
        }
    } else {
        for (int k = 0; k < comps; ++k) p[k] = uint8_t(std::lround(std::clamp(v[k], 0.0f, 1.0f) * 255.0f));
    }
}

// One layer of a channel blended over `v` (n components, 0..1) with weight a.
inline void blendOver(float* v, const float* s, int n, uint8_t blend, float a, bool normal) {
    if (normal && n == 3) {
        const glm::vec3 b = glm::normalize(decodeNormal(v[0], v[1], v[2]));
        const glm::vec3 d = glm::normalize(decodeNormal(s[0], s[1], s[2]));
        const glm::vec3 r = blend == kBlendNormalCombine ? normalCombine(b, d) : d;
        glm::vec3 m = b * (1.0f - a) + r * a;
        const float len = glm::length(m);
        m = len > 1e-6f ? m / len : glm::vec3(0, 0, 1);
        v[0] = m.x * 0.5f + 0.5f, v[1] = m.y * 0.5f + 0.5f, v[2] = m.z * 0.5f + 0.5f;
        return;
    }
    const BlendMode mode = blend < kBlendModeCount ? BlendMode(blend) : BlendMode::Normal;
    if (n == 1) {
        const float o = std::clamp(blendColors(mode, glm::vec3(v[0]), glm::vec3(s[0])).x, 0.0f, 1.0f);
        v[0] += (o - v[0]) * a;
        return;
    }
    const glm::vec3 o = glm::clamp(blendColors(mode, glm::vec3(v[0], v[1], v[2]), glm::vec3(s[0], s[1], s[2])), 0.0f, 1.0f);
    for (int k = 0; k < 3; ++k) v[k] += (o[k] - v[k]) * a;
}

int tileOf(int x) { return x / kTileImageTile; }

} // namespace

// --- Formats and names ---

const ChannelFormat& channelFormat(Channel c) { return kFormats[std::clamp(int(c), 0, kChannelCount - 1)]; }

bool parseChannel(const std::string& text, Channel& out) {
    const std::string s = squash(text);
    static const struct {
        const char* name;
        Channel c;
    } aliases[] = {{"color", Channel::BaseColor},     {"colour", Channel::BaseColor},   {"basecolour", Channel::BaseColor},
                   {"albedo", Channel::BaseColor},    {"bc", Channel::BaseColor},       {"rough", Channel::Roughness},
                   {"metal", Channel::Metallic},      {"metalness", Channel::Metallic}, {"occlusion", Channel::AO},
                   {"ambientocclusion", Channel::AO}, {"emission", Channel::Emissive},  {"emit", Channel::Emissive},
                   {"alpha", Channel::Opacity},       {"bump", Channel::Height},        {"displacement", Channel::Height},
                   {"normals", Channel::Normal}};
    for (int i = 0; i < kChannelCount; ++i)
        if (s == squash(kFormats[i].id) || s == squash(kFormats[i].label)) {
            out = Channel(i);
            return true;
        }
    for (const auto& a : aliases)
        if (s == a.name) {
            out = a.c;
            return true;
        }
    return false;
}

const char* channelBlendId(uint8_t blend) {
    return blend == kBlendNormalCombine ? "normal_combine" : blendModeId(BlendMode(std::min<int>(blend, kBlendModeCount - 1)));
}
const char* channelBlendLabel(uint8_t blend) {
    return blend == kBlendNormalCombine ? "Normal Combine" : blendModeLabel(BlendMode(std::min<int>(blend, kBlendModeCount - 1)));
}
bool parseChannelBlend(const std::string& text, uint8_t& out) {
    const std::string s = squash(text);
    if (s == "normalcombine" || s == "rnm" || s == "combine") {
        out = kBlendNormalCombine;
        return true;
    }
    BlendMode m;
    if (!parseBlendMode(text, m)) return false;
    out = uint8_t(m);
    return true;
}

std::array<float, 4> BrushMaterial::encoded(Channel c) const {
    switch (c) {
    case Channel::BaseColor:
        return {linearToSrgbValue(color.x), linearToSrgbValue(color.y), linearToSrgbValue(color.z), 1.0f};
    case Channel::Roughness: return {std::clamp(roughness, 0.0f, 1.0f), 0, 0, 0};
    case Channel::Metallic: return {std::clamp(metallic, 0.0f, 1.0f), 0, 0, 0};
    case Channel::Normal: {
        const float len = glm::length(normal);
        const glm::vec3 n = len > 1e-6f ? normal / len : glm::vec3(0, 0, 1);
        return {n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f, 0};
    }
    case Channel::Height: return {std::clamp(height, 0.0f, 1.0f), 0, 0, 0};
    case Channel::AO: return {std::clamp(ao, 0.0f, 1.0f), 0, 0, 0};
    case Channel::Emissive:
        return {linearToSrgbValue(emissive.x), linearToSrgbValue(emissive.y), linearToSrgbValue(emissive.z), 0};
    case Channel::Opacity: return {std::clamp(opacity, 0.0f, 1.0f), 0, 0, 0};
    }
    return {0, 0, 0, 0};
}

// --- Texture-level state ---

bool channelEnabled(const PaintTexture& t, Channel c) {
    if (c == Channel::BaseColor) return true;
    const ChannelPlane& p = t.channels[size_t(c)];
    return p.enabled && p.base.valid() && p.base.width() == t.width && p.base.height() == t.height;
}

bool hasMaterialChannels(const PaintTexture& t) {
    for (int c = 1; c < kChannelCount; ++c)
        if (channelEnabled(t, Channel(c))) return true;
    return false;
}

uint32_t enabledChannelMask(const PaintTexture& t) {
    uint32_t m = 1;
    for (int c = 1; c < kChannelCount; ++c)
        if (channelEnabled(t, Channel(c))) m |= 1u << c;
    return m;
}

const TileImage& channelImage(const PaintTexture& t, Channel c) {
    const ChannelPlane& p = t.channels[size_t(c)];
    if (!t.layers.empty() && p.composite.valid() && p.composite.width() == t.width && p.composite.height() == t.height)
        return p.composite;
    return p.base;
}

void syncLayerChannels(PaintTexture& t) {
    for (TextureLayer& l : t.layers)
        for (int c = 1; c < kChannelCount; ++c) {
            LayerChannel& lc = l.channels[size_t(c)];
            if (!channelEnabled(t, Channel(c))) {
                lc.image.clear();
                continue;
            }
            const ChannelFormat& f = channelFormat(c);
            if (!lc.image.valid() || lc.image.width() != t.width || lc.image.height() != t.height ||
                lc.image.components() != f.components + 1 || lc.image.bits() != f.bits)
                resetLayerImage(lc.image, t, Channel(c));
        }
    for (int c = 1; c < kChannelCount; ++c) {
        ChannelPlane& p = t.channels[size_t(c)];
        if (t.layers.empty() || !channelEnabled(t, Channel(c))) p.composite.clear();
    }
}

bool setChannelEnabled(PaintTexture& t, Channel c, bool on) {
    if (c == Channel::BaseColor) return false;
    ChannelPlane& p = t.channels[size_t(c)];
    if (on == channelEnabled(t, c)) return false;
    if (on) {
        p.enabled = true;
        resetBaseImage(p.base, t, c);
    } else {
        p = ChannelPlane();
    }
    syncLayerChannels(t);
    compositeChannel(t, c, 0, 0, t.width, t.height);
    t.markAllDirty();
    return true;
}

uint8_t layerChannelBlend(const PaintTexture& t, int layer, Channel c) {
    if (layer < 0 || layer >= int(t.layers.size())) return 0;
    const TextureLayer& l = t.layers[size_t(layer)];
    return c == Channel::BaseColor ? uint8_t(l.blend) : l.channels[size_t(c)].blend;
}

float layerChannelOpacity(const PaintTexture& t, int layer, Channel c) {
    if (layer < 0 || layer >= int(t.layers.size())) return 1.0f;
    const TextureLayer& l = t.layers[size_t(layer)];
    return c == Channel::BaseColor ? l.opacity : l.channels[size_t(c)].opacity;
}

// --- Compositing ---

void compositeChannel(PaintTexture& t, Channel c, int x0, int y0, int x1, int y1) {
    if (c == Channel::BaseColor || !channelEnabled(t, c)) return;
    ChannelPlane& p = t.channels[size_t(c)];
    if (t.layers.empty()) {
        p.composite.clear();
        return;
    }
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, t.width), y1 = std::min(y1, t.height);
    if (x0 >= x1 || y0 >= y1) return;
    const ChannelFormat& f = channelFormat(c);
    if (!p.composite.valid() || p.composite.width() != t.width || p.composite.height() != t.height) {
        p.composite = p.base; // shares every tile; the covered ones are redone below
        x0 = y0 = 0, x1 = t.width, y1 = t.height;
    }
    // The layers that can show in this channel.
    struct Src {
        const TileImage* img;
        float opacity;
        uint8_t blend;
    };
    std::vector<Src> srcs;
    for (const TextureLayer& l : t.layers) {
        const LayerChannel& lc = l.channels[size_t(c)];
        const float o = l.opacity * lc.opacity;
        if (!l.visible || !lc.enabled || o <= 0.0f || !lc.image.valid() || lc.image.width() != t.width) continue;
        srcs.push_back({&lc.image, o, lc.blend});
    }
    const int tx0 = tileOf(x0), ty0 = tileOf(y0), tx1 = tileOf(x1 - 1), ty1 = tileOf(y1 - 1);
    const int tw = tx1 - tx0 + 1, tilesX = p.base.tilesX();
    const int n = f.components, bits = f.bits, tb = p.base.texelBytes(), lb = tb + (bits == 16 ? 2 : 1);
    const bool normal = c == Channel::Normal;
    poolFor(size_t(tw) * size_t(ty1 - ty0 + 1), [&](size_t b, size_t e) {
        std::vector<const Src*> live;
        for (size_t k = b; k < e; ++k) {
            const int tx = tx0 + int(k % size_t(tw)), ty = ty0 + int(k / size_t(tw));
            const size_t ti = size_t(ty) * size_t(tilesX) + size_t(tx);
            live.clear();
            for (const Src& s : srcs)
                if (s.img->tile(ti)) live.push_back(&s); // null layer tiles are transparent
            if (live.empty()) {
                p.composite.setTilePtr(ti, p.base.tilePtr(ti));
                continue;
            }
            uint8_t* out = p.composite.writableTile(ti);
            const int ax = std::max(x0, tx * kTileImageTile), bx = std::min(x1, (tx + 1) * kTileImageTile);
            const int ay = std::max(y0, ty * kTileImageTile), by = std::min(y1, (ty + 1) * kTileImageTile);
            uint8_t scratch[8];
            for (int y = ay; y < by; ++y)
                for (int x = ax; x < bx; ++x) {
                    float v[4], s[5];
                    readUnits(p.base.texel(x, y, scratch), n, bits, v);
                    for (const Src* src : live) {
                        readUnits(src->img->texel(x, y, scratch), n + 1, bits, s);
                        const float a = std::min(s[n] * src->opacity, 1.0f);
                        if (a <= 0.0f) continue;
                        blendOver(v, s, n, src->blend, a, normal);
                    }
                    writeUnits(out + (size_t(y % kTileImageTile) * kTileImageTile + size_t(x % kTileImageTile)) * size_t(tb), n, bits, v);
                }
            (void)lb;
        }
    }, 1, 2);
}

void compositeChannels(PaintTexture& t, int x0, int y0, int x1, int y1) {
    for (int c = 1; c < kChannelCount; ++c) compositeChannel(t, Channel(c), x0, y0, x1, y1);
}

// --- Normals ---

glm::vec3 normalCombine(const glm::vec3& base, const glm::vec3& detail) {
    // Reoriented normal mapping (Barré-Brisebois and Hill): rotate `detail` from the flat frame onto `base`.
    const glm::vec3 t = base + glm::vec3(0, 0, 1);
    const glm::vec3 u = detail * glm::vec3(-1, -1, 1);
    if (t.z < 1e-4f) return detail; // base points straight down: nothing sensible to rotate by
    const glm::vec3 r = t * glm::dot(t, u) / t.z - u;
    const float len = glm::length(r);
    return len > 1e-6f ? r / len : glm::vec3(0, 0, 1);
}

glm::vec3 heightNormalAt(const TileImage& h, int x, int y, float strength) {
    if (!h.valid()) return {0, 0, 1};
    const float dx = (h.value(x + 1, y) - h.value(x - 1, y)) * 0.5f;
    const float dy = (h.value(x, y + 1) - h.value(x, y - 1)) * 0.5f;
    // Scaled by the texture size, so a stroke looks the same at every resolution.
    const float k = strength * float(h.width()) / 64.0f;
    return glm::normalize(glm::vec3(-dx * k, -dy * k, 1.0f));
}

glm::vec3 effectiveNormalAt(const PaintTexture& t, int x, int y) {
    glm::vec3 n(0, 0, 1);
    if (channelEnabled(t, Channel::Normal)) {
        float v[4];
        channelImage(t, Channel::Normal).values(x, y, v);
        n = decodeNormal(v[0], v[1], v[2]);
        const float len = glm::length(n);
        n = len > 1e-6f ? n / len : glm::vec3(0, 0, 1);
    }
    if (channelEnabled(t, Channel::Height))
        n = normalCombine(n, heightNormalAt(channelImage(t, Channel::Height), x, y, t.heightNormalStrength));
    return n;
}

// --- Sampling ---

void sampleMaterial(const PaintTexture& t, const glm::vec2& uv, BrushMaterial& m) {
    m.color = sampleTexture(t, uv);
    m.channels = enabledChannelMask(t);
    for (int c = 1; c < kChannelCount; ++c) {
        if (!channelEnabled(t, Channel(c))) continue;
        float v[4] = {0, 0, 0, 0};
        channelImage(t, Channel(c)).sample(uv.x, uv.y, v);
        switch (Channel(c)) {
        case Channel::Roughness: m.roughness = v[0]; break;
        case Channel::Metallic: m.metallic = v[0]; break;
        case Channel::Normal: m.normal = glm::normalize(decodeNormal(v[0], v[1], v[2]) + glm::vec3(0, 0, 1e-6f)); break;
        case Channel::Height: m.height = v[0]; break;
        case Channel::AO: m.ao = v[0]; break;
        case Channel::Emissive: m.emissive = glm::vec3(srgbToLinear(v[0]), srgbToLinear(v[1]), srgbToLinear(v[2])); break;
        case Channel::Opacity: m.opacity = v[0]; break;
        default: break;
        }
    }
}

bool sampleMaterialAt(const Mesh& mesh, uint32_t tri, const glm::vec3& point, BrushMaterial& m) {
    if (!hasTexture(mesh) || tri >= mesh.triangleCount()) return false;
    const glm::uvec3& tv = mesh.triangles[tri];
    const glm::vec3 a = mesh.positions[tv.x], b = mesh.positions[tv.y], c = mesh.positions[tv.z];
    const glm::vec3 v0 = b - a, v1 = c - a, v2 = point - a;
    const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
    const float den = d00 * d11 - d01 * d01;
    float v = 0.0f, w = 0.0f;
    if (std::abs(den) > 1e-20f) {
        v = (d11 * d20 - d01 * d21) / den;
        w = (d00 * d21 - d01 * d20) / den;
    }
    const glm::vec2 uv = mesh.uvs[size_t(tri) * 3] * (1.0f - v - w) + mesh.uvs[size_t(tri) * 3 + 1] * v + mesh.uvs[size_t(tri) * 3 + 2] * w;
    sampleMaterial(*mesh.texture, uv, m);
    return true;
}

// --- Solo channel view ---

void channelViewTexels(const PaintTexture& t, Channel c, int x0, int y0, int x1, int y1, std::vector<uint8_t>& out) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, t.width), y1 = std::min(y1, t.height);
    if (x0 >= x1 || y0 >= y1 || out.size() != t.texelCount() * 4) return;
    const bool on = channelEnabled(t, c);
    const TileImage* img = on ? &channelImage(t, c) : nullptr;
    const ChannelFormat& f = channelFormat(c);
    poolFor(size_t(y1 - y0), [&](size_t b, size_t e) {
        for (int y = y0 + int(b); y < y0 + int(e); ++y)
            for (int x = x0; x < x1; ++x) {
                uint8_t* o = &out[(size_t(y) * size_t(t.width) + size_t(x)) * 4];
                o[3] = 255;
                if (c == Channel::Normal) {
                    const glm::vec3 n = effectiveNormalAt(t, x, y);
                    o[0] = uint8_t(std::lround((n.x * 0.5f + 0.5f) * 255.0f));
                    o[1] = uint8_t(std::lround((n.y * 0.5f + 0.5f) * 255.0f));
                    o[2] = uint8_t(std::lround(std::clamp(n.z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f));
                    continue;
                }
                float v[4] = {f.defaultValue[0], f.defaultValue[1], f.defaultValue[2], 0};
                if (img) img->values(x, y, v);
                if (f.components == 1) v[1] = v[2] = v[0];
                for (int k = 0; k < 3; ++k) o[k] = uint8_t(std::lround(std::clamp(v[k], 0.0f, 1.0f) * 255.0f));
            }
    }, 16, 4);
}

uint32_t pbrViewPlanes(const PaintTexture& t) {
    uint32_t planes = 0;
    for (Channel c : {Channel::AO, Channel::Roughness, Channel::Metallic, Channel::Opacity})
        if (channelEnabled(t, c)) planes |= 1u;
    if (channelEnabled(t, Channel::Normal) || channelEnabled(t, Channel::Height)) planes |= 2u;
    if (channelEnabled(t, Channel::Emissive)) planes |= 4u;
    return planes;
}

void pbrViewTexels(const PaintTexture& t, int x0, int y0, int x1, int y1, uint8_t* orm, uint8_t* normal, uint8_t* emissive) {
    const int rw = x1 - x0, rx = x0, ry = y0; // the caller's rectangle
    x0 = std::max(x0, 0), y0 = std::max(y0, 0);
    const int cx1 = std::min(x1, t.width), cy1 = std::min(y1, t.height);
    if (x0 >= cx1 || y0 >= cy1) return;
    const TileImage* img[4] = {};
    const Channel ormCh[4] = {Channel::AO, Channel::Roughness, Channel::Metallic, Channel::Opacity};
    float def[4];
    for (int k = 0; k < 4; ++k) {
        img[k] = channelEnabled(t, ormCh[k]) ? &channelImage(t, ormCh[k]) : nullptr;
        def[k] = channelFormat(ormCh[k]).defaultValue[0];
    }
    const TileImage* em = channelEnabled(t, Channel::Emissive) ? &channelImage(t, Channel::Emissive) : nullptr;
    const bool nrm = normal && (channelEnabled(t, Channel::Normal) || channelEnabled(t, Channel::Height));
    auto u8 = [](float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    poolFor(size_t(cy1 - y0), [&](size_t b, size_t e) {
        for (int y = y0 + int(b); y < y0 + int(e); ++y) {
            const size_t row = size_t(y - ry) * size_t(rw);
            for (int x = x0; x < cx1; ++x) {
                const size_t i = (row + size_t(x - rx)) * 4;
                if (orm) {
                    for (int k = 0; k < 4; ++k) orm[i + size_t(k)] = u8(img[k] ? img[k]->value(x, y) : def[k]);
                }
                if (normal) {
                    glm::vec3 n(0, 0, 1);
                    if (nrm) n = effectiveNormalAt(t, x, y);
                    normal[i] = u8(n.x * 0.5f + 0.5f);
                    normal[i + 1] = u8(n.y * 0.5f + 0.5f);
                    normal[i + 2] = u8(n.z * 0.5f + 0.5f);
                    normal[i + 3] = 255;
                }
                if (emissive) {
                    float v[4] = {0, 0, 0, 0};
                    if (em) em->values(x, y, v);
                    for (int k = 0; k < 3; ++k) emissive[i + size_t(k)] = u8(v[k]);
                    emissive[i + 3] = 255;
                }
            }
        }
    }, 16, 4);
}

void syncChannelView(PaintTexture& t, Channel c) {
    if (c == Channel::BaseColor || !channelEnabled(t, c)) {
        if (t.viewChannel != Channel::BaseColor) {
            t.viewChannel = Channel::BaseColor;
            std::vector<uint8_t>().swap(t.viewPixels);
            t.markAllDirty();
        }
        return;
    }
    const size_t n = t.texelCount() * 4;
    if (t.viewChannel != c || t.viewPixels.size() != n || t.viewRevision > t.revision) {
        t.viewPixels.assign(n, 255);
        t.viewChannel = c;
        channelViewTexels(t, c, 0, 0, t.width, t.height, t.viewPixels);
        t.markAllDirty();
        t.viewRevision = t.revision;
        return;
    }
    if (t.viewRevision == t.revision) return;
    // Redo the dirty tiles (one pixel wider for Normal: the height's normal reads the neighbours).
    const int tilesX = (t.width + kTextureTile - 1) / kTextureTile, tilesY = (t.height + kTextureTile - 1) / kTextureTile;
    const int pad = c == Channel::Normal ? 1 : 0;
    for (int ty = 0; ty < tilesY; ++ty)
        for (int tx = 0; tx < tilesX; ++tx)
            if (t.tileDirty(tx, ty))
                channelViewTexels(t, c, tx * kTextureTile - pad, ty * kTextureTile - pad, (tx + 1) * kTextureTile + pad,
                                  (ty + 1) * kTextureTile + pad, t.viewPixels);
    t.viewRevision = t.revision;
}

size_t channelBytes(const PaintTexture& t) {
    size_t n = 0;
    for (int c = 1; c < kChannelCount; ++c) {
        const ChannelPlane& p = t.channels[size_t(c)];
        n += p.base.bytes() + p.composite.bytes();
    }
    for (const TextureLayer& l : t.layers)
        for (int c = 1; c < kChannelCount; ++c) n += l.channels[size_t(c)].image.bytes();
    return n + t.viewPixels.size();
}

} // namespace sf
