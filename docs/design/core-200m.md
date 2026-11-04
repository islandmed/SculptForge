# Core 200M: 200 million sculptable polygons on a 16 GB laptop with an integrated GPU

Research branch `research/core-200m` (2026-11-03). Generic techniques, designed here from first principles (regular
grids, multiresolution offsets, quantization, page tables, copy-on-write); no competitor code, files or docs.

**Headline target, floor profile** (integrated GPU, GL 3.3, 4 cores; perf-moonshots.md §0, `SF_HW_SIMULATE=floor`):

| | target |
|---|---|
| Asset | 200M triangles on a 16 GB laptop with an iGPU (shared video memory) |
| Stroke latency (pen to visible surface) | < 16 ms |
| Frame while sculpting | p99 < 33 ms |
| Orbit | smooth (p99 < 33 ms), full detail refined on release |
| Open | < 5 s to a sculptable, fully shaded view |
| Memory | 0.6-1.2 GB for the 200M asset (RSS, everything) |

The rule behind every piece: **cost follows the pixels on screen and the brush footprint, never the total count.**

## 0. What we already have (mapped before designing)

| Piece | Where | State | What it gives 200M | Gap |
|---|---|---|---|---|
| Regular grid patches over the base cage, implicit topology | `core/Micro.h` (`MicroMesh`, `MicroPatch`, `BaseCage`) | on | the storage model of §1: one grid of `(n+1)(n+2)/2` points per base triangle, no index buffer, edges stitched | heights are one level, measured from the Phong surface of the cage, not from a smoothed lower level |
| 16-bit quantized displacement | `MicroMesh::disp` (`MicroStore`) | on | 2 B a grid point, 1.11 B a triangle at Micro 5 | edges and corners stored once per patch (~10% extra at Micro 5) |
| Derived normals | `MicroShader` (CPU), shading grid on the GPU | on | no normals stored on the CPU | the GPU shading grid is 4 B a point for every patch (440 MB at 200M) and the first full shading runs at open |
| Per-cluster LOD to a triangle budget | `render/MicroRenderer` | on | draw cost bounded by `renderTriangleBudget` (3-4M drawn at 299M-1B, 8-13 ms on this laptop) | every patch's full grid is resident on the GPU (6 B a point); the budget is triangles, not pixels |
| Stride-sampled ("scale-adaptive") dabs | `MicroStroke::adaptive`, `kBrushSamples = 48` | on | the kernel evaluates ~48 samples across the radius at any level | the interpolated values are still written to every fine point: a 300 px dab at 1B writes millions of points (283 ms) |
| Out of core | `MicroStore` (mmap sparse file), `PatchPageTable` | on (auto above 1/4 of RAM) | levels larger than RAM; the OS page cache is the LRU | no compression in memory, no visible-first order |
| Compressed streamed patches | `core/FocusDetail` (`.sfdetail`, zstd, coarse level-3 grids, focus promotion 0.04 ms a tile) | prototype, off | 1.02B opens in < 1 s from 241 MB; `tileLevel(tri)` per patch | CPU array still dense; GPU doesn't use per-tile levels; not wired into projects |
| Per-patch undo | `MicroUndo` journal (Morton-ordered tiles, delta + bit-packed) | on | 0.6-11 MB a 30-dab stroke (31 KB for a short stroke at 5M) | rounding noise of fine writes inflates it; coarse-level edits (§3) would shrink it to KBs |
| Patch-parallel jobs | `ThreadPool::poolFor` in MicroSculpt / Micro / MicroRenderer / FocusDetail | on | deterministic block-parallel kernels | the parallel loops are fork-join per dab; dirty bits are plain words written by one thread at a time; stitching serialises at patch edges |
| Canvas cache | perf-moonshots.md §7 (research/perf-moonshots) | design | idle frames composite only; a dab re-rasterizes its rectangle | not built |

So phase 1 needs no new storage: a 200M micro level fits on today's code. What it doesn't do yet is make the GPU
copy, the open time and the big-brush cost follow the screen and the brush. That is phases 2-5.

## 1. Storage

