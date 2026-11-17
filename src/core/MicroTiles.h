// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adaptive Detail tiles (prototype, off by default: SF_MICRO_TILES=1; docs/design/perf-scoreboard.md, the 10x
// scoreboard's axis 2): detail finer than a micro level's grid, stored only where a brush put it.
//
// A micro level keeps one dense grid per base triangle (its ROOT, core/Micro.h). On top, each base triangle may hold
// a sparse quadtree of DELTA tiles: a tile at depth d covers one sub-triangle of the base triangle at tree depth
// d + 1 (quadrants 0, 1, 2 = the corner triangles, 3 = the middle one, inverted; 4^(d + 1) per base triangle) and
// holds a regular grid of kN = 16 segments (kSamples int16 points) with the DELTA, in units of the level's `step`,
// from the LINEAR interpolation of the next coarser level (the root, or the deltas of the tiles above it; a depth with
// no tile there is a zero delta: a dab makes tiles at its own depth only, never their parents). The root's n = 32 is
// two tiles across, so a depth d tile's cell is the root cell / 2^d; untouched = no tile. The surface under a point =
// root (linear in its cell) + the deltas of the tiles holding it, down to the deepest one.
//
// Why deltas: a dab writes only tiles of ITS depth (from its radius: a cell of r / 48, the brush kernel's sampling;
// the view it came from only caps it), so a big dab over fine detail rewrites a few coarse points and the fine deltas
// ride along (the multires promise), and a small dab over a plain area allocates only the tiles under it. Memory and
// the cost of a dab track what was sculpted, not the model's finest resolution.
//
// Memory: tiles win by sparsity, not compression (fully covered they cost ~1.2x a dense grid of the same spacing:
// a finished depth-2 pass over the whole 200M head is 12.6M tiles, 3.9 GB). So the store keeps a BUDGET of resident
// tiles (`residentBudget`): at the end of a stroke the least recently used tiles beyond it are PARKED, packed with the
// tile codec of core/MicroStore.h (a smooth delta a few bits per point, 3 bytes for a flat one), and come back when a
// brush or undo touches them. The index is per base triangle (a sorted vector of (depth, path) -> tile), 8 bytes per
// tile, not a global hash map (which cost 0.66 GB at that coverage).
//
// Shared edges need no stitch: a point on a base edge is at the same barycentric place in the tiles on both sides,
// the root's edge values are stitched, the interpolation parameters are exact binary fractions, and the brush sees the
// same position and normal (MicroTriangle::evalWeights sums the corners in vertex order), so both tiles compute the
// same value (tests/test_micro.cpp micro_tiles_shared_edges_are_bit_identical).
#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sf {

struct MicroMesh;

// A sub-triangle's barycentric frame: a point (a, b) of it (a, b >= 0, a + b <= 1) has the base triangle's corner
// weights o + a u + b v (as glm::vec3 (w0, w1, w2)).
struct TileFrame {
    glm::vec3 o{1, 0, 0}, u{-1, 1, 0}, v{-1, 0, 1}; // the whole base triangle
    TileFrame child(int quadrant) const;
    glm::vec3 at(float a, float b) const { return o + u * a + v * b; }
    // (a, b) of base weights w in this frame.
    void coords(const glm::vec3& w, float& a, float& b) const;
    // The quadrant of a point (a, b) of this frame.
    static int quadrantOf(float a, float b) { return a + b <= 0.5f ? 0 : a >= 0.5f ? 1 : b >= 0.5f ? 2 : 3; }
    // The frame of the sub-triangle `path` (depth + 1 quadrants, 2 bits each, the first taken highest) of a base triangle.
    static TileFrame of(int depth, uint32_t path);
};

struct MicroTiles {
    static constexpr uint32_t kN = 16, kSamples = (kN + 1) * (kN + 2) / 2; // 153
    static constexpr int kMaxDepth = 12;
    static constexpr int kBrushSamples = 48; // a dab's cell is about r / 48 (MicroStroke::kBrushSamples)
    struct Tile {
        std::array<int16_t, kSamples> delta{};
    };

