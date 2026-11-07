// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-tile GPU levels for a micro level that streams from a detail file (Focus Detail, core/FocusDetail.h;
// docs/design/focus-detail.md): MicroRenderer's GPU copy follows the focus instead of holding every tile's full grid.
//
// - Every tile ("slot") has a record in MicroRenderer's chunks instead of its full grid: its coarse grid (level
//   FocusDetail::kCoarseLevel, 45 points) and the inner points of its three edges at full resolution (3 (n - 1)),
//   426 values at level 7 against 8,385. The vertex shader rebuilds the grid from it the way FocusDetail upsamples
//   (the same integer sums), and the edges are the CPU's own, so a coarse tile draws exactly what `disp` holds and
//   meets its neighbours, coarse or full, without cracks. Its shading grid is the coarse level's (45 points), shaded
//   on that level (MicroShader::shadeLevel): opening shades 45 points a tile, not the full grid.
// - Full tiles that the last frame drew finer than the coarse level get a page in a pool: the full grid and its
//   full shading grid, as MicroRenderer holds every tile without Focus Detail. Pages go to the finest-drawn tiles
//   first, within poolBudgetBytes; a tile's shading follows within the frame's shading budget (until then it
//   lights with its coarse shading). Tiles that go coarse on the CPU give their page back at once.
// - Per slot a page table texture (0 = none, else page + 1, bit 31: its shading is in).
#pragma once

#include "core/Micro.h"
#include "render/GL.h"

#include <cstdint>
#include <vector>

namespace sf {

class MicroRenderer;

class MicroFocus {
public:
    ~MicroFocus();
    void release();
    // Sizes for MicroRenderer's chunks (per slot: record values, coarse shading points) and the coarse level.
    void setup(const Mesh& mesh, uint32_t slots);
    uint32_t recordPoints() const { return record_; }
    uint32_t shadePoints() const { return coarseSamples_; }
    // Everything from scratch (opening, a new level): records, coarse shading, no pages.
    void syncAll(Mesh& mesh, MicroRenderer& r);
    // A sync: `dirty` base triangles whose heights changed, `moved` (bitset or empty) those whose base moved.
    // Updates their records and shading, frees the pages of tiles that went coarse, gives pages to the tiles the
    // last draw wanted finer than the coarse level and shades pages within the budget.
    void sync(Mesh& mesh, MicroRenderer& r, const std::vector<uint32_t>& dirty, const std::vector<uint64_t>& moved,
              bool stroking, bool viewMoving);
    // draw(): binds the pool and the page table and sets the shader's focus uniforms; remembers the levels drawn
    // (`levels` per cluster, -1 = not drawn) for the next sync.
    void bind(GLuint program, const std::vector<uint8_t>& visible, const std::vector<int8_t>& levels);
    static void unbind();
    size_t gpuBytes() const { return bytes_; }
    size_t pages() const { return pageCount_; }
    size_t pagesShaded() const { return shadedPages_; }
    size_t pendingPages() const { return pendingShade_.size(); }

    size_t poolBudgetBytes = size_t(384) << 20; // the pool's most (full grid + full shading: 6 bytes a point)

private:
    void buildCoarse(const Mesh& mesh, const MicroMesh& mm, const std::vector<uint32_t>& tris);
    void writeRecords(const MicroMesh& mm, MicroRenderer& r, std::vector<uint32_t>& slots, bool all);
    void shadeCoarse(const Mesh& mesh, MicroRenderer& r, const std::vector<uint32_t>& tris, bool all);
    bool growPool(uint32_t pages);
    void sendPages(const MicroMesh& mm, MicroRenderer& r, const std::vector<uint32_t>& slots);
    void shadePages(const Mesh& mesh, MicroRenderer& r, size_t budgetPoints);
    void freePage(uint32_t slot);

    uint32_t n_ = 1, samples_ = 3, coarseN_ = 1, coarseSamples_ = 3, shift_ = 0, record_ = 0, slots_ = 0;
    MicroMesh coarse_;               // the coarse level: every tile's coarse grid (shading only)
    MicroShader shader_;             // coarse shading
    // The pool
    GLuint poolDisp_ = 0, poolDispTex_ = 0, poolShade_ = 0, poolShadeTex_ = 0, pageData_ = 0, pageTex_ = 0;
    uint32_t capacity_ = 0;          // pages the pool holds
    std::vector<uint32_t> page_;     // per slot: 0 = none, else page + 1 | shaded << 31 (the page table)
    std::vector<uint32_t> owner_;    // per page: its slot (~0 = free)
    std::vector<uint32_t> free_;
    std::vector<uint32_t> lastUse_;  // per page: the frame its tile was last drawn fine
    size_t pageCount_ = 0, shadedPages_ = 0;
    bool pageTableDirty_ = true;
    std::vector<uint32_t> pendingShade_; // slots whose page waits for its full shading
    std::vector<int8_t> drawn_;          // per cluster: the level the last draw used (-1 = not drawn)
    uint32_t frame_ = 0;
    double shadeNsPerPoint_ = 0.0;
    size_t bytes_ = 0;
    std::vector<int16_t> staging_;
    std::vector<uint32_t> shadeStaging_, written_, coarseShade_;
    struct Uniforms {
        GLuint program = 0;
        GLint focus = -1, cn = -1, cSamples = -1, record = -1, shift = -1;
    } u_;
};

} // namespace sf