**Layout.** Each base triangle of the cage (the Part's top subdivision level) carries a regular grid of
`n = 2^L` segments per edge: `n^2` micro triangles, `(n+1)(n+2)/2` points. Topology is implicit (index arithmetic,
`MicroMesh::index`); only heights are stored.

**Heights against the smoothed lower level.** Phase 1 stores, as today, one 16-bit height per point along the
normal of the Phong-tessellated cage. The target layout (phase 3) is multiresolution: level `l` stores, per point
it adds (the odd points of the level-`l` grid), a 16-bit offset from the *smoothed* level `l-1` surface (the
Loop-style average of its neighbours, evaluated from the coarser level), along that surface's normal. Points that
exist at `l-1` are not stored again. Total points over all levels are the same as the finest grid (each point is
stored once, at the level that introduces it), so the memory is the same 2 B a point; what changes is that a big
form lives in a few coarse values and the fine values stay small (and compress, §4).

**Arithmetic for 200M** (Micro 5: n = 32, 1,024 triangles and 561 points a patch; 195,313 base triangles):

| Item | Per | Count | Bytes |
|---|---|---|---|
| Micro triangles | | 195,313 x 1,024 | **200.0M** |
| Grid points, per-patch copies (today) | 561 a patch | 109.6M | |
| Grid points, unique (edges once) | ~triangles / 2 | 100.0M | |
| Heights, today's layout | 2 B a point | 109.6M | **219 MB** (1.10 B a triangle) |
| Heights, edges stored once (phase 3) | 2 B a point | 100.0M | 200 MB |
| Base cage + BVH (195K triangles, ~90 B + BVH) | | | ~25 MB |
| Mask, sparse 32x32-point tiles, 1 B a point | where masked | 10% of the surface | ~11 MB (0 when unused) |
| Colour, sparse tiles, RGBA8 | where painted | 25% painted / all | ~110 MB / 440 MB |
| Derived normals | | | 0 on the CPU (computed per patch when drawn) |
| GPU: today (2 B height + 4 B shading, every patch) | 6 B a point | 109.6M | 658 MB |
| GPU: pixel-bounded (§2), resident patches only | 6 B a point | ~10-20M points | 60-120 MB |
| Undo, per-patch copy-on-write (§5), 50 steps | | | ~5-50 MB |
| App, fonts, GL driver, UI | | | ~250 MB |

- **Sculpt only, pixel-bounded GPU:** 219 + 25 + 120 + 50 + 250 = **~0.66 GB**.
- **Mask + all-over colour:** + 11 + 440 = **~1.1 GB**.
- **Today's renderer (phase 1 prototype):** 219 + 25 + 658 (on unified memory this is system RAM) + 250 = ~1.15 GB,
  plus the shading pass at open. Phase 1 measures this.

So 2 B a vertex for shape, 3 with a mask, 6 with colour everywhere: 2-6 B a vertex, 0.6-1.2 GB for 200M.

## 2. Pixel-bounded draw

- **Per-patch stride LOD.** A patch draws at stride `s = 2^k` (its grid every `s` points: `(n/s)^2` triangles),
  chosen so its micro edge projects to about one pixel (`targetPx`), clamped by the frame's triangle budget from
  the hardware profile. MicroRenderer does this per cluster today; phase 2 makes it per patch and makes the GPU
  *residency* follow it: a patch's GPU grid holds only the levels the view needs (coarse level-3 grids for
  everything, full grids for the patches whose projected edge needs them), using `FocusDetail::tileLevel(tri)`.
  Crack-free borders: the finer side snaps its edge points to the coarser edge (as now).
- **Budget.** At 1920x1080 about 2M pixels are covered at most: ~2-4M triangles drawn at one triangle a pixel,
  whatever the asset size. On the floor profile 2M (1M moving).
- **Shading on demand.** Shading (octahedral normal, curvature) is computed for resident patches at their resident
  level only, visible first; a coarse patch shades its 45 points, not 561 (cuts the open-time shading ~12x at
  Micro 5, ~180x at Micro 7).
