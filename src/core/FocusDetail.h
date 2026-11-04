// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Focus Detail (prototype, off by default; docs/design/focus-detail.md): a micro level (core/Micro.h) far larger
// than what is worth decompressing, streamed by where the brush and the camera are.
//
// - On disk (a .sfdetail file, memory-mapped): the base mesh, then every base triangle's micro grid ("tile") as
//   quantized 16-bit heights, row-delta coded and compressed (zstd when the build has it, else the tile codec of
//   core/MicroStore.h). A directory gives each tile's place, its exact range and how far its coarse grid is from
//   it. Coarse grids (level kCoarseLevel, a subsample of the full grid) sit in compressed chunks of kChunk tiles.
// - In memory: MicroMesh::disp keeps its full-size layout, so brushes and the renderer work unchanged. A tile
//   outside the focus holds its coarse grid upsampled (exact at the coarse points, linear in between, so shared
//   edges stay watertight); a tile in the focus holds its full grid.
// - The focus: the tiles under and ahead of the brush (its position plus velocity, a short time ahead) and the
//   visible tiles whose coarse grid would be more than FocusView::errorPx off on screen. Promotions run each
//   frame within a time budget, highest priority first; demotions free the least recently wanted tiles once the
//   full tiles exceed budgetBytes (a tile wanted in the last 8 frames stays). Brushes and undo never write a coarse tile: MicroStroke::touch and
//   MicroUndo::swapWith promote the tiles they are about to change first (ensureFull).
// - Edited tiles are dirty until saved; a dirty tile that gets demoted is compressed into memory ("parked").
//   save() appends only the dirty and parked tiles, the coarse chunks holding them and a new directory, then
//   rewrites the header (the old directory stays valid until then: a crash keeps the last save).
// Undo needs nothing new: a stroke's MicroUndo already keeps copy-on-write deltas of the tiles it touched.
#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace sf {

struct MicroMesh;
struct MicroTriangle;

struct FocusView {
    bool valid = false;
    glm::mat4 viewProj{1.0f};
    glm::vec3 eye{0.0f};
    float focalPx = 1000.0f; // pixels per world unit at distance 1 (viewport height / (2 tan(fov / 2)))
    float errorPx = 0.5f;    // a visible tile is promoted when its coarse grid is further off than this on screen
};

struct FocusBrush {
    bool present = false;     // the cursor is over the model (hovering or stroking)
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f}; // world units per second
    float radius = 0.0f;      // world units
    bool mirrorX = false;     // X symmetry: the mirrored brush too
};

struct FocusStats {
    size_t tiles = 0, fullTiles = 0, dirtyTiles = 0, parkedTiles = 0;
    size_t parkedBytes = 0, fullBytes = 0, fileBytes = 0;
    uint64_t promoted = 0, demoted = 0, syncPromoted = 0; // since open
    double promoteMs = 0;     // total time promoting (decompress + upsample/copy + markDirty)
    double lastUpdateMs = 0;  // the last update() (scoring + promotions + demotions)
    size_t lastWanted = 0;    // tiles the last update() wanted full
    size_t lastPending = 0;   // of those, still coarse after it (the time budget ran out)
};

class FocusDetail {
public:
    static constexpr int kCoarseLevel = 3;  // 8 x 8 segments, 45 points per tile
    static constexpr uint32_t kChunk = 1024; // coarse grids per compressed chunk

    // Writes a detail file for `base` (its triangles are the tiles) at micro level `level` (> kCoarseLevel) with
    // `step` world units per integer unit; `tile(T, t, out)` fills tile t's samples (T is set for t). Tiles are
    // generated, compressed and written in parallel batches: the full level is never in memory.
    using TileFn = std::function<void(const MicroTriangle& T, uint32_t t, int16_t* out)>;
    static bool write(const std::string& path, const Mesh& base, int level, float step, const TileFn& tile,
                      std::string& error);
    // Opens a detail file: `out` becomes its base mesh with a micro level whose tiles all hold their coarse grids,
    // and the returned manager is attached to it (MicroMesh::focus).
    static std::shared_ptr<FocusDetail> open(const std::string& path, Mesh& out, std::string& error);
    ~FocusDetail();