    // Off unless SF_MICRO_TILES=1 (read when a stroke starts), and off on a GPU that can't draw them (setGpu).
    static bool enabled();
    // Whether the GL renderer (GL_RENDERER) can draw tiles. Not Mesa's OpenGL over D3D12 (WSL, Windows on ARM): its
    // first tiled frame removes the device, so strokes there keep writing the plain micro levels.
    static bool gpuSupported(std::string_view glRenderer);
    static void setGpu(std::string_view glRenderer); // (Renderer::init)
    // The resident budget for a new store (SF_MICRO_TILES_RESIDENT_MB, else a quarter of the tier's resident cache cap).
    static size_t defaultResidentBudget();

    static uint64_t key(uint32_t tri, int depth, uint32_t path) { return uint64_t(tri) << 32 | uint64_t(depth) << 28 | path; }
    static uint32_t keyTri(uint64_t k) { return uint32_t(k >> 32); }
    static int keyDepth(uint64_t k) { return int(k >> 28 & 15); }
    static uint32_t keyPath(uint64_t k) { return uint32_t(k & 0x0FFFFFFF); }
    static uint32_t index(uint32_t i, uint32_t j) { return j * (kN + 1) - j * (j - 1) / 2 + i; }

    // --- The index: per base triangle its tiles, sorted by (depth, path) ---
    struct Entry {
        uint32_t pathKey; // depth << 28 | path
        uint32_t ref;     // resident: slot + 1; parked: kParked | parked record
    };
    static constexpr uint32_t kParked = 0x80000000u;
    std::vector<std::vector<Entry>> byTri;
    std::vector<uint8_t> maxDepth; // per base triangle: the deepest tile it holds (an upper bound; 0 = none)
    // --- Resident tiles ---
    std::vector<Tile> pool;
    std::vector<uint64_t> poolKey;  // per slot: its key (~0 = free)
    std::vector<uint32_t> poolUse;  // per slot: the stamp of its last use (trim keeps the recent ones)
    std::vector<uint32_t> freeSlots;
    uint32_t useStamp = 0;
    // --- Parked tiles: packed into an arena (3 bytes + the values' bit width x kSamples) ---
    struct Parked {
        uint32_t offset = 0, size = 0;
    };
    std::vector<uint8_t> arena;
    std::vector<Parked> parked;
    std::vector<uint32_t> freeParked;
    size_t arenaGarbage = 0;
    size_t residentBudget = 0; // bytes of resident tiles trim() keeps at most (0 = unlimited)

    void resize(uint32_t baseTriangles);
    size_t alive() const { return residentCount() + parkedCount(); }
    size_t residentCount() const { return pool.size() - freeSlots.size(); }
    size_t parkedCount() const { return parked.size() - freeParked.size(); }
    size_t residentBytes() const { return residentCount() * sizeof(Tile); }
    size_t parkedBytes() const { return arena.size() - arenaGarbage; }
    size_t bytes() const; // everything the store holds (pool capacity, arena, index)
    bool has(uint64_t key) const { return findEntry(key) != nullptr; }
    // The tile for WRITING, resident (a parked one comes back first); null when absent.
    Tile* resident(uint64_t key);
    // The tile for writing, made (all zero, `born` = true) when absent. (May move the pool: keep slots across calls.)
    Tile* obtain(uint64_t key, bool& born);
    uint32_t slot(const Tile* t) const { return uint32_t(t - pool.data()); }
    Tile& at(uint32_t slot) { return pool[slot]; }
    // The tile for READING: resident -> the pool's; parked -> unpacked into `scratch`; null when absent.
    const Tile* read(uint64_t key, Tile& scratch) const;
    void release(uint64_t key);
    // Marks the tiles of base triangle `tri` above `depth` along `path` (its ancestors) resident (a brush about to read
    // them under its dab, once, rather than unpacking per point).
    void residentAbove(uint32_t tri, int depth, uint32_t path);
    // Parks the least recently used resident tiles until the resident bytes are within `budget` (0 = residentBudget;
    // tiles used since `keepSince` stay, 0 = none stays). Returns the tiles parked. Frees the pool's spare capacity
    // when half of it is free.
    size_t trim(size_t budget = 0, uint32_t keepSince = 0);
    // Every tile (parked ones unpacked), in key order: f(key, const Tile&).
    template <class F> void forEach(F&& f) const {
        Tile scratch;
        for (uint32_t tri = 0; tri < byTri.size(); ++tri)
            for (const Entry& e : byTri[tri]) {
                const uint64_t k = uint64_t(tri) << 32 | e.pathKey;
                if (const Tile* t = read(k, scratch)) f(k, *t);
            }
    }
    // Divides every delta by `div` (the level's step grew: MicroMesh::widen).
    void rescale(int div);

