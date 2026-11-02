// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Focus Detail (core/FocusDetail.h; design: docs/design/focus-detail.md).
#include "core/FocusDetail.h"

#include "core/Benchmark.h"
#include "core/Micro.h"
#include "core/MicroStore.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#if defined(SF_HAVE_ZSTD)
#include <zstd.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#define SF_FOCUS_POSIX 1
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sf {

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

constexpr char kMagic[8] = {'S', 'F', 'D', 'E', 'T', 'A', 'I', 'L'};
constexpr uint32_t kVersion = 1, kHeaderBytes = 128;
constexpr uint32_t kCodecZstd = 1, kCodecPack = 2;

uint32_t defaultCodec() {
#if defined(SF_HAVE_ZSTD)
    return kCodecZstd;
#else
    return kCodecPack;
#endif
}

uint64_t fnv1a(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

// Row-delta coding of a tile (as the undo journal): each point less the one before it in its row, a row's first
// less the point below it. Wrapping 16-bit arithmetic, so it is exact for every value.
void deltaEncode(const int16_t* v, uint32_t n, int16_t* out) {
    uint32_t p = 0;
    for (uint32_t j = 0; j <= n; ++j)
        for (uint32_t i = 0; i + j <= n; ++i, ++p) {
            const int prev = p == 0 ? 0 : i > 0 ? v[p - 1] : v[MicroMesh::index(0, j - 1, n)];
            out[p] = int16_t(uint16_t(uint16_t(v[p]) - uint16_t(prev)));
        }
}

void deltaDecode(int16_t* v, uint32_t n) { // in place
    uint32_t p = 0;
    for (uint32_t j = 0; j <= n; ++j)
        for (uint32_t i = 0; i + j <= n; ++i, ++p) {
            const int prev = p == 0 ? 0 : i > 0 ? v[p - 1] : v[MicroMesh::index(0, j - 1, n)];
            v[p] = int16_t(uint16_t(uint16_t(v[p]) + uint16_t(prev)));
        }
}

// `count` values (`tiles` x `samples`, for the pack codec's per-tile ranges) into `out` (replaced).
void compressValues(uint32_t codec, const int16_t* values, size_t tiles, uint32_t samples, std::vector<uint8_t>& out) {
    out.clear();
#if defined(SF_HAVE_ZSTD)
    if (codec == kCodecZstd) {
        thread_local ZSTD_CCtx* cctx = ZSTD_createCCtx();
        const size_t bytes = tiles * samples * sizeof(int16_t);
        out.resize(ZSTD_compressBound(bytes));
        const size_t r = ZSTD_compressCCtx(cctx, out.data(), out.size(), values, bytes, 3);
        out.resize(ZSTD_isError(r) ? 0 : r);
        return;
    }
#endif
    (void)codec;
    microPackTiles(values, tiles, samples, out);
}

bool decompressValues(uint32_t codec, const uint8_t* data, size_t size, size_t tiles, uint32_t samples, int16_t* out) {
#if defined(SF_HAVE_ZSTD)
    if (codec == kCodecZstd) {
        thread_local ZSTD_DCtx* dctx = ZSTD_createDCtx();
        const size_t bytes = tiles * samples * sizeof(int16_t);
        const size_t r = ZSTD_decompressDCtx(dctx, out, bytes, data, size);
        return !ZSTD_isError(r) && r == bytes;
    }
#endif
    if (codec != kCodecPack) return false;
    std::fill_n(out, tiles * samples, int16_t(0));
    return microUnpackTiles(data, size, tiles, samples, out);
}

void compressTile(uint32_t codec, const int16_t* v, uint32_t n, uint32_t samples, std::vector<uint8_t>& out) {
    thread_local std::vector<int16_t> d;
    d.resize(samples);
    deltaEncode(v, n, d.data());
    compressValues(codec, d.data(), 1, samples, out);
}

bool decompressTile(uint32_t codec, const uint8_t* data, size_t size, uint32_t n, uint32_t samples, int16_t* out) {
    if (!decompressValues(codec, data, size, 1, samples, out)) return false;
    deltaDecode(out, n);
    return true;
}

// The coarse grid (cn segments) as a subsample of the full grid (n segments).
void subsample(const int16_t* full, uint32_t n, uint32_t cn, int16_t* coarse) {
    const uint32_t s = n / cn;
    for (uint32_t J = 0; J <= cn; ++J)
        for (uint32_t I = 0; I + J <= cn; ++I) coarse[MicroMesh::index(I, J, cn)] = full[MicroMesh::index(I * s, J * s, n)];
}

// The full grid from the coarse one: exact at the coarse points, linear on each coarse micro triangle in between,
// in integers (a shared edge's points depend only on its two coarse ends, the same sum from either side).
void upsample(const int16_t* coarse, uint32_t cn, uint32_t n, int16_t* full) {
    const int s = int(n / cn);
    int sh = 0;
    while ((1 << sh) < s) ++sh;
    uint32_t p = 0;
    for (uint32_t j = 0; j <= n; ++j)
        for (uint32_t i = 0; i + j <= n; ++i, ++p) {
            const uint32_t I = i >> sh, J = j >> sh;
            const int fi = int(i) & (s - 1), fj = int(j) & (s - 1);
            int sum;
            if (fi + fj <= s) {
                sum = (s - fi - fj) * coarse[MicroMesh::index(I, J, cn)] + (fi ? fi * coarse[MicroMesh::index(I + 1, J, cn)] : 0) +
                      (fj ? fj * coarse[MicroMesh::index(I, J + 1, cn)] : 0);
            } else {
                sum = (fi + fj - s) * coarse[MicroMesh::index(I + 1, J + 1, cn)] + (s - fi) * coarse[MicroMesh::index(I, J + 1, cn)] +
                      (s - fj) * coarse[MicroMesh::index(I + 1, J, cn)];
            }
            // floor((sum + s / 2) / s), also for negative sums
            const int q = sum + s / 2;
            full[p] = int16_t(q >= 0 ? q >> sh : -((-q + s - 1) >> sh));
        }
}

struct TileInfo {
    uint16_t range = 0;
    float coarseErr = 0;
};

// Range and coarse error of a full tile (its coarse grid into `coarse`).
TileInfo analyse(const int16_t* full, uint32_t n, uint32_t samples, uint32_t cn, float step, int16_t* coarse) {
    thread_local std::vector<int16_t> up;
    up.resize(samples);
    subsample(full, n, cn, coarse);
    upsample(coarse, cn, n, up.data());
    int r = 0, e = 0;
    for (uint32_t k = 0; k < samples; ++k) {
        r = std::max(r, std::abs(int(full[k])));
        e = std::max(e, std::abs(int(full[k]) - int(up[k])));
    }
    return {uint16_t(std::min(r, 32767)), float(e) * step};
}

template <class T> void put(std::vector<uint8_t>& b, size_t at, T v) { std::memcpy(b.data() + at, &v, sizeof v); }
template <class T> T get(const uint8_t* b, size_t at) {
    T v;
    std::memcpy(&v, b + at, sizeof v);
    return v;
}

struct Header {
    uint32_t codec = 0, level = 0, coarseLevel = 0, tris = 0, verts = 0, chunkTiles = 0;
    float step = 0;
    uint64_t baseOffset = 0, baseBytes = 0, dirOffset = 0, dirBytes = 0, generation = 0, garbage = 0, dirHash = 0;
};

std::vector<uint8_t> encodeHeader(const Header& h) {
    std::vector<uint8_t> b(kHeaderBytes, 0);
    std::memcpy(b.data(), kMagic, 8);
    put(b, 8, kVersion);
    put(b, 12, h.codec);
    put(b, 16, h.level);
    put(b, 20, h.coarseLevel);
    put(b, 24, h.tris);
    put(b, 28, h.verts);
    put(b, 32, h.step);
    put(b, 36, h.chunkTiles);
    put(b, 40, h.baseOffset);
    put(b, 48, h.baseBytes);
    put(b, 56, h.dirOffset);
    put(b, 64, h.dirBytes);
    put(b, 72, h.generation);
    put(b, 80, h.garbage);
    put(b, 88, h.dirHash);
    return b;
}

bool decodeHeader(const uint8_t* b, size_t size, Header& h, std::string& error) {
    if (size < kHeaderBytes || std::memcmp(b, kMagic, 8) != 0) {
        error = "Not a SculptForge detail file";
        return false;
    }
    if (get<uint32_t>(b, 8) != kVersion) {
        error = "Detail file version not supported";
        return false;
    }
    h.codec = get<uint32_t>(b, 12);
    h.level = get<uint32_t>(b, 16);
    h.coarseLevel = get<uint32_t>(b, 20);
    h.tris = get<uint32_t>(b, 24);
    h.verts = get<uint32_t>(b, 28);
    h.step = get<float>(b, 32);
    h.chunkTiles = get<uint32_t>(b, 36);
    h.baseOffset = get<uint64_t>(b, 40);
    h.baseBytes = get<uint64_t>(b, 48);
    h.dirOffset = get<uint64_t>(b, 56);
    h.dirBytes = get<uint64_t>(b, 64);
    h.generation = get<uint64_t>(b, 72);
    h.garbage = get<uint64_t>(b, 80);
    h.dirHash = get<uint64_t>(b, 88);
    if (h.codec != kCodecZstd && h.codec != kCodecPack) {
        error = "Detail file codec unknown";
        return false;
    }
#if !defined(SF_HAVE_ZSTD)
    if (h.codec == kCodecZstd) {
        error = "This build can't read zstd detail files";
        return false;
    }
#endif
    if (h.level < 1 || h.level > uint32_t(MicroMesh::kMaxLevel) || h.coarseLevel >= h.level || h.chunkTiles == 0 ||
        !(h.step > 0.0f) || !std::isfinite(h.step) || h.tris == 0 || h.verts == 0) {
        error = "Detail file header is invalid";
        return false;
    }
    return true;
}

// The edges of tile t from `from` into the tile across each, for the neighbours `copyInto` picks.
template <class Pick>
void shareEdges(const std::vector<glm::uvec3>& triangles, MicroMesh& mm, uint32_t t, Pick&& copyInto) {
    const uint32_t n = mm.n, S = mm.samples;
    const glm::uvec3 tt = triangles[t];
    for (int ed = 0; ed < 3; ++ed) {
        const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(ed)];
        if (nb == MicroMesh::kNone) continue;
        const uint32_t u = nb >> 2;
        const int f = int(nb & 3);
        int dir = copyInto(u); // +1: t's edge into u, -1: u's edge into t, 0: nothing
        if (!dir) continue;
        int16_t* g = mm.disp.data() + size_t(t) * S;
        int16_t* h = mm.disp.data() + size_t(u) * S;
        const bool reversed = triangles[u][f] != tt[ed];
        for (uint32_t k = 1; k < n; ++k) {
            uint32_t i, j, i2, j2;
            MicroMesh::edgePoint(ed, k, n, i, j);
            MicroMesh::edgePoint(f, reversed ? n - k : k, n, i2, j2);
            int16_t& a = g[MicroMesh::index(i, j, n)];
            int16_t& c = h[MicroMesh::index(i2, j2, n)];
            if (dir > 0) c = a;
            else a = c;
        }
        mm.markDirty(dir > 0 ? u : t);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

bool FocusDetail::write(const std::string& path, const Mesh& baseIn, int level, float step, const TileFn& tile,
                        std::string& error) {
    if (level <= kCoarseLevel || level > MicroMesh::kMaxLevel) {
        error = "Focus Detail needs a micro level above " + std::to_string(kCoarseLevel);
        return false;
    }
    Mesh base = baseIn;
    if (base.normals.size() != base.vertexCount()) base.recomputeAllNormals();
    const uint32_t T = uint32_t(base.triangleCount()), V = uint32_t(base.vertexCount());
    const uint32_t n = 1u << level, S = (n + 1) * (n + 2) / 2, cn = 1u << kCoarseLevel, CS = (cn + 1) * (cn + 2) / 2;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = "Can't write " + path;
        return false;
    }
    Header h;
    h.codec = defaultCodec();
    h.level = uint32_t(level);
    h.coarseLevel = kCoarseLevel;
    h.tris = T;
    h.verts = V;
    h.chunkTiles = kChunk;
    h.step = step;
    h.baseOffset = kHeaderBytes;
    h.baseBytes = uint64_t(V) * 24 + uint64_t(T) * 12;
    bool ok = std::fwrite(encodeHeader(h).data(), 1, kHeaderBytes, f) == kHeaderBytes;
    ok = ok && std::fwrite(base.positions.data(), 12, V, f) == V;
    ok = ok && std::fwrite(base.normals.data(), 12, V, f) == V;
    ok = ok && std::fwrite(base.triangles.data(), 12, T, f) == T;
    uint64_t offset = kHeaderBytes + h.baseBytes;
    std::vector<Entry> dir(T);
    const uint32_t chunks = (T + kChunk - 1) / kChunk;
    std::vector<ChunkEntry> chunkDir(chunks);
    // Batches of whole chunks: tiles generated and compressed in parallel, written in order.
    const uint32_t batch = kChunk * 4;
    std::vector<std::vector<uint8_t>> blobs(batch), chunkBlobs(4);
    std::vector<int16_t> coarse(size_t(batch) * CS);
    for (uint32_t b0 = 0; ok && b0 < T; b0 += batch) {
        const uint32_t b1 = std::min(T, b0 + batch);
        poolFor(b1 - b0, [&](size_t b, size_t e) {
            std::vector<int16_t> v(S);
            MicroTriangle Tr;
            for (size_t k = b; k < e; ++k) {
                const uint32_t t = b0 + uint32_t(k);
                Tr.set(base, t, n);
                std::fill(v.begin(), v.end(), int16_t(0));
                tile(Tr, t, v.data());
                const TileInfo info = analyse(v.data(), n, S, cn, step, coarse.data() + k * CS);
                dir[t].range = info.range;
                dir[t].coarseErr = info.coarseErr;
                compressTile(h.codec, v.data(), n, S, blobs[k]);
            }
        }, 16);
        for (uint32_t t = b0; ok && t < b1; ++t) {
            std::vector<uint8_t>& blob = blobs[t - b0];
            if (blob.empty()) ok = false;
            dir[t].offset = offset;
            dir[t].bytes = uint32_t(blob.size());
            ok = ok && std::fwrite(blob.data(), 1, blob.size(), f) == blob.size();
            offset += blob.size();
        }
        const uint32_t c0 = b0 / kChunk, c1 = (b1 + kChunk - 1) / kChunk;
        poolFor(c1 - c0, [&](size_t b, size_t e) {
            for (size_t c = b; c < e; ++c) {
                const uint32_t first = uint32_t(c0 + c) * kChunk, count = std::min(kChunk, T - first);
                compressValues(h.codec, coarse.data() + size_t(first - b0) * CS, count, CS, chunkBlobs[c]);
            }
        }, 1, 1);
        for (uint32_t c = c0; ok && c < c1; ++c) {
            const std::vector<uint8_t>& blob = chunkBlobs[c - c0];
            chunkDir[c].offset = offset;
            chunkDir[c].bytes = uint32_t(blob.size());
            ok = std::fwrite(blob.data(), 1, blob.size(), f) == blob.size();
            offset += blob.size();
        }
    }
    std::vector<uint8_t> dirBytes(size_t(T) * sizeof(Entry) + size_t(chunks) * sizeof(ChunkEntry));
    std::memcpy(dirBytes.data(), dir.data(), size_t(T) * sizeof(Entry));
    std::memcpy(dirBytes.data() + size_t(T) * sizeof(Entry), chunkDir.data(), size_t(chunks) * sizeof(ChunkEntry));
    h.dirOffset = offset;
    h.dirBytes = dirBytes.size();
    h.dirHash = fnv1a(dirBytes.data(), dirBytes.size());
    ok = ok && std::fwrite(dirBytes.data(), 1, dirBytes.size(), f) == dirBytes.size();
    ok = ok && std::fseek(f, 0, SEEK_SET) == 0 && std::fwrite(encodeHeader(h).data(), 1, kHeaderBytes, f) == kHeaderBytes;
    ok = ok && std::fflush(f) == 0;
#if defined(SF_FOCUS_POSIX)
    ok = ok && ::fsync(fileno(f)) == 0;
#endif
    ok = std::fclose(f) == 0 && ok;
    if (!ok) error = "Writing " + path + " failed (disk full?)";
    return ok;
}

// ---------------------------------------------------------------------------
// Opening
// ---------------------------------------------------------------------------

FocusDetail::~FocusDetail() { unmap(); }

void FocusDetail::unmap() {
#if defined(SF_FOCUS_POSIX)
    if (map_) ::munmap(const_cast<uint8_t*>(map_), mapBytes_);
    map_ = nullptr;
    mapBytes_ = 0;
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
#endif
}

bool FocusDetail::mapFile(std::string& error) {
#if defined(SF_FOCUS_POSIX)
    if (map_) ::munmap(const_cast<uint8_t*>(map_), mapBytes_);
    map_ = nullptr;
    if (fd_ < 0) fd_ = ::open(path_.c_str(), O_RDWR);
    struct stat st {};
    if (fd_ < 0 || ::fstat(fd_, &st) != 0 || st.st_size < off_t(kHeaderBytes)) {
        error = "Can't open " + path_;
        return false;
    }
    mapBytes_ = size_t(st.st_size);
    void* p = ::mmap(nullptr, mapBytes_, PROT_READ, MAP_SHARED, fd_, 0);
    if (p == MAP_FAILED) {
        error = "Can't map " + path_;
        return false;
    }
    map_ = static_cast<const uint8_t*>(p);
    return true;
#else
    error = "Focus Detail needs Linux or macOS for now (memory-mapped files)";
    return false;
#endif
}

std::shared_ptr<FocusDetail> FocusDetail::open(const std::string& path, Mesh& out, std::string& error) {
    std::shared_ptr<FocusDetail> fd(new FocusDetail());
    fd->path_ = path;
    if (!fd->mapFile(error)) return nullptr;
    const uint8_t* b = fd->map_;
    Header h;
    if (!decodeHeader(b, fd->mapBytes_, h, error)) return nullptr;
    const uint32_t T = h.tris, V = h.verts, chunks = (T + h.chunkTiles - 1) / h.chunkTiles;
    if (h.chunkTiles != kChunk || h.coarseLevel != uint32_t(kCoarseLevel) || h.baseBytes != uint64_t(V) * 24 + uint64_t(T) * 12 ||
        h.baseOffset < kHeaderBytes || h.baseOffset + h.baseBytes > fd->mapBytes_ || h.dirOffset + h.dirBytes > fd->mapBytes_ ||
        h.dirBytes != uint64_t(T) * sizeof(Entry) + uint64_t(chunks) * sizeof(ChunkEntry) ||
        fnv1a(b + h.dirOffset, h.dirBytes) != h.dirHash) {
        error = "Detail file is damaged";
        return nullptr;
    }
    fd->level_ = int(h.level);
    fd->n_ = 1u << h.level;
    fd->samples_ = (fd->n_ + 1) * (fd->n_ + 2) / 2;
    fd->coarseN_ = 1u << h.coarseLevel;
    fd->coarseSamples_ = (fd->coarseN_ + 1) * (fd->coarseN_ + 2) / 2;
    fd->codec_ = h.codec;
    fd->step_ = h.step;
    fd->generation_ = h.generation;
    fd->garbage_ = h.garbage;
    fd->dir_.resize(T);
    fd->chunks_.resize(chunks);
    std::memcpy(fd->dir_.data(), b + h.dirOffset, size_t(T) * sizeof(Entry));
    std::memcpy(fd->chunks_.data(), b + h.dirOffset + size_t(T) * sizeof(Entry), size_t(chunks) * sizeof(ChunkEntry));
    for (const Entry& e : fd->dir_)
        if (e.offset < kHeaderBytes || e.offset + e.bytes > fd->mapBytes_) {
            error = "Detail file is damaged";
            return nullptr;
        }
    for (const ChunkEntry& c : fd->chunks_)
        if (c.offset < kHeaderBytes || c.offset + c.bytes > fd->mapBytes_) {
            error = "Detail file is damaged";
            return nullptr;
        }
    // The base mesh
    Mesh m;
    m.positions.resize(V);
    m.normals.resize(V);
    m.triangles.resize(T);
    const uint8_t* base = b + h.baseOffset;
    std::memcpy(m.positions.data(), base, size_t(V) * 12);
    std::memcpy(m.normals.data(), base + size_t(V) * 12, size_t(V) * 12);
    std::memcpy(m.triangles.data(), base + size_t(V) * 24, size_t(T) * 12);
    for (const glm::uvec3& t : m.triangles)
        if (t.x >= V || t.y >= V || t.z >= V) {
            error = "Detail file is damaged";
            return nullptr;
        }
    m.rebuildTopology();
    // Coarse grids, then every tile upsampled from its coarse grid.
    const uint32_t CS = fd->coarseSamples_, S = fd->samples_;
    fd->coarse_.assign(size_t(T) * CS, 0);
    std::atomic<bool> bad{false};
    poolFor(chunks, [&](size_t c0, size_t c1) {
        for (size_t c = c0; c < c1; ++c) {
            const uint32_t first = uint32_t(c) * kChunk, count = std::min(kChunk, T - first);
            if (!decompressValues(fd->codec_, b + fd->chunks_[c].offset, fd->chunks_[c].bytes, count, CS,
                                  fd->coarse_.data() + size_t(first) * CS))
                bad = true;
        }
    }, 1, 1);
    if (bad) {
        error = "Detail file is damaged";
        return nullptr;
    }
    MicroStore disp;
    if (microWantsOutOfCore(size_t(T) * S)) {
        std::string why;
        disp.assignMapped(size_t(T) * S, why);
    } else {
        disp = std::vector<int16_t>(size_t(T) * S);
    }
    poolFor(T, [&](size_t t0, size_t t1) {
        for (size_t t = t0; t < t1; ++t) upsample(fd->coarse_.data() + t * CS, fd->coarseN_, fd->n_, disp.data() + t * S);
    }, 64);
    if (!microAttach(m, fd->level_, fd->step_, std::move(disp), {}, error)) return nullptr;
    MicroMesh& mm = *m.micro;
    mm.rangeMax = 0;
    for (uint32_t t = 0; t < T; ++t) {
        mm.triRange[t] = fd->dir_[t].range; // the full tile's (brushes find triangles with it)
        mm.rangeMax = std::max(mm.rangeMax, mm.triRange[t]);
    }
    fd->state_.assign(T, kCoarse);
    fd->lastWanted_.assign(T, 0);
    fd->parked_.resize(T);
    fd->score_.assign(T, 0.0f);
    fd->tris_ = m.triangles;
    fd->baseHash_ = fnv1a(base, size_t(V) * 24);
    fd->stats_.tiles = T;
    mm.focus = fd;
    out = std::move(m);
    return fd;
}

// ---------------------------------------------------------------------------
// Promotion and demotion
// ---------------------------------------------------------------------------

bool FocusDetail::readTile(uint32_t t, int16_t* out) const {
    if (!parked_[t].empty()) return decompressTile(codec_, parked_[t].data(), parked_[t].size(), n_, samples_, out);
    const Entry& e = dir_[t];
    return map_ && decompressTile(codec_, map_ + e.offset, e.bytes, n_, samples_, out);
}

void FocusDetail::fillCoarse(MicroMesh& mm, uint32_t t) const {
    upsample(coarse_.data() + size_t(t) * coarseSamples_, coarseN_, n_, mm.disp.data() + size_t(t) * samples_);
}

void FocusDetail::promoteLocked(MicroMesh& mm, std::span<const uint32_t> tris, bool edit, bool sync) {
    const auto t0 = Clock::now();
    poolFor(tris.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const uint32_t t = tris[k];
            if (!readTile(t, mm.disp.data() + size_t(t) * samples_)) fillCoarse(mm, t); // (a damaged tile stays coarse)
        }
    }, 4);
    // A promoted tile's real edges go into its coarse neighbours (no cracks along the focus border).
    for (uint32_t t : tris) {
        state_[t] = edit ? kDirty : kFull;
        mm.markDirty(t);
        ++fullTiles_;
    }
    if (!tris.empty()) {
        for (uint32_t t : tris)
            shareEdges(tris_, mm, t, [&](uint32_t u) { return state_[u] == kCoarse ? 1 : 0; });
    }
    ++mm.revision;
    stats_.promoted += tris.size();
    if (sync) stats_.syncPromoted += tris.size();
    stats_.promoteMs += msSince(t0);
}

