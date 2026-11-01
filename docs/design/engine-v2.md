# SculptForge Engine v2: two-tier adaptive engine for 1-billion-polygon sculpts

Status: design proposal. Target: SculptForge 0.11-0.13. Owner: engine. Saved as `docs/design/engine-v2.md`.

## 0. Summary

Engine v2 lets one SubTool carry up to about 1 billion sculptable micro-triangles. It stays responsive on a 16 GB laptop and runs at 144 Hz with continuous shading on an RTX 4080-5090 workstation. The core library (`sculptforge_core`) remains the authority for save, undo, CLI, MCP and Python.

| Decision | Choice |
|---|---|
| Surface representation | Displaced micro-mesh on a quad base cage. The cage is the SubTool's mesh at its top subdivision level. Each quad patch has an n x n grid (n = 16..128, a power of two) of scalar heights along the Phong-tessellated cage normal. Topology is implicit, with no index bytes per patch. |
| Paging unit | A node page of 16 x 16 cells (17 x 17 points, 512 triangles). Pages form a quadtree per patch (mips by point subsampling). The same unit serves as GPU cluster, streaming page and shading tile. |
| Height formats | CPU master: int16 per point (lossless, as in feat/micromesh). GPU and page file: 8-11 bits per point relative to a per-page base. Lossless when the page range fits; otherwise the display copy is lossy and the master stays exact. |
| Tiers | `EngineTier::Laptop` (draft strokes, aggressive eviction, about 2M active triangles) and `EngineTier::Workstation` (resident at high resolution, 144 Hz continuous shading, RT AO). Derived at run time from feat/hw-profile's `HardwareProfile`, the backend's capabilities and the power state. |
| Sculpting on GPU | The CPU sends brush splines. The GPU places dabs, resolves them through the visibility buffer and accumulates **Stroke Delta Tiles (SDT)**: integer delta grids at the brush's own stride. |
| Authority | **The GPU computes, the CPU commits.** At commit the GPU's SDTs are read back per tile and applied to the CPU master by an integer-only function. The CPU and GPU results are therefore bit-identical, undo is the inverse delta, and undo size does not depend on polygon count. |
| Backend | **(c) Vulkan behind a render-hardware interface (RHI), delivered through (b)**. GL 4.3 compute ships first on Windows/Linux in today's GL renderer. Vulkan follows, with macOS (MoltenVK) first. GL 3.3 plus CPU brushes stays as the permanent fallback. |
| Undo | Micro-tile undo keyed by a Morton hash of (patch, level, x, y). SDT deltas are the normal case and full-tile copy-on-write is the fallback. Each stroke uses at most 500 KB of RAM, and anything larger spills to an append-only journal. |
| Crash safety | A CRC-checked journal with group commit every 250 ms. Recovery replays committed strokes on top of the last save. |
| File format | Version 7 is reconciled first (feat/micromesh and feat/pbr both claim it). Quad micro levels are format 8, with a chunked, 4 KiB-aligned tile blob inside the stored ZIP that can be loaded lazily. |

## 1. Context: what exists and what v2 builds on

### 1.1 main (0.9.7)
- `Mesh` (`src/core/Mesh.h`) holds positions, normals and triangles. Quads are triangle pairs (`quadPartners`). Divided levels use implicit adjacency, and dirty state is tracked in 2048-vertex blocks.
- `BVH` (`src/core/BVH.h`): raycast, sphere queries and a parallel refit.
- `UndoStack`/`UndoRecord` (`src/core/Undo.h`): sparse channels, a 1 GiB budget, and texture undo as 64 x 64 tiles (`PackedTexels`), which is the precedent for tiled undo.
- `Renderer` (`src/render/Renderer.cpp`, 2,351 lines): GL 3.3 core with clusters (`UploadOrder`), 12-byte `PackedVertex`, a `MultiDrawElementsBaseVertex`, texture buffers, 64 shader variants, a geometry shader for UV/normal-map/flat shading, dyntopo patches, splats, MSAA view targets, offscreen capture and thumbnails. `src/app` makes about 60 direct `gl::` calls (LightBox, Thumbnails, BrushGallery and others use GL textures as ImGui images).
- `Multires` (`src/core/Multires.h`): Catmull-Clark subdivision on quad meshes, with details in local frames. This already provides the "detail rides on a reshaped low level" behaviour for real levels.
- Projects (`src/core/Project.cpp`): a stored ZIP written with 64-byte alignment by `writeStoredZip` and an atomic `.tmp` + rename. `readStoredZipIndex` lists entry offsets without reading the data, which makes lazy loading possible.

### 1.2 feat/micromesh (agent-aee391c6b3d1a1677)
- `MicroMesh` (`core/Micro.h`), with `Mesh::micro` shared copy-on-write. Each **base triangle** has a barycentric grid of int16 displacements, level up to 7 (128 segments). The surface is `S(t,i,j) + d*N`, with Phong tessellation (factor 0.75) and the interpolated normal. Shared edges are evaluated in ascending vertex-index order so both sides are bit-identical. Grids are duplicated per triangle and stitched after every edit.
- `MicroStore` (`core/MicroStore.h`): int16 storage in memory or in a sparse memory-mapped file that is deleted once open. The page cache acts as the LRU. Projects store the tiles packed (minimum, bit width, deltas).
- `MicroStroke`: scale-adaptive brushes that sample about 48 points per radius at a power-of-two stride and interpolate between. `MicroUndo` stores the old int16 grid of every touched triangle.
- `MicroRenderer` (GL 3.3): Morton-ordered slots, a per-cluster level of detail under a triangle budget, crack-free edge snapping, and a full-resolution shading grid computed on the CPU (`MicroShader`). Measured on the M1 Pro: 13.4 ms per frame at 300M micro-triangles (3.3M drawn, 4x MSAA).
- It writes format 7 (`kProjectVersionWithoutMicro = 6` otherwise).

### 1.3 feat/hw-profile (agent-a0c0023b2779b0f0a)
- `HardwareProfile`, with `ProfileTier {Software, Laptop, Workstation, Monster}` mapped from **measurements** (triangle throughput, upload GB/s, dab ms, memory GB/s), never from GPU names. It produces `PerformanceSettings` (render triangle budget, VRAM budget, upload MB per frame, `microTriangleLimit`, threads, target fps) and a cached JSON profile.
- `FrameGovernor`: median-window hysteresis that adjusts the render triangle budget and the motion-proxy threshold. The app adds `Calibrator` and `GpuFrameTimer` (timer queries read a few frames later).

### 1.4 feat/zero-latency (agent-ad3056b5b213e928b)
- Ghost preview (`render/Ghost.h`): the dabs the solver hasn't placed yet are drawn as displacement in the vertex shader. `GhostDab::weight < 1` marks a provisional dab at the predicted head.
- `PenPredictor` (an alpha-beta-gamma filter, up to 25 ms ahead) and `App::StrokeSolver`, which places dabs on a thread.
- Partial uploads: `Renderer::prepareStrokeUpload` (packing on any thread) followed by `commitStrokeUpload` (on the GL thread). Loose cluster bounds are refined at the end of the stroke.

### 1.5 feat/pbr (agent-ac6c782628d136dbd)
- Also claims format 7, for the texture "pbr" key. It introduces `ProjectVersionNeed`, so a save writes the lowest format the content needs.

### 1.6 What v2 changes and what it keeps

| Kept unchanged | Extended | New |
|---|---|---|
| Mesh as the cage, the BVH over cage triangles, Multires, the CPU brushes on the cage (Move, Gizmo, Pose), the ghost for the CPU path, `MicroStore`, the tile codec, `HardwareProfile`, `FrameGovernor`, the GL 3.3 renderer | `MicroMesh` (adds `domain`), `MicroUndo` (adds SDT and keyed tiles), `PerformanceSettings` consumers, project format (7 reconciled, 8 added) | `BaseCage`, `MicroPatch`/`QuadMicro`, `PatchPageTable`, `TierConfig`, `StrokeDelta`, `MicroJournal`, `AsyncIo`, the RHI, GL 4.3 and Vulkan compute, `QuadMicroGpu` |

Triangle-domain micro levels from feat/micromesh stay fully supported and keep their CPU brushes and `MicroRenderer`. The v2 GPU pipeline is quad-only. A mesh that isn't all quads gets one Catmull-Clark level when converted (`micro_divide domain=quad`), using the existing Multires Divide. The level is real and undoable.

## 2. Scale model

Triangles T = 2 * Q * n^2, where Q is the number of quad patches and n the cells per patch side. With 16-cell pages, a patch has (n/16)^2 leaf pages and D = log2(n/16) + 1 quadtree depths (nodes per patch = (4^D - 1) / 3).

| Scene | Cage Q | n | Leaf pages | All nodes | Master int16 (17^2 per leaf) | Cold pages (416 B, with mips) | Shading for all leaves (1168 B) |
|---|---|---|---|---|---|---|---|
| 100M | 195k | 16 | 195k | 195k | 113 MB | 81 MB | 228 MB |
| 300M | 146k | 32 | 586k | 732k | 339 MB | 305 MB | 684 MB |
| 1B | 122k | 64 | 1.95M | 2.56M | 1.13 GB | 1.07 GB | 2.28 GB |

Per triangle: master about 1.13 B (plus 0.56 B with a mask), cold page about 1.07 B, full shading about 2.28 B, and node metadata under 0.07 B. A plain mesh at 1B would need about 60 GB.

## 3. Data structures (C++)

These headers follow the repository's conventions: GPL header, `#pragma once`, `namespace sf`, glm, explanatory prose comments, `Mesh`/`BVH`/`AABB` types, and no GL in core. They compile against main with feat/micromesh and feat/hw-profile merged (Phase 0).

### 3.1 `src/core/MicroPatch.h`

```cpp
// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Quad micro levels (Engine v2, docs/design/engine-v2.md): the per-patch record, the page layout and the height
// formats shared by the CPU (authoritative) and the GPU kernels (render/shaders/engine/*.glsl).
//
// A quad micro level puts an n x n grid of cells (n = 16..1024, a power of two) on every quad patch of the base
// cage (core/BaseCage.h). Point (i, j), 0 <= i, j <= n, sits at
//
//     P(i, j) = S(i, j) + h(i, j) * step * N(i, j)
//
// where S is the patch's Phong-tessellated bilinear surface (factor 0.75, like core/Micro.h for triangles), N is
// the normalised bilinear cage normal and h an int16. Nothing else is stored: no indices, no positions.
//
// Pages: the grid is cut into nodes of 16 x 16 cells (17 x 17 = 289 points, 512 triangles), arranged as a quadtree
// per patch. Depth D-1 holds the leaves (stride 1 cell), depth 0 the root (stride n / 16). A node at depth d
// samples every (n / 16 >> d)-th point: mips are point-subsampled, so a point stored at several depths has the same
// value at all of them (crack-free level-of-detail seams, section 5.6). Each node keeps its own 17 x 17 points,
// so the edges are duplicated between neighbouring nodes and patches. As in core/Micro.h they are kept equal: every
// edit computes them from the same inputs in the same order (the "seam rule"), and commits stitch them.
//
// Node index: patch * nodesPerPatch + depthOffset[d] + morton(x, y). Children are contiguous: the children of node
// m at depth d are depthOffset[d + 1] + (m << 2) + {0, 1, 2, 3}.
#pragma once

#include "core/MicroStore.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sf {

class BaseCage;
class Mesh;

// One quad patch as the GPU kernels read it (std430 `Patch`, 64 bytes). Corners are cage vertices in winding
// order at grid points (0,0), (n,0), (n,n), (0,n). Edge e runs from corner e to corner e + 1.
struct MicroPatch {
    static constexpr uint32_t kBoundary = ~0u;
    glm::uvec4 corners{0u};
    glm::uvec4 neighbors{kBoundary}; // per edge: neighbour patch << 2 | its edge (the shared edge runs the other way there)
    glm::uvec4 owners{kBoundary};    // per corner: patch << 2 | corner whose samples win at this corner (seam rule)
    glm::vec4 bound{0.0f};           // x: Phong bulge bound, y: lowest, z: highest displacement (world), w: mean cage edge

    static uint32_t neighborPatch(uint32_t v) { return v >> 2; }
    static uint32_t neighborEdge(uint32_t v) { return v & 3u; }
};
static_assert(sizeof(MicroPatch) == 64, "std430 Patch layout");

// The page layout of a quad micro level (one per level, same for every patch).
struct MicroPatchLayout {
    static constexpr uint32_t kTileCells = 16, kTilePoints = 17, kTileSamples = 289;
    static constexpr uint32_t kMaxDepths = 8; // n up to 16 << 7 = 2048
    uint32_t n = 16;               // cells per patch side
    uint32_t depths = 1;           // quadtree depths: log2(n / 16) + 1
    uint32_t leafTilesPerSide = 1; // n / 16
    uint32_t nodesPerPatch = 1;    // (4^depths - 1) / 3
    std::array<uint32_t, kMaxDepths> depthOffset{}; // first node of depth d within a patch

    static MicroPatchLayout forCells(uint32_t n); // n: a power of two >= 16
    uint32_t leafDepth() const { return depths - 1; }
    uint32_t strideCells(uint32_t depth) const { return (n / kTileCells) >> depth; }
    uint32_t nodesPerSide(uint32_t depth) const { return 1u << depth; }
    uint32_t nodeIndex(uint32_t patch, uint32_t depth, uint32_t x, uint32_t y) const;
    void decodeNode(uint32_t node, uint32_t& patch, uint32_t& depth, uint32_t& x, uint32_t& y) const;
    size_t leafTiles(size_t patches) const { return patches * size_t(leafTilesPerSide) * leafTilesPerSide; }
    size_t nodes(size_t patches) const { return patches * size_t(nodesPerPatch); }
    size_t microTriangles(size_t patches) const { return patches * size_t(n) * n * 2; }
};

uint32_t morton2(uint32_t x, uint32_t y);                   // 16 bits each
void mortonDecode2(uint32_t m, uint32_t& x, uint32_t& y);

// A cold page: 289 heights as 0-11 bits each above a per-page origin, optionally shifted (lossy) when the page's
// range needs more than 11 bits. 416 bytes, the GPU pool's slot (kColdStride = 104 words).
// Word 0: origin (int16, low half) | bits << 16 (4 bits) | shift << 20 (4 bits) | flags << 24.
struct ColdPage {
    static constexpr uint32_t kWords = 104;
    uint32_t words[kWords];
};
static_assert(sizeof(ColdPage) == 416);
// Packs 289 int16 points (row-major 17 x 17). `lossless` is false when a shift was needed (the display copy then
// differs from the master by less than 2^shift units). Deterministic: the GPU bake kernel encodes identically.
void packColdPage(const int16_t* points, ColdPage& out, bool* lossless = nullptr);
void unpackColdPage(const ColdPage& page, int16_t* points);

// The quad patch surface on the CPU: the same operations, in the same order, as the kernels' cageSurface().
struct QuadPatchEval {
    glm::vec3 p[4], nrm[4]; // corners in winding order
    uint8_t order[4];       // corners sorted by cage vertex index (seam rule)
    uint32_t n = 16;
    void set(const Mesh& cage, const MicroPatch& patch, uint32_t n);
    // S and N at grid point (i, j) (0..n). Bilinear weights come from integers ((n-i)(n-j) / n^2, exact in float).
    void eval(uint32_t i, uint32_t j, glm::vec3& surface, glm::vec3& normal) const;
    void evalUv(float u, float v, glm::vec3& surface, glm::vec3& normal) const; // continuous (raycast, resolve)
};

// The displacement data of a quad micro level: Engine v2's sibling of MicroMesh's triangle grids
// (MicroMesh::domain == MicroDomain::Quads, MicroMesh::quad).
struct QuadMicro {
    MicroPatchLayout layout;
    std::shared_ptr<const BaseCage> cage; // derived from the Mesh (rebuilt when its topology changes)
    float step = 1e-6f;                   // world units per height unit (int16 range = +-32767 * step)
    // Master heights, leaf tiles only (patch-major, Morton within a patch), 289 int16 each: in memory or out of core
    // (MicroStore's memory-mapped sparse file). The authority for save, undo, CLI and the GPU.
    MicroStore heights;
    std::vector<uint8_t> mask;            // empty, or 289 per leaf tile (0 = free, 255 = masked)
    // Per node (leaves and mips): lowest and highest height (culling, raycasts, LOD error). Kept by commits.
    std::vector<int16_t> nodeRange;       // 2 per node
    std::vector<uint64_t> dirtyLeaves;    // a bit per leaf tile changed since the page file / GPU last saw it
    uint64_t generation = 0;              // bumped by every commit (journal and project, section 7)
    uint64_t serial = 0;                  // identity for GPU caches (like MicroMesh::serial)

    size_t leafTileCount() const;
    size_t microTriangles() const;
    size_t bytes() const;
    int16_t* leaf(uint32_t patch, uint32_t x, uint32_t y);      // 289 points
    const int16_t* leaf(uint32_t patch, uint32_t x, uint32_t y) const;
    // A node's 289 points (a leaf's own, or subsampled from leaves for a mip): what its cold page encodes.
    void nodePoints(uint32_t node, int16_t* out) const;
};

} // namespace sf
```

Change to `core/Micro.h` (feat/micromesh), additive only:

```cpp
enum class MicroDomain : uint8_t { Triangles, Quads };
struct QuadMicro;
struct MicroMesh {
    // ...existing fields unchanged...
    MicroDomain domain = MicroDomain::Triangles;
    std::shared_ptr<QuadMicro> quad; // Quads: the level's data (disp/mask/triRange stay empty)
};
```

### 3.2 `src/core/BaseCage.h`

```cpp
// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The base cage of a quad micro level (docs/design/engine-v2.md): the SubTool's Mesh at its highest subdivision level,
// read as quad patches. The cage is the Mesh, so every existing tool that moves vertices (Move, Gizmo 3D, Pose,
// Transpose, the Multires lower levels) reshapes the micro surface, and the detail rides along in the cage's
// tangent frame (heights are along the interpolated cage normal). This class only adds what the micro level needs:
// patch records in Morton order, edge and corner adjacency, the triangle <-> patch maps for BVH hits, and bounds.
#pragma once

#include "core/BVH.h"
#include "core/Mesh.h"
#include "core/MicroPatch.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

class BaseCage {
public:
    static constexpr uint32_t kNone = ~0u;

    // Reads `mesh` as quads: every triangle must be half of a quad (Mesh::quadPartnerAt). Fails with `error` naming
    // the first one that isn't (micro_divide domain=quad adds a Catmull-Clark level to such meshes first).
    // Patches are numbered in Morton order of their centres over the mesh bounds.
    bool build(const Mesh& mesh, std::string& error);
    void clear();
    // Still describes `mesh`: same vertex and triangle counts, same quad pairs (checked by hash).
    bool matches(const Mesh& mesh) const;

    uint32_t patchCount() const { return uint32_t(patches_.size()); }
    const MicroPatch& patch(uint32_t p) const { return patches_[p]; }
    std::span<const MicroPatch> patches() const { return patches_; }
    // The mesh triangles of patch p: x holds corners 0, 1, 2 (in some rotation), y the other half.
    glm::uvec2 triangles(uint32_t p) const { return patchTris_[p]; }
    uint32_t patchOfTriangle(uint32_t t) const { return triPatch_[t]; }
    // A hit on mesh triangle t at barycentric weights (of t's corners) as patch coordinates in [0, 1]^2.
    glm::vec2 patchUv(uint32_t t, const glm::vec3& bary) const;
    // The patches around cage vertex v (CSR), and the cage vertex -> patch lists as flat arrays for the GPU
    // (cage_move.comp's normal pass).
    std::span<const uint32_t> vertexPatches(uint32_t v) const;
    const std::vector<uint32_t>& vertexPatchOffsets() const { return vpOffsets_; }
    const std::vector<uint32_t>& vertexPatchList() const { return vpList_; }

    // Bounds: per patch a sphere around its micro surface (the Phong bulge, measured on a 9 x 9 sample, and the
    // displacement range included) and MicroPatch::bound. Recomputed for the patches around `movedVerts` (all when
    // empty). The cage BVH (the SubTool's BVH over the mesh triangles) is refit by the caller as today.
    void refitBounds(const Mesh& mesh, const QuadMicro& micro, std::span<const uint32_t> movedVerts = {});
    std::span<const glm::vec4> spheres() const { return spheres_; }
    float meanEdge() const { return meanEdge_; }

    // The GPU's copy of cage vertices (std430 CageVertex: position, normal), for `verts` (all when empty).
    struct GpuVertex {
        glm::vec4 position, normal;
    };
    void packVertices(const Mesh& mesh, std::span<const uint32_t> verts, std::vector<GpuVertex>& out) const;

    uint64_t topologyHash() const { return topologyHash_; }
    size_t memoryBytes() const;

private:
    std::vector<MicroPatch> patches_;
    std::vector<glm::uvec2> patchTris_;
    std::vector<uint32_t> triPatch_;   // per mesh triangle
    std::vector<uint8_t> triRotation_; // per mesh triangle: which patch corner its corner 0 is, and which half
    std::vector<uint32_t> vpOffsets_, vpList_;
    std::vector<glm::vec4> spheres_;
    float meanEdge_ = 0.0f;
    uint32_t vertices_ = 0, triangleCount_ = 0;
    uint64_t topologyHash_ = 0;
};

// The micro surface point (world) of a quad micro level at patch coordinates (u, v), heights interpolated on the
// micro triangle holding it, and its geometric normal. The quad counterpart of microPointAt (core/Micro.h).
glm::vec3 quadMicroPointAt(const Mesh& cage, const QuadMicro& micro, uint32_t patch, glm::vec2 uv, glm::vec3* normal = nullptr);
// Ray against a quad micro level: the cage BVH, widened by each patch's displacement range, then the patch's node
// quadtree (nodeRange) down to the leaf cells. hit.triangle is the cage triangle; position and normal are on the
// micro surface. surfaceRaycast (core/Micro.h) calls this for MicroDomain::Quads.
bool quadMicroRaycast(const Mesh& cage, const QuadMicro& micro, const BVH& bvh, const glm::vec3& origin, const glm::vec3& dir,
                      RayHit& hit);

} // namespace sf
```

### 3.3 `src/core/PatchPageTable.h`

```cpp
// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Residency of a quad micro level's pages (docs/design/engine-v2.md, sections 5-7): which nodes the GPU holds, in
// which pool slot, what was last drawn, and what to load or evict next. Plain CPU logic (no GPU calls), driven by the
// renderer each frame and by the asynchronous I/O completions, so tests run it headless.
//
// Also the keys everything else uses: TileKey (a 64-bit Morton key of patch, kind, level and position) and
// TileMap, an open-addressing hash map on those keys (undo tiles, stroke delta tiles, the journal index).
#pragma once

#include "core/MicroPatch.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace sf {

// [63:40] patch (24 bits) | [39:36] kind | [35:32] level or depth | [31:0] morton(x, y) (16 bits each).
using TileKey = uint64_t;
enum class TileKind : uint8_t { Node = 0, StrokeDelta = 1, Leaf = 2 };
inline TileKey makeTileKey(uint32_t patch, TileKind kind, uint32_t level, uint32_t x, uint32_t y) {
    return (uint64_t(patch & 0xFFFFFFu) << 40) | (uint64_t(kind) << 36) | (uint64_t(level & 15u) << 32) | morton2(x, y);
}
inline uint32_t tileKeyPatch(TileKey k) { return uint32_t(k >> 40); }
inline TileKind tileKeyKind(TileKey k) { return TileKind((k >> 36) & 15u); }
inline uint32_t tileKeyLevel(TileKey k) { return uint32_t(k >> 32) & 15u; }
// Spreads the Morton bits (splitmix64 finaliser). Spatially near keys stay near in sorted order, but not in the hash.
inline uint64_t tileKeyHash(TileKey k) {
    k ^= k >> 30; k *= 0xBF58476D1CE4E5B9ull; k ^= k >> 27; k *= 0x94D049BB133111EBull; return k ^ (k >> 31);
}

// Open-addressing (linear probing) map from TileKey to V. Capacity a power of two, kept at most 70% full.
// Iteration order is slot order. sortedKeys() gives Morton order for files and the journal.
template <class V>
class TileMap {
public:
    V* find(TileKey key);
    const V* find(TileKey key) const;
    V& insert(TileKey key, bool* inserted = nullptr); // default-constructs a new value
    bool erase(TileKey key);                          // backward-shift deletion (no tombstones)
    void clear();
    size_t size() const { return size_; }
    std::vector<TileKey> sortedKeys() const;
    template <class F> void forEach(F&& f) const; // f(TileKey, const V&)
    size_t bytes() const { return keys_.capacity() * sizeof(TileKey) + values_.capacity() * sizeof(V); }

private:
    static constexpr TileKey kEmpty = ~TileKey(0);
    std::vector<TileKey> keys_;
    std::vector<V> values_;
    size_t size_ = 0;
    void grow();
};

// GPU page table entry (one uint32 per node, mirrored in the GPU's pageTable buffer):
// slot (22 bits) | state << 22 (2 bits) | flags << 24.
enum class PageState : uint8_t { Absent = 0, Cold = 1, Loading = 2 };
constexpr uint32_t kPageSlotMask = 0x3FFFFFu;
constexpr uint32_t kPageFlagLossy = 1u << 24;   // the cold page is shifted: the GPU may not bake strokes into it
constexpr uint32_t kPageFlagMask = 1u << 25;    // the node has a mask page (same slot in the mask pool)
constexpr uint32_t kPageFlagShadeStale = 1u << 26;

struct NodeRequest {
    uint32_t node = 0;
    float priority = 0.0f; // the projected error that wanted it (pixels): larger loads first
};

// What a pool holds: fixed slots of `slotBytes`, allocated once (never grown, so never out of memory).
struct PagePool {
    uint32_t slotBytes = 0;
    uint32_t slots = 0;
    std::vector<uint32_t> freeSlots;
    uint32_t used() const { return slots - uint32_t(freeSlots.size()); }
};

class PatchPageTable {
public:
    struct Config {
        uint32_t coldSlots = 0, shadeSlots = 0, maskSlots = 0;
        uint32_t evictAfterFrames = 8;     // a node unused this long may go (Laptop 8, Workstation 600)
        float frustumMargin = 0.2f;        // nodes outside the frustum grown by this fraction are evicted first
        uint32_t frustumGraceFrames = 120; // ... after this long
        float minScreenPx = 0.0f;          // nodes whose cells project smaller than this (too fine for the view) go first
        bool pinRoots = true;              // depth-0 nodes stay resident: never a hole (and never out of memory)
        uint32_t uploadBytesPerFrame = 8u << 20;
    };

    void reset(const MicroPatchLayout& layout, uint32_t patches, const Config& config);
    const Config& config() const { return config_; }

    // --- Per frame (main thread) ---
    void beginFrame(uint64_t frame);
    // The nodes the GPU drew `latency` frames ago (its draw records, read back), for the LRU.
    void noteDrawn(std::span<const uint32_t> nodes, uint64_t frame);
    // The GPU's requests (deduplicated on the GPU). Queued by priority; nodes already loading are skipped.
    void request(std::span<const NodeRequest> requests);
    // Nodes a stroke will touch (predicted pen path, mirrored dabs): loaded ahead of LOD requests and pinned until
    // unpinned (the residency gate, section 6.4).
    void prefetchAndPin(std::span<const uint32_t> nodes);
    void unpinAll();
    // Picks loads that fit this frame's budget, evicting where the pool is full. `evicted` gets the slots freed (the
    // GPU page table is patched with Absent for them before any load reuses a slot).
    struct Load {
        uint32_t node, slot;
    };
    void schedule(std::vector<Load>& loads, std::vector<uint32_t>& evicted);
    // An I/O completion (any thread, lock-free queue). The renderer then uploads and patches the GPU page table.
    void completeLoad(uint32_t node);
    void drainCompletions(std::vector<uint32_t>& ready);

    // Shrink to `fraction` of the cold pool (memory pressure, power, another application's VRAM): evicts the least
    // valuable nodes. Pinned and root nodes stay.
    void shrink(float fraction);

    PageState state(uint32_t node) const;
    uint32_t entry(uint32_t node) const { return entries_[node]; } // the GPU page table word
    struct Stats {
        uint64_t resident = 0, loading = 0, pinned = 0, loadsThisFrame = 0, evictionsThisFrame = 0, misses = 0;
        uint64_t coldBytes = 0;
    };
    Stats stats() const;
    size_t memoryBytes() const;

private:
    struct NodeState {
        uint32_t lastUsed = 0;   // frame (low 32 bits)
        uint16_t pins = 0;
        uint8_t clockBit = 0;    // CLOCK second chance
        uint8_t depth = 0;
    };
    MicroPatchLayout layout_;
    Config config_;
    std::vector<uint32_t> entries_; // GPU page table mirror
    std::vector<NodeState> nodes_;
    PagePool cold_, shade_, mask_;
    std::vector<uint32_t> slotNode_; // cold slot -> node (for eviction)
    uint32_t clockHand_ = 0;
    std::vector<NodeRequest> queue_;
    uint64_t frame_ = 0;
    // Completions from the I/O thread: a fixed ring (single producer per I/O backend, single consumer).
    std::vector<uint32_t> completionRing_;
    std::atomic<uint32_t> completionHead_{0}, completionTail_{0};
    bool evictOne(uint32_t& slot);
};

} // namespace sf
```

### 3.4 `src/core/TierConfig.h`

```cpp
// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine v2's tier and its concrete settings (docs/design/engine-v2.md, section 4). Built on the hardware profile
// (core/HardwareProfile.h: measured tiers Software / Laptop / Workstation / Monster and PerformanceSettings), the GPU
// backend's capabilities (handed in by the renderer: core has no GPU code), the storage probe and the power state.
// Like the profile, the mapping uses measurements and memory sizes, never GPU names, and is deterministic: the same
// inputs give the same TierConfig (tests).
#pragma once

#include "core/HardwareProfile.h"

#include <cstdint>
#include <string>

namespace sf {

class CommandRegistry;

enum class EngineTier : uint8_t { Laptop, Workstation };
const char* engineTierName(EngineTier tier); // "laptop", "workstation"

enum class ComputeApi : uint8_t { None, GL43, Vulkan }; // None: CPU brushes (GL 3.3 / macOS GL / sculptforge-cli)
enum class IoPath : uint8_t { Mmap, ThreadPool, IoUring, IoRing, DirectStorage, DispatchIo };
enum class StrokeShading : uint8_t { AtRelease, Interval, EveryFrame };
enum class AoMode : uint8_t { Off, HorizonAtRelease, Horizon, HorizonPlusRayQuery };
enum class ThermalState : uint8_t { Nominal, Fair, Serious, Critical };

// What the render backend can do (render/rhi fills it).
struct BackendCaps {
    ComputeApi compute = ComputeApi::None;
    bool unifiedMemory = false;          // device-local memory is host-visible and host-cached (Apple Silicon, iGPUs)
    bool hostVisibleDeviceLocal = false; // Resizable BAR on a discrete GPU
    bool asyncComputeQueue = false;
    bool transferQueue = false;
    bool rayQuery = false;               // VK_KHR_ray_query (not on MoltenVK)
    bool memoryBudget = false;           // VK_EXT_memory_budget / NVX meminfo: live budget
    bool timestamps = false;
    uint32_t maxStorageBuffers = 0;      // per compute stage; the kernels need 8
    uint32_t maxSharedBytes = 0;         // the kernels need 16 KiB
    uint64_t deviceLocalBytes = 0;
    uint32_t displayRefreshHz = 60;
};

// The disk the swap folder and page file live on (measured once, cached with the hardware profile).
struct StorageProbe {
    double readGBps = 0.0;    // 64 x 1 MiB direct reads, median
    double writeGBps = 0.0;
    uint32_t directAlign = 4096; // O_DIRECT alignment (statx STATX_DIOALIGN where available)
    bool ssd = true;
};

struct PowerState {
    bool onBattery = false;
    int batteryPercent = -1;   // -1 unknown
    bool lowPowerMode = false; // macOS Low Power Mode, Windows battery saver
    ThermalState thermal = ThermalState::Nominal;
    bool memoryPressure = false; // PSI / dispatch memory pressure / QueryMemoryResourceNotification
    bool operator==(const PowerState&) const = default;
};

struct TierConfig {
    EngineTier tier = EngineTier::Laptop;
    ComputeApi compute = ComputeApi::None;
    bool zeroCopy = false; // pools in host-visible device-local memory: I/O decodes straight into them
    std::string reason;    // why, in words (Preferences > Performance, engine_info)

    // --- GPU memory (fixed pools, allocated once in 64 MiB chunks: never grown, may shrink) ---
    uint64_t gpuCacheBytes = 0;      // cold + shade + mask pools together
    uint32_t coldSlots = 0;          // 416 B each
    uint32_t shadeSlots = 0;         // 1168 B each
    uint32_t sdtSlots = 0;           // stroke delta tiles, 1168 B each (plus as many scratch)
    uint32_t maxDrawRecords = 0;     // nodes drawn per frame (x 512 triangles)
    uint64_t readbackRingBytes = 0, stagingRingBytes = 0;
    bool residentAllLeaves = false;  // Workstation: keep every leaf resident when it fits

    // --- CPU memory and storage ---
    bool masterOutOfCore = false;    // MicroStore's memory-mapped file (microWantsOutOfCore)
    uint64_t pageFileCacheBytes = 0; // what the I/O layer may keep of the page file
    IoPath io = IoPath::ThreadPool;
    bool streamFromDisk = false;     // pages come from the page file (else everything is decoded from the master)

    // --- Level of detail and eviction ---
    uint64_t activeTriangleBudget = 0; // seeds the governor (renderTriangleBudget)
    float targetPx = 1.0f;             // micro edge on screen the LOD aims for
    uint32_t evictAfterFrames = 8;
    float evictFrustumMargin = 0.2f;
    float evictMinScreenPx = 0.35f;

    // --- Frame and strokes ---
    int targetFps = 60;
    int framesInFlight = 2;
    double brushSliceMs = 3.0;         // GPU time reserved for the stroke's kernels (render is elastic, the stroke isn't)
    StrokeShading strokeShading = StrokeShading::AtRelease;
    uint32_t shadeInterval = 4;        // frames, for StrokeShading::Interval
    AoMode ao = AoMode::HorizonAtRelease;
    bool asyncCompute = false;
    uint32_t maxDabsPerFrame = 64;
    uint32_t readbackInterval = 4;     // frames between SDT snapshots read back during a stroke

    // --- Undo and journal ---
    size_t undoStrokeRamBytes = 500u << 10; // above this a stroke's undo data lives in the journal
    size_t undoTotalRamBytes = size_t(256) << 20;
    uint32_t journalSyncMs = 250;

    // The tier and settings for this machine and backend, before the power state.
    static TierConfig derive(const HardwareProfile& profile, const BackendCaps& caps, const StorageProbe& storage);
    // The same settings adjusted for battery, heat and memory pressure (section 4.4). Never raises a budget above derive()'s.
    TierConfig underPower(const PowerState& power) const;
};

// The engine settings in effect (thread-safe snapshot, like currentPerformanceSettings).
TierConfig currentTierConfig();
void setCurrentTierConfig(const TierConfig& config);
PowerState currentPowerState();
void setPowerState(const PowerState& power); // the app's probe (SDL_GetPowerInfo, thermal, memory pressure) calls this

// engine_info, set_engine_override (registered from registerEngineCommands).
void registerTierCommands(CommandRegistry& registry);

} // namespace sf
```

