// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws Adaptive Detail tiles (core/MicroTiles.h, SF_MICRO_TILES=1): per-slot DELTA PAGES for MicroRenderer's plain
// program, modelled on MicroFocus's page pool.
//
// - A page is the tile chain's delta (MicroTiles::pageValues: whole units of step, int16) at every grid point of one
//   fixed page level P = the stored level + drawAbove (the finest level a cluster may draw): 8,385 points at level 7.
//   The vertex shader adds N(w) * page(i << (P - uLevel), j << (P - uLevel)) * uStep to the point it draws today, at
//   EVERY drawn level (a tile's delta at a stored point is not 0: without its page a coarser draw would drop the
//   stroke and pop it back in on zoom). Where the page is 0 the point is today's, to the bit, so a tiled triangle
//   next to an untiled one has no crack (a non-zero delta on a shared edge means both sides hold tiles there, with
//   the same values: tests/test_micro.cpp).
// - Pages go to the slots of the clusters the last draw drew whose base triangle has tiles, finest-drawn first, up to
//   `budgetPages` (1024 at least); beyond it the least recently drawn page goes. A dirty base triangle (a tile dab,
//   undo) refills its page in the same sync; one that has no tiles left gives its page back.
// - Lighting: beside each page its slopes (microTileSlopes: RG16F, the delta's slope along the page's two directions,
//   shared edges and corners averaged over the triangles there, so both sides light alike); the fragment stage
//   interpolates them like the shading grid (so the light does not change with the drawn level), turns them into the
//   surface gradient (the base triangle's frame, from the vertex stage) and tilts the stored shading normal by it.
// - Per slot a page table (R32UI: page + 1, 0 = none) on unit 13, the pool (R16I) on unit 14, the slopes on 15. Focus Detail levels
//   (their own program) don't draw tiles (M4).
#pragma once

#include "core/Micro.h"
#include "render/GL.h"
#include "render/TexelStore.h"

#include <cstdint>
#include <vector>

namespace sf {

class MicroRenderer;

class MicroTilePages {
public:
    ~MicroTilePages();
    void release();
    // A new layout: `slots` base triangles, pages at `pageLevel` (no pages kept).
    void setup(uint32_t slots, int pageLevel, bool wide); // wide: the page table is a 2D texture (render/TexelStore.h)
    int pageLevel() const { return level_; }
    // A sync: `dirty` base triangles whose heights changed (`all`: every page is out of date). Refills their pages,
    // frees those with no tiles left, gives pages to the tiled slots the last draw drew.
    void sync(const Mesh& mesh, MicroRenderer& r, const std::vector<uint32_t>& dirty, bool all);
    // draw(): binds the pool and the page table and sets uTilePageLevel (-1 when nothing is paged, or for one frame
    // when the draw goes finer than the pages, `maxDraw`: drawAbove changed since the sync); remembers the levels
    // drawn (`levels` per cluster where `visible`) for the next sync.
    void bind(GLuint program, const std::vector<uint8_t>& visible, const std::vector<int8_t>& levels, int maxDraw);
    static void unbind();
    size_t gpuBytes() const { return bytes_; }
    size_t pages() const { return pageCount_; }
    size_t pageBytes() const { return size_t(samples_) * 6; } // (delta R16I + slopes RG16F)
    // Tiled slots drawn without a page in the last sync (the budget was full of pages drawn this frame).
    size_t starved() const { return starved_; }
    // One more frame, please: the draw changed what it draws while tiles exist (the next sync pages it from that draw),
    // or the frame's upload held pages back. (Not for a full budget: that stays until the view changes.)
    bool wantsFrame() const { return wantSync_ || capped_; }
    // The last sync's fill: pages refilled or made, and the main-thread ms it took (fill + upload).
    size_t lastFilled = 0;
    double lastFillMs = 0.0;
    uint32_t budgetPages = 1024;

private:
    bool growPool(uint32_t want);
    void freePage(uint32_t slot);
    void markTable(uint32_t slot);

    int level_ = -1;
    uint32_t m_ = 1, samples_ = 3, slots_ = 0;
    GLuint pool_ = 0, poolTex_ = 0, slope_ = 0, slopeTex_ = 0;
    TexelStore table_;
    uint32_t capacity_ = 0;
    std::vector<uint32_t> page_;    // per slot: 0 = none, else page + 1
    std::vector<uint32_t> owner_;   // per page: its slot (~0 = free)
    std::vector<uint32_t> lastUse_; // per page: the frame its slot was last drawn
    std::vector<uint32_t> free_;
    size_t pageCount_ = 0, starved_ = 0, bytes_ = 0;
    bool tiled_ = false, wantSync_ = false, capped_ = false; // (the last sync saw tiles; see wantsFrame)
    uint32_t tableLo_ = ~0u, tableHi_ = 0; // the page table's range to send
    std::vector<int8_t> drawn_;            // per cluster: the level the last draw used (-1 = not drawn)
    uint32_t frame_ = 0;
    std::vector<int16_t> staging_;
    std::vector<uint16_t> slopeStaging_;
    struct Uniforms {
        GLuint program = 0;
        GLint level = -1, samples = -1;
    } u_;
};

} // namespace sf