void FocusDetail::demoteLocked(MicroMesh& mm, uint32_t t) {
    int16_t* g = mm.disp.data() + size_t(t) * samples_;
    if (state_[t] == kDirty) { // keep the edit: compressed in memory until saved, coarse grid refreshed
        const TileInfo info = analyse(g, n_, samples_, coarseN_, step_, coarse_.data() + size_t(t) * coarseSamples_);
        dir_[t].coarseErr = info.coarseErr;
        dir_[t].range = std::max(dir_[t].range, info.range);
        compressTile(codec_, g, n_, samples_, parked_[t]);
    }
    fillCoarse(mm, t);
    state_[t] = kCoarse;
    --fullTiles_;
    mm.markDirty(t);
    // full neighbours keep their real edges: take theirs; coarse ones take this tile's upsampled edges
    shareEdges(tris_, mm, t, [&](uint32_t u) { return state_[u] == kCoarse ? 1 : -1; });
    ++stats_.demoted;
}

void FocusDetail::ensureFull(MicroMesh& mm, std::span<const uint32_t> tris, bool edit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint32_t> need;
    for (uint32_t t : tris) {
        if (t >= state_.size()) continue;
        lastWanted_[t] = frame_;
        if (state_[t] == kCoarse) need.push_back(t);
        else if (edit) state_[t] = kDirty;
    }
    if (!need.empty()) promoteLocked(mm, need, edit, true);
}