## 4. Run-time tier detection and the adaptive policy

### 4.1 Inputs
1. `HardwareProfile` (feat/hw-profile): measured tier, `GpuKind` (Discrete, Integrated, Unified, Software), VRAM and RAM, upload GB/s, triangles/s, dab ms, threads.
2. `BackendCaps` from the RHI. In Vulkan these come from `VkPhysicalDeviceMemoryProperties`: UMA means every DEVICE_LOCAL heap is also HOST_VISIBLE, and a host-visible device-local type on a discrete GPU means ReBAR. Queue families give async compute and transfer; extensions give ray query and memory budget. GL 4.3 caps come from `GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS` and `GL_MAX_COMPUTE_SHARED_MEMORY_SIZE`. `displayRefreshHz` comes from `SDL_GetCurrentDisplayMode`.
3. `StorageProbe`: a new calibration stage of 64 direct 1 MiB reads in the swap folder, under 300 ms, cached in `hardware_profile.json` under a new key. Older caches ignore the key.
4. `PowerState`: polled every 2 s.
   - Battery state from `SDL_GetPowerInfo`.
   - Thermal state:
     - macOS: `NSProcessInfo.thermalState` (small Objective-C++ file in `app/`).
     - Linux: `/sys/class/thermal` trip proximity and `/sys/devices/system/cpu/cpufreq` scaling ratio.
     - Windows: none reliable. The governor infers throttling when GPU time per triangle drifts up more than 25% for 10 s at constant work.
   - Memory pressure:
     - Linux: `/proc/pressure/memory`, avg10 above 10.
     - macOS: `dispatch_source` `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE`.
     - Windows: `QueryMemoryResourceNotification`.

### 4.2 Tier rule (deterministic)

```
compute = caps.compute
if compute == None or profile.tier == Software:
    tier = Laptop, compute = None (CPU brushes, GL 3.3 rendering of quad micro)
else if profile.tier in {Workstation, Monster}
        and gpuKind == Discrete and vramMB >= 12288 and ramMB >= 32768
        and calibration.uploadGBps >= 10:          # PCIe 4 x16 measures ~20-25, PCIe 5 ~40+
    tier = Workstation
else:
    tier = Laptop
# Unified-memory machines (Apple Silicon Max/Ultra, big APUs) reach Workstation with >= 64 GB RAM and the same
# measured thresholds (feat/hw-profile 159df8a); 2/3 of RAM is the VRAM pool. Ray query only where the backend has it.
```

### 4.3 Settings per tier (derive)

