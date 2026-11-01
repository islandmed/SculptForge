// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MicroStore.h"

#include "core/ThreadPool.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#define SF_MICRO_MMAP 1
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace sf {

MicroStore::MicroStore(const MicroStore& o) { *this = o; }

MicroStore::MicroStore(MicroStore&& o) noexcept { *this = std::move(o); }

MicroStore& MicroStore::operator=(const MicroStore& o) {
    if (this == &o) return *this;
    release();
    if (o.mapped()) {
        std::string error;
        if (assignMapped(o.size(), error)) {
            // Only the resident (touched) pages are worth copying; holes read as zeros anyway.
            poolFor(o.size(), [&](size_t b, size_t e) {
                for (size_t i = b; i < e; ++i)
                    if (o.map_[i]) map_[i] = o.map_[i];
            }, 1 << 20);
            return *this;
        }
    }
    heap_.assign(o.begin(), o.end());
    return *this;
}

MicroStore& MicroStore::operator=(MicroStore&& o) noexcept {
    if (this == &o) return *this;
    release();
    heap_ = std::move(o.heap_);
    map_ = std::exchange(o.map_, nullptr);
    mapCount_ = std::exchange(o.mapCount_, 0);
    return *this;
}

MicroStore& MicroStore::operator=(std::vector<int16_t>&& v) {
    release();
    heap_ = std::move(v);
    return *this;
}

MicroStore::~MicroStore() { release(); }

void MicroStore::release() {
#if SF_MICRO_MMAP
    if (map_) munmap(map_, mapCount_ * sizeof(int16_t));
#endif
    map_ = nullptr;
    mapCount_ = 0;
    std::vector<int16_t>().swap(heap_);
}

void MicroStore::assign(size_t n, int16_t v) {
    release();
    heap_.assign(n, v);
}

std::string MicroStore::swapFolder() {
    if (const char* d = std::getenv("SF_MICRO_SWAP_DIR"); d && *d) return d;
    if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) return std::string(x) + "/sculptforge";
    if (const char* h = std::getenv("HOME"); h && *h) {
#if defined(__APPLE__)
        return std::string(h) + "/Library/Caches/SculptForge";
#else
        return std::string(h) + "/.cache/sculptforge";
#endif
    }
    std::error_code ec;
    return std::filesystem::temp_directory_path(ec).string();
}

bool MicroStore::assignMapped(size_t n, std::string& error) {
    release();
#if SF_MICRO_MMAP
    const std::string folder = swapFolder();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::string path = folder + "/micro-XXXXXX";
    std::vector<char> name(path.begin(), path.end());
    name.push_back('\0');
    const int fd = mkstemp(name.data());
    if (fd < 0) {
        error = "Cannot create an out-of-core file in " + folder;
        heap_.assign(n, 0);
        return false;
    }
    unlink(name.data()); // gone with the process, whatever happens
    const size_t bytes = std::max<size_t>(n * sizeof(int16_t), 1);
    if (ftruncate(fd, off_t(bytes)) != 0) { // sparse: no disk until written
        close(fd);
        error = "Not enough room for the out-of-core file in " + folder;
        heap_.assign(n, 0);
        return false;
    }
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd); // (the mapping keeps the file)
    if (p == MAP_FAILED) {
        error = "Cannot map the out-of-core file";
        heap_.assign(n, 0);
        return false;
    }
    madvise(p, bytes, MADV_RANDOM); // brushes touch scattered tiles: no read-ahead
    map_ = static_cast<int16_t*>(p);
    mapCount_ = n;
    return true;
#else
    error = "Out-of-core micro levels need memory mapping (not available here)";
    heap_.assign(n, 0);
    return false;
#endif
}

size_t MicroStore::residentBytes() const {
    if (!map_) return heap_.capacity() * sizeof(int16_t);
#if SF_MICRO_MMAP
    const size_t page = size_t(sysconf(_SC_PAGESIZE)), bytes = mapCount_ * sizeof(int16_t);
    const size_t pages = (bytes + page - 1) / page;
    std::vector<unsigned char> vec(pages);
#if defined(__APPLE__)
    if (mincore(map_, bytes, reinterpret_cast<char*>(vec.data())) != 0) return bytes;
#else
    if (mincore(map_, bytes, vec.data()) != 0) return bytes;
#endif
    size_t resident = 0;
    for (unsigned char c : vec) resident += c & 1;
    return resident * page;
#else
    return 0;
#endif
}

size_t MicroStore::trim(size_t maxBytes) {
    const size_t resident = residentBytes();
#if SF_MICRO_MMAP
    if (map_ && maxBytes && resident > maxBytes) {
        const size_t bytes = mapCount_ * sizeof(int16_t);
        msync(map_, bytes, MS_SYNC);           // the file keeps every value
        madvise(map_, bytes, MADV_DONTNEED);   // and the pages leave memory
        return residentBytes();
    }
#endif
    return resident;
}