void FocusDetail::update(Mesh& m, const FocusView& view, const FocusBrush& brush, double budgetMs) {
    if (!m.micro || m.micro->focus.get() != this) return;
    const auto t0 = Clock::now();
    MicroMesh& mm = *m.micro;
    const uint32_t T = uint32_t(state_.size());
    std::unique_lock<std::mutex> lock(mutex_);
    ++frame_;
    // Frustum planes (Gribb & Hartmann) for the visibility test.
    glm::vec4 planes[6];
    if (view.valid) {
        const glm::mat4 M = glm::transpose(view.viewProj);
        planes[0] = M[3] + M[0], planes[1] = M[3] - M[0], planes[2] = M[3] + M[1];
        planes[3] = M[3] - M[1], planes[4] = M[3] + M[2], planes[5] = M[3] - M[2];
        for (glm::vec4& p : planes) p /= glm::length(glm::vec3(p));
    }
    const glm::vec3 b0 = brush.position, b1 = brush.position + brush.velocity * lookaheadSeconds;
    const float reach = brush.radius * brushReach;
    poolFor(T, [&](size_t s, size_t e) {
        for (size_t t = s; t < e; ++t) {
            // (from the base as it is now: Move on a micro level moves the base cage)
            const glm::uvec3 tr = tris_[t];
            const glm::vec3 a = m.positions[tr.x], b = m.positions[tr.y], cc = m.positions[tr.z];
            const glm::vec3 c = (a + b + cc) / 3.0f;
            const glm::vec4 bs(c, std::sqrt(std::max({glm::dot(a - c, a - c), glm::dot(b - c, b - c), glm::dot(cc - c, cc - c)})) +
                                      mm.bulgeMax + float(mm.triRange[t]) * mm.step);
            float score = 0.0f;
            if (brush.present) { // distance to the brush's path ahead (and its mirror's)
                for (int k = 0; k < (brush.mirrorX ? 2 : 1); ++k) {
                    const glm::vec3 q = k ? glm::vec3(-c.x, c.y, c.z) : c;
                    const glm::vec3 d = b1 - b0;
                    const float len2 = glm::dot(d, d);
                    const float u = len2 > 0.0f ? std::clamp(glm::dot(q - b0, d) / len2, 0.0f, 1.0f) : 0.0f;
                    const float dist = glm::distance(q, b0 + d * u) - bs.w;
                    if (dist < reach) score = std::max(score, 1e6f - dist);
                }
            }
            if (score == 0.0f && view.valid) {
                bool in = true;
                for (const glm::vec4& p : planes)
                    if (glm::dot(glm::vec3(p), c) + p.w < -bs.w) in = false;
                if (in) {
                    const float px = dir_[t].coarseErr * view.focalPx / std::max(glm::distance(c, view.eye) - bs.w, 1e-4f);
                    if (px > view.errorPx) score = px;
                }
            }
            score_[t] = score;
        }
    }, 4096);
    scratch_.clear();
    size_t wanted = 0;
    for (uint32_t t = 0; t < T; ++t)
        if (score_[t] > 0.0f) {
            ++wanted;
            lastWanted_[t] = frame_;
            if (state_[t] == kCoarse) scratch_.push_back(t);
        }
    std::sort(scratch_.begin(), scratch_.end(), [&](uint32_t a, uint32_t b) { return score_[a] > score_[b] || (score_[a] == score_[b] && a < b); });
    const size_t maxFull = std::max<size_t>(budgetBytes / (size_t(samples_) * 2), 1);
    // Demotion candidates: full tiles not wanted for kLinger frames, least recently wanted first (never a dirty
    // one while stroking: a dab on the solver thread may be writing it).
    constexpr uint32_t kLinger = 8;
    std::vector<uint32_t> lru;
    auto demoteFor = [&](size_t room) {
        if (fullTiles_ + room <= maxFull) return true;
        if (lru.empty()) {
            for (uint32_t t = 0; t < T; ++t)
                if (state_[t] != kCoarse && lastWanted_[t] + kLinger < frame_ && !(mm.stroking && state_[t] == kDirty)) lru.push_back(t);
            std::sort(lru.begin(), lru.end(), [&](uint32_t a, uint32_t b) { return lastWanted_[a] > lastWanted_[b] || (lastWanted_[a] == lastWanted_[b] && a > b); });
        }
        while (fullTiles_ + room > maxFull && !lru.empty()) { // (back = oldest)
            const uint32_t t = lru.back();
            lru.pop_back();
            if (state_[t] != kCoarse) demoteLocked(mm, t);
        }
        return fullTiles_ + room <= maxFull;
    };
    size_t done = 0;
    constexpr size_t kBatch = 64;
    while (done < scratch_.size()) {
        size_t count = std::min(kBatch, scratch_.size() - done);
        const bool mustHave = score_[scratch_[done]] >= 1e5f; // the brush's tiles go in over the budget
        if (!demoteFor(count) && !mustHave) {
            count = std::min(count, maxFull - std::min(maxFull, fullTiles_));
            if (count == 0) break;
        }
        promoteLocked(mm, std::span<const uint32_t>(scratch_.data() + done, count), false, false);
        done += count;
        if (msSince(t0) > budgetMs) break;
        lock.unlock(); // let a brush's ensureFull in between batches
        lock.lock();
    }
    demoteFor(0);
    stats_.lastWanted = wanted;
    stats_.lastPending = scratch_.size() - done;
    stats_.lastUpdateMs = msSince(t0);
}