| Setting | Laptop (2-4 GB VRAM or UMA, 8-16 GB RAM) | Workstation (RTX 4080-5090, 64 GB+) |
|---|---|---|
| GPU cache (cold + shade + mask) | min(0.45 x vramBudgetMB, 3 GB), at least 1 GB. On UMA: min(0.2 x RAM, 3 GB) | min(0.6 x vramBudgetMB, 24 GB) |
| Cold slots | 70% of the cache / 416 B (1.2 GB = 2.9M slots: all of 1B fits on 4 GB cards, a part fits on 2 GB/8 GB UMA) | All nodes when they fit (`residentAllLeaves`) |
| Shade slots | 4 x maxDrawRecords (draw set plus margin) | All leaves when they fit (2.3 GB at 1B) |
| SDT slots | 16k (19 MB) | 64k (75 MB) |
| Draw records | 4k (2M triangles) | 48k (24M triangles) |
| Active triangle budget | 2M (the governor moves it within 1-4M) | `profile.renderTriangleBudget` (typically 15-30M) |
| Target | 60 fps (120 on ProMotion when on AC and thermals are nominal) | `displayRefreshHz`, up to 144 |
| Frames in flight | 2 | 3 |
| Brush slice | 3.0 ms | 1.2 ms (heights) plus 1.0 ms (shading) |
| Stroke shading | `AtRelease`, with draft normals from the SDT gradient (section 6.5) | `EveryFrame` |
| AO | `HorizonAtRelease` | `HorizonPlusRayQuery` when available, else `Horizon` |
| Readback interval | 4 frames | 2 frames |
| Master | In memory if under 1/4 of RAM, else out of core (feat/micromesh's rule) | In memory |
| I/O | UMA: mmap/dispatch_io straight into pools. Linux: io_uring. Windows: IoRing | io_uring with registered buffers / IoRing; DirectStorage optional (section 7.3) |
| Stream from disk | When the cold pool can't hold every node | Cold start only |
| Eviction | After 8 frames unused, frustum margin 20% for 2 s, screen-size eviction below 0.35 px | After 600 frames, no screen-size eviction |

### 4.4 Power-state adjustments (`underPower`)

| State | Changes (applied on top of `derive`, never raising anything) |
|---|---|
| On battery or low-power mode | targetFps = min(60, display), budget x 0.6, stroke shading `AtRelease`, AO `HorizonAtRelease` (ray query off), readback every 8 frames, journal sync 1000 ms, prefetch ring off, background repack and page-file compaction paused |
| Thermal Fair | budget x 0.85 |
| Thermal Serious | targetFps 45, budget x 0.5, Workstation drops to `Interval` shading (every 4 frames), CPU commit threads halved |
| Thermal Critical | targetFps 30, budget x 0.35, `AtRelease`, AO off during strokes |
| Memory pressure | `PatchPageTable::shrink(0.75)` per signal (down to roots), page-file cache dropped, undo RAM capped at 64 MB (older strokes spill to the journal), shade cache cleared outside the view |

Idle frames already render only on events. When nothing moves, nothing is drawn, regardless of tier.

### 4.5 Frame governor (extends feat/hw-profile's)

`FrameGovernor` keeps doing what it does today: it adjusts the render triangle budget on median windows with hysteresis. Engine v2 wraps it in `EngineGovernor` (`core/EngineGovernor.h`), which adds a **quality ladder** for knobs that triangles alone don't capture. It also adds one principle: **render is elastic, the stroke is not.** Dabs are never dropped or coarsened to save time.

```
struct LadderRung { budgetScale, aoMode, strokeShading, msaa, renderScale };
ladder (Workstation start = 0, Laptop start = 3):
  0: 1.00, RayQuery,  EveryFrame,  4x, 1.00
  1: 0.80, RayQuery,  EveryFrame,  4x, 1.00
  2: 0.80, Horizon,   EveryFrame,  4x, 1.00
  3: 0.70, Horizon,   Interval(2), 4x, 1.00
  4: 0.60, HorizonAR, AtRelease,   4x, 1.00   # Laptop default (draft)
  5: 0.50, HorizonAR, AtRelease,   2x, 1.00
  6: 0.40, HorizonAR, AtRelease,   2x, 0.85
  7: 0.30, Off,       AtRelease,   1x, 0.75   # floor

each frame:
  sample = {cpuMs, gpuMs(per-pass timestamps), drawn, scene, moving, stroking}
  if stroking: reserve = brushSliceMs measured (median of 10)
               frameBudget = targetMs - reserve        # rendering gets what the stroke leaves
  governor.addFrame(sample)                            # existing class: triangle budget
  every window (20 frames):
     cost = median(max(cpu, gpu))
     if cost > 1.0 * frameBudget and governor.budget() at its min for this rung: rung++ (settle 5 frames)
     if cost < 0.6 * frameBudget for 3 windows and rung > tierStart: rung--   # hysteresis
  tau (targetPx) feedback for the GPU LOD (single pass, no iteration):
     tau *= clamp(sqrt(drawnTriangles / budget), 0.8, 1.25), clamped to [0.5, 8] px
```

Workstation lands at rung 0-1 on a 4080 at 1440p and Laptop at rung 4. A laptop on AC with headroom may climb to rung 3 (shading every other frame). Per-pass GPU timestamps come from extending `GpuFrameTimer` (feat/hw-profile) to labelled ranges.

## 5. GPU compute pipeline

### 5.1 Backend decision matrix

Scores run from 1 (poor) to 5 (best).

| Criterion (weight) | (a) GL 3.3 + CPU everywhere | (b) GL 4.3 compute on Win/Linux; macOS CPU or Metal | (c) Vulkan (MoltenVK on macOS) behind an RHI, next to GL |
|---|---|---|---|
| Brush at 1B within frame (x3) | 1: CPU commit of tens of millions of points per dab is impossible at 60 Hz. feat/micromesh's adaptive brush caps quality | 4 on Win/Linux, 1 on macOS (CPU) or 4 with a separate Metal backend | 5 everywhere. Apple UMA is the best laptop target |
| macOS (Laptop tier's core platform) (x3) | 2 | 1 (CPU) or 3 (a third shading language and backend) | 4 (MoltenVK: no geometry shaders, no ray query, some limits; see 5.4) |
| Async compute and transfer, timeline sync, memory budget (x2) | 1 | 2 (fences, no queues, no budget query beyond NVX/ATI) | 5 |
| RT cavity/AO on Workstation (x1) | 1 | 1 | 5 (`VK_KHR_ray_query`) |
| Zero-copy UMA, ReBAR, explicit residency (x2) | 1 | 2 (persistent mapping via ARB_buffer_storage, implicit residency) | 5 |
| Port risk to today's renderer (x3) | 5 (none) | 5 (adds to it) | 2 (2.3k lines plus app GL usage; see 5.3) |
| Time to first user value (x2) | 5 | 4 (weeks) | 2 (months) |
| Maintenance cost (x2) | 5 | 4 (one backend) / 2 (with Metal) | 3 (two backends during transition, then one plus a GL fallback) |
| CI and headless (x1) | 4 (Xvfb + Mesa) | 4 (Mesa llvmpipe has GL 4.5 compute) | 5 (lavapipe, truly headless, no window) |
| **Weighted total** | 54 | 63 (CPU macOS) | **80** |

**Recommendation: (c) as the destination, reached through (b).** Shaders are written once (section 5.5) and run on both.
- (b) costs little and fits today's renderer: the micro renderer already lives in GL and compute dispatches share its buffers and context. It brings GPU sculpting to Windows and Linux (including the Asahi M1 Pro dev box, which has Mesa GL 4.6) months before Vulkan is ready. It also proves the data layout, the SDT authority protocol and the kernels against CPU reference kernels in CI on llvmpipe.
- (c) is needed for macOS compute (GL 4.1 has none and is deprecated), for UMA zero-copy, for async queues at 144 Hz and for ray query. A separate Metal backend was rejected because it means a third shading language and a second explicit API with no Linux/Windows benefit.
- GL 3.3 plus CPU brushes stays permanently: software renderers, old GPUs, broken Vulkan drivers, and `sculptforge-cli` (which has no GPU and uses the CPU kernels).

### 5.2 Migration phases (the current app never breaks)

| Phase | What ships | Default | Gate to next |
|---|---|---|---|
| M0 | Phase 0 merges, scaffolding (section 10) | Unchanged | CI green, no behaviour change |
| M1 | Quad micro on CPU: BaseCage, QuadMicro, SDT on CPU, journal, format 8, GL 3.3 quad micro renderer (texture buffers, as feat/micromesh) | Everywhere, CPU brushes | `test_quad_sculpt`, format round trips, 300M on M1 Pro at 60 fps orbit |
| M2 | **RHI extraction**: `Renderer` written against `rhi::Device` with one `RhiGL` backend (GL 3.3 core, plus 4.3 features where present). Pure refactor | Everywhere | Golden images bit-exact against M1 for 40 scenes (`renderToImage`), all `.sfui` UI tests |
| M3 | GL 4.3 compute: engine kernels, GPU strokes, ID buffer, GPU LOD for quad micro | Windows/Linux when `GL_ARB_compute_shader` and 8 SSBOs are present; else M1 | Kernel-against-CPU parity tests on llvmpipe, perf targets (section 8) on reference Linux and Windows machines |
| M4 | `RhiVulkan` (volk, VMA, SPIR-V from glslang at build time), ImGui Vulkan backend, swapchain via SDL3 | Opt-in `--backend=vulkan` / preference | Golden-image parity with GL within 1 LSB at 99.9% of pixels, every UI test on lavapipe, a 30-minute soak |
| M5 | macOS default flips to Vulkan (MoltenVK bundled) | macOS | Perf targets on M2 Air 16 GB, notarised bundle |
| M6 | Linux, then Windows, flip to Vulkan; GL 4.3 remains the fallback when Vulkan init fails or a driver is blocklisted | All | Two releases with no backend-specific crash among the top 10 |

Runtime selection order: `--backend` / preference, then Vulkan (from M6, or M5 on macOS), then GL 4.3, then GL 3.3. A failure at any stage falls through to the next, and the reason is logged.

### 5.3 Risk analysis: porting the 2.3k-line renderer

| Risk | Where | Likelihood / impact | Mitigation |
|---|---|---|---|
| Geometry shader (UV, normal map, flat) | `kMeshGS` | High / high: MoltenVK has no geometry shaders | Replace with primitive-ID fetch in the fragment shader (`gl_PrimitiveID` into an SSBO of per-corner UVs and tangents) plus barycentrics: `VK_KHR_fragment_shader_barycentric` where present (verify on MoltenVK), else un-indexed vertices for textured meshes only. Do it in M2 on GL first (GL 4.x path) so Vulkan starts without a geometry shader |
| Texture buffers | primData, groupData, uvData, tangentData, splats, feat/micromesh chunks | Medium / medium: Metal texel-buffer size limits; feat/micromesh already chunks at 2^26 | SSBOs in the RHI (GL 4.3+ / Vulkan); texture buffers only in the GL 3.3 backend |
| `gl_VertexID` base-vertex quirk | `checkVertexIdBase` | Low / medium | Vulkan's `gl_VertexIndex` includes vertexOffset: remove the quirk path in the Vulkan shader prelude |
| Multi-draw with base vertex | `drawClusters` | Medium / medium | `vkCmdDrawIndexedIndirect` with a CPU-written indirect buffer (multiDrawIndirect feature; MoltenVK emulates it). GPU-written for micro |
| 64 program variants, MSAA counts, wireframe | `programs_[64]` | Medium / medium: pipeline explosion and first-use hitches | Pipeline keys (features, samples, polygon mode, blend). Lazy creation with a `VkPipelineCache` persisted in the config dir, pre-warmed for the default matcap at startup on a thread |
| Implicit sync of `glBufferSubData` / `MapBufferRange` | uploads, dyntopo patches, partial stroke uploads | High / high: races cause garbage vertices | RHI staging ring with per-frame fences. feat/zero-latency's two-phase `prepareStrokeUpload`/`commitStrokeUpload` maps onto it directly |
| Offscreen paths | `renderToImage`, `beginCapture`, thumbnails (LightBox, Thumbnails.cpp, BrushGallery), Bench | High / medium: about 60 direct `gl::` calls in `src/app` | Move every app GL use behind `Renderer`/RHI handles (`rhi::TextureId` given to ImGui as `ImTextureID`). Done in M2 so M4 doesn't touch app code |
| Depth range and Y flip | Camera, shaders | Medium / low | A per-backend clip fix matrix (not a global `GLM_FORCE_DEPTH_ZERO_TO_ONE`, since GL coexists), negative-height viewport |
| sRGB, MSAA resolve, matcap colour | View targets | Medium / low | Golden-image tests per backend |
| OOM behaviour | `outOfMemory` per slot | Low / medium | Vulkan allocations are explicit (VMA): better than GL's late `GL_OUT_OF_MEMORY` |
| Packaging | macOS bundle, Flatpak, AppImage, AUR, Windows | Medium / medium | Bundle MoltenVK, the loader and the ICD JSON on macOS (notarised). Linux and Windows use the system loader. KosmicKrisp (LunarG's Vulkan-on-Metal) is an alternative to evaluate (verify status) |
| Threading | GL is single-context | Low / medium | Vulkan lets StrokeSolver record transfer commands: an opportunity, not required |
| Licences | volk (MIT), VMA (MIT), glslang (BSD/Apache-2.0): GPL-3 compatible | Low | THIRD_PARTY_NOTICES entries |

The RHI is narrow on purpose: Buffer, Texture, Sampler, Pipeline (graphics/compute, from a shader-set id), BindGroup, CommandList (draw, drawIndexedIndirect, dispatch, dispatchIndirect, copy, barrier), Fence or timeline, StagingRing, ReadbackRing and TimestampQuery. The CPU logic in `Renderer` stays shared across backends: `UploadOrder`, cluster bounds, culling, dyntopo patches and proxies.

### 5.4 Frame graph per tier

The stroke's kernels run **before** the frame's draw. Dabs therefore appear in the frame that shows them. Their surface points come from the previous frame's ID buffer, re-intersected against current heights.

```
Frame t (GPU tier, while stroking)                       Laptop          Workstation
 1  upload: per-frame UBO (camera, spline keys, dabs),   gfx queue       transfer queue (async)
    page-table patch list, streamed pages (staging)
 2  page_table_patch.comp      (node, entry) pairs       compute         compute
 3  dab_place.comp             spline -> dabs (1 WG)     compute         compute
 4  vis_resolve.comp           dab centres via ID[t-1]   compute         compute
 5  brush_gather.comp x2       patches, then SDT tiles   compute         compute
 6  brush_eval.comp            dabs -> scratch (atomic)  compute         compute
 7  brush_accumulate + seam    scratch -> SDT            compute         compute
 8  normals_curvature.comp     touched visible nodes     (at release)    compute (async queue, overlaps 10)
 9  lod_select + lod_neighbors indirect draw args        compute         compute
10  draw micro nodes (indirect) + meshes -> colour, depth, ID (R32UI)    graphics        graphics
11  horizon AO / ray-query AO  touched nodes             (at release)    async compute
12  copy: SDT snapshot (every N), draw records, requests -> readback ring
13  UI (ImGui)
```

Barriers:

| Producer to consumer | GL 4.3 | Vulkan (sync2) |
|---|---|---|
| compute write to compute read (SSBO) | `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)` | COMPUTE_SHADER / SHADER_STORAGE_WRITE to COMPUTE_SHADER / SHADER_STORAGE_READ |
| compute write to indirect args | `GL_COMMAND_BARRIER_BIT` | to DRAW_INDIRECT / INDIRECT_COMMAND_READ |
| compute write to vertex pulling | `GL_SHADER_STORAGE_BARRIER_BIT` | to VERTEX_SHADER / SHADER_STORAGE_READ |
| draw (ID target) to resolve read next frame | `GL_TEXTURE_FETCH_BARRIER_BIT` | COLOR_ATTACHMENT_OUTPUT to COMPUTE_SHADER / SHADER_SAMPLED_READ |
| compute to readback copy | `GL_BUFFER_UPDATE_BARRIER_BIT`, then `glFenceSync` | to COPY / TRANSFER_READ, timeline signal |
| async queue to graphics (Workstation) | n/a | timeline semaphore wait at VERTEX_SHADER. Pools use `VK_SHARING_MODE_CONCURRENT` (no ownership transfers) |

The readback ring has `framesInFlight + 1` slots of host-visible, host-cached memory. The CPU polls fences without blocking at frame start: `glClientWaitSync(…, 0)` or `vkGetSemaphoreCounterValue`. GL 4.3 without `ARB_buffer_storage` falls back to `glGetBufferSubData` after the fence has signalled.

### 5.5 Buffers, layouts and shader conventions

Kernels are written once in "SF-GLSL" and compiled two ways:
- As Vulkan GLSL 450, by glslang at build time (`SF_VULKAN` defined, SPIR-V embedded).
- As GL 4.3 GLSL, by the driver (the loader prepends `#version 430` and the prelude).

Constraints: at most 8 SSBOs per kernel (`GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS` is 8 in GL 4.3), no 16/64-bit types, no subgroup operations in the baseline, std430 throughout, constants in one UBO at binding 8 with vec4-aligned members only, and samplers at bindings 9 and up.

| Buffer | Layout (std430) | Size at 1B |
|---|---|---|
| `cageVerts` | `{vec4 position; vec4 normal;}` per cage vertex | 122k x 32 B = 4 MB |
| `patches` | `MicroPatch` (64 B) | 8 MB |
| `pageTable` | uint per node: slot:22, state:2, flags:8 | 10 MB |
| `nodeInfo` | uvec2 per node: (hmin, hmax int16), (oct8, oct8, cos8, flags8) | 20 MB |
| `coldPool` | 104 words per slot | pool size |
| `maskPool` | 73 words per slot (optional, parallel to cold slots) | pool size |
| `shadePool` | 292 words per slot: oct 8+8, curvature 8, AO 8 | pool size |
| `sdtDir` | per patch: (kMaxLevels + 1) uints = level mask, first SDT slot per level | 122k x 48 B = 6 MB |
| `sdtPool`, `sdtScratch` | 292 ints per slot (17 x 17 fixed point, 1/256 height unit) | SDT slots x 1168 B each |
| `drawRecords` | uvec4: node, patch, d \| x << 4 \| y << 18, neighbour depths 4 x 4 bits \| flags << 16 | records x 16 B |
| ID target | R32UI: record << 9 \| primitive (512 per node) | 1440p: 15 MB (60 MB at 4x MSAA) |
| Draw mesh | one shared 16 x 16 grid: 1,536 uint16 indices | 3 KB total: **0 index bytes per patch** |

### 5.6 The kernels

#### 5.6.1 Shared declarations: `render/shaders/engine/common.glsl`

```glsl
// SculptForge Engine v2 - declarations shared by every engine kernel.
// Built as Vulkan GLSL 450 (SF_VULKAN, glslang -> SPIR-V) and as OpenGL 4.3 GLSL (the loader prepends #version 430).
// Rules: <= 8 storage buffers per kernel, no 16/64-bit types, no subgroup operations, vec4-aligned uniform members.
#ifdef SF_VULKAN
#define SF_BUF(b) layout(std430, set = 0, binding = b)
#define SF_UBO(b) layout(std140, set = 0, binding = b)
#define SF_TEX(b) layout(set = 0, binding = b)
#else
#define SF_BUF(b) layout(std430, binding = b)
#define SF_UBO(b) layout(std140, binding = b)
#define SF_TEX(b) layout(binding = b)
#endif

const uint kNone = 0xFFFFFFFFu;
const uint kTilePoints = 17u, kTileSamples = 289u;
const uint kSlotStride = 292u;  // SDT, scratch, shade: 289 used, padded to 16 bytes
const uint kColdStride = 104u;  // cold page: header + up to 11 bits x 289
const uint kMaskStride = 73u;
const uint kMaxLevels = 11u;    // SDT levels: stride 1 .. 1024 cells
const uint kSlotMask = 0x3FFFFFu;
const uint kStateCold = 1u;

struct CageVertex { vec4 position; vec4 normal; };
struct Patch {
    uvec4 corners;    // cage vertices at (0,0) (n,0) (n,n) (0,n)
    uvec4 neighbors;  // per edge: patch << 2 | its edge, kNone on a boundary
    uvec4 owners;     // per corner: patch << 2 | corner that owns the corner's samples
    vec4 bound;       // x: Phong bulge, y: lowest, z: highest displacement (world), w: mean cage edge
};
struct Dab {
    vec4 centerRadius;    // world centre, radius
    vec4 normalStrength;  // area normal, signed strength (pressure, invert applied)
    vec4 planeFocal;      // area centre (plane brushes), focal shift
    uvec4 meta;           // x: brush kind, y: SDT level (log2 stride), z: mirror, w: flags (1 = front faces only)
};
const uint kBrushStandard = 0u, kBrushSmooth = 2u, kBrushInflate = 4u;

SF_UBO(8) Engine {
    uvec4 uGrid;       // x: n (cells per patch side), y: leaf depth, z: nodes per patch, w: patch count
    uvec4 uDepthOff0;  // first node of depths 0..3 within a patch
    uvec4 uDepthOff1;  // depths 4..7
    vec4  uScale;      // x: step (world per height unit), y: 1/256, z: Phong factor 0.75, w: SDT clamp (height units)
    mat4  uViewProj;
    mat4  uInvViewProj;
    vec4  uEye;        // xyz: eye (perspective), w: 1 = orthographic
    vec4  uViewport;   // x, y, w, h (framebuffer pixels)
    vec4  uLod;        // x: px per world unit at distance 1, y: tau (px), z: near, w: unused
    uvec4 uCounts;     // x: frame, y: dab count, z: max records, w: max requests
    vec4  uPlanes[6];  // frustum planes (xyz normal inwards, w)
    vec4  uViewDir;    // camera forward (front-face tests)
    Dab   uDabs[64];
};

uint part1by1(uint x) {
    x &= 0xFFu; x = (x | (x << 4)) & 0x0F0Fu; x = (x | (x << 2)) & 0x3333u; x = (x | (x << 1)) & 0x5555u; return x;
}
uint compact1by1(uint x) {
    x &= 0x5555u; x = (x | (x >> 1)) & 0x3333u; x = (x | (x >> 2)) & 0x0F0Fu; x = (x | (x >> 4)) & 0x00FFu; return x;
}
uint morton2(uint x, uint y) { return part1by1(x) | (part1by1(y) << 1); }
uint depthOffset(uint d) { return d < 4u ? uDepthOff0[d] : uDepthOff1[d - 4u]; }
uint nodeIndex(uint patch, uint d, uint x, uint y) { return patch * uGrid.z + depthOffset(d) + morton2(x, y); }

// core/Sculpt.cpp brushFalloff, operation for operation (test engine_falloff_matches_core).
float brushFalloff(float dist, float radius, float focal) {
    float t = clamp(1.0 - dist / radius, 0.0, 1.0);
    if (focal != 0.0) t = pow(t, exp2(2.0 * clamp(focal, -1.0, 1.0)));
    return t * t * (3.0 - 2.0 * t);
}

vec2 octEncode(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 p = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return p;
}
```

#### 5.6.2 Height, delta and surface access: `render/shaders/engine/micro_access.glsl`

Bindings 0-5 are the same in every kernel that includes this file.

```glsl
SF_BUF(0) readonly buffer CageVerts { CageVertex cageVerts[]; };
SF_BUF(1) readonly buffer Patches   { Patch patches[]; };
SF_BUF(2) readonly buffer PageTable { uint pageTable[]; };
SF_BUF(3) readonly buffer ColdPool  { uint coldPool[]; };
#ifdef SF_SDT_DIR_WRITABLE
SF_BUF(4) buffer SdtDir { uint sdtDir[]; };
#else
SF_BUF(4) readonly buffer SdtDir { uint sdtDir[]; };
#endif
#ifdef SF_SDT_WRITABLE
SF_BUF(5) buffer SdtPool { int sdtPool[]; };
#else
SF_BUF(5) readonly buffer SdtPool { int sdtPool[]; };
#endif

// Point p (0..288) of the cold page in `slot`, in height units. The CPU's unpackColdPage does the same.
int coldSample(uint slot, uint p) {
    uint base = slot * kColdStride;
    uint h = coldPool[base];
    int origin = int(h << 16) >> 16;
    uint bits = (h >> 16) & 15u, shift = (h >> 20) & 15u;
    if (bits == 0u) return origin;
    uint bit = p * bits, w = bit >> 5, o = bit & 31u;
    uint v = coldPool[base + 1u + w] >> o;
    if (o + bits > 32u) v |= coldPool[base + 2u + w] << (32u - o);
    return origin + int((v & ((1u << bits) - 1u)) << shift);
}

// The stored (master) height at leaf grid point (i, j) of `patch`: from the finest resident node holding the point.
// Mips are point-subsampled, so every depth holding the point gives the same value. Where the finest resident node
// doesn't hold it (a finer node is absent), bilinear between its points: an approximation used for shading only;
// strokes never run on such points (residency gate, section 6.4). Roots are pinned, so the loop always returns.
float storedHeight(uint patch, uint i, uint j) {
    uint D = uGrid.y;
    for (int d = int(D); d >= 0; --d) {
        uint sh = D - uint(d), side = 1u << uint(d);
        uint gi = i >> sh, gj = j >> sh;
        uint nx = min(gi >> 4, side - 1u), ny = min(gj >> 4, side - 1u);
        uint e = pageTable[nodeIndex(patch, uint(d), nx, ny)];
        if (((e >> 22) & 3u) != kStateCold) continue;
        uint slot = e & kSlotMask, a = gi - (nx << 4), b = gj - (ny << 4);
        uint s = 1u << sh, fi = i & (s - 1u), fj = j & (s - 1u);
        float h00 = float(coldSample(slot, b * 17u + a));
        if ((fi | fj) == 0u) return h00;
        uint a1 = min(a + 1u, 16u), b1 = min(b + 1u, 16u);
        float h10 = float(coldSample(slot, b * 17u + a1));
        float h01 = float(coldSample(slot, b1 * 17u + a));
        float h11 = float(coldSample(slot, b1 * 17u + a1));
        vec2 f = vec2(fi, fj) / float(s);
        return mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
    }
    return 0.0;
}

// One SDT sample (a, b in 0..m, m = n >> level) of the block starting at slot `first`.
int sdtSample(uint first, uint m, uint a, uint b) {
    if (m <= 16u) return sdtPool[first * kSlotStride + b * 17u + a];
    uint tiles = m >> 4;
    uint tx = min(a >> 4, tiles - 1u), ty = min(b >> 4, tiles - 1u);
    return sdtPool[(first + morton2(tx, ty)) * kSlotStride + (b - (ty << 4)) * 17u + (a - (tx << 4))];
}

// The open stroke's delta at leaf point (i, j): fixed point (1/256 height unit), the sum over the patch's SDT levels
// of an integer bilinear interpolation (separable, floor shifts). StrokeDelta::fineDelta on the CPU is the same
// integer function, so the GPU's display and the CPU's commit agree bit for bit (section 6.2).
int strokeDelta(uint patch, uint i, uint j) {
    uint dir = patch * (kMaxLevels + 1u), mask = sdtDir[dir];
    int sum = 0;
    while (mask != 0u) {
        uint l = uint(findLSB(mask));
        mask &= mask - 1u;
        uint first = sdtDir[dir + 1u + l], m = uGrid.x >> l, s = 1u << l;
        uint a = i >> l, b = j >> l, fa = i & (s - 1u), fb = j & (s - 1u);
        uint a1 = min(a + 1u, m), b1 = min(b + 1u, m);
        int v0 = (sdtSample(first, m, a, b) * int(s - fa) + sdtSample(first, m, a1, b) * int(fa)) >> l;
        int v1 = (sdtSample(first, m, a, b1) * int(s - fa) + sdtSample(first, m, a1, b1) * int(fa)) >> l;
        sum += (v0 * int(s - fb) + v1 * int(fb)) >> l;
    }
    return sum;
}

// The Phong-tessellated cage surface S and direction N at leaf grid point (i, j) (0..n).
// Seam rule: weights come from integers (exact in float, n is a power of two) and the corners are summed in
// ascending cage vertex order, so the two patches beside an edge produce bit-identical points on it.
void cageSurface(uint patch, uint i, uint j, out vec3 S, out vec3 N) {
    Patch pt = patches[patch];
    uint n = uGrid.x;
    float inv = 1.0 / float(n * n);
    float w[4] = float[4](float((n - i) * (n - j)) * inv, float(i * (n - j)) * inv,
                          float(i * j) * inv, float((n - i) * j) * inv);
    uint c[4] = uint[4](pt.corners.x, pt.corners.y, pt.corners.z, pt.corners.w);
    uint o[4] = uint[4](0u, 1u, 2u, 3u);
    // 4-element sorting network on vertex index
    #define SF_CSWAP(x, y) if (c[o[y]] < c[o[x]]) { uint t_ = o[x]; o[x] = o[y]; o[y] = t_; }
    SF_CSWAP(0, 1) SF_CSWAP(2, 3) SF_CSWAP(0, 2) SF_CSWAP(1, 3) SF_CSWAP(1, 2)
    precise vec3 f = vec3(0.0), nn = vec3(0.0);
    for (int k = 0; k < 4; ++k) {
        f += cageVerts[c[o[k]]].position.xyz * w[o[k]];
        nn += cageVerts[c[o[k]]].normal.xyz * w[o[k]];
    }
    precise vec3 pull = vec3(0.0);
    for (int k = 0; k < 4; ++k) {
        vec3 P = cageVerts[c[o[k]]].position.xyz, Nk = cageVerts[c[o[k]]].normal.xyz;
        pull += Nk * (w[o[k]] * dot(f - P, Nk));
    }
    S = f - pull * uScale.z;
    float len = length(nn);
    N = len > 1e-20 ? nn / len : vec3(0.0, 0.0, 1.0);
}

// The current micro surface (master + open stroke) at leaf point (i, j).
float currentHeight(uint patch, uint i, uint j) {
    return storedHeight(patch, i, j) + float(strokeDelta(patch, i, j)) * uScale.y;
}
vec3 microPoint(uint patch, uint i, uint j) {
    vec3 S, N;
    cageSurface(patch, i, j, S, N);
    return S + N * (currentHeight(patch, i, j) * uScale.x);
}

// Leaf point (i, j), at most one edge outside the patch, as the same point of the patch across that edge.
// Edge e runs from corner e to e + 1; the neighbour's edge f runs the other way (consistent winding). Points outside
// both ranges (beyond a corner) and boundary edges clamp: one-sided differences there.
void crossEdge(inout uint patch, inout ivec2 ij) {
    int n = int(uGrid.x);
    bool outI = ij.x < 0 || ij.x > n, outJ = ij.y < 0 || ij.y > n;
    if (!outI && !outJ) return;
    if (outI && outJ) { ij = clamp(ij, ivec2(0), ivec2(n)); return; }
    int e = ij.y < 0 ? 0 : (ij.x > n ? 1 : (ij.y > n ? 2 : 3));
    uint nb = patches[patch].neighbors[e];
    if (nb == kNone) { ij = clamp(ij, ivec2(0), ivec2(n)); return; }
    int t, depth;
    if (e == 0)      { t = ij.x;     depth = -ij.y; }
    else if (e == 1) { t = ij.y;     depth = ij.x - n; }
    else if (e == 2) { t = n - ij.x; depth = ij.y - n; }
    else             { t = n - ij.y; depth = -ij.x; }
    int f = int(nb & 3u), tp = n - t;
    patch = nb >> 2;
    if (f == 0)      ij = ivec2(tp, depth);
    else if (f == 1) ij = ivec2(n - depth, tp);
    else if (f == 2) ij = ivec2(n - tp, n - depth);
    else             ij = ivec2(depth, n - tp);
}
```

#### 5.6.3 Visibility-buffer patch-ID resolve: `vis_resolve.comp`

```glsl
// Resolves query pixels (dab centres placed by dab_place.comp, the cursor, the area-normal stencil) against the ID
// target: which node and micro triangle, the exact surface point (ray against that triangle at current heights,
// strokes included), its patch coordinates and normal. One thread per query.
layout(local_size_x = 64) in;
#include "common.glsl"
#include "micro_access.glsl"

struct Query {
    vec4 pixel;     // in: xy framebuffer pixel; z: dab index (or -1); w: unused
    vec4 position;  // out: world point, w = 1 hit, 0 miss
    vec4 normal;    // out: geometric normal of the micro triangle
    uvec4 where;    // out: patch, i * 65536, j * 65536 (leaf grid, 16.16 fixed), node
};
SF_BUF(6) readonly buffer Records { uvec4 records[]; };
SF_BUF(7) buffer Queries { uint queryCount; uint pad0, pad1, pad2; Query queries[]; };
#ifdef SF_MSAA
SF_TEX(9) uniform usampler2DMS uId;
#define ID_FETCH(p) texelFetch(uId, p, 0).r
#else
SF_TEX(9) uniform usampler2D uId;
#define ID_FETCH(p) texelFetch(uId, p, 0).r
#endif

void main() {
    uint q = gl_GlobalInvocationID.x;
    if (q >= queryCount) return;
    ivec2 px = ivec2(queries[q].pixel.xy);
    uint id = ID_FETCH(px);
    if (id == kNone) { queries[q].position = vec4(0.0); return; }
    uvec4 rec = records[id >> 9];
    uint tri = id & 511u, cell = tri >> 1, second = tri & 1u;
    uint d = rec.z & 15u, nx = (rec.z >> 4) & 0x3FFFu, ny = rec.z >> 18;
    uint s = 1u << (uGrid.y - d);                       // node stride in leaf cells
    uvec2 p00 = (uvec2(nx, ny) * 16u + uvec2(cell & 15u, cell >> 4)) * s;
    // the shared grid's split: (00, 10, 11) and (00, 11, 01)
    uvec2 pb = second == 0u ? p00 + uvec2(s, 0u) : p00 + uvec2(s, s);
    uvec2 pc = second == 0u ? p00 + uvec2(s, s) : p00 + uvec2(0u, s);
    uint patch = rec.y;
    vec3 A = microPoint(patch, p00.x, p00.y), B = microPoint(patch, pb.x, pb.y), C = microPoint(patch, pc.x, pc.y);

    vec2 ndc = (vec2(px) + 0.5 - uViewport.xy) / uViewport.zw * 2.0 - 1.0;
    vec4 a = uInvViewProj * vec4(ndc, 0.0, 1.0), b = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 o = a.xyz / a.w, dir = normalize(b.xyz / b.w - o);
    // Moller-Trumbore. The ID says the ray hits this triangle: clamp the barycentrics against rounding.
    vec3 e1 = B - A, e2 = C - A, pv = cross(dir, e2);
    float det = dot(e1, pv);
    if (abs(det) < 1e-30) { queries[q].position = vec4(0.0); return; }
    float invDet = 1.0 / det;
    vec3 tv = o - A;
    float u = clamp(dot(tv, pv) * invDet, 0.0, 1.0);
    vec3 qv = cross(tv, e1);
    float v = clamp(dot(dir, qv) * invDet, 0.0, 1.0 - u);
    vec3 hit = A + e1 * u + e2 * v;
    vec2 ij = vec2(p00) * (1.0 - u - v) + vec2(pb) * u + vec2(pc) * v;
    queries[q].position = vec4(hit, 1.0);
    queries[q].normal = vec4(normalize(cross(e1, e2)), 0.0);
    queries[q].where = uvec4(patch, uint(ij.x * 65536.0), uint(ij.y * 65536.0), rec.x);
}
```

`dab_place.comp` (one workgroup, not listed) walks the Catmull-Rom spline of this frame's pen keys (screen position, pressure, time; about 8 per frame) at the brush spacing. It writes query pixels, and after the resolve the `uDabs` entries for the next pass: world centre, area normal from a 3 x 3 query stencil at radius/2, radius from pressure, SDT level `l = clamp(floor(log2(radiusCells / 48)), 0, log2 n)` (feat/micromesh's 48 samples per radius), and mirrored copies (world-space mirror of each resolved centre). The provisional dab at the predicted head (`GhostDab::weight < 1` in feat/zero-latency) goes into a display-only SDT scratch that is cleared each frame and never committed.

#### 5.6.4 Brush dispatch over affected micro-tiles: `brush_gather.comp`, `brush_eval.comp`, `brush_accumulate.comp`

```glsl
// brush_gather.comp - two entry points (compiled twice):
//  SF_GATHER_PATCHES: one thread per patch. Tests the patch's sphere against every dab; for each dab that reaches it,
//    allocates the patch's SDT block at the dab's level if it has none (only this thread touches this patch's
//    directory: no contention), and appends (patch, dab) pairs.
//  SF_GATHER_TILES: one workgroup per pair. Tests the block's SDT tiles against the dab and appends (slot, dab) work
//    records; each slot also goes once into this frame's touched list (for accumulate and readback).
#define SF_SDT_DIR_WRITABLE
#include "common.glsl"
#include "micro_access.glsl"            // 0-5 (cold pool unused here)
SF_BUF(6) buffer Work {
    uint pairCount, workCount, touchedCount, slotTop;   // counters (slotTop: SDT pool bump allocator)
    uvec4 accumulateArgs;                               // indirect dispatch (x = touched slots)
    uvec4 evalArgs;                                     // indirect dispatch (x = work records)
    uvec4 tileArgs;                                     // indirect dispatch (x = pairs)
    uvec4 items[];                                      // pairs (patch, dab), then work records (slot, dab, patch, level|x|y)
};
SF_BUF(7) buffer Touched { uint touchedFlag[]; };       // per SDT slot: frame stamp; then touched list after maxSlots
const uint kPairBase = 0u, kWorkBase = 1u << 16;        // item ranges (sized by TierConfig::sdtSlots)

uint blockTiles(uint level) { uint m = uGrid.x >> level; return m <= 16u ? 1u : (m >> 4) * (m >> 4); }

#ifdef SF_GATHER_PATCHES
layout(local_size_x = 64) in;
// patch spheres: centre and radius from the cage corners + bound (CPU BaseCage::refitBounds keeps bound current)
void main() {
    uint p = gl_GlobalInvocationID.x;
    if (p >= uGrid.w) return;
    Patch pt = patches[p];
    vec3 c = 0.25 * (cageVerts[pt.corners.x].position.xyz + cageVerts[pt.corners.y].position.xyz +
                     cageVerts[pt.corners.z].position.xyz + cageVerts[pt.corners.w].position.xyz);
    float r = 0.0;
    for (int k = 0; k < 4; ++k) r = max(r, distance(c, cageVerts[pt.corners[k]].position.xyz));
    r += pt.bound.x + max(abs(pt.bound.y), abs(pt.bound.z));
    uint dir = p * (kMaxLevels + 1u);
    for (uint k = 0u; k < uCounts.y; ++k) {
        Dab dab = uDabs[k];
        if (distance(c, dab.centerRadius.xyz) > r + dab.centerRadius.w) continue;
        uint level = dab.meta.y;
        if ((sdtDir[dir] & (1u << level)) == 0u) {
            uint tiles = blockTiles(level);
            uint first = atomicAdd(slotTop, tiles);
            // pool exhaustion: the CPU sees slotTop past capacity, commits the stroke so far and restarts it (6.3)
            sdtDir[dir + 1u + level] = first;
            sdtDir[dir] |= 1u << level;
        }
        uint idx = atomicAdd(pairCount, 1u);
        items[kPairBase + idx] = uvec4(p, k, 0u, 0u);
        atomicMax(tileArgs.x, idx + 1u);
    }
}
#endif

#ifdef SF_GATHER_TILES
layout(local_size_x = 64) in;
void main() {
    uint pair = gl_WorkGroupID.x;
    if (pair >= pairCount) return;
    uvec4 it = items[kPairBase + pair];
    uint p = it.x, k = it.y;
    Dab dab = uDabs[k];
    uint level = dab.meta.y, m = uGrid.x >> level;
    uint side = m <= 16u ? 1u : m >> 4, tiles = side * side;
    uint first = sdtDir[p * (kMaxLevels + 1u) + 1u + level];
    float hReach = max(abs(patches[p].bound.y), abs(patches[p].bound.z)) + patches[p].bound.x;
    for (uint t = gl_LocalInvocationID.x; t < tiles; t += 64u) {
        uint tx = compact1by1(t), ty = compact1by1(t >> 1);
        // the tile's region in leaf points, its cage-surface corners, and a sphere around them
        uint span = (m <= 16u ? m : 16u) << level;
        uint i0 = tx * span, j0 = ty * span;
        vec3 S00, S11, N0;
        cageSurface(p, i0, j0, S00, N0);
        cageSurface(p, min(i0 + span, uGrid.x), min(j0 + span, uGrid.x), S11, N0);
        vec3 c = 0.5 * (S00 + S11);
        float r = 0.5 * distance(S00, S11) * 1.4142 + hReach;  // half diagonal (both diagonals bounded) + reach
        if (distance(c, dab.centerRadius.xyz) > r + dab.centerRadius.w) continue;
        uint slot = first + t;
        uint idx = atomicAdd(workCount, 1u);
        items[kWorkBase + idx] = uvec4(slot, k, p, level | (tx << 4) | (ty << 18));
        atomicMax(evalArgs.x, idx + 1u);
        if (atomicExchange(touchedFlag[slot], uCounts.x) != uCounts.x) {  // first touch this frame
            uint tIdx = atomicAdd(touchedCount, 1u);
            touchedFlag[(1u << 22) + tIdx] = slot;                         // touched list after the flags
            atomicMax(accumulateArgs.x, tIdx + 1u);
        }
    }
}
#endif
```

```glsl
// brush_eval.comp - one workgroup per (SDT tile, dab) work record. Evaluates the dab at the tile's samples (the
// dab's stride: SDT level l samples every 2^l leaf points) against the surface as it stood at the start of the
// frame (master + SDT), and adds the change, in fixed point, to the tile's scratch with integer atomics: the sum
// doesn't depend on the order dabs and workgroups run in. Mask is not applied here: it multiplies the fine delta
// (section 6.2), so masked edges stay sharp at any stride.
layout(local_size_x = 64) in;
#include "common.glsl"
#include "micro_access.glsl"                 // 0-5, SDT read-only (the frame's starting state)
SF_BUF(6) readonly buffer Work {
    uint pairCount, workCount, touchedCount, slotTop;
    uvec4 accumulateArgs, evalArgs, tileArgs;
    uvec4 items[];
};
SF_BUF(7) buffer Scratch { int scratch[]; };
const uint kWorkBase = 1u << 16;

void main() {
    uint w = gl_WorkGroupID.x;
    if (w >= workCount) return;
    uvec4 it = items[kWorkBase + w];
    uint slot = it.x, patch = it.z, level = it.w & 15u, tx = (it.w >> 4) & 0x3FFFu, ty = it.w >> 18;
    Dab dab = uDabs[it.y];
    uint m = uGrid.x >> level, s = 1u << level;
    uint per = min(m, 16u) + 1u;            // samples per side in this tile
    float r = dab.centerRadius.w, strength = dab.normalStrength.w;
    float toFixed = 256.0 / uScale.x;       // world -> 1/256 height units
    for (uint k = gl_LocalInvocationID.x; k < per * per; k += 64u) {
        uint a = k % per, b = k / per;
        uint i = ((tx << 4) + a) << level, j = ((ty << 4) + b) << level;
        if (i > uGrid.x || j > uGrid.x) continue;
        vec3 S, N;
        cageSurface(patch, i, j, S, N);
        float h = currentHeight(patch, i, j);
        vec3 P = S + N * (h * uScale.x);
        float dist = distance(P, dab.centerRadius.xyz);
        if (dist >= r) continue;
        if ((dab.meta.w & 1u) != 0u && dot(N, uViewDir.xyz) > 0.0) continue;  // front faces only
        float f = brushFalloff(dist, r, dab.planeFocal.w);
        float dh = 0.0;                                                        // world units along N
        if (dab.meta.x == kBrushStandard) {
            // core/Sculpt.cpp: d = areaNormal * (sign * s * r * 0.06 * f); its component along N
            dh = dot(dab.normalStrength.xyz, N) * strength * r * 0.06 * f;
        } else if (dab.meta.x == kBrushInflate) {
            dh = strength * r * 0.05 * f;
        } else if (dab.meta.x == kBrushSmooth) {
            // core/Sculpt.cpp: (avg - x) * min(1, s * f), on heights in the cage's tangent frame, at this level's stride
            float sum = 0.0;
            ivec2 off[4] = ivec2[4](ivec2(int(s), 0), ivec2(-int(s), 0), ivec2(0, int(s)), ivec2(0, -int(s)));
            for (int q = 0; q < 4; ++q) {
                uint pq = patch;
                ivec2 ij = ivec2(i, j) + off[q];
                crossEdge(pq, ij);
                sum += currentHeight(pq, uint(ij.x), uint(ij.y));
            }
            dh = (sum * 0.25 - h) * uScale.x * min(1.0, abs(strength) * f);
        }
        int dq = int(round(dh * toFixed));
        if (dq != 0) atomicAdd(scratch[slot * kSlotStride + b * 17u + a], dq);
    }
}
```

```glsl
// brush_accumulate.comp - one workgroup per SDT slot touched this frame: SDT += scratch (clamped to what the
// separable interpolation can hold at this level without overflow), scratch = 0.
// brush_seam.comp (next dispatch, same shape) then copies, for samples on patch edges and corners, the owner
// patch's value (MicroPatch::owners; the lower patch index owns an edge), so duplicated samples are equal even at
// extraordinary vertices, where the two sides' stencils differ.
layout(local_size_x = 64) in;
#include "common.glsl"
SF_BUF(5) buffer SdtPool { int sdtPool[]; };
SF_BUF(6) readonly buffer Work { uint pairCount, workCount, touchedCount, slotTop; uvec4 a0, a1, a2; uvec4 items[]; };
SF_BUF(7) buffer Scratch { int scratch[]; };
SF_BUF(4) readonly buffer Touched { uint touchedFlag[]; };
void main() {
    uint w = gl_WorkGroupID.x;
    if (w >= touchedCount) return;
    uint slot = touchedFlag[(1u << 22) + w];
    int limit = int(uScale.w) * 256;   // TierConfig: +-2^(30 - level) fixed point, the CPU's StrokeDelta::kLimit
    for (uint k = gl_LocalInvocationID.x; k < kTileSamples; k += 64u) {
        uint o = slot * kSlotStride + k;
        int v = scratch[o];
        if (v == 0) continue;
        sdtPool[o] = clamp(sdtPool[o] + v, -limit, limit);
        scratch[o] = 0;
    }
}
```

**Move-on-cage: `cage_move.comp`.** Move with a radius of at least 1.5 cage edges deforms the cage, and the detail rides in tangent space because heights are along the cage normal. Below that radius, Move works on heights as in feat/micromesh. Pose and the Gizmo always act on the cage through existing Mesh code.

```glsl
// cage_move.comp - three entry points:
//  SF_MOVE_CAPTURE (press): per cage vertex, its position and the falloff weight for each mirror.
//  SF_MOVE_APPLY (every frame): position = captured + sum over mirrors of weight * mirrored grab delta.
//  SF_CAGE_NORMALS: per moved vertex, the normalised sum of its patches' normals (cross of the quad's diagonals).
// At release the moved range (positions and normals, <= 32 B x cage vertices) is read back into Mesh::positions /
// normals: the CPU then holds exactly what the GPU drew (UndoRecord positions channel, as for any Move stroke).
layout(local_size_x = 64) in;
#include "common.glsl"
SF_BUF(0) buffer CageVerts { CageVertex cageVerts[]; };
SF_BUF(1) readonly buffer Patches { Patch patches[]; };
SF_BUF(2) buffer Grab { vec4 grabBase[]; };          // per vertex: captured position, then 2 x vec4 weights (8 mirrors)
SF_BUF(3) readonly buffer VertPatchOffsets { uint vpOffsets[]; };
SF_BUF(4) readonly buffer VertPatchList { uint vpList[]; };
SF_BUF(5) readonly buffer Moved { uint movedCount; uint movedVerts[]; };  // vertices with any weight (built on press)

void main() {
    uint t = gl_GlobalInvocationID.x;
#if defined(SF_MOVE_CAPTURE)
    if (t >= uint(cageVerts.length())) return;
    vec3 p = cageVerts[t].position.xyz;
    grabBase[t * 3u] = vec4(p, 0.0);
    vec4 w0 = vec4(0.0), w1 = vec4(0.0);
    for (uint m = 0u; m < min(uCounts.y, 8u); ++m) {   // uDabs[m]: the grab dab of mirror m
        float wt = brushFalloff(distance(p, uDabs[m].centerRadius.xyz), uDabs[m].centerRadius.w, uDabs[m].planeFocal.w);
        if (m < 4u) w0[m] = wt; else w1[m - 4u] = wt;
    }
    grabBase[t * 3u + 1u] = w0;
    grabBase[t * 3u + 2u] = w1;
#elif defined(SF_MOVE_APPLY)
    if (t >= movedCount) return;
    uint v = movedVerts[t];
    vec3 p = grabBase[v * 3u].xyz;
    for (uint m = 0u; m < min(uCounts.y, 8u); ++m) {
        float wt = m < 4u ? grabBase[v * 3u + 1u][m] : grabBase[v * 3u + 2u][m - 4u];
        p += uDabs[m].normalStrength.xyz * wt;          // normalStrength.xyz: the mirror's grab delta this frame
    }
    cageVerts[v].position = vec4(p, 1.0);
#elif defined(SF_CAGE_NORMALS)
    if (t >= movedCount) return;
    uint v = movedVerts[t];
    vec3 n = vec3(0.0);
    for (uint k = vpOffsets[v]; k < vpOffsets[v + 1u]; ++k) {
        uvec4 c = patches[vpList[k]].corners;
        n += cross(cageVerts[c.z].position.xyz - cageVerts[c.x].position.xyz,
                   cageVerts[c.w].position.xyz - cageVerts[c.y].position.xyz);
    }
    float len = length(n);
    cageVerts[v].normal = vec4(len > 1e-30 ? n / len : vec3(0.0, 0.0, 1.0), 0.0);
#endif
}
```

After `SF_CAGE_NORMALS`, the patches around moved vertices get `kPageFlagShadeStale` on their nodes and a refreshed `bound` (approximate on the GPU, exact from `BaseCage::refitBounds` at release). Their resident drawn nodes are reshaded according to the tier's stroke-shading policy.

#### 5.6.5 Deferred normal and curvature recompute: `normals_curvature.comp`

```glsl
// One workgroup per node to shade: the stroke's touched visible nodes (Workstation: every frame; Laptop: at
// release), nodes just loaded, and nodes whose cage moved. Positions of the node's 17 x 17 points plus a one-point
// apron (from neighbouring nodes and across patch edges) go into shared memory; normals are central differences,
// curvature the shading's cavity term (the mesh shader's: neighbour average minus the point, along the normal,
// over the mean edge, stored sign(c) sqrt|c|). The AO byte is kept (the AO passes write it).
layout(local_size_x = 16, local_size_y = 16) in;
#include "common.glsl"
#include "micro_access.glsl"                    // 0-5
struct ShadeItem { uvec4 v; };                  // node, patch, d | x << 4 | y << 18, shade slot
SF_BUF(6) readonly buffer ShadeList { uint shadeCount; uint p0, p1, p2; ShadeItem items[]; };
SF_BUF(7) buffer ShadePool { uint shadePool[]; };

shared vec3 sPos[19 * 19];

void main() {
    uint w = gl_WorkGroupID.x;
    if (w >= shadeCount) return;
    uvec4 it = items[w].v;
    uint patch = it.y, d = it.z & 15u, nx = (it.z >> 4) & 0x3FFFu, ny = it.z >> 18, slot = it.w;
    int s = int(1u << (uGrid.y - d));
    uint lid = gl_LocalInvocationIndex;
    for (uint k = lid; k < 361u; k += 256u) {
        int a = int(k % 19u) - 1, b = int(k / 19u) - 1;
        uint pq = patch;
        ivec2 ij = (ivec2(int(nx), int(ny)) * 16 + ivec2(a, b)) * s;
        crossEdge(pq, ij);
        sPos[k] = microPoint(pq, uint(ij.x), uint(ij.y));
    }
    memoryBarrierShared();
    barrier();
    for (uint k = lid; k < kTileSamples; k += 256u) {
        uint a = k % 17u + 1u, b = k / 17u + 1u, c = b * 19u + a;
        vec3 p = sPos[c];
        vec3 dx = sPos[c + 1u] - sPos[c - 1u], dy = sPos[c + 19u] - sPos[c - 19u];
        vec3 n = cross(dx, dy);
        float len = length(n);
        n = len > 1e-30 ? n / len : vec3(0.0, 0.0, 1.0);
        vec3 avg = 0.25 * (sPos[c + 1u] + sPos[c - 1u] + sPos[c + 19u] + sPos[c - 19u]);
        float edge = 0.25 * (length(dx) + length(dy));     // mean edge (central differences span two)
        float curv = edge > 1e-30 ? clamp(dot(avg - p, n) / edge, -1.0, 1.0) : 0.0;
        float sc = sign(curv) * sqrt(abs(curv));
        vec2 o = octEncode(n) * 0.5 + 0.5;
        uint idx = slot * kSlotStride + k;
        uint ao = shadePool[idx] & 0xFF000000u;
        shadePool[idx] = uint(o.x * 255.0 + 0.5) | (uint(o.y * 255.0 + 0.5) << 8) |
                         (uint((sc * 0.5 + 0.5) * 255.0 + 0.5) << 16) | ao;
    }
}
```

Shade mips: a coarse node's shading is the box-filtered (renormalised) shading of its children where they are resident, so distant views don't alias. Heights stay point-subsampled for crack-free geometry. `horizon_ao.comp` (8 directions x 8 steps in the node's tangent-space heightfield, same workgroup shape) writes the AO byte. Workstation's `rt_ao.comp` traces one ray per pixel per frame with `rayQueryEXT` against a BLAS of per-patch AABBs (procedural). Candidate AABB hits are intersected by marching the patch's root and depth-1 pages, results are accumulated temporally with reprojection, and the cost is capped at about 1 ms by the governor (ladder rungs 0-1).

#### 5.6.6 LOD selection and cluster culling: `lod_select.comp`, `lod_neighbors.comp`

```glsl
// One workgroup per patch: every node of the patch decides locally, in parallel, whether it is part of the cut.
//   wantSplit(N) = depth < leaf && projectedEdge(N) > tau && (N is a root || wantSplit(parent))   (monotonic by AND)
//   canSplit(N)  = wantSplit(N) && all four children resident
//   draw(N)      = resident(N) && !canSplit(N) && (N is a root || canSplit(parent))
// Exactly one node on every root-to-leaf path is drawn (the first that can't split), and it is resident; roots are
// pinned. A node that wants to split but whose children are absent requests them (deduplicated with a bit per
// node). Drawn nodes record their depth in the patch's cut map (lod_neighbors.comp reads it for crack-free edges),
// then pass frustum and normal-cone culling into the indirect draw.
layout(local_size_x = 64) in;
#include "common.glsl"
SF_BUF(0) readonly buffer CageVerts { CageVertex cageVerts[]; };
SF_BUF(1) readonly buffer Patches   { Patch patches[]; };
SF_BUF(2) readonly buffer PageTable { uint pageTable[]; };
SF_BUF(3) readonly buffer NodeInfo  { uvec2 nodeInfo[]; };
SF_BUF(4) writeonly buffer Records  { uvec4 records[]; };
SF_BUF(5) buffer Output {
    uint indexCount, instanceCount, firstIndex, baseVertex, baseInstance;  // DrawElementsIndirectCommand
    uint requestCount, droppedRecords, pad0;
    uvec2 requests[];                                                     // node, priority (float bits)
};
SF_BUF(6) buffer Requested { uint requestedBits[]; };
SF_BUF(7) writeonly buffer CutMap { uint cutDepth[]; };                   // per patch, per leaf tile

shared bool sVisible;
shared vec3 sP[4];

bool resident(uint node) { return ((pageTable[node] >> 22) & 3u) == kStateCold; }
vec3 bil(float u, float v) { return mix(mix(sP[0], sP[1], u), mix(sP[3], sP[2], u), v); }

// Sphere of node (d, x, y): its bilinear cage region, plus the Phong bulge and its height range.
vec4 nodeSphere(uint patch, uint node, uint d, uint x, uint y) {
    float inv = 1.0 / float(1u << d);
    vec2 u0 = vec2(x, y) * inv, u1 = u0 + inv;
    vec3 q00 = bil(u0.x, u0.y), q10 = bil(u1.x, u0.y), q11 = bil(u1.x, u1.y), q01 = bil(u0.x, u1.y);
    vec3 c = 0.25 * (q00 + q10 + q11 + q01);
    float r = max(max(distance(c, q00), distance(c, q10)), max(distance(c, q11), distance(c, q01)));
    uint hr = nodeInfo[node].x;
    float hmin = float(int(hr << 16) >> 16) * uScale.x, hmax = float(int(hr) >> 16) * uScale.x;
    return vec4(c, r + patches[patch].bound.x + max(abs(hmin), abs(hmax)));
}
float projectedEdge(vec4 sph, uint d, float meanEdge) {
    float edge = meanEdge / float(16u << d);   // micro cell edge at depth d
    float dist = uEye.w > 0.5 ? 1.0 : max(distance(uEye.xyz, sph.xyz) - sph.w, uLod.z);
    return edge * uLod.x / dist;
}
bool wantSplit(uint patch, uint d, uint x, uint y, float meanEdge, out float err) {
    err = 0.0;
    if (d >= uGrid.y) return false;
    bool want = true;
    for (uint k = 0u; k <= d && want; ++k) {           // ancestors first: AND keeps the decision monotonic
        uint ax = x >> (d - k), ay = y >> (d - k);
        uint node = nodeIndex(patch, k, ax, ay);
        float e = projectedEdge(nodeSphere(patch, node, k, ax, ay), k, meanEdge);
        want = e > uLod.y;
        if (k == d) err = e;
    }
    return want;
}
bool childrenResident(uint patch, uint d, uint m) {
    uint base = patch * uGrid.z + depthOffset(d + 1u) + (m << 2);
    return resident(base) && resident(base + 1u) && resident(base + 2u) && resident(base + 3u);
}
bool canSplit(uint patch, uint d, uint x, uint y, float meanEdge, out float err) {
    return wantSplit(patch, d, x, y, meanEdge, err) && childrenResident(patch, d, morton2(x, y));
}

void main() {
    uint patch = gl_WorkGroupID.x;
    Patch pt = patches[patch];
    if (gl_LocalInvocationIndex == 0u) {
        for (int k = 0; k < 4; ++k) sP[k] = cageVerts[pt.corners[k]].position.xyz;
        vec3 c = 0.25 * (sP[0] + sP[1] + sP[2] + sP[3]);
        float r = 0.0;
        for (int k = 0; k < 4; ++k) r = max(r, distance(c, sP[k]));
        r += pt.bound.x + max(abs(pt.bound.y), abs(pt.bound.z));
        bool inside = true;
        for (int k = 0; k < 6; ++k) inside = inside && dot(uPlanes[k].xyz, c) + uPlanes[k].w > -r;
        sVisible = inside;
    }
    barrier();
    // A patch outside the frustum still needs its cut map (neighbours' crack fixing): write its roots' depth.
    uint tilesSide = uGrid.w == 0u ? 1u : (uGrid.x >> 4);
    if (!sVisible) {
        for (uint t = gl_LocalInvocationIndex; t < tilesSide * tilesSide; t += 64u)
            cutDepth[patch * tilesSide * tilesSide + t] = 0u;
        return;
    }
    for (uint k = gl_LocalInvocationIndex; k < uGrid.z; k += 64u) {
        uint d = 0u;
        while (d + 1u <= uGrid.y && k >= depthOffset(d + 1u)) ++d;
        uint m = k - depthOffset(d), x = compact1by1(m), y = compact1by1(m >> 1);
        uint node = patch * uGrid.z + k;
        if (!resident(node)) continue;
        float err, perr;
        bool split = canSplit(patch, d, x, y, pt.bound.w, err);
        bool parentSplits = d == 0u || canSplit(patch, d - 1u, x >> 1, y >> 1, pt.bound.w, perr);
        // requests: wants detail it doesn't have
        if (wantSplit(patch, d, x, y, pt.bound.w, err) && !childrenResident(patch, d, m) && parentSplits) {
            uint base = patch * uGrid.z + depthOffset(d + 1u) + (m << 2);
            for (uint c = 0u; c < 4u; ++c) {
                uint child = base + c;
                if (resident(child)) continue;
                uint bit = 1u << (child & 31u);
                if ((atomicOr(requestedBits[child >> 5], bit) & bit) != 0u) continue;
                uint idx = atomicAdd(requestCount, 1u);
                if (idx < uCounts.w) requests[idx] = uvec2(child, floatBitsToUint(err));
            }
        }
        if (split || !parentSplits) continue;  // not part of the cut
        // the cut map: this node's depth over the leaf tiles it covers
        uint cover = 1u << (uGrid.y - d);
        for (uint ty = 0u; ty < cover; ++ty)
            for (uint tx = 0u; tx < cover; ++tx)
                cutDepth[patch * tilesSide * tilesSide + (y * cover + ty) * tilesSide + x * cover + tx] = d;
        // culling
        vec4 sph = nodeSphere(patch, node, d, x, y);
        bool inside = true;
        for (int q = 0; q < 6; ++q) inside = inside && dot(uPlanes[q].xyz, sph.xyz) + uPlanes[q].w > -sph.w;
        uint cone = nodeInfo[node].y;
        if (inside && (cone >> 24 & 1u) != 0u && uEye.w < 0.5) {   // cone valid (shading current)
            vec2 o = vec2(cone & 255u, (cone >> 8) & 255u) / 255.0 * 2.0 - 1.0;
            vec3 axis = vec3(o, 1.0 - abs(o.x) - abs(o.y));
            if (axis.z < 0.0) axis.xy = (1.0 - abs(axis.yx)) * vec2(axis.x >= 0.0 ? 1.0 : -1.0, axis.y >= 0.0 ? 1.0 : -1.0);
            axis = normalize(axis);
            float cosA = float((cone >> 16) & 255u) / 255.0;
            vec3 toNode = normalize(sph.xyz - uEye.xyz);
            float sinA = sqrt(max(0.0, 1.0 - cosA * cosA));
            // whole cone faces away (with the sphere's angular size as margin)
            float margin = sph.w / max(distance(sph.xyz, uEye.xyz), 1e-6);
            if (dot(toNode, axis) > sinA + margin) inside = false;
        }
        if (!inside) continue;
        uint r = atomicAdd(instanceCount, 1u);
        if (r >= uCounts.z) { atomicAdd(droppedRecords, 1u); continue; }
        records[r] = uvec4(node, patch, d | (x << 4) | (y << 18), 0u);
    }
}
```

```glsl
// lod_neighbors.comp - one thread per draw record: the depth drawn just outside the middle of each of its edges
// (across patch edges through the neighbour mapping), packed 4 x 4 bits into record.w. A coarser neighbour covers
// the whole edge (quadtree alignment, and the same alignment across patches since every patch has the same n), so
// one sample per edge suffices. Depth gaps are capped at 4 (the vertex shader snaps by 2^gap <= 16 points).
layout(local_size_x = 64) in;
#include "common.glsl"
SF_BUF(1) readonly buffer Patches { Patch patches[]; };
SF_BUF(4) buffer Records { uvec4 records[]; };
SF_BUF(5) readonly buffer Output { uint indexCount, instanceCount; };
SF_BUF(7) readonly buffer CutMap { uint cutDepth[]; };
void main() {
    uint r = gl_GlobalInvocationID.x;
    if (r >= min(instanceCount, uCounts.z)) return;
    uvec4 rec = records[r];
    uint d = rec.z & 15u, x = (rec.z >> 4) & 0x3FFFu, y = rec.z >> 18;
    int T = int(uGrid.x >> 4), cover = int(1u << (uGrid.y - d));
    ivec2 lo = ivec2(int(x), int(y)) * cover, mid = lo + cover / 2;
    ivec2 probe[4] = ivec2[4](ivec2(mid.x, lo.y - 1), ivec2(lo.x + cover, mid.y), ivec2(mid.x, lo.y + cover), ivec2(lo.x - 1, mid.y));
    uint packed = 0u;
    for (int e = 0; e < 4; ++e) {
        uint p = rec.y;
        ivec2 c = probe[e];
        uint nd = d;
        if (c.x < 0 || c.y < 0 || c.x >= T || c.y >= T) {
            uint nb = patches[p].neighbors[e];
            if (nb != kNone) {   // cell mapping: as crossEdge with n -> T and t' = T - 1 - t
                int t = e == 0 ? c.x : e == 1 ? c.y : e == 2 ? T - 1 - c.x : T - 1 - c.y;
                int f = int(nb & 3u), tp = T - 1 - t;
                ivec2 q = f == 0 ? ivec2(tp, 0) : f == 1 ? ivec2(T - 1, tp) : f == 2 ? ivec2(T - 1 - tp, T - 1) : ivec2(0, T - 1 - tp);
                nd = cutDepth[(nb >> 2) * uint(T * T) + uint(q.y * T + q.x)];
            }
        } else {
            nd = cutDepth[p * uint(T * T) + uint(c.y * T + c.x)];
        }
        uint gap = d > nd ? min(d - nd, 4u) : 0u;
        packed |= gap << (4 * e);
    }
    records[r].w = packed;
}
```

Micro vertex shader (draw pass, instanced over draw records with the shared 17 x 17 grid):
1. The point (a, b) is `gl_VertexIndex`. A vertex on edge e with gap g > 0 whose edge parameter k isn't a multiple of 2^g lies on the line between the neighbouring multiples k0 and k1 = k0 + 2^g. It is placed at `mix(P(k0), P(k1), (k - k0) / 2^g)`. Those points coincide with the coarser neighbour's vertices because mips are point-subsampled.
2. Height is `coldSample` of the node plus `strokeDelta` (only for records flagged as touched by the open stroke) times the mask factor.
3. The fragment shader writes colour and the ID (`record << 9 | gl_PrimitiveID`).

### 5.7 How the CPU stays authoritative

**Choice: GPU results are read back per tile, as Stroke Delta Tiles. The CPU master is never replaced by a GPU copy.** Justification:

1. `sculptforge-cli`, MCP and Python have no GPU. Save, undo, export, bake and every command read `Mesh` and `QuadMicro` on the CPU. A GPU-authoritative design would need a GPU in every headless context.
2. Laptop VRAM can't hold 1B heights plus everything else, so the GPU only ever holds a cache.
3. A device loss (TDR, driver reset, eGPU unplug) would destroy GPU-authoritative data. Here it loses at most one readback interval of the open stroke (section 7.5).
4. Readback volume is small: SDTs are proportional to the brush footprint divided by its stride squared, not to the number of points changed. That is about 0.1-2 MB per frame and 150-300 KB per reference stroke.
5. Exactness: the fine delta is an integer function of the SDT and the mask (section 6.2). The CPU commit therefore produces bit-for-bit what the GPU drew, with no tolerance tests and no drift between save and screen.

GPU and CPU brush kernels compute SDT samples in float and may differ by an LSB. That only matters when replaying the same spline on the other device (CLI against GUI). The journal stores SDTs, not splines, so recovery is exact.

## 6. Strokes: Stroke Delta Tiles

### 6.1 Structures (`src/core/StrokeDelta.h`)

```cpp
// The open (or recorded) change of one micro stroke: per patch and SDT level, a grid of fixed-point deltas
// (1/256 height unit) at stride 2^level leaf cells, in 17 x 17 tiles (one partial tile when n >> level <= 16).
struct StrokeDelta {
    static constexpr int kFracBits = 8;
    static int32_t limit(uint32_t level) { return int32_t(1) << (30 - int(level)); } // keeps the separable lerp in int32
    struct Tile {
        TileKey key;              // TileKind::StrokeDelta, patch, level, tile x/y
        std::array<int32_t, 289> v;
    };
    TileMap<uint32_t> index;      // key -> tiles[i]
    std::vector<Tile> tiles;
    std::vector<uint32_t> levelMask; // per patch (sparse in practice: TileMap<uint16_t> when patches > 1M)

    // The fine delta (1/256 units) at leaf point (i, j) of patch p: the same integer function as strokeDelta().
    int32_t fineDelta(const MicroPatchLayout& layout, uint32_t patch, uint32_t i, uint32_t j) const;
    // Applies sign * round(fine * maskFactor) to the master. Points that would saturate int16 are collected in
    // `saturated` (their leaf tiles get copy-on-write undo instead). Parallel over leaf tiles, deterministic.
    void apply(QuadMicro& micro, int sign, std::vector<TileKey>* saturated) const;
    size_t packedBytes() const;
    void pack(std::vector<uint8_t>& out) const;   // Morton order, microPackTiles-style codec on int32 with a
    bool unpack(const uint8_t* data, size_t size); // per-tile planar predictor (deltas are smooth)
};
```

### 6.2 Commit semantics (integer, identical on CPU and GPU)

```
mask factor  mf(p)   = 256 - mask(p) - (mask(p) >> 7)        # 255 -> 0, 0 -> 256
fine delta   F(p)    = (strokeDelta(p) * mf(p)) >> 8          # arithmetic shift, as GLSL and C++20 define it
committed    h'(p)   = clamp16(h(p) + ((F(p) + 128) >> 8))
undo         h(p)    = h'(p) - ((F(p) + 128) >> 8)            # exact unless clamp16 bit (then copy-on-write)
display      hot(p)  = h(p) + strokeDelta(p) * mf(p) / 65536  # the vertex shader, while the stroke is open
```

The master isn't touched during the stroke, so the mask, the master and the SDTs at commit and at undo are the same values the GPU used. The display shows sub-unit fractions while stroking. After commit it shows the rounded value: under one height unit, which is invisible.

### 6.3 Stroke lifecycle

```
press:
  cfg = currentTierConfig()
  pin nodes under the cursor + mirrored centres + predicted path (PenPredictor, 150 ms) at the needed depths
  GPU: clear sdtDir level masks of patches touched by the previous stroke (list kept), slotTop = 0
each frame:
  CPU -> GPU: spline keys since last frame, brush params (64 B UBO block)
  GPU: section 5.4 steps 3-8
  every cfg.readbackInterval frames: GPU copies touched SDT slots + new directory entries -> readback ring
  CPU (non-blocking): newest snapshot per slot replaces the older (SDTs are cumulative: idempotent, loss-tolerant)
  if slotTop > 90% capacity: split: commit what is there (as below) without ending the undo step, clear, continue
release:
  GPU: final accumulate, copy all stroke slots, fence F
  GPU: normals + curvature (+ horizon AO) for touched resident nodes (Laptop draft ends here)
  CPU worker (async, the UI keeps running; undo/save wait on it, bounded):
     wait F; build StrokeDelta from the snapshot
     saturated = []; delta.apply(master, +1, &saturated)
     for tile in saturated: COW old 289 values (captured before apply) into the undo record
     update nodeRange for touched leaves, propagate to parents; mark dirtyLeaves; generation++
     journal.append(StrokeDelta record, COW tiles, Commit{generation})
     undo.push(UndoRecord{micro = MicroUndo{delta, cow}})
  GPU (same frame as release): bake.comp: for resident cold pages under the stroke that are lossless, decode +
     the same integer delta + encode (packColdPage's algorithm) in place, then clear the SDT directory. Lossy pages
     are re-encoded from the master by the CPU and uploaded. Non-resident pages: page file updated by the CPU.
```

Commit cost for the reference stroke at 1B is about 72M fine points: integer and memory-bound, about 20-40 ms on 8 cores, plus repacking about 330k pages (about 30 ms). It runs off the main thread, and the display never waits for it.

### 6.4 Residency gate
A dab may only run on SDT tiles whose sample points are resident at the depth matching its level, or finer. Otherwise `currentHeight` would read interpolated values.
- `dab_place.comp` holds back dabs whose tiles aren't ready. The stroke continues from them next frame, in order, so no dab is ever dropped.
- Prefetch and pinning on press and along the predicted path make stalls rare. A stall shows as the brush trailing for a frame or two, covered by the ghost.

### 6.5 Laptop draft normals
In draft mode the fragment shader perturbs the stored shading normal by the analytic gradient of the bilinear SDT field (two extra samples). Strokes therefore read as shaded relief at no compute cost. The real normals and curvature arrive at release.

### 6.6 CPU path (no compute: GL 3.3, macOS GL 4.1, CLI)
`QuadSculpt` (`core/QuadSculpt.cpp`) computes the same SDTs on the CPU from `Sculptor`'s dabs (feat/micromesh's 48-samples-per-radius stride logic), and commits with the same `StrokeDelta::apply`. Undo, journal and save are identical. In the GUI, feat/zero-latency's StrokeSolver thread and ghost preview carry over unchanged. The GL 3.3 quad renderer adds `strokeDelta` in its vertex shader through texture buffers.

## 7. Asynchronous chunk eviction, undo journaling and the save format

### 7.1 Data structures

```cpp
// core/MicroUndo (extends feat/micromesh's MicroUndo; triangle-domain fields unchanged)
struct MicroUndo {
    // ...feat/micromesh fields (tris, disp, mask, maskAdded, swapWhole, whole)...
    // Quad domain:
    int deltaSign = -1;                    // the next swapWith applies deltaSign * delta, then flips the sign
    std::vector<uint8_t> deltaPacked;      // StrokeDelta::pack, in RAM while small
    struct CowTile { TileKey key; std::array<int16_t, 289> values; std::array<uint8_t, 289> mask; bool hasMask; };
    std::vector<CowTile> cow;              // leaf tiles restored whole (saturation, whole-level operations)
    struct JournalRef { uint32_t segment = 0; uint64_t offset = 0; uint32_t bytes = 0; } spilled; // bytes == 0: in RAM
    size_t bytes() const;                  // RAM only (spilled data counts 0 against UndoStack::byteBudget)
    void swapWith(Mesh& m);                // COW tiles swap; the delta applies with deltaSign, skipping COW tiles
};

// core/MicroJournal.h
struct JournalRecordHeader {               // 32 bytes, little-endian
    uint32_t magic = 0x314A4653;           // "SFJ1"
    uint16_t type = 0, flags = 0;
    uint64_t sequence = 0;
    uint64_t generation = 0;               // QuadMicro::generation after this record applies
    uint32_t payloadBytes = 0;
    uint32_t crc32c = 0;                   // over the header (crc field zero) and the payload
};
enum class JournalType : uint16_t {
    SegmentStart = 1, // payload: project uuid, subtool id, micro serial, base generation, layout
    StrokeDelta = 2,  // payload: undo id, sign, StrokeDelta::pack
    CowTiles = 3,     // payload: undo id, keys + old values (packed) [+ new values for redo]
    CageDelta = 4,    // payload: undo id, vertex indices, old and new positions/normals (cage moves)
    Commit = 5,       // payload: undo id; everything with this undo id before it is now durable and applied
    Undo = 6, Redo = 7, // payload: undo id
    Checkpoint = 8,   // payload: saved generation, project path hash: segments before it are only needed by undo
};
class MicroJournal {
public:
    bool open(const std::string& folder, const std::string& projectUuid, uint32_t subtool, std::string& error);
    uint64_t append(JournalType type, uint64_t generation, std::span<const uint8_t> payload); // returns offset
    void requestSync();                     // group commit: the I/O thread fdatasyncs at most every journalSyncMs
    bool read(const MicroUndo::JournalRef& ref, std::vector<uint8_t>& out); // spilled undo data (pread)
    void releaseBefore(uint64_t sequence);  // undo steps dropped (UndoStack::enforceBudget): segments may go
    static bool recover(const std::string& folder, const std::string& projectUuid, uint64_t savedGeneration,
                        const std::function<void(JournalType, std::span<const uint8_t>)>& apply, std::string& report);
};

// core/AsyncIo.h - one interface, several backends
class AsyncIo {
public:
    struct Request { int fileId; uint64_t offset; uint32_t bytes; void* dst; uint64_t tag; bool write; };
    virtual ~AsyncIo() = default;
    virtual bool submit(std::span<const Request> requests) = 0;     // non-blocking
    virtual size_t poll(std::span<uint64_t> completedTags) = 0;     // non-blocking
    virtual void fsync(int fileId, uint64_t tag) = 0;               // ordered after earlier writes to fileId
    static std::unique_ptr<AsyncIo> create(IoPath path, uint32_t queueDepth, std::span<void*> registeredBuffers);
};
```

### 7.2 Files
- **Master**: `QuadMicro::heights`, a `MicroStore` (feat/micromesh) in memory, or out of core as a sparse mapped file in the swap folder that is deleted on open. Crash-volatile by design, because the project plus the journal rebuild it.
- **Page file** `~/.cache/sculptforge/pages/<micro serial>.sfpg`: a derived cache of every node's 416-B cold page. Each patch's nodes are stored in Morton order in 4 KiB page groups of 9 pages plus a 352-B group directory with per-page generation and CRC. Rebuilt lazily from the master and discarded freely. It exists so the GPU can stream with direct I/O instead of encoding pages from the master on the CPU.
- **Journal** `~/.cache/sculptforge/journal/<project uuid>/<subtool>/seg-NNNNNN.sfj`: 64 MiB segments, append-only, `O_APPEND` without `O_DIRECT`, synced by group commit.

### 7.3 I/O paths

| Platform | Reads (pages to GPU) | Writes (journal, page file) |
|---|---|---|
| Linux 5.6+ | `io_uring`: `IORING_OP_READ_FIXED` into staging-ring buffers registered once (`io_uring_register_buffers`), page file opened `O_DIRECT` (alignment from `statx` `STATX_DIOALIGN`, else 4096), queue depth 64 (Laptop) or 256 (Workstation), completions peeked each frame | `IORING_OP_WRITE` then `IORING_OP_FSYNC` (`IORING_FSYNC_DATASYNC`) linked with `IOSQE_IO_LINK` |
| Windows 11 | `IoRing` (`CreateIoRing`, `BuildIoRingReadFile` with registered buffers; OS component). **DirectStorage** (`DSTORAGE_REQUEST_DESTINATION_MEMORY`) only as an optional runtime-loaded DLL, never bundled: its redistributable licence and GPL-3 compatibility need review. GPU-destination DirectStorage needs D3D12 and is out of scope | `BuildIoRingWriteFile` (22H2+) or overlapped `WriteFile` + `FlushFileBuffers` on the I/O thread |
| macOS (UMA) | `mmap` of the page file with `madvise(MADV_WILLNEED)` per group, decoded straight into host-visible pool memory (zero-copy target). `pread` with `F_NOCACHE` on a `dispatch_io` channel when mapping is unsuitable. Importing mapped pages as GPU memory (`VK_EXT_external_memory_host` on MoltenVK; verify) is an optimisation, not a requirement | `pwrite` + `F_FULLFSYNC` only at Checkpoint, `fdatasync`-equivalent (`fsync`) at group commit |
| Fallback (any) | `ThreadPool` workers: `pread` into staging | `pwrite` + `fdatasync` |

On UMA and ReBAR, pools are host-visible, and the I/O completion handler decodes or copies into the slot directly. On discrete GPUs it writes the staging ring, and a transfer-queue copy (Workstation) or a graphics-queue copy (Laptop) moves the data into the device-local pool.

### 7.4 Eviction and streaming algorithm

```
per frame (main thread, < 0.3 ms at 1B):
  drawn    = readback(drawRecords, frame - framesInFlight)        # node ids
  requests = readback(requests,   frame - framesInFlight)
  table.noteDrawn(drawn)                                          # lastUsed = frame, clockBit = 1
  table.request(requests)                                         # priority queue by projected error
  table.schedule(loads, evicted):
     budget = cfg.uploadBytesPerFrame (power state applied)
     for r in queue by priority (pinned prefetch first):
        if budget exhausted: break
        if state(r) != Absent: continue
        slot = cold.freeSlots.pop() or evictOne()
        if no slot: break                                          # admission control: LOD stays coarser
        entries[r] = slot | Loading; loads.push(r, slot); budget -= 416
  for (node, slot) in loads:
     group = pageFileGroup(node)                                   # 4 KiB, 9 Morton-adjacent pages
     if page file has a current copy (group directory generation == micro.generation of that page):
        io.submit(read group -> staging[slot range])               # siblings come along: mark them Cold too if free
     else: encode from master on a worker (packColdPage of nodePoints) -> staging
  GPU: page_table_patch.comp writes Absent for `evicted` before any upload reuses their slots (same submission)
  completions (io thread -> table.completeLoad): copy staging -> pool slot, entries[node] = slot | Cold,
     enqueue node for normals_curvature (shade is derived, computed on load)

evictOne():                                    # CLOCK with tier-specific victims first
  if tier == Laptop:
     1. nodes outside the frustum grown by cfg.evictFrustumMargin for > frustumGraceFrames (from noteDrawn history)
     2. nodes finer than the view wants: projected cell < cfg.evictMinScreenPx at last use (screen-size eviction)
  3. CLOCK sweep: skip pinned, roots, Loading, pages with a pending bake/upload, lastUsed within evictAfterFrames;
     clear clockBit on first pass (second chance)
  4. on victim: if it has resident children, evict children first (a node never outlives... ancestors are cheaper
     and the cut relies on parents: children go before parents)
```

**Never OOM:**
1. Every GPU pool is allocated once at start in 64 MiB chunks. A failed allocation halves the request down to the root-pinned minimum. Nothing grows during the session.
2. Roots are pinned, so the cut always exists and there are no holes.
3. A live budget (`VK_EXT_memory_budget`, NVX meminfo) is polled every 60 frames. Above 95%, the tail chunks are released via `shrink`.
4. `micro_divide` checks admission. It refuses a level whose pinned roots plus the in-memory master can't fit, and suggests out-of-core mode or a lower level.
5. The CPU side shrinks under memory pressure (section 4.4).

### 7.5 Crash safety
- **Torn writes**: the CRC32C covers header and payload. Recovery stops at the first bad record of the last segment (a torn tail) and truncates it.
- **Ordering**: a stroke's records are StrokeDelta, then CowTiles, then Commit. Recovery applies a stroke only if its Commit is present. Undo and Redo records are applied in sequence.
- **Durability window**: group commit every 250 ms (1 s on battery), so a crash loses at most about 250 ms of committed work. A GPU device loss also loses the open stroke since its last readback (up to 4 frames). The CPU snapshot is committed as a partial stroke.
- **Recovery flow**: `open_project` loads the project at generation G0, finds journal segments for its uuid, and replays committed records with generation greater than G0. It then shows "Recovered N strokes from <time>" with Keep / Discard. The master and page file are rebuilt from project plus journal, never trusted from the crashed session.
- **Save as checkpoint**: save writes `.tmp`, then renames (as today), then appends Checkpoint{G}. Segments before the checkpoint are kept only while the undo stack references them (`releaseBefore`).
- **Device loss**: rebuild the device, re-upload roots and the visible set from the master and page file, commit the newest SDT snapshot, and log the event.

### 7.6 Interaction with the project format

**Version 7 reconciliation (Phase 0, before any v2 work).** feat/micromesh writes 7 when a SubTool has a triangle micro level (`kProjectVersionWithoutMicro = 6`). feat/pbr writes 7 when a texture has material channels (`ProjectVersionNeed`, `kProjectBaseWriteVersion = 6`). Merge as follows:
- Keep feat/pbr's `ProjectVersionNeed`.
- feat/micromesh calls `need(7)` when it writes `"micro"`.
- Both keys are optional in format 7, and each is absent when unused.
- Readers of 7 accept either or both.

**Format 8: quad micro levels.** Written only when a SubTool carries a quad micro level, via `need(8)`. Projects without one stay at 6 or 7.

| Manifest key (in `subtools[i]`) | File | Layout |
|---|---|---|
| `micro.domain` | (manifest) | `"quad"` (absent means `"triangles"`, format 7 semantics) |
| `micro.cells`, `micro.step`, `micro.generation`, `micro.journal` | (manifest) | n, step (world per unit), generation at save, journal uuid |
| `micro.tiles` | `subtools/i/micro/tiles.bin` | Chunk directory, then chunks: 256 leaf tiles each (patch-major, Morton within a patch), feat/micromesh's tile codec (min + bit width + deltas, 289 samples per tile; untouched tiles cost 3 B). Each chunk starts on a 4 KiB boundary within the file, and the entry itself is aligned to 4 KiB in the ZIP (`writeStoredZip` alignment 4096 for this entry) |
| `micro.tiles_index` | `subtools/i/micro/tiles_index.bin` | u64 per chunk: offset, u32 bytes, u32 CRC32C. Lets `open_project` map the entry (`readStoredZipIndex` gives its offset) and decode chunks lazily: first frame after reading roots only |
| `micro.mask` | `subtools/i/micro/mask.bin` | Optional, same chunking, u8 |
| `micro.node_range` | `subtools/i/micro/node_range.bin` | Optional cache (int16 x 2 per node). Recomputed if absent |

The cage itself is the SubTool's existing `mesh` plus `quad_partners` (and `subdiv`), with no new data. `BaseCage` is rebuilt on load.

**Writer changes:**
1. `ZipEntry` gains a streaming producer: a callback that writes chunks as they are packed from the mapped master. A 1B save then never holds about 600 MB of packed tiles in RAM. CRC is computed while streaming, and the local header is patched after.
2. Per-entry alignment.
3. Commits are queued, not blocked, while a save snapshots the master. GPU strokes continue, and their commits land after the save, so the saved generation G is consistent.

**Load:**
1. Read the index.
2. Decode the root-covering chunks first (all patches' roots are derived from their leaves' corner and subsampled points, using a mips pass in the background).
3. Map the rest on demand. The master stays out of core when the micro-level rule says so, and decodes lazily otherwise.

## 8. Acceptance targets

Reference scenes: a head cage (QRemesh, mixed quads converted with one Catmull-Clark step) with `micro_noise` detail.
- 100M: 195k quads, n = 16.
- 300M: 146k quads, n = 32.
- 1B: 122k quads, n = 64.

Reference stroke: Standard, radius 120 px at 2560 x 1440, a 1,500 px arc in 1.5 s, X symmetry, model framed full view.

Reference machines:
- Laptop A: MacBook Air M2, 16 GB (UMA).
- Laptop B: RTX 3050 Laptop 4 GB, 16 GB RAM, Windows 11.
- Workstation: RTX 4080 16 GB, 64 GB, PCIe 4 NVMe, Linux and Windows, 144 Hz.

All numbers are measured with a new `sculptforge --bench-engine --sizes 100,300,1000 --tier auto|laptop|workstation --out report.json` and the existing `--measure-stroke`.

| Metric | Laptop 100M | Laptop 300M | Laptop 1B | Workstation 100M | Workstation 300M | Workstation 1B |
|---|---|---|---|---|---|---|
| Orbit frame p50 / p99 (ms) | 11 / 16.7 | 12 / 16.7 | 13 / 18 | 4.0 / 6.9 | 4.5 / 6.9 | 5.0 / 6.9 |
| Sculpt frame p50 / p99 (ms) | 13 / 20 | 14 / 22 | 15 / 25 (draft) | 5.5 / 6.9 | 6.0 / 7.5 | 6.5 / 8.3 (continuous normals, curvature, AO) |
| Pen sample to photon p95 (ms) | 33 | 33 | 36 | 12 | 13 | 14 |
| Brush behind pointer, mean / worst (px) | 12 / 30 | 14 / 35 | 18 / 45 | 5 / 12 | 6 / 14 | 7 / 16 (today, 21M mesh: 137 / 189) |
| Release to final shading (ms) | 60 | 90 | 150 | 0 (continuous) | 0 | 0 |
| Commit (CPU, async) to undo available (ms) | 30 | 60 | 120 | 20 | 40 | 80 |
| Undo / redo of reference stroke (ms) | 40 | 60 | 120 | 25 | 40 | 80 |
| Undo RAM per reference stroke | <= 500 KB (typically 150-300 KB) at every size | | | <= 500 KB | | |
| Journal bytes per reference stroke | <= 1 MB | <= 1 MB | <= 1 MB | <= 1 MB | <= 1 MB | <= 1 MB |
| COW fallback tiles | <= 1% of touched leaf tiles, 0 for Standard and Smooth within range | | | | | |
| Anonymous RSS (whole app) | <= 0.5 GB | <= 0.75 GB | <= 1.6 GB (master in memory), <= 0.7 GB out of core on 8 GB | <= 0.6 GB | <= 0.9 GB | <= 2.5 GB |
| GPU allocations (pools + targets) | <= 1.0 GB (UMA), <= 1.5 GB (4 GB dGPU), at every size | | | <= 0.8 GB | <= 1.6 GB | <= 4.5 GB (all leaves resident, with shading) |
| Open to first interactive frame (s) | 1.0 | 1.5 | 3.0 | 0.8 | 1.0 | 2.0 |
| Full residency after open (s) | n/a (view set <= 0.5 s) | | | 0.3 | 0.6 | 1.2 |
| Save (background, UI frame p99 +<= 2 ms) (s) | 1.5 | 3 | 8 | 1 | 2 | 5 |
| Soak: 30 min random orbit + sculpt | 0 allocation failures, 0 holes, GPU <= budget, RSS growth <= 5% | | | same | | |
| Recovery after `kill -9` mid-session | Every stroke committed > 250 ms before the kill recovered, bit-exact (hash of master) | | | same | | |

Correctness gates in CI:
- Kernel-against-CPU parity on llvmpipe (GL 4.5) and lavapipe (Vulkan): SDT within 1 LSB, `fineDelta` and commit bit-exact.
- Seam watertightness: maximum shared-edge difference of 0 after commit.
- Format 6/7/8 round trips.
- Journal replay equals the live master hash.
- Golden images per backend.

## 9. Risks (beyond the port)

| Risk | Mitigation |
|---|---|
| int16 saturation in one stroke | `step` headroom at divide (range = 1/4 of the cage diagonal), per-level SDT clamps, COW fallback, stroke splitting |
| Point-subsampled mips alias in silhouettes | Shading mips are filtered, and tau keeps cells near 1 px |
| Seam rule broken by compiler contraction differences between the two edge sides | Same shader binary on both sides, `precise`, owner copy for duplicates, CPU stitch at commit, a test with maximum shared-edge difference 0 |
| Atomics contention on hot SDT tiles (many dabs per frame) | Integer atomics per sample, per-frame dab cap (64), work records per (tile, dab) |
| 8-SSBO limit in GL 4.3 | Kernels designed to the limit, dab data in the UBO |
| MoltenVK feature gaps | Geometry-shader removal in M2, no ray query on Apple (horizon AO), capability checks via `BackendCaps` |
| Spline-based placement differs from CPU `StrokePath` spacing | Same spacing rule ported; `--measure-stroke` comparison |
| Workload of continuous AO at 144 Hz | Ladder rungs 0-2, 1 ray per pixel with temporal reuse, cap of 1 ms |

## 10. Phased roadmap (branch-sized, parallel without merge conflicts)

Principle: **phase 0 creates every shared touchpoint once.** Afterwards each parallel branch owns only new files plus its own test file. Integration branches are sequential and own the shared files.

**Shared files and their owners per phase:**

| File | Phase 0 | Phase 1 | Phase 2 | Phase 3 |
|---|---|---|---|---|
| `CMakeLists.txt` | P0.3 adds all engine sources (stubs) and a `sf_embed_shaders()` function | nobody | nobody (R2 adds shaders via the glob P0.3 set up) | V1 (Vulkan option) |
| `src/core/Commands.cpp` | P0.3 adds `registerEngineCommands()` | nobody (E-branches fill `registerEngineCommands` in `core/EngineCommands.cpp`, owned by E5) | nobody | nobody |
| `src/core/Project.cpp` | P0.1 (v7) | I1 only | nobody | nobody |
| `src/core/Micro*.cpp` | P0.1 | I1 only | nobody | nobody |
| `src/render/Renderer.cpp/.h` | P0.2 (merge micromesh + zero-latency) | I1 (hook only) | R1 only | V2 only |
| `src/render/GL.h` | P0.3 adds GL 4.3 entry points (dispatch, barriers, SSBO bind, indirect, fences, buffer storage) | nobody | nobody | nobody |
| `src/app/App.cpp` | P0.2, P0.3 adds call sites (`gpuStroke_` hooks, power probe, backend flag) | nobody | R4 only | V1 only |

### Phase 0: land and scaffold (sequential, about 1 week)
- **P0.1 `merge/v7-reconcile`**: merge feat/micromesh and feat/pbr, adopt `ProjectVersionNeed`, `"micro"` needs 7. Tests: `test_project` format matrix.
- **P0.2 `merge/engine-base`**: merge feat/hw-profile and feat/zero-latency (the Renderer.cpp/.h overlap with feat/micromesh is resolved here).
- **P0.3 `feat/engine-v2-scaffold`**: every header in section 3 plus `StrokeDelta.h`, `MicroJournal.h`, `AsyncIo.h`, `EngineGovernor.h`, `QuadSculpt.h`, `ProjectDmm.h`, `render/rhi/Rhi.h`, `render/QuadMicroGpu.h` and `app/GpuStroke.h`, with stub `.cpp` files. Also the CMake entries, `registerEngineCommands`, GL 4.3 entry points, App call sites behind a `SF_ENGINE_V2` runtime flag (off), empty `tests/test_engine_*.cpp`, and `render/shaders/engine/` with `common.glsl` and `micro_access.glsl`.

### Phase 1: CPU engine (6 parallel agents, then 1 integration)

| Branch | Owns | Delivers |
|---|---|---|
| E1 `feat/engine-cage` | `core/BaseCage.cpp`, `tests/test_base_cage.cpp` | build (Morton order, neighbours, corner owners), patchUv, bounds, `quadMicroRaycast` |
| E2 `feat/engine-patch` | `core/MicroPatch.cpp`, `tests/test_micro_patch.cpp` | layout, Morton, cold codec (lossless and shifted), `QuadPatchEval` (seam rule), `nodePoints`, mips |
| E3 `feat/engine-pagetable` | `core/PatchPageTable.cpp`, `core/AsyncIo*.cpp`, `tests/test_page_table.cpp` | TileMap, CLOCK eviction, tier victims, completions ring, mmap/thread-pool/io_uring backends |
| E4 `feat/engine-sdt` | `core/StrokeDelta.cpp`, `core/MicroJournal.cpp`, `tests/test_stroke_delta.cpp`, `tests/test_journal.cpp` | integer `fineDelta`, apply/undo exactness, packing, journal append/recover, crash tests (truncation, bad CRC) |
| E5 `feat/engine-tier` | `core/TierConfig.cpp`, `core/EngineGovernor.cpp`, `core/EngineCommands.cpp`, `core/PowerState.cpp`, `tests/test_tier_config.cpp` | derive / underPower tables, ladder, `engine_info` / `set_engine_override` commands, storage probe |
| E6 `feat/engine-cpu-brush` | `core/QuadSculpt.cpp`, `tests/test_quad_sculpt.cpp` | CPU kernels producing SDTs (Standard, Smooth, Inflate, Clay, Flatten, Layer, Mask), cage-or-height Move rule |
| I1 `feat/engine-quad-micro` (after E1-E6) | `Micro.cpp`, `MicroSculpt.cpp`, `Project.cpp` hook, `core/ProjectDmm.cpp`, `render/QuadMicroGL33.cpp`, `Renderer.cpp` hook | `MicroMesh::domain`, `micro_divide domain=quad`, format 8 with streaming ZIP entry, GL 3.3 quad renderer, MicroUndo v2, CLI end-to-end |

### Phase 2: GL 4.3 compute (4 parallel agents, then 1 integration)

| Branch | Owns | Delivers |
|---|---|---|
| R1 `feat/rhi` | `render/rhi/*`, `Renderer.cpp/.h`, app GL call sites, `tests/test_render_golden.cpp` | RHI plus GL backend, geometry-shader removal (GL 4.x path), app GL uses behind handles, golden images bit-exact |
| R2 `feat/engine-shaders` | `render/shaders/engine/*.comp/.glsl`, `tests/gpu/test_engine_kernels.cpp` | the kernels in section 5.6 plus `dab_place`, `bake`, `page_table_patch`, `horizon_ao`; parity against E2/E4/E6 on llvmpipe |
| R3 `feat/engine-gpu-renderer` | `render/QuadMicroGpu.cpp` | pools, page-table mirror upload, LOD and draw, ID target, streaming uploads, vertex/fragment shaders with snapping and draft normals |
| R4 `feat/engine-gpu-stroke` | `app/GpuStroke.cpp`, the App.cpp hooks from P0.3 | spline packaging, readback ring, residency gate, commit pipeline, fallback to the CPU path |
| I2 `feat/engine-gl43-integrate` | wiring, perf tuning, `docs/performance.md`, guide page | M3 gate (section 8 Laptop/Workstation targets on Linux and Windows) |

### Phase 3: Vulkan (5 parallel agents, then parity)

| Branch | Owns | Delivers |
|---|---|---|
| V1 `feat/vk-device` | `render/vk/Device*.cpp`, `main.cpp` flag, App.cpp backend selection | instance and device, swapchain via SDL3, ImGui Vulkan backend, `--backend=vulkan` |
| V2 `feat/vk-rhi` | `render/rhi/RhiVulkan.cpp`, third-party volk/VMA | RHI on Vulkan, SPIR-V pipelines, pipeline cache, staging and readback rings, timeline semaphores, async compute and transfer |
| V3 `feat/vk-macos` | `cmake/`, `packaging/macos/`, CI workflow | MoltenVK bundle, notarisation, lavapipe and MoltenVK CI jobs |
| V4 `feat/vk-io` | `core/AsyncIoWin.cpp`, `core/AsyncIoMac.cpp` | IoRing, optional DirectStorage loader, dispatch_io/mmap zero-copy, registered buffers |
| V5 `feat/vk-rt-ao` | `render/shaders/engine/rt_ao.comp`, `render/RtAo.cpp` | patch-AABB BLAS, ray-query AO, temporal accumulation |
| V6 `feat/vk-parity` (after V1-V5) | `tests/` | golden-image parity across backends, UI tests on lavapipe, soak, M4 gate |

### Phase 4: defaults (one small PR each)
- `release/vk-default-macos` (M5).
- `release/vk-default-linux`, then `release/vk-default-windows` (M6), each gated on section 8 plus two releases without backend-specific crashes in the top 10.
- GL 4.3 and GL 3.3 remain as fallbacks.

Agents that can run concurrently: E1-E6 (6), then R1-R4 (4), then V1-V5 (5). Each touches disjoint files after P0.3. Integration branches (I1, I2, V6) serialise access to the few shared files.

### Critical files for implementation
- `src/core/Micro.h` and `src/core/MicroStore.h` (feat/micromesh): the v1 surface definition, the out-of-core store and the tile codec v2 extends
- `src/core/HardwareProfile.h` and `src/core/FrameGovernor.h` (feat/hw-profile): the inputs to `TierConfig` and the governor v2 wraps
- `src/render/Renderer.cpp` / `src/render/Renderer.h`: the RHI extraction and the port-risk surface (feat/zero-latency's `StrokeUpload` and ghost included)
- `src/core/Undo.h` and `src/core/Project.cpp` / `Project.h`: undo integration (`MicroUndo` v2) and format 7/8 reconciliation (`ProjectVersionNeed` from feat/pbr)
- `src/core/Mesh.h` and `src/core/BVH.h`: the cage (`quadPartners`, normals, dirty tracking) and picking that `BaseCage` builds on