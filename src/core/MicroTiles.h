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
// no tile there is a zero delta: a dab makes tiles at its own depth only, never their parents). The root's
// n = 32 is two tiles across, so a depth d tile's cell is the root cell / 2^d; untouched = no tile. The surface under a
// point = root (linear in its cell) + the deltas of the tiles holding it, down to the deepest one.
//
// Why deltas: a dab writes only tiles of ITS depth (from its radius: a cell of r / 48, the brush kernel's sampling;
// the view it came from only caps it), so a big dab over fine detail rewrites a few coarse points and the
// fine deltas ride along (the multires promise), and a small dab over a plain area allocates only the tiles under it.
// Memory and the cost of a dab track what was sculpted, not the model's finest resolution.
//
// Shared edges need no stitch: a point on a base edge is at the same barycentric place in the tiles on both sides,
// the root's edge values are stitched, the interpolation parameters are exact binary fractions, and the brush sees the
// same position and normal (MicroTriangle::evalWeights sums the corners in vertex order), so both tiles compute the
// same value (tests/test_micro.cpp micro_tiles_shared_edges_are_bit_identical).
#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
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

    // Off unless SF_MICRO_TILES=1 (read once).
    static bool enabled();

    static uint64_t key(uint32_t tri, int depth, uint32_t path) { return uint64_t(tri) << 32 | uint64_t(depth) << 28 | path; }
    static uint32_t keyTri(uint64_t k) { return uint32_t(k >> 32); }
    static int keyDepth(uint64_t k) { return int(k >> 28 & 15); }
    static uint32_t keyPath(uint64_t k) { return uint32_t(k & 0x0FFFFFFF); }
    static uint32_t index(uint32_t i, uint32_t j) { return j * (kN + 1) - j * (j - 1) / 2 + i; }

    std::unordered_map<uint64_t, uint32_t> slotOf; // key -> slot + 1
    std::vector<Tile> pool;
    std::vector<uint32_t> freeSlots;
    std::vector<uint8_t> maxDepth; // per base triangle: the deepest tile it holds (an upper bound; 0 = none)

    void resize(uint32_t baseTriangles) { maxDepth.assign(baseTriangles, 0); }
    size_t alive() const { return slotOf.size(); }
    size_t bytes() const;
    Tile* find(uint64_t key);
    const Tile* find(uint64_t key) const;
    // The tile, made (all zero, `born` = true) when absent. (May move the pool: keep slots across calls, not pointers.)
    Tile* obtain(uint64_t key, bool& born);
    uint32_t slot(const Tile* t) const { return uint32_t(t - pool.data()); }
    void release(uint64_t key);

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
    static float lerpGrid(const int16_t* g, uint32_t n, float a, float b);
};

} // namespace sf
