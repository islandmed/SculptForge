# Focus Detail: a billion-polygon asset, sculpted locally at interactive rates (prototype)

Branch `research/focus-detail`. Status: **prototype, off by default** (`sculptforge --focus-detail FILE.sfdetail`,
`sculptforge --benchmark standard-1b`, `sculptforge-cli --bench-focus`). Owner of this doc: the data, memory and
streaming side. GPU drawing and stroke preview: [perf-moonshots.md](perf-moonshots.md) (idea #8 is the GPU half
of this design).

Comparison point: the publicly described idea of "HD" geometry in other sculpting apps (only a region you pick is
held at full resolution and you sculpt inside it). Focus Detail is our own design and differs on purpose: there is
no region to pick, the focus follows the brush and the camera on its own, ahead of time; out-of-focus detail stays
visible at a coarse level instead of disappearing; and the whole asset is one memory-mapped file that saves
incrementally.

## 1. Data model

We already have micro levels (`core/Micro.h`): above the top subdivision level, every base triangle carries a
regular grid of 16-bit scalar heights (`n = 2^level` segments per edge, `(n+1)(n+2)/2` points), displaced along the
interpolated normal of the Phong-tessellated base. Micro triangles are never stored. Brushes evaluate the grid on the
CPU (`MicroStroke`), the renderer in its vertex shader (`MicroRenderer`), and undo keeps per-tile deltas
(`MicroUndo` journal). Focus Detail reuses all of this. **A tile is one base triangle's grid.**