    // The depth a dab of radius r writes at, from the root cell (`rootCell`, world units): the finest depth whose
    // cell is still <= r / kBrushSamples (the kernel's own sampling: finer would only interpolate it), capped by the
    // view the dab was placed from when known (`pxPerUnit` > 0: screen pixels per world unit at the dab): a cell is
    // never smaller than `minPx` pixels there, so a tiny brush seen from afar doesn't spawn detail nobody can see.
    // The result does not depend on the view otherwise (design critique 11-16: the same stroke stores the same
    // geometry from any zoom). <= 0: the root.
    static int depthFor(float rootCell, float r, float pxPerUnit, float minPx = 0.25f);

    // The surface height (units of step) at base weights w of base triangle `tri`: the root grid (linear in its cell)
    // plus the deltas of the tiles under w down to `maxDepthWanted` (-1 = all). Bit-identical on both sides of a base edge.
    float heightAt(const MicroMesh& mm, uint32_t tri, const glm::vec3& w, int maxDepthWanted = -1) const;
    // The tiles' share of it: heightAt less the root's linear height (the delta chain's sum; 0 where no tile).
    float deltaAt(uint32_t tri, const glm::vec3& w, int maxDepthWanted = -1) const;
    bool hasTiles(uint32_t tri) const { return tri < byTri.size() && !byTri[tri].empty(); }

    // Delta PAGES (render/MicroTilePages.h draws them): the tile delta of base triangle `tri` at every grid point of
    // a level with m segments, rounded to whole units of step (int16). A point (i, j) is at base weights
    // ((m - i - j) / m, i / m, j / m), exact binary fractions, so a point on a base edge gets the same value from
    // both triangles (as heightAt). pageValue is one point of it, the same bits (the CPU twin, core/Micro.h
    // microDrawPoint, and the tests read it). `out` holds (m + 1)(m + 2) / 2 values, MicroMesh::index order.
    void pageValues(uint32_t tri, uint32_t m, int16_t* out) const;
    int16_t pageValue(uint32_t tri, uint32_t m, uint32_t i, uint32_t j) const;
    static glm::vec3 pageWeights(uint32_t m, uint32_t i, uint32_t j) {
        const float inv = 1.0f / float(m);
        return glm::vec3(float(m - i - j) * inv, float(i) * inv, float(j) * inv);
    }
    static int16_t pageRound(float delta) { return int16_t(delta >= 32767.0f ? 32767 : delta <= -32767.0f ? -32767 : int(std::lround(delta))); }
    static float lerpGrid(const int16_t* g, uint32_t n, float a, float b);

    // The tile codec for one tile (the format of core/MicroStore.h microPackTiles): appended to `out`.
    static void packTile(const Tile& t, std::vector<uint8_t>& out);
    static bool unpackTile(const uint8_t* data, size_t size, Tile& t);

private:
    // The delta chain's sum at w, the tiles found through lookup(key) -> const Tile* (null = none there).
    template <class Lookup> float descend(uint32_t tri, const glm::vec3& w, int deepest, Lookup&& lookup) const;
    const Entry* findEntry(uint64_t key) const;
    Entry* findEntry(uint64_t key);
    Entry& insertEntry(uint64_t key);
    uint32_t newSlot(uint64_t key);
    void parkSlot(Entry& e, uint32_t slot);
    void compactArena();
    void compactPool();
};

} // namespace sf