    // Once a frame (main thread): scores every tile, promotes the wanted ones (most wanted first) for at most
    // `budgetMs`, demotes the least recently wanted while over budgetBytes.
    void update(Mesh& m, const FocusView& view, const FocusBrush& brush, double budgetMs = 4.0);
    // Makes tiles `tris` full now (brushes, undo); `edit` marks them dirty. Thread-safe.
    void ensureFull(MicroMesh& mm, std::span<const uint32_t> tris, bool edit);
    // Writes the changed tiles to the file (appended), a new directory and header. Returns false with `error`.
    bool save(Mesh& m, std::string& error);

    // 0 = coarse (upsampled), 1 = full.
    int tileLevel(uint32_t tri) const { return tri < state_.size() && state_[tri] != kCoarse ? 1 : 0; }
    FocusStats stats() const;
    const std::string& path() const { return path_; }

    size_t budgetBytes = size_t(512) << 20; // full tiles kept (2 bytes a point) before the LRU demotes
    float lookaheadSeconds = 0.25f;         // how far ahead of the brush promotion looks
    float brushReach = 2.0f;                // promoted around the brush: this many radii

private:
    FocusDetail() = default;
    enum : uint8_t { kCoarse = 0, kFull = 1, kDirty = 2 };
    struct Entry {
        uint64_t offset = 0;
        uint32_t bytes = 0;
        uint16_t range = 0;   // the largest |value| of the full tile
        uint16_t pad = 0;
        float coarseErr = 0;  // world units: the most the upsampled coarse grid is off
        uint32_t pad2 = 0;
    };
    struct ChunkEntry {
        uint64_t offset = 0;
        uint32_t bytes = 0, pad = 0;
    };
    bool mapFile(std::string& error);
    void unmap();
    bool readTile(uint32_t t, int16_t* out) const;    // the full grid (parked first, else the file)
    void fillCoarse(MicroMesh& mm, uint32_t t) const; // upsamples the coarse grid into disp
    void promoteLocked(MicroMesh& mm, std::span<const uint32_t> tris, bool edit, bool sync);
    void demoteLocked(MicroMesh& mm, uint32_t t);

    std::string path_;
    int level_ = 0;
    uint32_t n_ = 0, samples_ = 0, coarseN_ = 0, coarseSamples_ = 0, codec_ = 0;
    float step_ = 0;
    uint64_t generation_ = 0, garbage_ = 0;
    std::vector<Entry> dir_;
    std::vector<ChunkEntry> chunks_;
    std::vector<int16_t> coarse_;                // tiles * coarseSamples, always resident
    std::vector<uint8_t> state_;
    std::vector<uint32_t> lastWanted_;           // per tile: the update it was last wanted in
    uint32_t frame_ = 0;
    std::vector<std::vector<uint8_t>> parked_;   // per tile: a demoted dirty tile, compressed (or empty)
    std::vector<glm::uvec3> tris_;               // the base triangles (edges between tiles)
    uint64_t baseHash_ = 0;                      // of the saved base positions and normals
    size_t fullTiles_ = 0;
    mutable std::mutex mutex_;
    FocusStats stats_;
    std::vector<uint32_t> scratch_;
    std::vector<float> score_;
    // The mapped file
    int fd_ = -1;
    const uint8_t* map_ = nullptr;
    size_t mapBytes_ = 0;
};

// A synthetic asset for benchmarks and tests: the standard scenes' procedural head with about `baseTriangles`
// base triangles at micro level `level`, plus skin-like detail (wrinkles and pores) generated per tile. Written
// to `path` (nothing in memory at full resolution).
bool focusWriteSyntheticHead(const std::string& path, size_t baseTriangles, int level, std::string& error);
// The standard-1b asset (base ~61K triangles at level 7: just over a billion micro triangles) in the cache folder,
// written the first time (a minute or so). Returns its path, or empty with `error`.
std::string focusStandard1bPath(std::string& error, const std::function<void(const std::string&)>& log = {});

struct BenchOptions;
// The Focus Detail benchmark (sculptforge-cli --bench-focus; docs/design/focus-detail.md): open, memory, promotion
// latency, an orbit close to the surface, Clay and Move strokes with and without prediction, undo, incremental
// save, and every tile decompressed for comparison. Markdown.
std::string runFocusBenchmark(const BenchOptions& options);

} // namespace sf