| | |
|---|---|
| Base mesh | positions, normals, triangles (62,208 triangles for standard-1b) |
| Tile | the base triangle's micro grid: level 7 = 8,385 points = 16,384 micro triangles |
| Quantization | int16 × `step` world units (`step` = base edge / 4096, as `microDivide`) |
| Coarse grid | level 3 (45 points), a *subsample* of the full grid: exact at its points |
| Compression | row-delta (each point less its row predecessor; a row's first less the point below), then zstd level 3 (or our tile codec when the build has no zstd) |

### The `.sfdetail` file (v1)

```
header (128 B)    magic SFDETAIL, version, codec, level, coarse level, counts, step,
                  base offset/bytes, directory offset/bytes, generation, garbage bytes, directory FNV-1a hash
base block        positions (V×12), normals (V×12), triangles (T×12)
tile blobs        one compressed full tile each
coarse chunks     1,024 coarse grids per compressed chunk
directory         per tile: offset, bytes, exact range (max |d|), coarse error (world units);
                  per chunk: offset, bytes
```

The file is opened read-only with `mmap` (POSIX; Windows isn't done yet), so the page cache holds the
recently read blobs. The directory carries each tile's **exact range**, so brushes find candidate triangles
(`triRange`) correctly even for tiles that aren't loaded. It also carries the **coarse error**, the most the
upsampled coarse grid is off from the full one, so the camera test needs no tile data.

## 2. Resident states

`MicroMesh::disp` keeps its full-size, contiguous layout, so every brush, the renderer and undo work unchanged.
Each tile is in one of these states:

| State | `disp` holds | Cost |
|---|---|---|
| **coarse** | the coarse grid upsampled: exact at the 45 coarse points, linear in between, in integer arithmetic | coarse grid always resident (5.6 MB for 62K tiles) |
| **full** | the decompressed tile | read from the mapped file |
| **dirty** | full and edited since the last save | |
| *parked* | a dirty tile that was demoted: compressed in memory until saved | ~4-8 KB |

**No cracks at the focus border.** A shared edge's upsampled points depend only on the edge's two coarse ends and
give the same integer sum from either side, so coarse–coarse edges are watertight. Coarse points are a subsample,
so corners match between coarse and full. When a tile is promoted, its real edge values are copied into coarse
neighbours. When it is demoted, it takes its full neighbours' real edges and gives its upsampled edges to coarse
neighbours (`shareEdges`). Tested: `focus_detail_open_is_coarse_then_full_is_exact`.

## 3. The focus: predictive promotion and demotion

`FocusDetail::update(mesh, view, brush, budgetMs)` runs once a frame (app: just before the GPU sync):

1. **Score every tile** (parallel, ~62K tiles):
   - **Brush:** distance from the tile's bounds to the brush's *path ahead*, from position *p* to
     *p + v × lookahead* (0.25 s). The X-symmetry mirror is included. A tile within 2 radii gets priority 10⁶ − distance.
     The app gets *p* from the hover pick each frame and *v* from the smoothed motion. So tiles are promoted while
     the cursor hovers, and ahead of a moving stroke.
   - **Camera:** a tile inside the frustum gets the score `coarseErr × focalPx / distance` (its screen-space error
     at the coarse level) if that is above `errorPx` (0.5 px).
2. **Promote** the wanted coarse tiles, highest score first, in parallel batches of 64, until the frame's time
   budget runs out (4 ms). The rest wait for the next frame (`lastPending`). Each promoted tile gets `markDirty`
   so the renderer re-uploads it.
3. **Demote by LRU** while the full tiles exceed `budgetBytes` (512 MB, perf-moonshots' figure for the upsampled
   copy). A demoted tile is one not wanted this frame or the last. Dirty tiles are never demoted during a stroke.
   The brush's own tiles may go over budget.

**Edits write only full tiles.** `MicroStroke::touch` (every dab and grab, before it saves undo tiles) and
`MicroUndo::swapWith` call `ensureFull(tiles, edit)`, which promotes on the spot and marks the tiles dirty. The
mutex makes that safe against the frame's `update()` (the predictive stroke solver runs dabs on another thread).
If prediction worked, `ensureFull` finds nothing to load. The benchmark counts the tiles it had to load while the
brush waited.

**Undo** needs nothing new: a stroke's `MicroUndo` already holds copy-on-write deltas of exactly the tiles it
changed. Undo and redo promote those tiles first, including from parked state. Undo of a tile that was saved and
then evicted reads its saved value and applies the delta.

**Saving** (`FocusDetail::save`) appends only the dirty and parked tiles, the coarse chunks that contain them, the
base block if the base cage moved (Move on a micro level moves the cage), and a new directory. Then comes `fsync`, then
the 128-byte header is rewritten in place, then `fsync` again. Until the header is written, the old directory is
still the valid one, so a crash keeps the last save. Garbage is counted in the header; compaction isn't done yet.

## 4. Prototype

| Piece | Where |
|---|---|
| File, codec, states, focus, save | `src/core/FocusDetail.{h,cpp}` |
| Hooks (2 lines) | `MicroStroke::touch`, `MicroUndo::swapWith`; `MicroMesh::focus` |
| Synthetic asset | `focusWriteSyntheticHead`: the standard scenes' procedural head (`makeBenchHead`), wrinkles + pores generated per tile, written in parallel batches; the full level is never in memory |
| standard-1b | `focusStandard1bPath()`: 62,208 base triangles × level 7 = **1,019,215,872 micro triangles**, cached in `~/.cache/sculptforge/standard-1b-v1.sfdetail` (written once, ~5 s) |
| App | `src/app/FocusDetailApp.cpp`: open `.sfdetail`, per-frame update; `--benchmark standard-1b` (proof mode, standard stroke set) |
| Benchmark | `sculptforge-cli --bench-focus [--bench-focus-base N --bench-micro-level L]` (`src/core/FocusBench.cpp`) |
| Tests | `tests/test_focus_detail.cpp`: exactness, watertight borders, damaged files, brush promotion, undo/redo, incremental save + reopen, moved base, prediction ahead, LRU budget, parking |

## 5. Measurements

M1 Pro (10 cores, 32 GB unified memory), Asahi Linux, Release, `nice -n 19`. The results are in
[§5.1](#51-results) (filled from `--bench-focus` and `--benchmark standard-1b`, run only at load < 2).

Methodology (`--bench-focus`):
- **Open**: map, read base + directory, decompress the 61 coarse chunks, upsample every tile, attach (`microAttach`), then BVH.
- **Promotion latency**: 256 single tiles through `ensureFull` (decompress + edges + markDirty), one thread.
- **Orbit + zoom**: 240 frames, a full turn at distance 2.2, then a zoom to 0.8, 1920×1080, 40°, 4 ms budget.
- **Strokes**: a figure eight over the face at 240 Hz, X symmetry, each stroke on a cold focus (everything demoted
  first), hover for 3 frames first. Clay at 3 sizes; Move along normals (writes tiles) and Move on the base cage
  (the default); Clay without prediction for comparison.
- **Save**: incremental after all strokes. **Focus off**: every tile decompressed, which is what a plain micro
  level holds.

### 5.1 Results

Measured 11-02 with `sculptforge-cli --bench-focus` (CPU only, 40 dabs a stroke) on a **shared machine, load average ~4**.
Treat each row as ±30%. A quiet-machine run and the app's `--benchmark standard-1b` (real GPU memory and orbit frame
times) are still to do; until then, the GPU rows are estimates from the renderer's 6 bytes per point.

standard-1b: **1,019,215,872 micro triangles** (62,208 base triangles, micro level 7, 522M grid points).

| | Result |
|---|---:|
| File on disk | 241 MB (0.24 B per micro triangle); written in 5.4 s |
| **Open total** (map, base, coarse grids, upsample, BVH) | **0.41 s** |
| RSS after open (focus on) | 1,147 MB; 1,256 MB after orbit + zoom |
| RSS with every tile full (focus off) | 1,526 MB (decompressing all of them: 0.31 s) |
| Promote one tile (decompress + edges + markDirty), p50 / p99 | 0.041 / 0.051 ms |
| Promotion throughput, parallel | 65,540 tiles/s ≈ 1.07 G micro triangles/s |
| Orbit + zoom, 240 frames: focus update p50 / p99 / max | 0.78 / 4.42 / 5.53 ms |
| Orbit: tiles promoted; most waiting in one frame | 11,338; 1,132 (caught up within frames) |
| Full tiles after zooming in | 11,594 of 62,208 (194 MB of the 512 MB budget) |
| GPU buffers, today's renderer (estimate) | 3,130 MB |
| GPU buffers, per-tile levels with this focus (estimate, perf-moonshots #8) | 597 MB |

| Stroke (X symmetry, 240 Hz) | dab p50 | **dab p99** | tiles loaded while the brush waited | undo / redo | undo step |
|---|---:|---:|---:|---:|---:|
| Clay r 0.03 (~25 px) | 2.65 ms | **5.25 ms** | 225 | 27 / 27 ms | 2.6 MB |
| Clay r 0.08 (~60 px) | 3.66 ms | **5.46 ms** | 620 | 25 / 24 ms | 5.6 MB |
| Clay r 0.16 (~120 px) | 9.11 ms | **14.68 ms** | 1,656 | 53 / 53 ms | 12.2 MB |
| Move r 0.1, micro surface | 1.07 ms | **1.57 ms** | 0 | 5 / 5 ms | 0.2 MB |
| Move r 0.1, base cage (default) | 0.01 ms | **0.02 ms** | 0 | 0 / 0 ms | – |
| Clay r 0.08, no prediction | 3.77 ms | 6.83 ms | 542 | 25 / 24 ms | 4.6 MB |

| Save | Result |
|---|---:|
| Incremental save (1,687 dirty + 2,574 parked tiles) | 53 ms, 17.8 MB appended |
| Whole file, for comparison | 5.4 s |

The brief's targets: open < 10 s (0.41 s), RAM < 6 GB (1.1-1.5 GB CPU; adding the estimated 3.1 GB of GPU buffers on
unified memory gives ~4.6 GB), Clay and Move p99 < 50 ms (≤ 15 ms) are all met in this run. Note that the run
above predates mirrored prediction and the hover frames. The "tiles loaded while waiting" column was measured
without them, so it overstates what prediction misses. The quiet run will remeasure it.

## 6. Honestly: what works, what's faked, what's missing

**Works (tested, measured):**
- A 1.02-billion-micro-triangle head opens from a 241 MB file in well under a second and sculpts with Clay and Move.
  Tiles under and ahead of the brush come in before the brush gets there.
- Watertight focus borders; edits, undo/redo and parking never lose detail; incremental save in tens of ms against a
  full rewrite in seconds; crash-safe header-last commit.

**Faked (LOD):**
- Out-of-focus tiles show their **level-3 grid upsampled**: forms and wrinkles a few base edges across survive;
  pores and finer detail are flat until the tile is promoted. Their shading also comes from the upsampled grid, so
  it is smooth. At a framing view the coarse error is far below a pixel, so nothing is visibly lost. Zoomed in, the
  screen-error test promotes what's visible within ~1-3 frames.
- The renderer still draws by its own per-cluster LOD and triangle budget (`MicroRenderer`). That's what makes
  orbiting interactive; Focus Detail doesn't change drawing.

**Missing / not true yet:**
- **CPU memory isn't sparse.** `disp` stays a dense 2 bytes per point (1.04 GB at 1B) because every consumer indexes it
  directly. Promotion saves decompression and I/O, not RAM. At 1B this is fine (RSS ~1.2-1.5 GB). At 4B+ it isn't.
- **GPU memory doesn't scale with the focus.** The renderer uploads every tile's full grid (6 B a point: ~3.1 GB at
  1B; on unified-memory machines that's system RAM). Per-tile GPU levels are perf-moonshots #8 (estimate: 3.1 GB →
  ~0.6 GB with this focus).
- **First shading** of all tiles in the renderer (~5 s at 1B Micro 5, docs/performance.md) still runs at open,
  coarse or not. Shading coarse tiles at their 45 points would cut it ~180×.
- **Not wired into projects:** a `.sfproj` doesn't reference a `.sfdetail`; the app has no Save for it yet (the core
  `save()` is tested). Compaction of garbage, Windows mapping, masks on streamed tiles (`mask` stays dense), layers,
  and multiple streamed Parts aren't done.
- **Today's micro level already meets the brief's 1B targets** (docs/performance.md: 1.02B at Micro 5, RSS 1.7 GB,
  open 0.5 s). Focus Detail at 1B wins on disk size, save time and the scaling path, not on RAM.

## 7. Recommended 0.11 scope

Data, not wishes. At 1B the dense CPU array (1 GB) is affordable; **the GPU copy (3.1 GB) and the renderer's first
shading of every tile (~5 s at 1B) are what limit us.** So, in order:

1. **Per-tile GPU levels** (perf-moonshots #8): coarse tiles hold 45 points on the GPU, not 8,385. Estimate 3.1 → 0.6 GB
   at 1B. This is what makes 4B+ fit at all. Uses `FocusDetail::tileLevel()`, which already exists.
2. **Shade coarse tiles at their own level** (45 points, then upsample): cuts first shading ~180×, so open-to-sculpt
   becomes about a second.
3. **Projects and Save in the app:** `.sfproj` references its `.sfdetail`; Ctrl+S calls `FocusDetail::save()` (53 ms
   measured); compaction when garbage passes half the file.
4. **Sparse CPU pool** (full tiles only, indexed per tile) once 1 and 2 land. Only needed beyond ~2B on 16 GB machines.
5. Windows mapping (`CreateFileMapping`), masks on streamed tiles, and multiple streamed Parts.

Leave for later: a middle level between coarse and full (the screen-error test promotes fast enough: ~65K tiles/s),
and a GPU decompressor.