FocusStats FocusDetail::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    FocusStats s = stats_;
    s.fullTiles = fullTiles_;
    s.dirtyTiles = size_t(std::count(state_.begin(), state_.end(), uint8_t(kDirty)));
    for (const auto& p : parked_)
        if (!p.empty()) ++s.parkedTiles, s.parkedBytes += p.size();
    s.fullBytes = fullTiles_ * size_t(samples_) * 2;
    s.fileBytes = mapBytes_;
    return s;
}

// ---------------------------------------------------------------------------
// Saving: changed tiles only
// ---------------------------------------------------------------------------

bool FocusDetail::save(Mesh& m, std::string& error) {
#if defined(SF_FOCUS_POSIX)
    if (!m.micro || m.micro->focus.get() != this) {
        error = "Not this detail file's mesh";
        return false;
    }
    MicroMesh& mm = *m.micro;
    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t T = uint32_t(state_.size()), S = samples_, CS = coarseSamples_;
    std::vector<uint32_t> tiles;
    for (uint32_t t = 0; t < T; ++t)
        if (state_[t] == kDirty || !parked_[t].empty()) tiles.push_back(t);
    // The base cage moved (Move on a micro level): its block goes too.
    std::vector<uint8_t> baseBlock;
    if (m.normals.size() == m.vertexCount()) {
        baseBlock.resize(m.vertexCount() * 24 + tris_.size() * 12);
        std::memcpy(baseBlock.data(), m.positions.data(), m.vertexCount() * 12);
        std::memcpy(baseBlock.data() + m.vertexCount() * 12, m.normals.data(), m.vertexCount() * 12);
        std::memcpy(baseBlock.data() + m.vertexCount() * 24, tris_.data(), tris_.size() * 12);
        if (fnv1a(baseBlock.data(), m.vertexCount() * 24) == baseHash_) baseBlock.clear();
    }
    if (tiles.empty() && baseBlock.empty()) return true;
    std::vector<std::vector<uint8_t>> blobs(tiles.size());
    poolFor(tiles.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const uint32_t t = tiles[k];
            if (state_[t] == kDirty) {
                const int16_t* g = mm.disp.data() + size_t(t) * S;
                const TileInfo info = analyse(g, n_, S, coarseN_, step_, coarse_.data() + size_t(t) * CS);
                dir_[t].coarseErr = info.coarseErr;
                dir_[t].range = info.range;
                compressTile(codec_, g, n_, S, blobs[k]);
            } else {
                blobs[k] = parked_[t];
            }
        }
    }, 8);
    std::vector<uint32_t> chunkList;
    for (uint32_t t : tiles)
        if (chunkList.empty() || chunkList.back() != t / kChunk) chunkList.push_back(t / kChunk);
    std::vector<std::vector<uint8_t>> chunkBlobs(chunkList.size());
    poolFor(chunkList.size(), [&](size_t b, size_t e) {
        for (size_t k = b; k < e; ++k) {
            const uint32_t first = chunkList[k] * kChunk, count = std::min(kChunk, T - first);
            compressValues(codec_, coarse_.data() + size_t(first) * CS, count, CS, chunkBlobs[k]);
        }
    }, 1);
    // Append blobs, then the directory; the header last.
    uint64_t offset = mapBytes_;
    std::vector<Entry> dir = dir_;
    std::vector<ChunkEntry> chunkDir = chunks_;
    uint64_t garbage = garbage_;
    std::vector<uint8_t> tail;
    for (size_t k = 0; k < tiles.size(); ++k) {
        garbage += dir[tiles[k]].bytes;
        dir[tiles[k]].offset = offset + tail.size();
        dir[tiles[k]].bytes = uint32_t(blobs[k].size());
        tail.insert(tail.end(), blobs[k].begin(), blobs[k].end());
    }
    for (size_t k = 0; k < chunkList.size(); ++k) {
        garbage += chunkDir[chunkList[k]].bytes;
        chunkDir[chunkList[k]].offset = offset + tail.size();
        chunkDir[chunkList[k]].bytes = uint32_t(chunkBlobs[k].size());
        tail.insert(tail.end(), chunkBlobs[k].begin(), chunkBlobs[k].end());
    }
    Header h;
    if (!decodeHeader(map_, mapBytes_, h, error)) return false;
    garbage += h.dirBytes;
    if (!baseBlock.empty()) {
        garbage += h.baseBytes;
        h.baseOffset = offset + tail.size();
        tail.insert(tail.end(), baseBlock.begin(), baseBlock.end());
    }
    const size_t dirAt = tail.size();
    tail.resize(dirAt + size_t(T) * sizeof(Entry) + chunkDir.size() * sizeof(ChunkEntry));
    std::memcpy(tail.data() + dirAt, dir.data(), size_t(T) * sizeof(Entry));
    std::memcpy(tail.data() + dirAt + size_t(T) * sizeof(Entry), chunkDir.data(), chunkDir.size() * sizeof(ChunkEntry));
    h.dirOffset = offset + dirAt;
    h.dirBytes = tail.size() - dirAt;
    h.dirHash = fnv1a(tail.data() + dirAt, h.dirBytes);
    h.generation = generation_ + 1;
    h.garbage = garbage;
    auto pwriteAll = [&](const uint8_t* p, size_t size, uint64_t at) {
        while (size) {
            const ssize_t w = ::pwrite(fd_, p, size, off_t(at));
            if (w <= 0) return false;
            p += w, size -= size_t(w), at += uint64_t(w);
        }
        return true;
    };
    const std::vector<uint8_t> head = encodeHeader(h);
    if (!pwriteAll(tail.data(), tail.size(), offset) || ::fsync(fd_) != 0 || !pwriteAll(head.data(), head.size(), 0) ||
        ::fsync(fd_) != 0) {
        error = "Saving " + path_ + " failed (disk full?)";
        return false;
    }
    dir_ = std::move(dir);
    chunks_ = std::move(chunkDir);
    garbage_ = garbage;
    generation_ = h.generation;
    if (!baseBlock.empty()) baseHash_ = fnv1a(baseBlock.data(), m.vertexCount() * 24);
    for (uint32_t t : tiles) {
        if (state_[t] == kDirty) state_[t] = kFull;
        std::vector<uint8_t>().swap(parked_[t]);
    }
    return mapFile(error);
