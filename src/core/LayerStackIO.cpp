// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/LayerStackIO.h"

#include "core/Texture.h"
#include "core/TextureChannels.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#define STBI_NO_STDIO
#include "stb_image.h"

extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace sf {

namespace {

constexpr uint32_t kMagic = 0x49544653; // "SFTI"
constexpr uint32_t kTileFormat = 1;

void put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int b = 0; b < 4; ++b) o.push_back(uint8_t(v >> (8 * b)));
}
void put16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

} // namespace

int textureFormatVersion(const PaintTexture& tex) {
    const bool fill = std::any_of(tex.layers.begin(), tex.layers.end(), [](const TextureLayer& l) { return l.fill != nullptr; });
    return hasMaterialChannels(tex) || fill ? 7 : 6; // (Fill layers: "fill" and "mask" on their layer, core/Project.cpp)
}

std::vector<uint8_t> encodeTileImage(const TileImage& img) {
    std::vector<uint8_t> raw;
    put32(raw, kMagic);
    put32(raw, kTileFormat);
    put32(raw, uint32_t(img.width()));
    put32(raw, uint32_t(img.height()));
    raw.push_back(uint8_t(img.components()));
    raw.push_back(uint8_t(img.bits()));
    put16(raw, 0);
    for (uint16_t f : img.fill()) put16(raw, f);
    uint32_t listed = 0;
    for (size_t i = 0; i < img.tileCount(); ++i) listed += img.tile(i) != nullptr;
    put32(raw, listed);
    const size_t tb = size_t(img.texelBytes());
    for (size_t i = 0; i < img.tileCount(); ++i) {
        const TileImage::Tile* t = img.tile(i);
        if (!t) continue;
        put32(raw, uint32_t(i));
        raw.push_back(t->uniform ? 1 : 2);
        if (t->uniform) {
            for (uint16_t v : t->value) put16(raw, v);
        } else if (img.bits() == 16) {
            for (size_t k = 0; k + 1 < t->data.size(); k += 2) {
                uint16_t v;
                std::memcpy(&v, &t->data[k], 2);
                put16(raw, v);
            }
        } else {
            raw.insert(raw.end(), t->data.begin(), t->data.begin() + std::ptrdiff_t(kTileImageTexels * tb));
        }
    }
    int zlen = 0;
    unsigned char* z = stbi_zlib_compress(raw.data(), int(raw.size()), &zlen, 6);
    std::vector<uint8_t> out;
    put32(out, uint32_t(raw.size()));
    if (z) {
        out.insert(out.end(), z, z + zlen);
        std::free(z);
    }
    return out;
}

bool decodeTileImage(const std::vector<uint8_t>& bytes, TileImage& img, std::string& error) {
    const size_t tb = size_t(img.texelBytes());
    const size_t maxRaw = 40 + img.tileCount() * (5 + std::max<size_t>(8, size_t(kTileImageTexels) * tb));
    if (bytes.size() < 5) {
        error = "truncated channel image";
        return false;
    }
    const size_t rawSize = get32(bytes.data());
    if (rawSize < 32 || rawSize > maxRaw || bytes.size() - 4 > (size_t(1) << 31)) {
        error = "channel image has an impossible size";
        return false;
    }
    std::vector<uint8_t> raw(rawSize);
    const int got = stbi_zlib_decode_buffer(reinterpret_cast<char*>(raw.data()), int(rawSize),
                                            reinterpret_cast<const char*>(bytes.data() + 4), int(bytes.size() - 4));
    if (got != int(rawSize)) {
        error = "channel image does not decompress";
        return false;
    }
    const uint8_t* p = raw.data();
    if (get32(p) != kMagic || get32(p + 4) != kTileFormat || int(get32(p + 8)) != img.width() || int(get32(p + 12)) != img.height() ||
        p[16] != img.components() || p[17] != img.bits()) {
        error = "channel image has the wrong size or format";
        return false;
    }
    std::array<uint16_t, 4> fill;
    for (int k = 0; k < 4; ++k) fill[size_t(k)] = get16(p + 20 + k * 2);
    img.reset(img.width(), img.height(), img.components(), img.bits(), fill);
    const uint32_t listed = get32(p + 28);
    size_t at = 32;
    if (listed > img.tileCount()) {
        error = "channel image lists too many tiles";
        return false;
    }
    for (uint32_t k = 0; k < listed; ++k) {
        if (at + 5 > raw.size()) {
            error = "truncated channel image";
            return false;
        }
        const uint32_t index = get32(p + at);
        const uint8_t kind = p[at + 4];
        at += 5;
        if (index >= img.tileCount()) {
            error = "channel image tile out of range";
            return false;
        }
        if (kind == 1) {
            if (at + 8 > raw.size()) {
                error = "truncated channel image";
                return false;
            }
            std::array<uint16_t, 4> v;
            for (int c = 0; c < 4; ++c) v[size_t(c)] = get16(p + at + c * 2);
            if (img.bits() == 8)
                for (auto& x : v) x = std::min<uint16_t>(x, 255);
            img.fillTile(index, v);
            at += 8;
        } else if (kind == 2) {
            const size_t n = size_t(kTileImageTexels) * tb;
            if (at + n > raw.size()) {
                error = "truncated channel image";
                return false;
            }
            uint8_t* d = img.writableTile(index);
            if (img.bits() == 16) {
                for (size_t i = 0; i < n; i += 2) {
                    const uint16_t v = get16(p + at + i);
                    std::memcpy(d + i, &v, 2);
                }
            } else {
                std::memcpy(d, p + at, n);
            }
            at += n;
        } else {
            error = "channel image has an unknown tile kind";
            return false;
        }
    }
    return true;
}

