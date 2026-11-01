// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TileImage.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace sf {

namespace {

std::atomic<size_t> gLiveTileBytes{0}; // TileImage::liveBytes

// Writes `value` (component units) as one raw texel of `comps` components of `bits`.
void putTexel(uint8_t* out, const std::array<uint16_t, 4>& value, int comps, int bits) {
    if (bits == 16) {
        for (int c = 0; c < comps; ++c) std::memcpy(out + c * 2, &value[size_t(c)], 2);
    } else {
        for (int c = 0; c < comps; ++c) out[c] = uint8_t(value[size_t(c)]);
    }
}

std::array<uint16_t, 4> readTexel(const uint8_t* p, int comps, int bits) {
    std::array<uint16_t, 4> v{};
    if (bits == 16) {
        for (int c = 0; c < comps; ++c) std::memcpy(&v[size_t(c)], p + c * 2, 2);
    } else {
        for (int c = 0; c < comps; ++c) v[size_t(c)] = p[c];
    }
    return v;
}

} // namespace

TileImage::Tile::~Tile() { gLiveTileBytes.fetch_sub(counted, std::memory_order_relaxed); }

size_t TileImage::liveBytes() { return gLiveTileBytes.load(std::memory_order_relaxed); }

std::array<uint16_t, 4> TileImage::toUnits(int bits, float r, float g, float b, float a) {
    const float m = bits == 16 ? 65535.0f : 255.0f;
    auto u = [m](float x) { return uint16_t(std::lround(std::clamp(x, 0.0f, 1.0f) * m)); };
    return {u(r), u(g), u(b), u(a)};
}

void TileImage::reset(int width, int height, int components, int bits, std::array<uint16_t, 4> fill) {
    width_ = std::max(width, 0);
    height_ = std::max(height, 0);
    components_ = std::clamp(components, 1, 4);
    bits_ = bits == 16 ? 16 : 8;
    tilesX_ = (width_ + kTileImageTile - 1) / kTileImageTile;
    tilesY_ = (height_ + kTileImageTile - 1) / kTileImageTile;
    fill_ = fill;
    tiles_.assign(size_t(tilesX_) * size_t(tilesY_), nullptr);
}

uint8_t* TileImage::writableTile(size_t index) {
    TilePtr& t = tiles_[index];
    const size_t n = size_t(kTileImageTexels) * size_t(texelBytes());
    if (t && !t->uniform && t.use_count() == 1) return t->data.data();
    auto made = std::make_shared<Tile>();
    if (t && !t->uniform) {
        made->data = t->data; // shared dense tile: copy on write
    } else {
        made->data.resize(n);
        uint8_t px[8];
        putTexel(px, t ? t->value : fill_, components_, bits_);
        const int tb = texelBytes();
        for (size_t i = 0; i < size_t(kTileImageTexels); ++i) std::memcpy(&made->data[i * size_t(tb)], px, size_t(tb));
    }
    made->counted = sizeof(Tile) + made->data.size();
    gLiveTileBytes.fetch_add(made->counted, std::memory_order_relaxed);
    t = std::move(made);
    return t->data.data();
}

void TileImage::fillTile(size_t index, const std::array<uint16_t, 4>& value) {
    bool isFill = true;
    for (int c = 0; c < components_; ++c) isFill = isFill && value[size_t(c)] == fill_[size_t(c)];
    if (isFill) {
        tiles_[index] = nullptr;
        return;
    }
    auto t = std::make_shared<Tile>();
    t->uniform = true;
    t->value = value;
    t->counted = sizeof(Tile);
    gLiveTileBytes.fetch_add(t->counted, std::memory_order_relaxed);
    tiles_[index] = std::move(t);
}

void TileImage::compactTile(size_t index) {
    const TilePtr& t = tiles_[index];
    if (!t) return;
    if (t->uniform) {
        fillTile(index, t->value); // nulls it when it equals the fill
        return;
    }
    const int tb = texelBytes();
    const uint8_t* d = t->data.data();
    // Only the texels inside the image count (edge tiles).
    const int tx = int(index % size_t(tilesX_)) * kTileImageTile, ty = int(index / size_t(tilesX_)) * kTileImageTile;
    const int cw = std::min(kTileImageTile, width_ - tx), ch = std::min(kTileImageTile, height_ - ty);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
            if (std::memcmp(d + (size_t(y) * kTileImageTile + size_t(x)) * size_t(tb), d, size_t(tb)) != 0) return;
    fillTile(index, readTexel(d, components_, bits_));
}

void TileImage::compact() {
    for (size_t i = 0; i < tiles_.size(); ++i) compactTile(i);
}

const uint8_t* TileImage::texel(int x, int y, uint8_t* scratch) const {
    const Tile* t = tiles_[tileIndex(x, y)].get();
    if (t && !t->uniform)
        return &t->data[(size_t(y % kTileImageTile) * kTileImageTile + size_t(x % kTileImageTile)) * size_t(texelBytes())];
    putTexel(scratch, t ? t->value : fill_, components_, bits_);
    return scratch;
}

void TileImage::values(int x, int y, float out[4]) const {
    uint8_t s[8];
    const uint8_t* p = texel(std::clamp(x, 0, width_ - 1), std::clamp(y, 0, height_ - 1), s);
    if (bits_ == 16) {
        for (int c = 0; c < components_; ++c) {
            uint16_t v;
            std::memcpy(&v, p + c * 2, 2);
            out[c] = float(v) / 65535.0f;
        }
    } else {
        for (int c = 0; c < components_; ++c) out[c] = float(p[c]) / 255.0f;
    }
}

