// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Storage for a micro level's displacements (core/Micro.h): in memory, or out of core.
//
// Out of core, the array is a memory-mapped sparse file in the user's cache folder (not /tmp, which is often RAM):
// - untouched tiles are holes in the file: they take neither disk nor memory (a fresh micro level is all holes);
// - the operating system's page cache is the LRU: tiles a brush or the renderer touched stay resident while there
//   is memory to spare and are written back and dropped when there isn't, so a level larger than RAM works (1B
//   grid points = 2 GB of tiles, mostly cold);
// - the file is deleted as soon as it is open (it disappears with the process, crash or not).
// Saved projects use the tile codec below (delta + per-tile range), so untouched tiles cost 3 bytes on disk.
//
// The interface is the part of std::vector<int16_t> the micro code uses, so the brushes, renderer and undo don't
// care where the values live.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

class MicroStore {
public:
    MicroStore() = default;
    MicroStore(std::vector<int16_t>&& v) : heap_(std::move(v)) {}
    MicroStore(const MicroStore& o);
    MicroStore(MicroStore&& o) noexcept;
    MicroStore& operator=(const MicroStore& o);
    MicroStore& operator=(MicroStore&& o) noexcept;
    MicroStore& operator=(std::vector<int16_t>&& v);
    ~MicroStore();

    // In memory: n copies of v.
    void assign(size_t n, int16_t v);
    // Out of core: n zeros in a sparse mapped file (falls back to memory, returning false with `error`, where
    // mapping isn't available).
    bool assignMapped(size_t n, std::string& error);
    bool mapped() const { return map_ != nullptr; }

    int16_t* data() { return map_ ? map_ : heap_.data(); }
    const int16_t* data() const { return map_ ? map_ : heap_.data(); }
    size_t size() const { return map_ ? mapCount_ : heap_.size(); }
    bool empty() const { return size() == 0; }
    int16_t& operator[](size_t i) { return data()[i]; }
    int16_t operator[](size_t i) const { return data()[i]; }
    int16_t* begin() { return data(); }
    int16_t* end() { return data() + size(); }
    const int16_t* begin() const { return data(); }
    const int16_t* end() const { return data() + size(); }
    // Bytes of memory it takes: the array in memory; mapped, what is resident now (mincore).
    size_t capacity() const; // (in elements, like std::vector: residentBytes() / 2 when mapped)
    size_t residentBytes() const;
    // Out of core: when more than `maxBytes` are resident, writes the dirty tiles back and lets the system drop
    // them (they page in again when touched). TierConfig::residentCacheCapMB. Returns the bytes resident after.
    size_t trim(size_t maxBytes);
    // True when every page holding elements [first, first + count) is in memory (always, in memory).
    bool rangeResident(size_t first, size_t count) const;
    std::vector<int16_t> toVector() const { return std::vector<int16_t>(begin(), end()); }
    operator std::vector<int16_t>() const { return toVector(); }
    bool operator==(const std::vector<int16_t>& v) const;
    bool operator!=(const std::vector<int16_t>& v) const { return !(*this == v); }
    bool operator==(const MicroStore& o) const;

    // Where out-of-core files go: $SF_MICRO_SWAP_DIR, else the user cache folder (~/.cache/sculptforge).
    static std::string swapFolder();

private:
    void release();
    std::vector<int16_t> heap_;
    int16_t* map_ = nullptr;
    size_t mapCount_ = 0;
};

// `out` must hold zeros (untouched tiles aren't written).
// Tile codec (projects): each tile of `samples` values as its minimum (int16), a bit width b (0-16) and the
// values minus the minimum in b bits each. An untouched tile (all zero) is 3 bytes.
void microPackTiles(const int16_t* values, size_t tiles, uint32_t samples, std::vector<uint8_t>& out);
bool microUnpackTiles(const uint8_t* data, size_t size, size_t tiles, uint32_t samples, int16_t* out);

} // namespace sf