- **Canvas cache** (perf-moonshots.md §7): with the screen cached, an idle frame composites one quad and a dab
  re-rasterizes only the patches under its dirty rectangle, so stroke frames cost the brush's pixels.
- **Orbit:** draw the moving budget (coarser stride, lower view resolution on the floor), refine to full on release.

## 3. Scale-adaptive dabs and lazy push-up

- A dab is applied at the level `l` where its radius spans **64-256 samples** (`r / edge(l)` in that range), not
  at the finest level. A 300 px brush at 200M lands on a level with ~10^4 points under it instead of ~10^6.
- With the multiresolution layout (§1) the finer levels are offsets from the smoothed coarser one, so the detail
  rides along with no write at all; Clay, Standard, Inflate and Move are pure coarse edits.
- Brushes that need fine values (Smooth of fine detail, Flatten to a plane, Pinch) read the finest level under the
  brush, so they apply at the finest level their radius allows (radius / 256 or coarser) and only there.
- **Lazy push-up:** where a coarse edit invalidates something fine (the shading grid, a cached finest-level
  position on the GPU, a Focus Detail tile's compressed copy), the patch is marked `stale(l)`. Visible stale patches
  are refreshed within the frame's budget, nearest the brush first; hidden ones by a background job at idle
  ("catch-up"), or when they become visible. Nothing is pushed into patches nobody looks at.
- **Checkpoint numbers to beat** (docs/performance.md, today): Standard 300 px 115 ms a step at 299M Micro 4,
  283 ms at 1.02B; phase 3 target < 8 ms CPU a dab at 200M on the laptop, < 16 ms on the floor profile.

## 4. Out-of-core compressed patches, visible-first

Reuse Focus Detail (`core/FocusDetail`, docs/design/focus-detail.md): patches as quantized, row-delta coded,
zstd-compressed tiles, a coarse level-3 grid per tile in compressed chunks, a directory, crash-safe header-last
saves, promotion 0.04 ms a tile. What 200M adds:
- **Visible-first open:** read the cage and the coarse chunks (a few MB), draw; then decompress the visible patches
  nearest the camera first; the rest stream in the background or stay compressed. First frame < 1 s.
- **Sparse CPU pool** (focus-detail.md §7 item 4): full grids only for promoted patches, indexed per patch, so RSS
  follows the focus, not the asset. Only required past ~1B on 16 GB; for 200M the dense array (219 MB) is
  affordable, so this is the last step, not the first.
- **Project integration:** `.sfproj` stores or references the detail file; Save writes dirty tiles only (53 ms).

## 5. Per-patch copy-on-write undo

A stroke saves, the first time it touches a patch, that patch's old heights (and mask / colour tile); the step is
the set of touched patches, delta-coded and bit-packed (the `MicroUndo` journal already does this, Morton
ordered). With coarse-level edits (§3) a big stroke touches thousands of coarse values instead of millions of fine
ones, so steps become **KBs per stroke** (today 0.6-11 MB for a 30-dab benchmark stroke, from rounding noise in
fine writes). Undo and redo apply the same deltas; Focus Detail promotes the patches first (`ensureFull`).

## 6. Lock-free patch-parallel jobs

- Patches are the unit of work and ownership: a job owns a patch for the dab (no locks inside), interior points
  are independent; only edge stitching reads a neighbour, done in a second pass after a barrier, edge by edge
  (each shared edge has one owner: the patch with the lower index).
- Dirty and stale state is an atomic bitset (`fetch_or`), so the solver, the renderer's sync and background
  catch-up never take a lock; the renderer swaps out the dirty words it consumed.
- A small job graph per frame (pick → dab kernels per patch → stitch → shade visible → upload) on the existing
  `ThreadPool`, with the block-ordered determinism it already guarantees; background catch-up and decompression
  run at low priority and yield to the stroke.
- Floor profile: 4 workers; the stroke's jobs run first, everything else fills idle time.

## 7. Phases and checkpoints

Every number twice: this laptop and the floor profile (`SF_HW_SIMULATE=floor`), at load < 2.

| Phase | What | Checkpoint (measurable) |
|---|---|---|
| **1** (this branch) | Memory-only 200M prototype on today's micro levels: `--benchmark core-200m` (off by default; writes the scene once, then opens it and orbits) | RSS, open time (project open, first frame, fully shaded), orbit p50 / p99, GPU MB: measured below |
| 2 | Pixel-bounded GPU: per-patch resident levels (coarse grids + full where needed), shading on demand, visible first; lower view resolution while moving (phase 1's p99 is per-pixel GPU cost, §8) | GPU < 150 MB at 200M; open to fully shaded < 2 s; orbit p99 < 33 ms on the floor profile; picture equal to today's at rest (screenshot test) |
| 3 | Multiresolution offsets + dabs at the 64-256-sample level + lazy push-up + background catch-up | 300 px dab < 8 ms CPU on the laptop, < 16 ms floor; stroke latency < 16 ms; p99 frame while sculpting < 33 ms floor; same result as the fine-level brush within quantization (test) |
| 4 | Focus Detail for 200M: compressed patches, visible-first open, project integration, sparse pool | open < 5 s floor (first frame < 1 s); file < 300 MB; RSS with colour < 1.2 GB |
| 5 | COW undo on coarse edits; atomic dirty/stale bits; per-frame job graph | undo < 100 KB a typical stroke; 4-thread speed-up > 3x on dab kernels; no lock on the stroke path (TSan clean) |
| 6 | Canvas cache (perf-moonshots §7) on top; real iGPU runs by testers (Windows UHD 620 / Iris Xe first) | idle frame ~0 ms GPU; floor numbers confirmed on real hardware before anything defaults on |

Nothing here merges into a release without the floor-tier numbers.

## 8. Phase 1 results

Run: `sculptforge --benchmark core-200m --benchmark-no-video` (Release). The scene: the bench head at 196,608 base
triangles (asked for 195,313; the head's cube quads round it), Micro 5, two octaves of `microNoise` (wrinkle- and pore-sized), in memory; written once as a project
in the swap folder, then each run opens it (`loadProject`), shows it, waits until the shading is complete and
orbits twice (10 s each, the view moving every frame).

**This laptop** (MacBook Pro 16" M1 Pro, 10 cores, 32 GB unified, Asahi Mesa 26.1.8 GL 4.6, framebuffer 3408x2136;
Release @ the branch's SHA; three runs back to back at 1-minute load 1.8 / 2.2 / 1.8, so at or just about load 2):

| | run a | run b | run c |
|---|---:|---:|---:|
| Micro triangles (196,608 base x 1,024, Micro 5) | 201.3M | 201.3M | 201.3M |
| Project file (written once) | 148 MB | | |
| Open project (`loadProject`) | 0.17 s | 0.17 s | 0.17 s |
| Open to first frame (shading and upload of every patch included) | 0.69 s | 0.69 s | 0.70 s |
| RSS when shown | 569 MB | 567 MB | 578 MB |
| Peak RSS (whole run) | 579 MB | 569 MB | 591 MB |
| GPU buffers (renderer, not in RSS; on unified memory this is system RAM too) | 640 MB | 640 MB | 640 MB |
| Orbit 1 (10 s, moving every frame): p50 / p99 | 12.4 / 51.7 ms | 12.9 / 35.9 ms | 12.6 / 32.1 ms |
| Orbit 2: p50 / p99 | 12.2 / 37.5 ms | 12.8 / 24.4 ms | 13.3 / 37.7 ms |
| Worst single frame | 68 ms | 66 ms | 67 ms |

Reading it:
- **Memory: met.** 0.57-0.59 GB RSS plus 0.64 GB of GPU buffers = ~1.2 GB in all for 200M, the top of the 0.6-1.2 GB
  range of §1, as predicted for today's renderer (every patch's full grid on the GPU). Phase 2 takes the 640 MB
  down to ~100 MB.
- **Open: met** (0.7 s to a fully shaded view, target < 5 s). The first frame shades every patch at once
  (~0.5 s at 110M points on 10 cores); on 4 floor cores that is ~1.3 s, still a single stall phase 2 removes.
- **Orbit: p50 met (~80 fps), p99 not yet.** p99 24-52 ms against < 33 ms; one 60-68 ms frame per run. The p50 is
  the per-cluster LOD doing its job (the draw follows the budget, not the 200M); the tail is not yet explained
  (candidates: the motion-proxy switch at the start of a move, shared-machine noise at load ~2, the 7.3M-pixel
  framebuffer). To profile in phase 2 before claiming "smooth".
- **Floor profile:** see the next section.

### Floor profile (`SF_HW_SIMULATE=floor`, research/perf-moonshots 94c9136f cherry-picked)

Before the run: the micro renderer did **not** read the governed `renderTriangleBudget()`. It took
`RenderSettings::renderTriangleBudget` (never set) or else `TierConfig::activeTriangleBudget`, which is 2M on the
Laptop tier, so a floor run would have drawn exactly what the laptop draws. Fixed on this branch: the micro budget is
now `min(tier budget, governed budget)`, so the governor and the floor profile can lower it and nothing ever raises
it (the laptop's draw is unchanged). `residentCacheCapMB` (1 GB on the floor) is read only by the out-of-core trim;
this in-memory scene never reaches it (RSS 0.58 GB), so it has no effect here.

Six runs interleaved laptop / floor, each started at 1-minute load < 2 (1.49-1.96):

| | laptop (3 runs) | floor (3 runs) |
|---|---|---|
| Open project / to fully shaded first frame | 0.16-0.19 / 0.69-0.72 s | 0.16-0.17 / 0.68-0.75 s |
| RSS when shown / GPU buffers | 567-583 / 640 MB | 579-582 / 640 MB |
| Governed budget at the end / triangles drawn while orbiting (p50) | 1-2M / 1.22M | 1M / 0.30-0.62M |
| Orbit p50 | 8.1-12.6 ms | 7.1-10.9 ms |
| Orbit p99 | 21.3-41.4 ms | 18.1-38.5 ms |
| Worst frame | 55-71 ms | 50-68 ms |

Orbit frame split (main thread, ms p50 / p99; sculpt and upload are 0.0 / <= 0.1 everywhere):

| | events | UI | draw (issue) | swap wait (GPU) |
|---|---|---|---|---|
| laptop, 6 orbits | 0.0 / 0.1-1.8 | 0.5-1.0 / 1.7-3.3 | 1.2-2.2 / 6.3-14.6 | 6.0-8.7 / 13.7-28.8 |
| floor, 6 orbits | 0.0 / 0.1-1.2 | 0.6-1.0 / 1.8-3.3 | 1.0-1.7 / 6.0-12.1 | 5.5-7.5 / 14.3-25.4 |

**Cause of the p99 miss: the GPU, per pixel, not per triangle and not the CPU.** 70-80% of a frame and nearly all
of its tail is the swap wait (the GPU finishing the frame); sculpt, upload and UI are negligible. The floor profile
draws a quarter of the triangles (0.3M against 1.2M) yet its swap wait drops only from ~8.7 to ~6.5 ms p50 and its
p99 barely moves, so the cost follows the pixels: a 3408x2136 framebuffer (7.3M pixels) where every fragment
interpolates the full-resolution shading grid from a texture buffer. The tail (one frame in 50-100 at 2-4x the
median) on top of that is GPU-side and varies run to run (18-41 ms), consistent with sharing the GPU with the
desktop at load ~2; the draw-issue p99 (6-15 ms) is the driver stalling on the same GPU work. What fixes it is
exactly the floor plan's pixel-side items: a lower view resolution while moving (perf-moonshots §0: 75%, not yet in
the simulation) and the canvas cache (§7); fewer triangles alone does not.

What the floor simulation does not show: a real iGPU's fill rate (it doesn't slow the GPU) and 4-core open time
(the open stayed 0.7 s; whether the first shading runs on the capped pool is not verified). Real UHD 620 / Iris Xe
runs by testers are what confirm it.

Reproduce: `sculptforge --benchmark core-200m --benchmark-no-video` (the first run writes
`~/.cache/sculptforge/core-200m-v1.sfproj`, about 3 s). Test: `micro_core200m_scene_small` (scene build, in memory,
project round trip bit-exact).