bool MicroStore::rangeResident(size_t first, size_t count) const {
    if (!map_ || count == 0) return true;
#if SF_MICRO_MMAP
    const uintptr_t page = uintptr_t(sysconf(_SC_PAGESIZE));
    const uintptr_t a = reinterpret_cast<uintptr_t>(map_ + first) & ~(page - 1);
    const uintptr_t b = reinterpret_cast<uintptr_t>(map_ + first + count);
    std::vector<unsigned char> vec((b - a + page - 1) / page);
#if defined(__APPLE__)
    if (mincore(reinterpret_cast<void*>(a), b - a, reinterpret_cast<char*>(vec.data())) != 0) return false;
#else
    if (mincore(reinterpret_cast<void*>(a), b - a, vec.data()) != 0) return false;
#endif
    for (unsigned char c : vec)
        if (!(c & 1)) return false;
#endif
    return true;
}

size_t MicroStore::capacity() const { return map_ ? residentBytes() / sizeof(int16_t) : heap_.capacity(); }

bool MicroStore::operator==(const std::vector<int16_t>& v) const {
    return size() == v.size() && std::equal(begin(), end(), v.begin());
}

bool MicroStore::operator==(const MicroStore& o) const { return size() == o.size() && std::equal(begin(), end(), o.begin()); }

// ---------------------------------------------------------------------------
// Tile codec
// ---------------------------------------------------------------------------

namespace {

size_t tileBytes(uint32_t bits, uint32_t samples) { return 3 + (size_t(bits) * samples + 7) / 8; }

} // namespace

void microPackTiles(const int16_t* values, size_t tiles, uint32_t samples, std::vector<uint8_t>& out) {
    // Sizes first (in parallel), then each tile into its place.
    std::vector<uint8_t> bits(tiles);
    std::vector<int16_t> lows(tiles);
    poolFor(tiles, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            const int16_t* v = values + t * samples;
            int lo = v[0], hi = v[0];
            for (uint32_t k = 1; k < samples; ++k) lo = std::min(lo, int(v[k])), hi = std::max(hi, int(v[k]));
            uint32_t span = uint32_t(hi - lo), w = 0;
            while (span) ++w, span >>= 1;
            bits[t] = uint8_t(w);
            lows[t] = int16_t(lo);
        }
    }, 4096);
    std::vector<size_t> offset(tiles + 1, 0);
    for (size_t t = 0; t < tiles; ++t) offset[t + 1] = offset[t] + tileBytes(bits[t], samples);
    out.assign(offset[tiles], 0);
    poolFor(tiles, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            uint8_t* o = out.data() + offset[t];
            const uint16_t lo = uint16_t(lows[t]);
            o[0] = uint8_t(lo), o[1] = uint8_t(lo >> 8), o[2] = bits[t];
            const uint32_t w = bits[t];
            if (!w) continue;
            uint64_t acc = 0;
            uint32_t have = 0;
            size_t pos = 3;
            const int16_t* v = values + t * samples;
            for (uint32_t k = 0; k < samples; ++k) {
                acc |= uint64_t(uint32_t(int(v[k]) - int(lows[t]))) << have;
                have += w;
                while (have >= 8) o[pos++] = uint8_t(acc), acc >>= 8, have -= 8;
            }
            if (have) o[pos++] = uint8_t(acc);
        }
    }, 4096);
}

bool microUnpackTiles(const uint8_t* data, size_t size, size_t tiles, uint32_t samples, int16_t* out) {
    std::vector<size_t> offset(tiles + 1, 0);
    for (size_t t = 0; t < tiles; ++t) {
        if (offset[t] + 3 > size) return false;
        const uint32_t w = data[offset[t] + 2];
        if (w > 16) return false;
        offset[t + 1] = offset[t] + tileBytes(w, samples);
    }
    if (offset[tiles] != size) return false;
    poolFor(tiles, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            const uint8_t* in = data + offset[t];
            const int lo = int16_t(uint16_t(in[0]) | uint16_t(in[1]) << 8);
            const uint32_t w = in[2];
            int16_t* v = out + t * samples;
            if (!w) {
                // (`out` starts zeroed; writing zeros into an out-of-core file's holes would make them real)
                if (lo != 0) std::fill(v, v + samples, int16_t(lo));
                continue;
            }
            uint64_t acc = 0;
            uint32_t have = 0;
            size_t pos = 3;
            const uint64_t mask = (uint64_t(1) << w) - 1;
            for (uint32_t k = 0; k < samples; ++k) {
                while (have < w) acc |= uint64_t(in[pos++]) << have, have += 8;
                v[k] = int16_t(lo + int(acc & mask));
                acc >>= w;
                have -= w;
            }
        }
    }, 4096);
    return true;
}

} // namespace sf