#else
    error = "Focus Detail needs Linux or macOS for now";
    return false;
#endif
}

// ---------------------------------------------------------------------------
// The synthetic asset
// ---------------------------------------------------------------------------

namespace {

float hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu ^ seed * 0x9E3779B9u;
    h ^= h >> 13;
    h *= 0x85EBCA6Bu;
    h ^= h >> 16;
    return float(h & 0xFFFFFF) / float(0xFFFFFF) * 2.0f - 1.0f;
}

float noise3(const glm::vec3& p, uint32_t seed) {
    const glm::vec3 f = glm::floor(p), t = p - f, u = t * t * (3.0f - 2.0f * t);
    const int x = int(f.x), y = int(f.y), z = int(f.z);
    auto h = [&](int dx, int dy, int dz) { return hash3(x + dx, y + dy, z + dz, seed); };
    const float a = glm::mix(glm::mix(h(0, 0, 0), h(1, 0, 0), u.x), glm::mix(h(0, 1, 0), h(1, 1, 0), u.x), u.y);
    const float b = glm::mix(glm::mix(h(0, 0, 1), h(1, 0, 1), u.x), glm::mix(h(0, 1, 1), h(1, 1, 1), u.x), u.y);
    return glm::mix(a, b, u.z);
}

} // namespace

