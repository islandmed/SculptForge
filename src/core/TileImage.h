// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TileImage: a sparse, copy-on-write image made of 64x64 tiles (kTileImageTile), used for the material
// channels of a painted texture (core/TextureChannels.h).
//
// Every tile is one of three kinds:
//   - null:    no tile at all; every texel is the image's fill value (a fresh channel costs nothing),
//   - uniform: one texel value for the whole tile,
//   - dense:   64 x 64 texels (edge tiles are stored whole too; texels outside the image are ignored).
// A texel has 1-4 components of 8 or 16 bits (16-bit values are stored native-endian as uint16_t).
//
// Copies share their tiles (std::shared_ptr): copying an image costs one pointer per tile, and an edit makes
// the tile it writes unique first (writableTile). Undo keeps the old tile pointers of the tiles a stroke
// touched, so a stroke's undo record costs nothing beyond the tiles it changed. Rows are bottom-up, like
// PaintTexture's (row 0 is v = 0).
//
// Reads and writes of different tiles may run on different threads; making a tile writable must not race with
// another thread doing the same for the same tile (callers make tiles writable under a lock, then write texels).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sf {

constexpr int kTileImageTile = 64;
constexpr int kTileImageTexels = kTileImageTile * kTileImageTile;

class TileImage {
public:
    struct Tile {
        bool uniform = false;
        std::array<uint16_t, 4> value{}; // the uniform tile's texel (in component units: 0-255 or 0-65535)
        std::vector<uint8_t> data;       // a dense tile: kTileImageTexels * texelBytes(), rows of 64 texels
        size_t counted = 0;              // what this tile added to liveBytes() (taken back when it goes)
        Tile() = default;
        Tile(const Tile&) = delete;
        Tile& operator=(const Tile&) = delete;
        ~Tile();
    };
    using TilePtr = std::shared_ptr<Tile>;

    TileImage() = default;
    // An image of `width` x `height` texels with `components` (1-4) of `bits` (8 or 16), every texel `fill`
    // (component units). Drops any old tiles.
    void reset(int width, int height, int components, int bits, std::array<uint16_t, 4> fill);
    void clear() { *this = TileImage(); }

    bool valid() const { return width_ > 0 && height_ > 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    int components() const { return components_; }
    int bits() const { return bits_; }
    int texelBytes() const { return components_ * (bits_ == 16 ? 2 : 1); }
    int tilesX() const { return tilesX_; }
    int tilesY() const { return tilesY_; }
    size_t tileCount() const { return tiles_.size(); }
    float maxValue() const { return bits_ == 16 ? 65535.0f : 255.0f; }
    const std::array<uint16_t, 4>& fill() const { return fill_; }
    // Changes the fill (what null tiles read as). Null tiles take the new value.
    void setFill(std::array<uint16_t, 4> fill) { fill_ = fill; }

    // --- Tiles ---
    uint32_t tileIndex(int x, int y) const { return uint32_t(y / kTileImageTile) * uint32_t(tilesX_) + uint32_t(x / kTileImageTile); }
    const Tile* tile(size_t index) const { return index < tiles_.size() ? tiles_[index].get() : nullptr; }
    const TilePtr& tilePtr(size_t index) const { return tiles_[index]; }
    void setTilePtr(size_t index, TilePtr tile) { tiles_[index] = std::move(tile); }
    // The tile's texels for writing: a dense tile that no copy shares (made from the null / uniform / shared tile
    // first). Not thread-safe for the same tile.
    uint8_t* writableTile(size_t index);
    // Sets every texel of the tile to `value` (a uniform tile; the fill becomes a null tile).
    void fillTile(size_t index, const std::array<uint16_t, 4>& value);
    // Turns dense tiles whose texels are all equal into uniform ones, and uniform tiles equal to the fill into
    // null ones (after big edits; keeps memory proportional to what was painted).
    void compactTile(size_t index);
    void compact();

    // --- Texels (component units) ---
    // Pointer to texel (x, y)'s components: inside a dense tile, a uniform tile's value or the fill, as raw
    // bytes of texelBytes() (uniform and fill values are converted into `scratch`, which must hold 8 bytes).
    const uint8_t* texel(int x, int y, uint8_t* scratch) const;
    // Component `c` of texel (x, y) in 0..1.
    float value(int x, int y, int c = 0) const;
    // All components of texel (x, y) in 0..1 (out[components()..3] are left alone).
    void values(int x, int y, float out[4]) const;
    // Sets texel (x, y) from values in 0..1 (makes its tile writable).
    void setValues(int x, int y, const float* v);
    // Bilinear sample at uv (0..1, texel centres at (x + 0.5) / w), components in 0..1.
    void sample(float u, float v, float out[4]) const;

    // --- Whole image ---
    // Every texel as one array, rows bottom-up, texelBytes() per texel.
    std::vector<uint8_t> dense() const;
    // Replaces the texels with `data` (width * height * texelBytes(), rows bottom-up), compacted.
    void assignDense(const std::vector<uint8_t>& data);
    // True when every tile is null (the whole image is the fill value).
    bool empty() const;
    size_t denseTiles() const;
    // Bytes of tile memory (dense tiles' texels, uniform tiles' values) plus the tile table.
    size_t bytes() const;
    // Of those, the bytes of tiles no other image shares (what this copy alone keeps alive; undo accounting).
    size_t uniqueBytes() const;
    // Same size, format and texels (tiles compared by value).
    bool sameTexels(const TileImage& other) const;

    // Bytes of every tile alive in the process (all images, their copies and undo steps), kept in an atomic
    // counter as tiles are made and freed: safe to read from any thread at any time (memory readouts), unlike
    // bytes(), which walks an image's tiles and must run where the image is edited.
    static size_t liveBytes();

    // A fill value from components in 0..1.
    static std::array<uint16_t, 4> toUnits(int bits, float r, float g = 0, float b = 0, float a = 0);

private:
    int width_ = 0, height_ = 0, components_ = 1, bits_ = 8, tilesX_ = 0, tilesY_ = 0;
    std::array<uint16_t, 4> fill_{};
    std::vector<TilePtr> tiles_;
};

} // namespace sf