Json writeTextureChannels(const PaintTexture& t, const std::string& prefix, const BlobWriter& write) {
    if (!hasMaterialChannels(t)) return Json();
    Json out = Json::object();
    out.set("height_normal_strength", double(t.heightNormalStrength));
    Json channels = Json::array();
    for (int c = 1; c < kChannelCount; ++c) {
        if (!channelEnabled(t, Channel(c))) continue;
        Json cj = Json::object();
        cj.set("channel", channelFormat(c).id);
        cj.set("base", write(prefix + "texture/pbr/" + channelFormat(c).id + ".sftiles", encodeTileImage(t.channels[size_t(c)].base)));
        channels.push(std::move(cj));
    }
    out.set("channels", std::move(channels));
    Json layers = Json::array();
    for (size_t li = 0; li < t.layers.size(); ++li) {
        const TextureLayer& l = t.layers[li];
        Json lj = Json::array();
        for (int c = 0; c < kChannelCount; ++c) {
            if (!channelEnabled(t, Channel(c))) continue;
            const LayerChannel& lc = l.channels[size_t(c)];
            Json cj = Json::object();
            cj.set("channel", channelFormat(c).id);
            cj.set("enabled", lc.enabled);
            if (c > 0) {
                cj.set("blend", channelBlendId(lc.blend));
                cj.set("opacity", double(lc.opacity));
                if (lc.image.valid() && !lc.image.empty())
                    cj.set("image", write(prefix + "texture/pbr/" + std::to_string(li) + "_" + channelFormat(c).id + ".sftiles",
                                          encodeTileImage(lc.image)));
            }
            lj.push(std::move(cj));
        }
        layers.push(std::move(lj));
    }
    out.set("layers", std::move(layers));
    return out;
}

bool readTextureChannels(const Json& pbr, PaintTexture& t, const BlobReader& read, std::string& error) {
    if (!pbr.isObject()) return true;
    const double strength = pbr["height_normal_strength"].asNumber(4.0);
    t.heightNormalStrength = std::isfinite(strength) ? std::clamp(float(strength), 0.0f, 100.0f) : 4.0f;
    auto loadImage = [&](const Json& desc, TileImage& img, const char* what) {
        const size_t maxBytes = 64 + img.tileCount() * (16 + size_t(kTileImageTexels) * size_t(img.texelBytes())) * 2;
        std::vector<uint8_t> bytes;
        if (!read(desc, what, maxBytes, bytes)) {
            if (error.empty()) error = std::string("Project ") + what + " cannot be read";
            return false;
        }
        std::string why;
        if (!decodeTileImage(bytes, img, why)) {
            error = std::string("Project ") + what + ": " + why;
            return false;
        }
        return true;
    };
    for (size_t i = 0; i < pbr["channels"].size(); ++i) {
        const Json& cj = pbr["channels"][i];
        Channel c;
        if (!parseChannel(cj["channel"].asString(), c) || c == Channel::BaseColor) continue; // unknown: skip (newer file)
        setChannelEnabled(t, c, true);
        if (cj["base"].isObject() && !loadImage(cj["base"], t.channels[size_t(c)].base, "material channel")) return false;
    }
    syncLayerChannels(t);
    const Json& layers = pbr["layers"];
    for (size_t li = 0; li < layers.size() && li < t.layers.size(); ++li) {
        TextureLayer& l = t.layers[li];
        for (size_t k = 0; k < layers[li].size(); ++k) {
            const Json& cj = layers[li][k];
            Channel c;
            if (!parseChannel(cj["channel"].asString(), c) || !channelEnabled(t, c)) continue;
            LayerChannel& lc = l.channels[size_t(c)];
            lc.enabled = cj["enabled"].asBool(true);
            if (c == Channel::BaseColor) continue;
            uint8_t blend = 0;
            if (parseChannelBlend(cj["blend"].asString(), blend) && (blend != kBlendNormalCombine || c == Channel::Normal)) lc.blend = blend;
            const double o = cj["opacity"].asNumber(1.0);
            lc.opacity = std::isfinite(o) ? std::clamp(float(o), 0.0f, 1.0f) : 1.0f;
            if (cj["image"].isObject() && !loadImage(cj["image"], lc.image, "material layer channel")) return false;
        }
    }
    compositeChannels(t, 0, 0, t.width, t.height);
    t.markAllDirty();
    return true;
}

} // namespace sf