bool focusWriteSyntheticHead(const std::string& path, size_t baseTriangles, int level, std::string& error) {
    Mesh base = makeBenchHead(baseTriangles);
    if (base.normals.size() != base.vertexCount()) base.recomputeAllNormals();
    double edge = 0.0;
    for (const glm::uvec3& t : base.triangles)
        edge += double(glm::distance(base.positions[t.x], base.positions[t.y]));
    const float avgEdge = float(edge / double(std::max<size_t>(base.triangleCount(), 1)));
    const float step = std::max(avgEdge / 4096.0f, 1e-12f);
    // Skin: ridged wrinkles a few base edges long, pores a few dozen micro edges apart (only the full level has them).
    const float wrinkleF = 0.8f / avgEdge, poreF = 12.0f / avgEdge, wrinkleA = 0.05f * avgEdge, poreA = 0.012f * avgEdge;
    const uint32_t n = 1u << level;
    return FocusDetail::write(path, base, level, step, [&](const MicroTriangle& T, uint32_t, int16_t* out) {
        uint32_t idx = 0;
        for (uint32_t j = 0; j <= n; ++j)
            for (uint32_t i = 0; i + j <= n; ++i, ++idx) {
                glm::vec3 s, nn;
                T.eval(i, j, s, nn);
                const float w = 1.0f - 2.0f * std::abs(noise3(s * wrinkleF, 7) * 0.7f + noise3(s * wrinkleF * 2.1f, 8) * 0.3f);
                const float pore = noise3(s * poreF, 9);
                const float d = wrinkleA * w - poreA * std::max(0.0f, pore) * std::max(0.0f, pore) * 2.0f;
                out[idx] = int16_t(std::clamp(std::lround(d / step), -32767L, 32767L));
            }
    }, error);
}

std::string focusStandard1bPath(std::string& error, const std::function<void(const std::string&)>& log) {
    namespace fs = std::filesystem;
    const fs::path dir = MicroStore::swapFolder();
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path path = dir / "standard-1b-v1.sfdetail";
    if (std::FILE* f = std::fopen(path.string().c_str(), "rb")) { // (the header is written last: a valid one = a whole file)
        uint8_t head[kHeaderBytes];
        Header h;
        std::string why;
        const bool ok = std::fread(head, 1, kHeaderBytes, f) == kHeaderBytes && decodeHeader(head, kHeaderBytes, h, why) &&
                        h.dirOffset > 0;
        std::fclose(f);
        if (ok) return path.string();
    }
    if (log) log("Writing the standard-1b asset (once): " + path.string());
    const fs::path tmp = fs::path(path.string() + ".tmp");
    if (!focusWriteSyntheticHead(tmp.string(), 61035, 7, error)) return {};
    fs::rename(tmp, path, ec);
    if (ec) {
        error = "Can't write " + path.string();
        return {};
    }
    return path.string();
}

} // namespace sf