float TileImage::value(int x, int y, int c) const {
    float v[4] = {0, 0, 0, 0};
    values(x, y, v);
    return v[std::clamp(c, 0, 3)];
}

void TileImage::setValues(int x, int y, const float* v) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    uint8_t* d = writableTile(tileIndex(x, y));
    uint8_t* p = d + (size_t(y % kTileImageTile) * kTileImageTile + size_t(x % kTileImageTile)) * size_t(texelBytes());
    putTexel(p, toUnits(bits_, v[0], components_ > 1 ? v[1] : 0, components_ > 2 ? v[2] : 0, components_ > 3 ? v[3] : 0),
             components_, bits_);
}

void TileImage::sample(float u, float v, float out[4]) const {
    const float fx = std::clamp(u, 0.0f, 1.0f) * float(width_) - 0.5f, fy = std::clamp(v, 0.0f, 1.0f) * float(height_) - 0.5f;
    const float flx = std::floor(fx), fly = std::floor(fy);
    const int x0 = std::clamp(int(flx), 0, width_ - 1), y0 = std::clamp(int(fly), 0, height_ - 1);
    const int x1 = std::min(x0 + 1, width_ - 1), y1 = std::min(y0 + 1, height_ - 1);
    const float ax = std::clamp(fx - flx, 0.0f, 1.0f), ay = std::clamp(fy - fly, 0.0f, 1.0f);
    float a[4] = {}, b[4] = {}, c[4] = {}, d[4] = {};
    values(x0, y0, a);
    values(x1, y0, b);
    values(x0, y1, c);
    values(x1, y1, d);
    for (int k = 0; k < components_; ++k)
        out[k] = (a[k] * (1 - ax) + b[k] * ax) * (1 - ay) + (c[k] * (1 - ax) + d[k] * ax) * ay;
}

std::vector<uint8_t> TileImage::dense() const {
    const size_t tb = size_t(texelBytes());
    std::vector<uint8_t> out(size_t(width_) * size_t(height_) * tb);
    uint8_t px[8];
    for (int ty = 0; ty < tilesY_; ++ty)
        for (int tx = 0; tx < tilesX_; ++tx) {
            const Tile* t = tiles_[size_t(ty) * size_t(tilesX_) + size_t(tx)].get();
            const int x0 = tx * kTileImageTile, y0 = ty * kTileImageTile;
            const int cw = std::min(kTileImageTile, width_ - x0), ch = std::min(kTileImageTile, height_ - y0);
            if (t && !t->uniform) {
                for (int y = 0; y < ch; ++y)
                    std::memcpy(&out[(size_t(y0 + y) * size_t(width_) + size_t(x0)) * tb], &t->data[size_t(y) * kTileImageTile * tb],
                                size_t(cw) * tb);
                continue;
            }
            putTexel(px, t ? t->value : fill_, components_, bits_);
            for (int y = 0; y < ch; ++y)
                for (int x = 0; x < cw; ++x) std::memcpy(&out[(size_t(y0 + y) * size_t(width_) + size_t(x0 + x)) * tb], px, tb);
        }
    return out;
}

void TileImage::assignDense(const std::vector<uint8_t>& data) {
    const size_t tb = size_t(texelBytes());
    if (data.size() != size_t(width_) * size_t(height_) * tb) return;
    for (int ty = 0; ty < tilesY_; ++ty)
        for (int tx = 0; tx < tilesX_; ++tx) {
            const size_t i = size_t(ty) * size_t(tilesX_) + size_t(tx);
            tiles_[i] = nullptr;
            uint8_t* d = writableTile(i);
            const int x0 = tx * kTileImageTile, y0 = ty * kTileImageTile;
            const int cw = std::min(kTileImageTile, width_ - x0), ch = std::min(kTileImageTile, height_ - y0);
            for (int y = 0; y < ch; ++y)
                std::memcpy(&d[size_t(y) * kTileImageTile * tb], &data[(size_t(y0 + y) * size_t(width_) + size_t(x0)) * tb], size_t(cw) * tb);
            compactTile(i);
        }
}

bool TileImage::empty() const {
    for (const auto& t : tiles_)
        if (t) return false;
    return true;
}

size_t TileImage::denseTiles() const {
    size_t n = 0;
    for (const auto& t : tiles_) n += t && !t->uniform;
    return n;
}

size_t TileImage::bytes() const {
    size_t n = tiles_.size() * sizeof(TilePtr);
    for (const auto& t : tiles_)
        if (t) n += sizeof(Tile) + t->data.size();
    return n;
}

size_t TileImage::uniqueBytes() const {
    size_t n = tiles_.size() * sizeof(TilePtr);
    for (const auto& t : tiles_)
        if (t && t.use_count() == 1) n += sizeof(Tile) + t->data.size();
    return n;
}

bool TileImage::sameTexels(const TileImage& o) const {
    if (width_ != o.width_ || height_ != o.height_ || components_ != o.components_ || bits_ != o.bits_) return false;
    const size_t tb = size_t(texelBytes());
    uint8_t a[8], b[8];
    for (size_t i = 0; i < tiles_.size(); ++i) {
        if (tiles_[i] == o.tiles_[i]) continue;
        const int x0 = int(i % size_t(tilesX_)) * kTileImageTile, y0 = int(i / size_t(tilesX_)) * kTileImageTile;
        const int cw = std::min(kTileImageTile, width_ - x0), ch = std::min(kTileImageTile, height_ - y0);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                if (std::memcmp(texel(x0 + x, y0 + y, a), o.texel(x0 + x, y0 + y, b), tb) != 0) return false;
    }
    return true;
}

} // namespace sf
