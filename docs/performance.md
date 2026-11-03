# Performance figures

Every number here comes from the benchmark built into SculptForge, so anyone can reproduce it:

```
sculptforge --bench [--bench-sizes 4,16,33] [--bench-dabs 40] [--bench-no-project] [--bench-out report.md]
sculptforge --bench-view                        # only the viewport's costs
sculptforge --bench-brushes large               # only the benchmark rows whose name contains "large"
sculptforge-cli --bench ...                     # the same without the GPU (CI, servers)
```

Machine for all figures: Apple M1 Pro (8 performance + 2 efficiency cores, 32 GB unified memory, 30 GB usable
under Asahi Linux), integrated GPU, Mesa 26.1 (OpenGL 4.6). How the benchmark works is described under v0.5 below.

## 0.10.9: background dabs, Move once a frame (perf/bigbrush-p99)

`--benchmark standard` placed every stroke in the frame loop (predictive strokes are off by default), and on the
33M head one 240 px dab moves 3-11M vertices (the symmetric brush covers a large part of the face). Where one such
frame went (`SF_FRAME_LOG=40 SF_STROKE_LOG=1 SF_SYNC_LOG=1`): the dab 70-220 ms (kernel 20-100, normals 40-100,
BVH refit 10-30), then the vertex upload 40-110 ms (packing 30-75, cluster bounds 12-32), then the swap. Every step
was already threaded and near memory bandwidth, so the work can't fit a frame; it has to leave the frame.

From 2M vertices a sculpt stroke now uses the solver thread without the preview (`App::backgroundDabs`,
`SF_BACKGROUND_DABS=0/1`): the same dabs in the same order, packed into the two-part upload there. A batch is about
a frame of work (at least 10 ms), the frame waits up to 12.5 ms for it (so light dabs still show in the frame that
placed them) and otherwise keeps drawing, and the next frame commits it. The same dabs through either path give the
same mesh, bit for bit, positions and normals (`ui_stroke_background`; `--measure-stroke 3,120` and `3,240` with
`SF_MEASURE_VERIFY=1` on a subdivided sphere). The benchmark scene itself isn't reproducible to that level: two
frame-loop runs with every dab placed differ by up to 0.26, as much as one of each. Frame p99 / worst stall ms,
before (quiet) -> after (provisional: load 3-4, the quiet run is still to do):

| Phase | 15M | 33M |
|---|---|---|
| Clay large | 77 / 136 -> 31 / 34 | 139 / 154 -> 31 / 40 |
| 240 px brush | 139 / 172 -> 31 / 36 | 306 / 325 -> 33 / 41 |
| Pen lift (240 px) | 20 ms -> 0 | 162 ms -> 0 |

On the sphere (`new_sphere subdivisions=9`) at 120 px (`--measure-stroke 3,120`, background dabs forced) the
stroke places as many dabs (156 vs 141 in the frame loop) with about the same lag (62 vs 53 ms). Each 240 px dab at 33M still takes 100-200 ms, so the surface
trails the pointer by about a dab while the view stays smooth.

## Next version: predictive strokes (feat/zero-latency)

A stroke on a heavy mesh used to run in the frame loop: each frame placed a dab (45-50 ms at 21M with a 200 px
brush), re-packed what it moved for the GPU (about 28 ms) and only then drew. Now the brush runs on its own
thread and the frame loop only commits what it finished, so the view draws at the display's rate whatever a dab
costs; what the brush hasn't finished is drawn ahead of it by the GPU.

```
tools/bench_strokes.sh [-s 5,21,33] [-n RUNS] [-r RADIUS_PX] BINARY [BINARY_B]   # A/B runs interleaved
```

It runs `sculptforge --run SCENE --measure-stroke 3,R` (a fast two-turn circle, X symmetry) on 5.2M
(`new_sphere subdivisions=8; subdiv_divide`), 21.0M (two Divides) and 33.6M triangles
(`new_sphere triangles=2100000; subdiv_divide; subdiv_divide`) and averages SculptForge's own figures.

**How to read the figures below.** They were taken on the M1 Pro while other agents were building and testing on
the same machine (load average 9 to 45 on 10 cores during the runs), so absolute times are two to four times
those of an idle machine and vary from run to run (0.9.7 measured 65 ms frames at 21M on an idle machine; here
71-180 ms). A and B were interleaved run by run, two runs each, so they saw the same load. "A" is main (0.9.7)
with only the measurement additions, "B" this branch with Predictive Strokes on (the default).

- Frame: the interval between frames on screen, mean / p95 / p99 (120 Hz display, vsync).
- Surface lag: how far round the circle the last dab whose result is on the GPU is behind the pointer.
- Preview lag: the same for the preview's head (B; for A it is the surface).
- Press: from the press to the first frame on screen that shows it (B: the preview of the first dab); first
  surface: until the first dab's own result is on screen.
- Release: how long letting go holds up the UI; until done: until the last dab's result is on screen.

200 px brush:

| Mesh | | Frame mean / p95 / p99 | Surface lag | Preview lag | Press | First surface | Release | Until done |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 5.2M | A | 47.0 / 98.5 / 134 ms | 95 px | 95 px | 98 ms | 98 ms | 31 ms | 44 ms |
| | B | **8.4 / 11.3 / 14.3 ms** | 72 px | **1.0 px** | **24 ms** | 68 ms | **0 ms** | 64 ms |
| 21.0M | A | 71.0 / 245 / 651 ms | 37 px | 37 px | 623 ms | 623 ms | 1.4 ms | 8 ms |
| | B | **8.8 / 14.1 / 17.6 ms** | 198 px | **1.5 px** | **14 ms** | 144 ms | **0 ms** | 224 ms |
| 33.6M | A | 169 / 311 / 323 ms | 245 px | 245 px | 315 ms | 315 ms | 374 ms | 493 ms |
| | B | **12.2 / 22.8 / 27.0 ms** | 307 px | **4.1 px** | **13 ms** | 264 ms | **0 ms** | 500 ms |

60 and 240 px brushes (the sizes proof mode reported stalls with):

| Mesh, brush | | Frame mean / p95 / p99 | Surface lag | Preview lag | Press | Release | Until done |
|---|---|---:|---:|---:|---:|---:|---:|
| 21.0M, 60 px | A | 14.8 / 22.1 / 26.2 ms | 16 px | 16 px | 26 ms | 5 ms | 10 ms |
| | B | **8.4 / 9.9 / 11.4 ms** | 31 px | **0.7 px** | **8 ms** | 0 ms | 33 ms |
| 33.6M, 60 px | A | 24.9 / 31.6 / 34.8 ms | 48 px | 48 px | 34 ms | 31 ms | 42 ms |
| | B | **8.9 / 14.3 / 17.2 ms** | 63 px | **1.9 px** | **13 ms** | 0 ms | 82 ms |
| 21.0M, 240 px | A | 215 / 433 / 433 ms | 283 px | 283 px | 582 ms | 544 ms | 675 ms |
| | B | **10.2 / 18.5 / 23.8 ms** | 465 px | **3.1 px** | **21 ms** | 0 ms | 1132 ms |
| 33.6M, 240 px | A | 292 / 636 / 636 ms | 226 px | 226 px | 779 ms | 826 ms | 1362 ms |
| | B | **12.8 / 27.2 / 31.1 ms** | 337 px | 117 px* | **11 ms** | 0 ms | 1689 ms |

\* One of the two runs had a preview lag of 230 px, the other 4 px; repeated runs with the head logged gave
4 px and couldn't reproduce it (a tiling window manager resizing the window mid-stroke, which other agents'
windows did during these runs, also moves the canvas under the scripted circle).

What the figures say:

- **Frames no longer depend on the brush.** B's frames are 8-13 ms on average at every size, p99 11-31 ms,
  where A's worst frames were 0.3 to 1.2 s (the solver and the upload ran inside the frame). The long stalls proof
  mode saw are gone: the biggest was a vertex leaving the GPU's position grid (a big brush raising the surface),
  which re-packed the whole mesh inside a frame (seconds at 33M); it is re-packed on the brush thread now, and
  the stroke's end work (exact culling bounds of everything it moved) runs there too, so release is 0 ms.
- **The brush trails the pointer by a few pixels on screen**, where it trailed by 40-280 px: the preview shows
  every dab not yet solved, and the pen's next position. The surface itself (the solver's result) still trails
  by as much as before, or more on a loaded machine (B batches dabs so that packing stays under half the work,
  so the real surface updates less often); when it lands it replaces the preview without a visible jump: with
  `SF_MEASURE_POP=1` the view changes by 0.055 (mean, 0-255) when a batch replaces its preview, against 0.36 if
  there were no preview (5M, 200 px).
- **Press** is the first frame (8-24 ms, the preview of the first dab); the first dab's own result follows when
  the solver has it.
- **Until done** is longer for B with big brushes: B places every queued dab after the pen lifts (the preview
  already shows them) and A's frame loop thinned more. It doesn't hold anything up: the UI keeps going, and a new
  stroke or command first finishes the old one.
- The targets set for this work were 33M with a 200 px brush at 60 fps (frames under 16.7 ms), the brush under
  20 px behind the pointer, press and release under 50 ms. Met on this loaded machine: press (13 ms), release
  (0 ms), the brush on screen behind the pointer (4 px), the mean frame at 21M (8.8 ms). Not met: 33M frames are
  12.2 ms mean but 22.8 / 27.0 ms at p95 / p99. Those frames wait on the GPU (`SF_FRAME_LOG=16`: the time is
  in the swap, drawing 33M triangles; the preview costs about 1-2 ms of it), not on the CPU.

### Where a batch's time goes (21M, 200 px, `SF_STROKE_LOG=1`, loaded)

The solver thread: the dab 160-300 ms (kernel 50-115, normals 65-150, BVH 25-50), then packing the upload
110-180 ms before this work, 25-45 ms after it (vertices 25-40, bounds under 1.5). On an idle machine divide by
about four. The main thread's commit (glBufferSubData of 30-55 MB): 2-3 ms, occasionally 10.

What changed, in order:

1. **Predictive strokes** (app/AsyncStroke.cpp). The queued dabs go to a solver thread in batches; each frame
   the finished batch is committed and the next one starts. The mesh, its BVH and the sculptor belong to the
   solver while it runs (syncScene and the pollers wait; every path that edits the document ends the stroke
   first). The dabs, their order and parameters are the frame loop's (each batch sees the view as it was when it
   began), so the result is the same bit for bit: `SF_MEASURE_VERIFY=1` replays the measured stroke without the
   thread and checks undo.
2. **The preview** (render/Ghost.h, the GHOST shader variant). The dabs not yet on the GPU (in flight, queued, and
   the stroke's head where the pen will be a frame later, core/PenPredictor.h) move the uploaded vertices as each
   brush kernel would; the kernels are differentiated forward (height gradient and Laplacian over the tangent
   plane) for the bent normal and the cavity term, with no extra varyings (a tiled GPU writes every vertex's
   outputs to memory). Each dab's surface point comes from a sphere fitted to the last two placed dabs (their
   normals turn by an angle over the distance between them): centre within 0.003-0.07 r, normal within 1-5
   degrees (measured with a debug log since removed). Only the clusters a ghost dab reaches use it. The brush cursor is drawn at the
   predicted pen position too.
3. **Two-part upload** (Renderer::prepareStrokeUpload / commitStrokeUpload). The solver thread packs the dirty
   vertices and the culling bounds; the GL thread only calls glBufferSubData. The cavity term comes from the
   normals pass (Mesh::curvatureAt from each vertex's triangles, the ring average on a manifold) instead of a
   second walk of every vertex's ring (packing 110 -> 30 ms a batch at 21M, loaded). Clusters a stroke moved get
   bounds round their vertices' boxes (found while packing) without a normal cone; exact ones at the end.
4. **No stalls**: the position grid re-pack and the end-of-stroke bounds on the solver thread (above); batches
   sized so packing stays under half the work, and after release every dab left in one batch with the end
   work (21M, 240 px: until done 2.9 -> 0.6 s).
5. **Mesh::recomputeAllNormals** sums each vertex's own triangles in parallel when the topology is known (same
   order, same bits, no face array): it summed on one thread, which dominated full-history timeline seeks.

Not done: persistent-mapped upload buffers (the commit is 2-3 ms, not worth the complexity on GL 3.3 / macOS
4.1); per-vertex dirty bitsets per cluster beyond what UploadOrder already does; normals in the shader (they
are already 2 x 11-bit octahedral in 12-byte vertices since v0.6). The CPU dab itself (kernel, normals, BVH) is
unchanged: it bounds how soon the real surface catches up, not the frame rate.

### Draft strokes (33.6M, measured both ways)

`SF_DRAFT_STROKE=1` (Preferences > Performance > Draft Strokes): dabs only move vertices and refit the BVH;
the moved clusters draw with per-pixel face normals; normals, cavities and exact bounds are worked out once at
pen lift, on the solver thread. Interleaved, two runs each, A = draft off, B = draft on:

| Brush | | Frame mean / p95 / p99 | Surface lag | Press | First surface | Until done |
|---|---|---:|---:|---:|---:|---:|
| 200 px | off | 12.4 / 24.6 / 28.6 ms | 438 px | 14 ms | 476 ms | 1182 ms |
| | draft | 13.6 / 27.0 / 30.6 ms | 445 px | 38 ms | 523 ms | 2985 ms |
| 240 px | off | 12.8 / 26.6 / 30.9 ms | 281 px | 18 ms | 757 ms | 1988 ms |
| | draft | 13.8 / 29.0 / 32.1 ms | 427 px | 22 ms | 719 ms | 3070 ms |

On this machine draft strokes don't pay: the frames were never CPU-bound (the solver is on its own thread), the
dab saves its normals pass but the stroke's end then recomputes the normals of everything the stroke touched,
so the last result arrives later. (The draft runs coincided with the heaviest load of the session, 18-45.)
It should help where the solver's cores are the bottleneck and energy matters (a laptop on battery, fewer
cores): off by default, for the hardware tiers to turn on.

## Micro-displacement levels: 300M-1B sculptable triangles (feat/micromesh)

```
sculptforge --bench-micro --bench-sizes 33,100,300 [--bench-micro-level 5] [--bench-brushes TEXT]
sculptforge-cli --bench-micro ...      # without the GPU: memory, brush and shading times
```

A micro level (core/Micro.h) stores the surface above the highest subdivision level as one 16-bit height per
point of a regular grid on each base triangle, along the interpolated normal of the Phong-tessellated base.
Micro triangles are never stored: brushes evaluate them on the CPU, the renderer in its vertex shader
(render/MicroRenderer.h). The benchmark divides a bench sphere to the size, sculpts strokes of 30 dabs with X
symmetry (two dabs a step) and orbits. M1 Pro, Release; the machine was shared with other jobs during these runs
(load average 10-20), so treat single rows as +/-30%.

Memory and frames:

| | 33M (Micro 4) | 104M (Micro 4) | 299M (Micro 4) | 299M (Micro 5) | **1.02B (Micro 5)** |
|---|---:|---:|---:|---:|---:|
| Base triangles | 130k | 406k | 1.2M | 292k | 995k |
| Micro level in memory | 42 MB | 130 MB | 374 MB | 332 MB | 1131 MB |
| per micro triangle / micro vertex | 1.25 / 2.50 B | 1.25 / 2.50 B | 1.25 / 2.50 B | 1.11 / 2.22 B | 1.11 / 2.22 B |
| Process RSS, everything | 263 MB | 432 MB | 801 MB | 710 MB | **1713 MB** |
| GPU buffers | 125 MB | 392 MB | 1128 MB | 997 MB | 3398 MB |
| Divide to micro | 5 ms | 32 ms | 60 ms | 29 ms | 93 ms |
| First shading + upload | 0.1 s | 0.7 s | 1.2 s | 1.7 s | 5.1 s |
| Orbit, 4x AA | 8.3 ms | 9.7 ms | 14.0 ms | 8.9 ms | 13.4 ms |
| Orbit, no AA / moving | 5.0 / 2.9 ms | 6.6 / 4.7 ms | 10.4 / 4.5 ms | 6.2 / 4.1 ms | 11.3 / 5.0 ms |
| Triangles drawn (still / moving) | 4.3M / 1.1M | 3.9M / 1.0M | 3.4M / 0.9M | 3.4M / 0.9M | 3.0M / 0.8M |
| Save / open project | 31 / 61 ms | 71 / 100 ms | 217 / 285 ms | 187 / 264 ms | 649 / 489 ms |

The same 300M as a plain mesh would need about 27 GB (90 B per triangle, see 33M below). The frame time follows
the triangle budget and the number of base triangles on screen, not the micro triangle count: 33M at Micro 4
and 299M at Micro 5 (130k and 292k base triangles) both draw in 8-9 ms. Patches of one or four triangles are
drawn unrolled (gl_VertexID), not instanced: instancing them cost 21 ms a moving frame at 299M, unrolled 4.4 ms.

Brushes, per dab step (both mirrors), GPU upload included:

| | 33M | 104M | 299M (Micro 4) | 299M (Micro 5) | 1.02B (Micro 5) |
|---|---:|---:|---:|---:|---:|
| Standard (r 0.12, ~64 px) | 2.4 ms | 11 ms | 13 ms | 17 ms | 39 ms |
| Standard, 300 px (r 0.56) | 13.5 ms | 56 ms | 115 ms | 48 ms | 283 ms |
| Smooth (large, r 0.4) | 9.4 ms | 21 ms | 50 ms | 44 ms | 54 ms |
| Move, 300 px (base cage) | 13 ms | 19 ms | 58 ms (45 after the pick fix) | 22 ms | 55 ms |
| Undo step, Standard stroke (30 dabs) | 0.6 MB | 1.7 MB | 4.7 MB | 3.4 MB | 11.2 MB |

Scale-adaptive brushes: a dab is evaluated on grid points `stride` apart (about 48 across its radius) and
interpolated in between, so the evaluation (the "kernel" column of the report) stays bounded; writing the
interpolated values, stitching and the GPU upload still grow with the points under the brush. The 300 px grab
(Move) deforms the base cage and doesn't touch the micro level at all: 20 ms at 31M against 45 ms at 299M (Micro 4,
ten times the base triangles) in one run; at Micro 5 (2.4x the base) 22 ms. Picking at 299M went from 37 to 11 ms
once the candidates were filtered by their own displacement range along the ray.

Undo journal: tiles in Morton order, each the stroke's change predicted from its neighbour and packed (minimum +
bit width): about a third of whole tiles. A short stroke stays under the 500 KB target (10 dabs of r 0.15 at
5M: 31 KB); the benchmark's 30-dab strokes over 300M are 2-5 MB, limited by the rounding noise each dab leaves
(2-4 bits per point). A whole-surface edit (micro_noise) keeps whole tiles.

What a frame still costs: the CPU shading (normals, curvature) of what a dab changed, about 20 ns per grid point,
within a budget per frame (TierConfig::shadeBudgetPoints, 3M: the rest of a big dab's normals follow over the next
frames); the first full shading of a level (1-5 s). Candidates: shading on the GPU from the displacement texture
(needs the edge neighbours in the fragment shader), and storing the coarse part of big strokes on a coarse grid
(multires-style) so a 300 px dab writes thousands of points instead of millions.
## Hardware profile, calibration and the frame governor (feat/hw-profile)

SculptForge measures the computer it runs on and sets its budgets from what it measured. Code:
`src/core/HardwareProfile.*` (probe types, mapping, cache, JSON, commands), `src/core/TierConfig.*` (the knobs other
modules read), `src/core/FrameGovernor.*` (run-time adaptation), `src/app/Calibration.*` (OpenGL probe and GPU
benchmarks, `--calibrate`), `src/app/PanelPerformanceProfile.cpp` (first-launch flow, Preferences > Performance).
Nothing in it uses the network.

### When it runs

On first launch, and again when `GL_VENDOR`, `GL_RENDERER` or `GL_VERSION` (which carries the driver version)
or the app version changes, or when the cache holds only a CPU calibration from `sculptforge-cli`. The result
is cached in `<settings folder>/hardware_profile.json` (Linux `~/.local/share/SculptForge/SculptForge/`, macOS
`~/Library/Application Support/SculptForge/SculptForge/`, Windows `%APPDATA%\SculptForge\SculptForge\`). In the
app it runs over the first frames, one step per frame under a small overlay (Esc or Skip skips it; the profile
then comes from the probe alone). UI tests, renders, recordings, demos and the `--measure-*` runs never
calibrate and keep the fixed defaults (Fast Navigation at 10 million triangles), so their results don't depend
on the machine. `SF_NO_CALIBRATION=1` skips it too.

```
sculptforge --calibrate [--calibrate-quick] [--calibrate-no-save]   # prints JSON (schema sculptforge.calibration/1)
sculptforge-cli -c 'calibrate'              # CPU half only (no OpenGL); GPU numbers from the cache
sculptforge-cli -c 'performance_profile'    # the cached profile as JSON
sculptforge-cli -c 'set_performance_override name=render_triangle_budget value=12000000'   # value=auto clears
```

The same three commands are in the command registry, so the console, scripts and MCP clients have them; in the
app `calibrate` measures the GPU too.

### What it probes

| | How |
|---|---|
| GPU | `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`, `GL_MAX_TEXTURE_SIZE`, timer queries (`GL_TIME_ELAPSED`) |
| Memory type | Software (llvmpipe, softpipe, SwiftShader, GDI Generic, Microsoft Basic Render), unified (Apple GPUs on macOS and Asahi), integrated or discrete, from the renderer string, refined by `GLX_RENDERER_UNIFIED_MEMORY_ARCHITECTURE_MESA` / `..._ACCELERATED_MESA` where available. Only the memory type comes from names; tiers come from measurements and memory sizes |
| Video memory | the first that answers: `GL_NVX_gpu_memory_info` (NVIDIA; Mesa's radeonsi and asahi too), `GL_ATI_meminfo` (free memory only), `GLX_MESA_query_renderer` (Mesa on X11), amdgpu's `mem_info_vram_total` in sysfs (Linux, also on Wayland), DXGI (Windows), CGL (macOS). Unified memory counts at most two thirds of the RAM (Metal's working set), whatever the driver reports |
| Machine | RAM, hardware threads, SIMD (SSE4.2 / AVX2 / AVX-512 on x86, NEON / SVE on ARM) and what the build uses, OS, CPU brand (x86 cpuid, macOS sysctl, the device tree on ARM Linux) |

### What it measures (about 0.5-1.5 s of work; medians of 5 runs, 3 quick)

| Measurement | Work |
|---|---|
| Triangle throughput | a 512 x 256 quad grid (262k triangles, about 4 pixels each, like a whole sculpt on screen) drawn instanced into a 1024 x 1024 offscreen target, every instance in front of the last; the instance count is chosen for about 25 ms a run; GL timer queries, else glFinish around the CPU clock |
| Upload bandwidth | `glBufferSubData` of a 32 MB buffer, waited for (glFinish) |
| Brush dab | 20 Standard dabs (X symmetry, radius 0.12) on a generated 1M-triangle sphere, undone between runs |
| Memory bandwidth | `memcpy` of 64 MB |

### Profiles

Two main tiers and a safe mode:

- **Workstation**: 64 GB or more of RAM (60 GB reported) and either a discrete GPU with 16 GB or more (15 GB
  reported) or a GPU sharing the system memory (Apple Silicon Max / Ultra, big APUs), whose pool counts as two
  thirds of the RAM; and in both cases at least 1000M triangles/s, dabs under 3 ms, memory over 4 GB/s.
- **Laptop / Budget**: everything else (8-32 GB RAM, 2-8 GB GPUs, Apple Silicon with less than 64 GB, thermal and
  battery limits).
- **Software** (safe mode): a software renderer, or under 60M triangles/s.

A re-calibration of the same GPU keeps the previous tier while the new triangle throughput is within 15% of the
boundary, so a busy machine doesn't flip between tiers. Settings, before overrides:

| Setting | How |
|---|---|
| `render_triangle_budget` | half a 60 Hz frame of the measured throughput, clamped per tier (Software 0.25-4M, Laptop 2-40M, Workstation 8-600M) |
| `motion_proxy_threshold` | the budget (at least 262k): scenes over it draw the motion proxy while the view moves |
| `vram_budget_mb` | 80% of dedicated memory; unified: two thirds of RAM; integrated: half of RAM; software: a quarter |
| `upload_budget_mb_per_frame` | a quarter of a 60 Hz frame at the measured upload bandwidth (4-1024 MB) |
| `max_texture_size` | the GL limit, at most 4096 / 8192 / 16384 by tier, 8192 under 2 GB of GPU memory |
| `max_subdivision_triangles` | the smaller of half the RAM at 100 bytes per triangle and the GPU budget at 16 bytes per triangle, capped by tier |
| `micro_triangle_limit` | a quarter of the GPU budget at 8 bytes per micro-triangle, capped by tier |
| `threads` | hardware threads (at most 16, the pool's limit); half in safe mode, where llvmpipe draws on the same cores. Applies at the next start |
| `target_fps` | 60, 120 or 144 from the display (`TierConfig::targetHz`) |

Every setting can be overridden by hand (Preferences > Performance > Manual overrides, or `set_performance_override`),
and overrides survive re-calibration.

### TierConfig (core/TierConfig.h)

What feat/micromesh and feat/zero-latency read, through `currentTierConfig()` (thread-safe; `tierConfigRevision()`
changes when it does):

| Field | Software | Laptop / Budget | Workstation |
|---|---|---|---|
| `residentCacheCapMB` | RAM / 8, at most 1 GB | RAM / 4, at most 4 GB | RAM x 2/5, at most 64 GB |
| `activeTriangleBudget` | 0.5M | 2M | 8M+ (half the render budget, at most 64M) |
| `uploadBudgetMBPerFrame` | the profile's | the profile's | the profile's |
| `draftBrushMode` (normals and cavity at pen release) | off | off | off (the Draft Strokes setting turns them on) |
| `zeroCopyUMA` | the GPU shares system memory: unified or integrated, in any tier (a 64 GB+ Apple Silicon Workstation too) | | |
| `targetHz` | 60 | 60 / 120 / 144 from the display | 60 / 120 / 144 from the display |

The app reads the power source with `SDL_GetPowerInfo` at start and every 5 seconds. On battery, whatever the tier
(an Apple Silicon Workstation is a laptop too): no draft brushes (they are a manual setting),
half the upload budget and 60 Hz (unless set by hand), and the status bar says so. The `draft_brush` override
(on / off) wins over both. `draftBrushMode` is a flag for the brush engine; the brushes don't read it yet.

### Render triangle budget and the frame governor

`renderTriangleBudget()` (core/HardwareProfile.h) is the number of triangles the 3D view may draw per frame to
hold the target frame rate; `motionProxyThreshold()` is where Fast Navigation switches big scenes to their
motion proxy. The frame governor (core/FrameGovernor.h) adjusts both from real frames:

- Inputs per drawn frame: CPU time from input to swap (without the vsync wait), GPU time from a `GL_TIME_ELAPSED`
  query read up to 4 frames later (never waiting), triangles drawn, the scene's full triangle count, whether
  the view is moving and whether the proxy was used. Only moving frames count: still frames draw everything in
  full and the governor can't change them. Frames over 250 ms (dialogs, loading) are ignored.
- Every 20 counted frames it takes the medians. Above the target frame time it lowers the budget (aiming at 80%
  of the frame, at most halving it); under 60% it raises it (at most 1.25x) when the budget is what limits the
  view. In between it holds (the dead band). After a change, 5 frames are ignored.
- Raising the budget past the scene's size would switch the proxy off; it only does when the predicted full
  frame stays under 90% of the target, so the view never flips between proxy and full detail.
- Bounds: an eighth to four times the profile's budget. It changes only how much is drawn, never the mesh.

Today's renderer uses the governed threshold for the motion proxy (Preferences > Performance > Fast Navigation;
`SF_FAST_NAVIGATION_TRIANGLES` still overrides it for tests).

**How micromesh consumes it:** read `renderTriangleBudget()` once per frame (an atomic load) and amplify
micro-displacement only until the frame's drawn triangles reach it, finest where the screen-space error is
largest; keep the generated micro-triangles under `currentPerformanceSettings().microTriangleLimit` and their
pages under `currentTierConfig().residentCacheCapMB`. Report the triangles actually drawn through
`Renderer::lastDrawnTriangles()` so the governor sees them; it then raises or lowers the budget continuously
(continuous level of detail converges without the proxy's step, see the `governor_converges_*` tests).

### This machine

Apple M1 Pro under Asahi Linux, on battery, with about 35 other processes compiling (load average 35 on 10
cores), so the CPU numbers here are low (memory bandwidth measured 23 GB/s on a quieter run, dabs 0.07 ms on a
0.2M sphere):

```
"gl_renderer": "Apple M1 Pro (G13S C0)", "gl_version": "4.6 (Core Profile) Mesa 26.1.8", "gpu_kind": "unified",
"vram_mb": 31667 (GL_NVX_gpu_memory_info: all of the RAM), "timer_query": true, "cpu_threads": 10, "simd": ["neon"]
measurements: 1358M triangles/s (GPU timer), upload 12.8 GB/s, dab 0.37 ms (2.5k vertices), memory 5.9 GB/s, 607 ms
profile: Laptop (32 GB of RAM is under the 64 GB Workstation needs; this run predates the unified-memory rule, so its
         reason read "no discrete GPU"; it now reads "30.9 GB RAM")
settings: render budget 11.3M, motion proxy from 11.3M, GPU memory 20.6 GB, uploads 50 MB/frame, textures 8192,
          subdivision 120M, micro-triangles 256M, 10 threads, 60 fps
tier_config: cache 4 GB, active 2M, uploads 25 MB/frame (battery), draft brushes, zero-copy UMA, 60 Hz, on battery
```

Across runs on the busy machine the triangle throughput ranged 913-1410M/s and upload 4.3-21 GB/s.

## 0.9.7: big brushes on dense meshes (feat/perf-pass)

## Kelvinlet brushes and Capture from view (feat/kelvinlet-alpha)

### After the merge with 0.10.1 (predictive strokes off and on)

Measured with `--measure-stroke 3,R` on the benchmark sphere, best of two full-window runs, Move then KelvinGrab
back to back, `SF_PREDICTIVE_STROKE=0/1`. Load average 4-23. Frame time is mean / worst ms:

| Mesh | Brush | Move (off) | KelvinGrab (off) | ratio | Move (on) | KelvinGrab (on) |
|---|---|---:|---:|---:|---:|---:|
| 5M | 50 px | 8.6 / 19 | 13.1 / 19 | 1.5x | 8.3 / 11 | 8.4 / 15 |
| 5M | 300 px | 43 / 67 | 78 / 159 | 1.8x | 42 / 62 | 8.4 / 17 |
| 21M | 50 px | 12.5 / 17 | 56.7 / 101 | 4.5x | 11.2 / 20 | 8.9 / 20 |
| 21M | 300 px | 199 / 217 | 428 / 491 | 2.2x | 200 / 226 | 9.7 / 21 |
| 33M | 50 px | 17.1 / 76 | 57.9 / 164 | 3.4x | 70 / 100 | 10.5 / 30 |
| 33M | 300 px | 656 / 1056 | 1449 / 1654 | 2.2x | 429 / 523 | 11.8 / 28 |

**With predictive strokes off (0.10.1's default), the target is not met.** The target was KelvinGrab within
about 2x of Move at 33M, with no frame over 100 ms at 50 px. At 33M / 50 px KelvinGrab is 3.4x Move with a
164 ms worst frame; at 21M it is 4.5x with 101 ms. At 300 px it is about 2.2x Move, and at 5M 1.5-1.8x. A
KelvinGrab move at 33M / 50 px costs about 40 ms on the CPU. It touches about a million vertices with X
symmetry (kernel 15-20, normals 10-14, BVH 5-7 ms), where Move touches about 240 000.

**With predictive strokes on, the Kelvinlet moves run on the solver thread** (`App::kelvinPump`: one bounded move
per batch, taken by value on the main thread; the solver reads only the mesh, the BVH and the sculptor). Frames
stay at 8-12 ms at every size. But the frame column then only says the UI no longer waits. **The surface itself
still updates at the solver's pace**: about every 40-60 ms at 33M / 50 px, and every 0.4-1.5 s for a 300 px
whole-model move. The "brush behind the pointer" figure the harness prints is not meaningful for Kelvinlet
strokes in this mode, because they don't report their dabs to the probe.

What changed in this pass:
- The far field's catch-up groups are blocks of 4096 consecutive triangles (block b in group b mod 6, each
  vertex in the block of its first triangle). These are spatial patches like the renderer's chunks, so a frame
  dirties few clusters; renderer sync went from about 60 to about 20-25 ms a frame at 33M.
- The near field is the vertices within one radius of the brush's path, with a smooth weight from half a radius.
- The vertex budget is a third of `currentTierConfig().activeTriangleBudget`.
- Error metric: after the stroke ends, the surface is within 2.5% of the brush radius of the immediate
  evaluation (about a pixel for a 50 px brush). The lattice interpolation itself stays within 0.53% of the step.

Recommendation: ship Kelvinlets as best below about 10M polygons with predictive strokes off; with them on, the
UI stays fluid at any size and the surface follows at the solver's pace.


Measured with the stroke the app draws itself, on the benchmark sphere at 5M, 21M and 33M triangles (with
subdivision levels, X symmetry on), Move as the reference in the same session:

```
SF_MEASURE_BRUSH=KelvinGrab sculptforge --run "new_bench_sphere millions=21" --measure-stroke 3,50
SF_KELVIN_LOG=1 ...   # per move: pieces, vertices, kernel / normals / BVH ms, renderer sync so far
```

**Caveat: the machine was shared** with several other builds and test runs (load average 14-57 on 10 cores, noted
per row), and the tiling window manager sometimes gave the window half the size (those runs were thrown away:
every figure here is with the full window, circle radius 176 px). Each figure is the better of two runs. Treat them
as an upper bound and as a comparison with Move in the same conditions, not as clean numbers.

Frame time while drawing the fast two-turn circle (mean / worst, ms), 50 px and 300 px brush (300 px is as big as
the sphere on screen: a whole-model grab). "Before" is the first version of the branch, "now" after the
dense-mesh pass described below; Move was measured in each session:

| Mesh | Brush | Move | KelvinGrab before -> now | KelvinTwist before -> now | KelvinScale before -> now | load |
|---|---|---:|---:|---:|---:|---:|
| 5M | 50 px | 8.3 / 11 | 14.2 -> **9.6** / 22 | 14.6 -> **8.9** / 18 | 35.8 -> **9.3** / 15 | 12-16 |
| 5M | 300 px | 42 / 57 | 197 -> 145 / 210 | 124 -> 163 / 232 | 120 -> 384 / 491 | 13-24 |
| 21M | 50 px | 20.6 / 38 | 43.8 -> 138 / 195 | 35.2 -> **55** / 152 | 77.6 -> **52** / 122 | 17-22 |
| 21M | 300 px | 203 / 223 | 1297 -> **557** / 668 | 968 -> **499** / 688 | 935 -> **415** / 558 | 17-23 |
| 33M | 50 px | 19.2 / 37 | 179 -> 187 / 805 | 110 -> **58** / 99 | 279 -> **57** / 104 | 23-58 |
| 33M | 300 px | 381 / 405 | no frame in 3 s -> **920** / 1022 | no frame -> **505** / 746 | 1546 -> **1170** / 2234 | 16-38 |

(Both columns are under heavy, varying load; KelvinGrab's "before" at 21M / 50 px came from a quieter moment.)

**Targets not met.** The goal was KelvinGrab at 33M under 20 ms (50 px) and 50 ms (300 px). Measured: 187 and 920
ms. Twist and Scale at 33M / 50 px came down from 110-280 to 57-58 ms. On the same machine Move took 19 and 381
ms. So a 300 px move under 50 ms at 33M would be well beyond what plain Move does today. A KelvinGrab move at
33M / 50 px costs about 39 ms on the CPU (`SF_KELVIN_LOG`: about 900 000 vertices, kernel 15, normals 14, BVH
10 ms). What is left is the renderer: about 60 ms a frame of sync, because the deferred far field touches
vertices spread over a large box every frame. The next step is GPU-side: show the far field as a vertex-shader
displacement from the lattice (a 3D texture) and write it to the mesh when the stroke ends. That needs the
render pipeline (feat/zero-latency).

What the dense-mesh pass does (`src/core/Kelvinlet.cpp`, `src/app/PanelKelvinlet.cpp`):

1. **Bounded work per frame.** The app applies at most about one brush radius of a long move per frame (half that
   when the last move found more vertices than `KelvinletSettings::vertexBudget`, 600 000, a constant until the
   hardware profile sets it). For Twist and Scale the cap is a 0.3 rad turn or a 0.2 log-scale step. It moves
   once per frame and catches up in later frames, also when the pointer rests, and completely when the stroke
   ends.
2. **A coarse proxy for the far field.** Regions above 150 000 vertices evaluate the field on a lattice (spacing
   0.06 radius) beyond half a radius from the load's path and interpolate it trilinearly; the near field stays
   exact. Regions above 400 000 vertices (predicted from the last move) go further: only the near field (within
   1.2 radii, smoothly weighted) moves at once. The far field is gathered on a world lattice (spacing 0.1 radius),
   and a sixth of the vertices (runs of 4096 indices) take what they gathered each frame.
3. **An adaptive cutoff.** It is 4% of the peak, rising to 10% when the predicted region is over the budget.
4. **A threaded VDM capture.** The flood fill runs one ring at a time in parallel, and the adjacency and the
   triangles are built in parallel.

Error metric (tests/test_kelvinlet.cpp):
- **Lattice proxy:** the largest deviation from the exact field is 0.53% of the step's size. That includes the
  0.5% floor below which a vertex's move is skipped, which the exact path has too.
- **Deferred far field, at the stroke's end:** within 1.1% of the brush radius of the immediate evaluation
  (under a pixel for brushes up to about 70 px; about 3 px for a 300 px brush). The deviation is smooth, with no
  seams.
- **During a drag:** the far part of the surface (beyond about one radius) trails the near part by up to six
  frames, then settles.
- **Undo** is exact.

The core per-move cost on its own (`sculptforge_tests kelvinlet_grab_dense_mesh_timing`, 1.3M triangles, r 0.3,
no renderer): about 1.6-3.4 ms a move for 13 000-27 000 vertices, depending on the load.

Capture from view (`sculptforge-cli`, a VDM horn and a Sharp Crease cut sculpted on the sphere, square half size
0.15 unless noted):

| Mesh | Height alpha 256 / 1024 px | VDM 256 px (points) | VDM, half size 0.4 (points) |
|---|---:|---:|---:|
| 21M (load ~20) | 18 / 106 ms | 117 ms (50 k) | 1329 ms (456 k) |
| 33M (load ~21) | 55 / 240 ms | 439 ms (76 k) | 3852 ms (696 k) |
| 33M, threaded region (load ~11) | | 128 ms (76 k) | 1212 ms (696 k) |

The alpha is a z-buffer of the triangles under the square plus a box-blur high pass (three box blurs: the
1024 px alpha went from 652 to 106 ms at 21M). The VDM is region finding (flood fill over the vertices near the
square), a quadric fit to its outer band, and implicit fairing by conjugate gradients (90-270 iterations); on a
large patch the flood fill and adjacency build dominated; they are threaded now (the last row).

## Next version: big brushes on dense meshes (feat/perf-pass)

What a user feels on a heavy model is a large brush: how far it trails the pointer, how long a frame takes, and
what happens on press and release. Measured with the stroke the app draws itself (pointer read once per pass of
the loop, as a busy program gets it):

```
sculptforge --run "new_sphere subdivisions=10; subdiv_divide; subdiv_divide" --measure-stroke 3,200
```

A 21M-triangle sphere (10.5M vertices, two levels made by Divide), a 200 px brush (about 3.3M vertices per
dab, X symmetry), a fast two-turn circle in 3 s. M1 Pro, Release build; repeated runs agree within a few ms.

| | 0.9.6 | After |
|---|---:|---:|
| Frame, mean / worst | 98 / 182 ms | **65 / 111 ms** |
| Brush behind the pointer, mean / worst | 293 / 544 px | **137 / 189 px** |
| Press to the first dab on screen | 361 ms | **157 ms** |
| Release | 990 ms | **67-130 ms** |
| Dabs off the circle, worst | 6.9 px | 2.6 px |

Where a frame's time went in 0.9.6 (timers around each step): the dab 50-72 ms (normals 18-29, BVH refit
17-23, brush 14-18), the renderer's sync 28-78 ms, the draw about 1 ms. What changed, in order of what it saved:

1. **The stroke queue's floor.** The backlog limit was about 50 ms of dabs but never fewer than 24; at 45 ms a
   dab that is a second of work behind the pointer, and releasing placed all of it. The floor is 4 now
   (`strokeBacklogLimit`, core/StrokePath.h). Light meshes are unchanged (the time limit allows hundreds).
2. **Upload: changed blocks up to three quarters.** `UploadOrder::dirtyRuns` sent the whole mesh once a quarter
   of the vertices changed; a 200 px brush changes about a third, so every frame re-packed 10.5M vertices
   (cavity included): 78 ms. The changed blocks now: 14-30 ms.
3. **BVH refit.** The leaf boxes were recomputed in parallel but each leaf's ancestors walked up on one thread:
   6.5 ms per mirror. With 4,096+ leaves the ancestors are flagged in parallel and recomputed from their children
   in parallel subtrees (`BVH::refitAncestors`): BVH 21-23 -> 8.5 ms a dab; at 33M (benchmark, large
   Standard) 5.4 -> 2.9 ms.
4. **Press.** Starting a stroke cleared the LODs even when there were none, which made the renderer treat the
   mesh as new: a full upload of positions and faces before the first dab showed.
5. **Normals.** The vertices to recompute were each moved vertex and its neighbour ring; on levels made by
   Divide the ring is worked out per vertex (7.3 ms of the 22). Now the corners of the triangles in the BVH leaves
   the dab's queries listed (a superset: every triangle with a moved vertex is in one). Frames 71 -> 65 ms.
6. **Upload runs.** `dirtyRuns` walked the changed vertices on one thread (3.7 ms); with 65,536+ it marks the
   GPU blocks in parallel (0.4 ms).

Tried and dropped: keeping each triangle's corners from `clusterBounds`' first pass for its radius pass (the
culling bounds, 9 ms a frame here): no faster (8.7 against 8.8-9.8 ms), the pass is bound by memory, not by the
second read.

33M benchmark (`--bench --bench-sizes 33 --bench-no-project`): large Standard dab 44.6 -> 41.8 ms; every other row
within run-to-run noise (the benchmark's dabs touch 7% of the mesh, so the upload and queue changes don't show
there).

What a frame still costs at 21M with this brush: the dab about 45-50 ms (normals about 15 ms computing, brush
about 17 ms, BVH about 8.5 ms) and the sync about 28 ms (upload about 19 ms, of which the cavity term about a
quarter; culling bounds about 9 ms). Each step is already parallel and scales with the 3.3M vertices under the
brush; the next gains need a sampling profiler (perf) to see where the loops stall. Candidates: the cavity term
computed in the normals pass instead of from each vertex's ring at upload, face normals shared between a
triangle's corners, the double-precision volume in `clusterBounds`.

## v0.9: Make sculptable (feat/sculptable)

Splat capture to a sculptable Part (`splat_to_mesh`, core/Sculptable.h): the built-in test head capture
(`splat_test_capture count=N floaters=20`), Film preset (12,000 quads) with face loops and vertex paint, from the
command to the finished Part. M1 Pro (10 cores), Release build, `sculptforge-cli`; peak RSS of the whole run
(capture generation included). "Surface" is the splat density, solid and surface nets; the rest is face
landmarks, Quad Remesh and colour.

| Splats | Detail | Total | Surface | Peak RSS |
|---|---|---|---|---|
| 1.05M | 256 | 3.4 s | 1.3 s | 0.29 GB |
| 1.05M | 384 | 5.5 s | 2.6 s | 0.47 GB |
| 1.05M | 512 | 8.8 s | 5.1 s | 0.97 GB |
| 5.25M | 256 | 7.2 s | 4.2 s | 0.83 GB |
| 5.25M | 384 | 9.1 s | 5.6 s | 0.90 GB |
| 5.25M | 512 | 13.2 s | 8.0 s | 1.20 GB |

The density pass splits the grid into slabs of 4 z-layers, each summing its splats in index order, so the result
is the same on any number of threads (test `sculptable_deterministic`). No other figure in this file changes:
the feature adds code paths only.

## 0.8.1: texture painting (feat/texpaint)

One dab of the Paint brush into a texture, X symmetry on, a stroke of 12 dabs across an icosphere with spherical
UVs (`sculptforge_tests texture_stroke_benchmark`, CPU only). Small brush radius 0.03, big 0.35 (about a million
texels per dab at 4K). Upload: texels sent to the GPU per dab.

| Texture | Triangles | Brush | Before | After | Upload before -> after |
|---|---|---|---|---|---|
| 2K | 1.3K | big | 7.6 ms | 2.6 ms | 0.13 -> 0.16 Mtexels |
| 4K | 1.3K | big | 29.5 ms | 6.0 ms | 0.52 -> 0.53 Mtexels |
| 2K | 328K | big | 18.1 ms | 5.3 ms | 0.13 -> 0.16 Mtexels |
| 4K | 328K | big | 47.3 ms | 11.4 ms | 0.52 -> 0.53 Mtexels |
| 4K | 328K | small | 0.40 ms | 0.43 ms | 0.01 -> 0.03 Mtexels |

What changed: the texels of a dab are painted in parallel over 16-row bands of the texture, each band visiting
the triangles under the brush in the same order, so the result is identical on one thread or many
(`texture_dab_is_deterministic_with_threads`). The view uploads only the 64x64 tiles that changed instead of one
rectangle around everything painted since the last frame, so two mirrored dabs far apart in UV space no longer
send the texture between them (the upload grows a little here because it is rounded to whole tiles). The rest
of a dab with a dense mesh is the serial search for the triangles under the brush.

## v0.8: dynamic topology (feat/dyntopo)

```
sculptforge --bench --bench-dyntopo --bench-sizes 0.25,1,4
```

Spheres without subdivision levels (Sculptris Pro works on such meshes only), dynamic topology on with relative
detail, strokes with X symmetry. Each row starts from the same sphere. "remesh" is the dynamic topology work of a
dab (gathering the area, matching mirror images, splits, collapses, flips, relaxing, filling the freed slots,
rings and normals of what changed, BVH edits); "GPU upload" sends the clusters whose triangles changed as patches
(render/Renderer.h) instead of a full upload. "undo" rebuilds the adjacency, normals and BVH of the whole mesh.

What to read from it:

- **The brief's target, a dab on a 1M-triangle mesh under 15 ms with a medium brush (radius 0.12), holds: 9 to
  13 ms** with the GPU update. A very large brush (radius 0.4) is 29 ms.
- The cost follows the work under the brush, not the mesh size: the 4M sphere is four times finer than the 1M
  one, so at the same Detail the brush merges four times as many edges per dab (Standard, Detail 50%: about
  2,000 collapses per dab at 4M against 500 at 1M). A dab does at most 6,000 operations; a big brush over very
  fine triangles coarsens them over several dabs.
- Undo memory is what the stroke touched: 5 MB for a 40-dab stroke at 1M.
- Undo and redo take a full rebuild (85-95 ms at 1M, 350 ms at 4M); see Known limits.
- Since the first measurements, a randomized stress test (tests/test_dyntopo.cpp, : hundreds of
  strokes with every brush, detail, radius and symmetry, masks, face groups, hidden parts, a sculpt layer, random
  undo / redo) found and fixed sliver chains, brush-squashed triangles outside the remeshed area and on the mirror
  side, and undo bits leaking between strokes. The repairs cost about 1 ms per medium dab at 1M.

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Dynamic topology on (relative detail), X symmetry (two dabs per step), 40 dabs per stroke on a unit sphere without subdivision levels. Times in milliseconds.

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Dynamic topology on (relative detail), X symmetry (two dabs per step), 40 dabs per stroke on a unit sphere without subdivision levels. Times in milliseconds.

#### 249k triangles (124k vertices, no subdivision levels)

| Stroke | remesh | kernel | normals | BVH | GPU upload | **dab total** | triangles after | stroke end | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard, Detail 50% | 0.94 | 0.05 | 0.04 | 0.02 | 0.68 | **1.74** | 246k | 1.4 | 45.3 | 45.0 | 0.53 MB |
| Standard, Detail 100% | 10.38 | 0.27 | 0.31 | 0.11 | 2.38 | **13.49** | 322k | 29.5 | 47.0 | 37.8 | 0.79 MB |
| Tendril, Detail 50% | 4.18 | 0.06 | 0.05 | 0.02 | 1.18 | **5.50** | 253k | 42.1 | 48.7 | 50.5 | 0.59 MB |
| Clay Block, Detail 75% | 6.75 | 0.19 | 0.19 | 0.07 | 2.05 | **9.27** | 272k | 24.1 | 47.2 | 29.4 | 0.58 MB |
| Smooth (simplifies), Detail 50% | 1.48 | 0.02 | 0.01 | 0.01 | 1.58 | **3.13** | 239k | 1.1 | 45.9 | 43.2 | 1.38 MB |
| Standard (large, r 0.4), Detail 50% | 4.49 | 0.08 | 0.06 | 0.04 | 2.89 | **7.62** | 214k | 36.1 | 48.3 | 40.8 | 4.20 MB |

#### 995k triangles (498k vertices, no subdivision levels)

| Stroke | remesh | kernel | normals | BVH | GPU upload | **dab total** | triangles after | stroke end | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard, Detail 50% | 4.31 | 0.06 | 0.05 | 0.03 | 4.12 | **8.64** | 959k | 6.0 | 86.1 | 82.7 | 5.20 MB |
| Standard, Detail 100% | 9.39 | 0.28 | 0.33 | 0.11 | 2.90 | **13.08** | 1.0M | 4.4 | 92.8 | 89.7 | 1.47 MB |
| Tendril, Detail 50% | 6.56 | 0.07 | 0.05 | 0.03 | 4.40 | **11.18** | 969k | 4.3 | 85.1 | 85.3 | 4.59 MB |
| Clay Block, Detail 75% | 6.60 | 0.21 | 0.20 | 0.08 | 3.06 | **10.22** | 977k | 5.1 | 84.8 | 84.1 | 3.38 MB |
| Smooth (simplifies), Detail 50% | 4.57 | 0.04 | 0.02 | 0.01 | 4.80 | **9.50** | 952k | 5.3 | 83.4 | 79.9 | 5.94 MB |
| Standard (large, r 0.4), Detail 50% | 18.00 | 0.24 | 0.18 | 0.15 | 9.90 | **28.60** | 851k | 68.6 | 87.1 | 79.2 | 17.20 MB |

#### 4.0M triangles (2.0M vertices, no subdivision levels)

| Stroke | remesh | kernel | normals | BVH | GPU upload | **dab total** | triangles after | stroke end | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard, Detail 50% | 14.29 | 0.13 | 0.07 | 0.06 | 10.44 | **25.18** | 3.8M | 23.9 | 337.6 | 310.0 | 23.02 MB |
| Standard, Detail 100% | 13.04 | 0.39 | 0.40 | 0.18 | 5.68 | **19.87** | 3.9M | 22.7 | 347.2 | 325.5 | 13.31 MB |
| Tendril, Detail 50% | 15.42 | 0.15 | 0.07 | 0.06 | 11.93 | **27.82** | 3.8M | 23.1 | 333.8 | 317.4 | 20.75 MB |
| Clay Block, Detail 75% | 20.88 | 0.32 | 0.27 | 0.19 | 14.36 | **36.25** | 3.8M | 27.6 | 348.1 | 333.9 | 25.86 MB |
| Smooth (simplifies), Detail 50% | 16.56 | 0.11 | 0.05 | 0.03 | 10.64 | **27.57** | 3.8M | 26.5 | 351.1 | 327.8 | 23.79 MB |
| Standard (large, r 0.4), Detail 50% | 91.91 | 1.97 | 1.59 | 0.97 | 42.63 | **139.86** | 3.5M | 261.6 | 341.7 | 336.1 | 57.48 MB |

### 33M benchmark unchanged

Dynamic topology touches hot paths (Mesh::neighbors / vertexTriangles, BVH traversal, the renderer's vertex
gather), so the regular benchmark was run at 33M against main at ffb3747 built the same way, alternating the
two builds: memory is identical (3070 MB RSS, 37 / 17.1 / 29 B per triangle for mesh / BVH / levels, 14.8 B on
the GPU) and every brush, Lower / Higher Res, project and orbit time is within run-to-run noise (for example
Standard 7.60 / 7.54 ms, Clay 7.31-7.50 / 7.35-7.53 ms, Fast Navigation 3.4-3.7 / 3.3-3.6 ms; base / feat/dyntopo).

### Known limits

- Undo / redo of a dynamic-topology stroke rebuilds topology, normals and BVH for the whole mesh and re-uploads
  it (about 0.1 s per million triangles). Making it incremental like the dabs is possible (the record lists the
  changed slots) but not done.
- The GPU patches redraw whole 4,096-triangle clusters; when more than a quarter of the clusters are patched the
  next frame does a full upload (about 50 ms at 4M). A stroke's end packs the adjacency (a few ms at 1M) and
  rebuilds the BVH when edits have worn it down (up to 70 ms at 1M with a large brush).

## v0.6

v0.6 is three pieces of work merged together: less memory per triangle (feat/mem), faster brushes on huge
meshes (feat/brush) and a faster, lighter viewport (feat/view). "0.5" is main at aac5f68 (0.5.0 plus the STL
fix) with the same benchmark; "0.6" is the merged tree, measured after the merge. Each branch's own report,
with its before / after tables and known limits, follows the summary.

### Summary (33 million triangles, 7 subdivision levels, 40 dabs per stroke, GPU upload included)

| | 0.5 | 0.6 |
|---|---:|---:|
| Memory: mesh + BVH + subdivision levels, per triangle | 156 bytes | **83 bytes** |
| Memory: whole process (RSS) | 5.6 GB | **3.4 GB** |
| Graphics card memory, per triangle | 46 bytes | **15 bytes** |
| Turning the view, 4x antialiasing | 20.6 ms (49 fps) | **9.2 ms (109 fps)** |
| Turning the view, Fast Navigation | 14.8 ms (68 fps) | **3.7 ms (270 fps)** |
| Medium brush (Standard), one dab | 11.9 ms (78k vertices) | **7.5 ms** (103k vertices) |
| Large brush (Standard, radius 0.4), one dab | 78.5 ms (1.2M vertices) | **45 ms** (987k vertices) |
| Mask / Paint, one dab | 5.6 / 7.0 ms | **2.5 / 3.5 ms** |
| First GPU upload | 701 ms | **423 ms** |
| Opening the project | 4.7 s | **2.1 s** |
| Lower Res after sculpting | 1588 ms | **829 ms** |
| Largest Divide | 50M (fixed limit) | **from the memory: about 160M on 32 GB** |

The vertex counts per dab differ because 0.6 also fixes the benchmark sphere (New Cube left a few vertices
unwelded at some resolutions, see feat/mem below): the same radius now reaches a slightly different number of
vertices. Compare the times per dab, or the per-vertex costs in the full tables.

Where 0.6 gives something back: on levels made by Divide the mesh works out its vertex neighbours from the level
topology instead of storing them (about 24 bytes per triangle saved). That makes normal updates on those levels
slower than in feat/brush alone: a large dab at 33M is 45 ms merged against 39 ms on feat/brush. Keeping the
stored lists would win that back for about 10 bytes per triangle; v0.6 keeps the memory.

At 104 million triangles (CPU-side figures in feat/mem, viewport in feat/view) the view turns at 19.6 ms with 4x
antialiasing and 9.9 ms (101 fps) with Fast Navigation.

### Merged benchmark (feat/v06)



Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

#### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 154 |
| BVH build | 233 |
| First GPU upload | 53 |
| GPU memory (buffers) | 59 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 146 MB = 37 B/triangle |
| BVH | 68 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 117 MB = 29 B/triangle |
| Process memory (RSS) | 434 MB (+363 MB = 91 B/triangle) |
| Peak while dividing (RSS) | 406 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 25k (14) | 0.59 | 0.53 | 0.26 | 0.00 | 0.63 | **2.12** | 0.07 | 1.6 | 2.5 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 155k (32) | 1.49 | 1.26 | 0.98 | 0.01 | 2.53 | **7.09** | 0.07 | 4.7 | 4.8 | 4.6 MB |
| Clay | 7k | 16k (9) | 0.40 | 0.41 | 0.14 | 0.01 | 0.54 | **1.58** | 0.03 | 1.7 | 1.6 | 0.9 MB |
| Clay Block | 8k | 19k (11) | 0.51 | 0.46 | 0.19 | 0.01 | 1.05 | **2.29** | 0.02 | 1.6 | 1.6 | 0.8 MB |
| Smooth | 5k | 12k (9) | 0.29 | 0.25 | 0.08 | 0.01 | 0.40 | **1.07** | 0.03 | 1.4 | 1.4 | 0.6 MB |
| Smooth (large, r 0.4) | 59k | 84k (23) | 1.32 | 0.89 | 0.45 | 0.01 | 1.45 | **4.54** | 0.06 | 2.6 | 3.0 | 2.6 MB |
| Inflate | 5k | 12k (9) | 0.27 | 0.27 | 0.09 | 0.01 | 0.43 | **1.13** | 0.03 | 1.7 | 1.4 | 0.6 MB |
| Sharp Crease | 4k | 12k (9) | 0.31 | 0.32 | 0.09 | 0.01 | 0.40 | **1.18** | 0.02 | 1.3 | 1.2 | 0.5 MB |
| Layer | 5k | 12k (9) | 0.33 | 0.26 | 0.08 | 0.01 | 0.45 | **1.18** | 0.03 | 1.4 | 2.1 | 0.6 MB |
| Move | 4k | 10k (8) | 0.05 | 0.28 | 0.08 | 0.01 | 0.36 | **0.82** | 0.01 | 0.3 | 0.4 | 0.1 MB |
| Move (large, r 0.4) | 42k | 63k (20) | 0.21 | 0.79 | 0.34 | 0.01 | 1.10 | **2.73** | 0.03 | 1.5 | 1.1 | 0.7 MB |
| Recipe (pattern) | 5k | 12k (9) | 0.53 | 0.25 | 0.08 | 0.01 | 0.42 | **1.34** | 0.08 | 1.3 | 1.2 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.27 | 0.00 | 0.00 | 0.01 | 0.29 | **0.62** | 0.10 | 0.3 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.27 | 0.00 | 0.00 | 0.01 | 0.38 | **0.72** | 0.03 | 0.2 | 0.2 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 99 |
| Higher Res (back up) | 2 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Save project (165 MB) | 53 |
| Open project | 279 |
| Open project: memory (RSS) | +408 MB, peak +453 MB |
| Motion proxy build (first move after an upload) | 17 |
| Orbit: frame time, 4x antialiasing | 2.9 (349 fps) |
| Orbit: frame time, no antialiasing | 1.6 (628 fps) |
| Orbit: triangles drawn | 2.1M (53%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 1.1 (926 fps) |
| Orbit: triangles drawn (motion proxy) | 766k (19%) |
| GPU memory with the motion proxy | 76 MB = 19.1 B/triangle |

#### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 671 |
| BVH build | 807 |
| First GPU upload | 204 |
| GPU memory (buffers) | 235 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 583 MB = 37 B/triangle |
| BVH | 272 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 470 MB = 29 B/triangle |
| Process memory (RSS) | 1655 MB (+1177 MB = 74 B/triangle) |
| Peak while dividing (RSS) | 1365 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 77k (26) | 0.97 | 0.89 | 0.57 | 0.00 | 1.43 | **4.34** | 0.08 | 5.1 | 5.9 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 564k (63) | 3.42 | 3.84 | 2.89 | 0.01 | 9.02 | **22.09** | 0.18 | 18.7 | 18.3 | 18.5 MB |
| Clay | 28k | 47k (19) | 0.92 | 0.59 | 0.36 | 0.01 | 1.03 | **3.21** | 0.07 | 4.1 | 3.7 | 3.5 MB |
| Clay Block | 33k | 54k (21) | 0.97 | 0.70 | 0.39 | 0.01 | 3.21 | **5.61** | 0.04 | 4.1 | 3.7 | 3.8 MB |
| Smooth | 19k | 35k (17) | 1.01 | 0.54 | 0.32 | 0.01 | 0.83 | **2.94** | 0.03 | 3.1 | 3.3 | 2.7 MB |
| Smooth (large, r 0.4) | 233k | 289k (45) | 2.48 | 2.01 | 1.07 | 0.01 | 4.42 | **11.47** | 0.13 | 11.2 | 10.6 | 11.0 MB |
| Inflate | 18k | 34k (17) | 0.85 | 0.58 | 0.37 | 0.01 | 0.86 | **2.91** | 0.08 | 3.1 | 3.2 | 2.6 MB |
| Sharp Crease | 18k | 33k (17) | 0.86 | 0.54 | 0.35 | 0.02 | 0.83 | **2.82** | 0.03 | 2.9 | 2.9 | 2.5 MB |
| Layer | 19k | 34k (17) | 1.04 | 0.52 | 0.30 | 0.01 | 0.76 | **2.83** | 0.07 | 2.6 | 3.1 | 2.6 MB |
| Move | 16k | 32k (12) | 0.15 | 0.59 | 0.12 | 0.01 | 0.71 | **1.80** | 0.05 | 0.8 | 0.7 | 0.3 MB |
| Move (large, r 0.4) | 168k | 217k (37) | 0.32 | 1.68 | 0.77 | 0.02 | 3.44 | **7.36** | 0.05 | 3.2 | 2.9 | 2.8 MB |
| Recipe (pattern) | 19k | 35k (17) | 1.88 | 0.59 | 0.28 | 0.02 | 0.88 | **3.87** | 0.07 | 3.2 | 2.9 | 2.7 MB |
| Mask | 19k | 33k (17) | 0.60 | 0.00 | 0.00 | 0.01 | 0.63 | **1.44** | 0.03 | 0.7 | 0.6 | 1.3 MB |
| Paint | 19k | 33k (17) | 0.58 | 0.00 | 0.00 | 0.02 | 0.92 | **1.71** | 0.07 | 1.2 | 0.8 | 2.7 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 404 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 18 |
| Higher Res again (no edits) | 5 |
| Save project (658 MB) | 206 |
| Open project | 1030 |
| Open project: memory (RSS) | +1567 MB, peak +1822 MB |
| Motion proxy build (first move after an upload) | 61 |
| Orbit: frame time, 4x antialiasing | 5.8 (172 fps) |
| Orbit: frame time, no antialiasing | 4.1 (243 fps) |
| Orbit: triangles drawn | 7.8M (49%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 2.2 (454 fps) |
| Orbit: triangles drawn (motion proxy) | 3.1M (19%) |
| GPU memory with the motion proxy | 304 MB = 19.1 B/triangle |

#### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1362 |
| BVH build | 1666 |
| First GPU upload | 423 |
| GPU memory (buffers) | 490 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 1217 MB = 37 B/triangle |
| BVH | 567 MB = 17.1 B/triangle |
| Subdivision levels (stack) | 980 MB = 29 B/triangle |
| Process memory (RSS) | 3351 MB (+2061 MB = 62 B/triangle) |
| Peak while dividing (RSS) | 2632 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 146k (38) | 1.50 | 1.27 | 0.94 | 0.01 | 2.79 | **7.49** | 0.13 | 11.4 | 11.9 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (93) | 6.27 | 8.59 | 5.65 | 0.01 | 18.48 | **45.14** | 0.30 | 39.8 | 38.4 | 38.7 MB |
| Clay | 58k | 87k (28) | 1.18 | 0.91 | 0.53 | 0.02 | 1.76 | **4.97** | 0.09 | 7.2 | 7.1 | 7.2 MB |
| Clay Block | 66k | 98k (30) | 1.35 | 1.03 | 0.62 | 0.02 | 6.50 | **10.14** | 0.08 | 7.8 | 7.8 | 8.0 MB |
| Smooth | 39k | 63k (24) | 1.17 | 0.78 | 0.35 | 0.02 | 1.33 | **4.07** | 0.08 | 6.0 | 6.7 | 5.7 MB |
| Smooth (large, r 0.4) | 468k | 556k (64) | 4.04 | 3.79 | 1.86 | 0.03 | 8.59 | **21.28** | 0.18 | 20.2 | 21.1 | 20.5 MB |
| Inflate | 38k | 61k (24) | 0.99 | 0.82 | 0.44 | 0.02 | 1.37 | **4.06** | 0.09 | 6.3 | 5.7 | 5.6 MB |
| Sharp Crease | 37k | 60k (23) | 1.03 | 0.87 | 0.39 | 0.02 | 1.36 | **4.11** | 0.07 | 5.3 | 5.6 | 5.4 MB |
| Layer | 37k | 60k (23) | 1.46 | 0.82 | 0.33 | 0.02 | 1.25 | **4.32** | 0.08 | 5.6 | 5.6 | 5.6 MB |
| Move | 34k | 55k (23) | 0.18 | 0.95 | 0.22 | 0.02 | 1.29 | **3.11** | 0.10 | 1.8 | 1.4 | 0.5 MB |
| Move (large, r 0.4) | 352k | 440k (49) | 0.57 | 3.03 | 1.57 | 0.04 | 7.22 | **14.76** | 0.06 | 5.3 | 5.3 | 5.8 MB |
| Recipe (pattern) | 38k | 61k (24) | 2.15 | 0.81 | 0.33 | 0.03 | 1.42 | **5.16** | 0.08 | 9.3 | 6.8 | 5.6 MB |
| Mask | 38k | 60k (24) | 0.98 | 0.00 | 0.00 | 0.03 | 1.11 | **2.54** | 0.11 | 1.6 | 1.3 | 2.8 MB |
| Paint | 38k | 60k (24) | 1.35 | 0.00 | 0.00 | 0.03 | 1.66 | **3.47** | 0.08 | 2.0 | 3.0 | 5.7 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 829 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 42 |
| Higher Res again (no edits) | 10 |
| Save project (1373 MB) | 427 |
| Open project | 2132 |
| Open project: memory (RSS) | +3226 MB, peak +3769 MB |
| Motion proxy build (first move after an upload) | 127 |
| Orbit: frame time, 4x antialiasing | 9.2 (109 fps) |
| Orbit: frame time, no antialiasing | 6.9 (146 fps) |
| Orbit: triangles drawn | 15.9M (48%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 3.7 (270 fps) |
| Orbit: triangles drawn (motion proxy) | 6.4M (19%) |
| GPU memory with the motion proxy | 633 MB = 19.1 B/triangle |



### Memory per triangle (feat/mem)

Target from the brief: the whole scene under ~60 bytes per triangle at the top level, Divide to 100M+, opening a
33M project in under ~2 s without a 2x peak, and no slower dabs, undo or level stepping. Measured with the same
benchmark (it now also reports the process memory per triangle, the peak while dividing, and the memory and peak
of opening the project). Another agent's builds and benchmarks shared the machine, so every number is the better
of two runs, before and after alternating.

#### Summary (33M benchmark sphere)

| | before (main at aac5f68) | after (feat/mem) |
|---|---|---|
| Scene memory (CPU benchmark, process RSS) | 5.3 GB = 159 bytes per triangle | **3.0 GB = 90 bytes per triangle** |
| Sculpting mesh (with its adjacency) | 62 B/triangle | **39** |
| Subdivision level stack | 75 B/triangle | **29** |
| BVH (feat/brush's) | 19.5 B/triangle | 19.5 |
| Biggest Divide on this machine (30 GB) | 50M triangles (fixed limit) | **about 160M** (104M measured: 9.0 GB, 11 GB peak while dividing) |
| Divide up to 33M (the benchmark's setup) | 2428 ms | **1415 ms** |
| Open project | 4721 ms, the whole 1.9 GB file read into memory first | **2137 ms**, peak 1.15x the scene (+3.5 GB, peak +4.0 GB) |
| Save project | 663 ms, 1.9 GB file | **433 ms, 1.37 GB** |
| Lower Res after sculpting | 1543 ms | **876 ms** |
| Higher / Lower Res without edits | 10 / 40 ms | 8-13 / 43-55 ms |
| Medium dab (Standard), GPU upload included | 11.1 ms | 13.7 ms (CPU-only runs: 9.16 -> 9.09 ms) |
| Large dab (radius 0.4) | 62.6 ms | 64.8 ms |
| Smooth / Clay dab | 5.47 / 6.57 ms | 5.77 / 7.02 ms |
| Undo of a medium / large stroke | 21.8 / 69.1 ms | 25.5 / 70.0 ms (CPU-only: 21.4 -> 21.6 ms) |
| Orbit, 4x antialiasing | 19.7 ms | 20.0 ms |

At 16M the dabs are 0-20% slower (Standard 7.79 -> 7.69 ms, Clay 4.05 -> 4.68, Smooth 2.98 -> 3.50, Mask 1.50 ->
1.71): the rings and triangle lists of implicit adjacency take a few nanoseconds more per vertex than reading
stored lists, which shows in the normal update, the BVH refit, Smooth and the renderer's curvature (part of the
GPU upload). Undo is the same or faster.

#### What changed

- **Generated levels** (core/Multires.h, `SubdivTopology::generated`): a level made by Divide numbers its
  vertices as coarse vertices, then edge points, then face points, and lays its triangles out polygon by polygon
  (`generateFaces`). Such a level stores no triangles or quad pairs (in `SubdivLevel::data`), no child maps and no
  per-triangle parents and corner codes; all of them follow from the coarse polygons. The working mesh gets its
  triangles generated when the level is entered. Levels from Reconstruct Subdiv, imports and older projects whose
  numbering matches are recognised as generated; other levels keep the old path.
- **Details or positions, not both**: a level that isn't stale keeps its positions and no detail; the detail is
  fitted to the positions when a lower level is about to change (before the levels above are marked stale), and
  dropped again once a stale level is evaluated. Folding edits at the current level into the stack no longer
  re-fits every level's detail, which is why Lower Res after sculpting got faster.
- **Coarse topology**: exact capacities (the polygon lists were reserved for all triangles), the quad pairing and
  the UV corner table are derived (a byte per quad says which diagonal it has).
- **Implicit adjacency** (`Mesh::implicitAdjacency`): the working mesh of a generated level (manifold, coarse
  valences up to 32) stores no vertex-vertex or vertex-triangle lists. `Mesh::neighbors` and
  `Mesh::vertexTriangles` return an `IndexRing` (a span-like list, ascending as before) worked out from the
  level's topology: closed forms per kind of fine vertex (corner, edge point, face point; Catmull-Clark and Loop),
  with two small indexes built when the level becomes current (each coarse vertex's polygon corners, each coarse
  edge's sides; about 4.5 bytes per triangle) so most lookups read one or two cache lines and sort at most 4
  values with a branchless network. The lists are identical to the stored ones (tested on quads, mixed
  quads/triangles, open meshes and Loop), so every brush, the normals, undo, the BVH and the renderer give
  bit-identical results. Meshes without levels, non-manifold ones and higher valences keep the stored lists.
- **Project format 6** (core/Project.cpp): generated levels are written without triangles or child maps, and a
  level without a detail takes the one that fits its positions. Projects of format 5 and older open unchanged
  (tested with files saved by 0.5) and their levels become generated. Opening reads each blob straight into its
  array (`readStoredZipIndex` + `pread`, big arrays in parallel) instead of reading the whole archive into memory
  first; the mesh's topology is built once, after its levels are attached; level data is no longer given a full
  topology and normals just to be thrown away; and the BVH of a big mesh is built on its own thread (it only needs
  the positions and triangles) while the levels and topology load, with the main thread's work kept off the
  shared pool meanwhile. Saving writes the mesh arrays straight from memory (`ZipEntry::external`) instead of
  copying them into the archive list first.
- **Divide's limit** (`subdivMaxTriangles`) is what fits in 60% of the computer's memory at 110 bytes per triangle
  (the measured scene plus the peak while dividing), between 50 million and 1 billion: about 160 million on this
  machine.
- **makeCube** welds at every resolution: mirrored sides computed 2i/n - 1 and 2(n-i)/n - 1, which aren't exact
  opposites in floating point, so resolutions such as 13 and 26 (the base of the 33M benchmark sphere) left a few
  unwelded vertices along the cube's edges (reported by the view agent).

#### Memory per triangle (33M benchmark sphere)

| | before | after |
|---|---|---|
| Mesh: positions 6, normals 6, triangles 12, quad pairs 4, face groups 4, normal-update stamps 2, dirty bits | 34 | 34 |
| Mesh: vertex-vertex and vertex-triangle lists (CSR) | 28 | **0** (implicit) |
| Mesh: implicit adjacency indexes (corners per coarse vertex, sides per coarse edge) | | 4.5 |
| Levels: the top level's data (positions 6, triangles 12, quad pairs 4, face groups 4) | 26 | **10** (positions, face groups) |
| Levels: the top level's detail | 6 | **0** (fitted to the positions when needed) |
| Levels: the top level's topology (coarse polygons and edges 18 -> 12; child maps 2, parents and corner codes 11 -> 0) | 31 | **12** |
| Levels: everything below the top (a quarter, a sixteenth, ...) | 12 | 7 |
| BVH | 19.5 | 19.5 |
| **Total (CPU)** | **157** | **87** |
| GPU buffers (unchanged; estimate) | about 26 | about 26 |

#### Known limits

- The scene is at about 87 bytes per triangle (90 measured as RSS), not the ~60 of the brief: the mesh (39) and
  the levels (29) are down from 137 to 68, but the BVH (19.5, feat/brush's) and the GPU-side buffers are unchanged
  here. What is left in mine: the current level's reference copy of its positions in the stack (6, needed to see
  what a stroke changed), the coarse topology of the top level (12: edges, their polygons, polygon lists), the
  sculpting mesh's triangles (12) and quad pairs (4, which on a generated level are just 2i, 2i + 1), the
  implicit adjacency indexes (4.5) and the normal-update stamps (2). The quad pairs and the coarse edge lists are
  the next candidates.
- Implicit lookups cost a few nanoseconds more per vertex than reading stored lists, so dabs are 0-20% slower
  (most at 4-16M with small brushes; at 33M the large dab is 3.5% slower and CPU-only medium dabs and undo are
  unchanged). The brief asked for no slower dabs: this trades about 23 bytes per triangle (760 MB at 33M) for it.
  Keeping the stored vertex-vertex lists (and only the triangle lists implicit) would win most of that speed back
  for about 10 of those bytes; I tried denser layouts for the implicit lists, which didn't help (the cost is the
  work per lookup, not memory reads).
- Opening a 33M project is bounded by the BVH build (about 1.7 s, running in parallel with the rest, which takes
  about 1.5 s): 2.1-2.5 s instead of 4.7 s, not yet reliably under 2 s. A faster BVH build (feat/brush) or
  storing the BVH in the project would bring it there.
- Format 6 projects don't open in 0.5.

### Benchmark tables (feat/mem)

#### Before (main at aac5f68), with the GPU

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 268 |
| BVH build | 217 |
| First GPU upload | 92 |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 719 MB (+648 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 24k (15) | 0.50 | 0.47 | 0.39 | 0.00 | 0.35 | **1.75** | 0.00 | 4.7 | 4.7 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 148k (45) | 3.26 | 3.40 | 1.93 | 0.00 | 2.28 | **11.10** | 0.00 | 9.5 | 9.7 | 4.6 MB |
| Clay | 7k | 16k (11) | 0.34 | 0.33 | 0.23 | 0.00 | 0.28 | **1.22** | 0.00 | 2.2 | 2.6 | 0.9 MB |
| Clay Block | 8k | 18k (12) | 0.43 | 0.31 | 0.25 | 0.00 | 0.25 | **1.27** | 0.00 | 3.1 | 2.9 | 0.8 MB |
| Smooth | 5k | 12k (9) | 0.24 | 0.18 | 0.12 | 0.00 | 0.17 | **0.74** | 0.00 | 2.4 | 3.2 | 0.6 MB |
| Inflate | 5k | 12k (9) | 0.20 | 0.17 | 0.13 | 0.00 | 0.16 | **0.68** | 0.00 | 3.0 | 3.0 | 0.6 MB |
| Sharp Crease | 4k | 12k (9) | 0.20 | 0.16 | 0.12 | 0.00 | 0.16 | **0.65** | 0.00 | 3.2 | 3.3 | 0.5 MB |
| Layer | 5k | 12k (9) | 0.31 | 0.18 | 0.12 | 0.00 | 0.17 | **0.80** | 0.00 | 4.4 | 4.3 | 0.6 MB |
| Move | 4k | 10k (8) | 0.02 | 0.10 | 0.09 | 0.00 | 0.13 | **0.37** | 0.00 | 0.3 | 0.2 | 0.1 MB |
| Recipe (pattern) | 5k | 13k (9) | 0.48 | 0.17 | 0.12 | 0.00 | 0.17 | **0.97** | 0.00 | 4.2 | 4.4 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.19 | 0.00 | 0.00 | 0.00 | 0.23 | **0.44** | 0.00 | 0.1 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.18 | 0.00 | 0.00 | 0.00 | 0.30 | **0.50** | 0.00 | 0.1 | 0.1 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 191 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Save project (228 MB) | 91 |
| Open project | 573 |
| Orbit: frame time, 4x antialiasing | 5.0 (200 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 3.2 (314 fps) |

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1183 |
| BVH build | 790 |
| First GPU upload | 358 |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2720 MB (+2224 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 76k (15) | 2.45 | 2.28 | 1.37 | 0.00 | 1.52 | **7.79** | 0.00 | 14.8 | 12.8 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 525k (45) | 8.96 | 9.50 | 5.84 | 0.01 | 8.50 | **33.62** | 0.00 | 34.3 | 32.8 | 18.5 MB |
| Clay | 28k | 47k (11) | 1.20 | 1.20 | 0.78 | 0.01 | 0.78 | **4.05** | 0.00 | 7.7 | 7.8 | 3.5 MB |
| Clay Block | 33k | 55k (12) | 1.58 | 1.35 | 0.82 | 0.01 | 0.85 | **4.70** | 0.00 | 8.1 | 8.0 | 3.8 MB |
| Smooth | 19k | 37k (9) | 0.97 | 0.80 | 0.58 | 0.01 | 0.56 | **2.98** | 0.00 | 6.6 | 7.0 | 2.7 MB |
| Inflate | 18k | 36k (9) | 0.80 | 0.83 | 0.61 | 0.01 | 0.55 | **2.87** | 0.00 | 7.3 | 7.0 | 2.6 MB |
| Sharp Crease | 18k | 35k (9) | 0.89 | 0.85 | 0.62 | 0.01 | 0.53 | **2.96** | 0.00 | 6.7 | 6.4 | 2.5 MB |
| Layer | 19k | 36k (9) | 1.22 | 0.91 | 0.63 | 0.01 | 0.57 | **3.41** | 0.00 | 9.6 | 6.8 | 2.6 MB |
| Move | 16k | 29k (8) | 0.14 | 0.47 | 0.63 | 0.01 | 0.44 | **1.75** | 0.00 | 1.3 | 1.2 | 0.3 MB |
| Recipe (pattern) | 19k | 37k (9) | 1.98 | 0.86 | 0.60 | 0.01 | 0.57 | **4.09** | 0.00 | 8.7 | 8.4 | 2.6 MB |
| Mask | 19k | 36k (9) | 0.73 | 0.00 | 0.00 | 0.01 | 0.70 | **1.50** | 0.00 | 0.3 | 0.3 | 1.3 MB |
| Paint | 19k | 36k (9) | 0.66 | 0.00 | 0.00 | 0.01 | 0.96 | **1.70** | 0.00 | 0.5 | 0.4 | 2.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 780 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 19 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 345 |
| Open project | 2199 |
| Orbit: frame time, 4x antialiasing | 11.6 (86 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 8.4 (119 fps) |

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2428 |
| BVH build | 1665 |
| First GPU upload | 736 |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5664 MB (+4824 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 149k (21) | 4.05 | 3.26 | 1.98 | 0.00 | 3.07 | **12.72** | 0.00 | 21.8 | 21.2 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (57) | 17.52 | 18.05 | 11.48 | 0.01 | 14.05 | **62.60** | 0.00 | 69.1 | 68.4 | 38.7 MB |
| Clay | 58k | 91k (16) | 2.10 | 1.82 | 1.06 | 0.01 | 1.39 | **6.57** | 0.00 | 13.6 | 13.6 | 7.2 MB |
| Clay Block | 66k | 102k (17) | 2.61 | 2.11 | 1.14 | 0.01 | 1.72 | **7.82** | 0.00 | 15.5 | 14.8 | 8.0 MB |
| Smooth | 39k | 67k (14) | 1.75 | 1.61 | 0.79 | 0.02 | 1.15 | **5.47** | 0.00 | 11.4 | 10.8 | 5.7 MB |
| Inflate | 38k | 66k (14) | 1.63 | 1.53 | 0.80 | 0.02 | 1.29 | **5.43** | 0.00 | 10.8 | 10.3 | 5.6 MB |
| Sharp Crease | 37k | 65k (15) | 1.62 | 1.61 | 0.80 | 0.02 | 1.24 | **5.44** | 0.00 | 10.4 | 12.2 | 5.4 MB |
| Layer | 37k | 65k (14) | 2.11 | 1.65 | 0.77 | 0.02 | 1.21 | **5.92** | 0.00 | 12.0 | 10.1 | 5.6 MB |
| Move | 33k | 59k (10) | 0.31 | 0.84 | 0.80 | 0.02 | 1.26 | **3.42** | 0.00 | 1.8 | 2.1 | 0.5 MB |
| Recipe (pattern) | 38k | 68k (15) | 2.50 | 1.61 | 0.78 | 0.02 | 1.28 | **6.39** | 0.00 | 11.1 | 10.7 | 5.4 MB |
| Mask | 39k | 67k (15) | 1.41 | 0.00 | 0.00 | 0.02 | 1.65 | **3.22** | 0.00 | 0.8 | 0.6 | 2.7 MB |
| Paint | 39k | 67k (15) | 1.35 | 0.00 | 0.00 | 0.02 | 2.14 | **3.64** | 0.00 | 1.2 | 1.0 | 5.5 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1583 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 40 |
| Higher Res again (no edits) | 10 |
| Save project (1905 MB) | 663 |
| Open project | 4721 |
| Orbit: frame time, 4x antialiasing | 20.9 (48 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 14.4 (70 fps) |

#### After (feat/mem), with the GPU

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 157 |
| BVH build | 247 |
| First GPU upload | 92 |
| Mesh (incl. topology) | 154 MB = 39 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 117 MB = 29 B/triangle |
| Process memory (RSS) | 451 MB (+381 MB = 96 B/triangle) |
| Peak while dividing (RSS) | 491 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 24k (15) | 0.49 | 0.59 | 0.46 | 0.00 | 0.49 | **2.06** | 0.00 | 3.7 | 3.8 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 148k (45) | 3.05 | 3.52 | 1.86 | 0.00 | 3.12 | **11.76** | 0.00 | 11.3 | 10.7 | 4.6 MB |
| Clay | 7k | 16k (11) | 0.31 | 0.35 | 0.24 | 0.00 | 0.32 | **1.24** | 0.00 | 2.4 | 2.3 | 0.9 MB |
| Clay Block | 8k | 18k (12) | 0.41 | 0.40 | 0.29 | 0.00 | 0.37 | **1.51** | 0.00 | 3.1 | 2.2 | 0.8 MB |
| Smooth | 5k | 12k (9) | 0.26 | 0.23 | 0.15 | 0.00 | 0.24 | **0.89** | 0.00 | 2.2 | 2.1 | 0.6 MB |
| Inflate | 5k | 12k (9) | 0.19 | 0.21 | 0.15 | 0.00 | 0.23 | **0.81** | 0.00 | 2.8 | 2.0 | 0.6 MB |
| Sharp Crease | 4k | 12k (9) | 0.20 | 0.21 | 0.14 | 0.00 | 0.23 | **0.79** | 0.00 | 2.1 | 2.0 | 0.5 MB |
| Layer | 5k | 12k (9) | 0.28 | 0.22 | 0.14 | 0.00 | 0.24 | **0.90** | 0.00 | 2.0 | 1.9 | 0.6 MB |
| Move | 4k | 10k (8) | 0.02 | 0.13 | 0.11 | 0.00 | 0.20 | **0.47** | 0.00 | 0.3 | 0.3 | 0.1 MB |
| Recipe (pattern) | 5k | 13k (9) | 0.47 | 0.22 | 0.14 | 0.00 | 0.24 | **1.09** | 0.00 | 1.9 | 2.0 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.17 | 0.00 | 0.00 | 0.00 | 0.28 | **0.48** | 0.00 | 0.1 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.17 | 0.00 | 0.00 | 0.00 | 0.35 | **0.54** | 0.00 | 0.1 | 0.1 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 110 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 2 |
| Save project (165 MB) | 54 |
| Open project | 298 |
| Open project: memory (RSS) | +414 MB, peak +457 MB |
| Orbit: frame time, 4x antialiasing | 4.8 (209 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 3.0 (337 fps) |

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 797 |
| BVH build | 1098 |
| First GPU upload | 360 |
| Mesh (incl. topology) | 614 MB = 39 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 470 MB = 29 B/triangle |
| Process memory (RSS) | 1640 MB (+1272 MB = 80 B/triangle) |
| Peak while dividing (RSS) | 1599 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 76k (15) | 2.30 | 2.46 | 1.56 | 0.00 | 1.95 | **8.46** | 0.00 | 16.6 | 10.8 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 525k (45) | 9.02 | 9.63 | 5.99 | 0.01 | 10.29 | **35.68** | 0.00 | 35.2 | 34.5 | 18.5 MB |
| Clay | 28k | 47k (11) | 1.18 | 1.44 | 0.93 | 0.01 | 1.03 | **4.68** | 0.00 | 8.7 | 8.6 | 3.5 MB |
| Clay Block | 33k | 55k (12) | 1.75 | 1.77 | 1.18 | 0.01 | 1.28 | **6.09** | 0.00 | 11.7 | 9.5 | 3.8 MB |
| Smooth | 19k | 37k (9) | 1.05 | 0.94 | 0.65 | 0.01 | 0.77 | **3.50** | 0.00 | 6.5 | 6.6 | 2.7 MB |
| Inflate | 18k | 36k (9) | 0.83 | 1.07 | 0.79 | 0.01 | 0.76 | **3.52** | 0.00 | 9.3 | 7.2 | 2.6 MB |
| Sharp Crease | 18k | 35k (9) | 0.86 | 0.96 | 0.67 | 0.01 | 0.73 | **3.30** | 0.00 | 6.6 | 6.5 | 2.5 MB |
| Layer | 19k | 36k (9) | 1.22 | 1.03 | 0.72 | 0.01 | 0.77 | **3.80** | 0.00 | 8.2 | 6.9 | 2.6 MB |
| Move | 16k | 29k (8) | 0.14 | 0.60 | 0.74 | 0.01 | 0.60 | **2.16** | 0.00 | 1.7 | 1.7 | 0.3 MB |
| Recipe (pattern) | 19k | 37k (9) | 2.07 | 1.02 | 0.67 | 0.01 | 0.80 | **4.63** | 0.00 | 8.0 | 7.9 | 2.6 MB |
| Mask | 19k | 36k (9) | 0.72 | 0.00 | 0.00 | 0.01 | 0.91 | **1.71** | 0.00 | 0.3 | 0.3 | 1.3 MB |
| Paint | 19k | 36k (9) | 0.64 | 0.00 | 0.00 | 0.01 | 1.20 | **1.92** | 0.00 | 0.5 | 0.4 | 2.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 434 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 24 |
| Higher Res again (no edits) | 6 |
| Save project (658 MB) | 231 |
| Open project | 1047 |
| Open project: memory (RSS) | +1621 MB, peak +1875 MB |
| Orbit: frame time, 4x antialiasing | 11.9 (84 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 8.4 (118 fps) |

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1415 |
| BVH build | 1675 |
| First GPU upload | 710 |
| Mesh (incl. topology) | 1281 MB = 39 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 980 MB = 29 B/triangle |
| Process memory (RSS) | 3306 MB (+2520 MB = 76 B/triangle) |
| Peak while dividing (RSS) | 3185 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 147k (22) | 4.19 | 3.47 | 1.94 | 0.00 | 3.77 | **13.71** | 0.00 | 25.5 | 23.4 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (57) | 17.54 | 19.03 | 11.59 | 0.01 | 15.89 | **65.42** | 0.00 | 71.5 | 69.9 | 38.7 MB |
| Clay | 58k | 90k (16) | 2.09 | 1.82 | 1.03 | 0.01 | 1.85 | **7.02** | 0.00 | 14.3 | 14.1 | 7.2 MB |
| Clay Block | 66k | 101k (17) | 2.48 | 2.17 | 1.25 | 0.01 | 2.06 | **8.19** | 0.00 | 15.3 | 15.2 | 8.0 MB |
| Smooth | 39k | 67k (14) | 1.71 | 1.58 | 0.80 | 0.02 | 1.51 | **5.77** | 0.00 | 11.0 | 10.8 | 5.7 MB |
| Inflate | 38k | 65k (14) | 1.54 | 1.80 | 0.95 | 0.02 | 1.46 | **5.94** | 0.00 | 10.7 | 11.0 | 5.6 MB |
| Sharp Crease | 37k | 64k (14) | 1.54 | 1.76 | 0.91 | 0.02 | 1.64 | **6.03** | 0.00 | 10.4 | 10.4 | 5.4 MB |
| Layer | 37k | 65k (14) | 2.14 | 1.66 | 0.82 | 0.02 | 1.43 | **6.22** | 0.00 | 11.3 | 11.2 | 5.6 MB |
| Move | 33k | 58k (11) | 0.27 | 0.88 | 0.76 | 0.02 | 1.22 | **3.32** | 0.00 | 1.5 | 2.0 | 0.5 MB |
| Recipe (pattern) | 38k | 67k (14) | 2.66 | 1.78 | 0.81 | 0.02 | 1.58 | **7.03** | 0.00 | 11.4 | 11.4 | 5.4 MB |
| Mask | 39k | 66k (15) | 1.48 | 0.00 | 0.00 | 0.02 | 1.84 | **3.48** | 0.00 | 0.8 | 0.6 | 2.7 MB |
| Paint | 39k | 66k (15) | 1.38 | 0.00 | 0.00 | 0.02 | 2.30 | **3.83** | 0.00 | 1.2 | 1.0 | 5.5 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 876 |
| Higher Res (back up) | 13 |
| Lower Res again (no edits) | 55 |
| Higher Res again (no edits) | 11 |
| Save project (1373 MB) | 433 |
| Open project | 2137 |
| Open project: memory (RSS) | +3475 MB, peak +4017 MB |
| Orbit: frame time, 4x antialiasing | 20.0 (50 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 14.6 (68 fps) |

#### After, CPU only, 64M

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

##### 63.7M triangles (31.9M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2809 |
| BVH build | 3347 |
| Mesh (incl. topology) | 2456 MB = 39 B/triangle |
| BVH | 1244 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1879 MB = 29 B/triangle |
| Process memory (RSS) | 5763 MB (+5753 MB = 90 B/triangle) |
| Peak while dividing (RSS) | 6786 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 198k | 259k (28) | 6.58 | 4.76 | 2.82 | 0.00 | **14.68** | 0.00 | 43.2 | 43.1 | 21.8 MB |
| Standard (large, r 0.4) | 1.9M | 2.0M (79) | 33.33 | 36.58 | 22.48 | 0.01 | **94.81** | 0.00 | 138.0 | 135.5 | 74.0 MB |
| Clay | 110k | 155k (21) | 4.23 | 2.97 | 1.75 | 0.02 | **9.31** | 0.00 | 25.6 | 25.8 | 13.8 MB |
| Clay Block | 120k | 166k (23) | 4.38 | 3.07 | 1.75 | 0.02 | **9.58** | 0.00 | 29.2 | 28.1 | 15.1 MB |
| Smooth | 71k | 108k (19) | 3.11 | 2.26 | 1.12 | 0.03 | **6.76** | 0.00 | 20.9 | 21.3 | 10.8 MB |
| Inflate | 69k | 106k (19) | 2.81 | 2.30 | 1.25 | 0.03 | **6.65** | 0.00 | 20.6 | 20.2 | 10.7 MB |
| Sharp Crease | 68k | 104k (18) | 2.59 | 2.19 | 1.15 | 0.03 | **6.21** | 0.00 | 19.6 | 19.4 | 10.4 MB |
| Layer | 70k | 107k (19) | 3.73 | 2.34 | 1.18 | 0.03 | **7.55** | 0.00 | 20.7 | 21.6 | 10.8 MB |
| Move | 64k | 98k (13) | 0.55 | 1.12 | 1.14 | 0.03 | **3.17** | 0.00 | 2.5 | 2.9 | 1.0 MB |
| Recipe (pattern) | 72k | 109k (19) | 4.01 | 2.42 | 1.15 | 0.04 | **7.90** | 0.00 | 21.0 | 21.2 | 10.6 MB |
| Mask | 72k | 108k (19) | 2.46 | 0.00 | 0.00 | 0.03 | **2.71** | 0.00 | 1.6 | 1.3 | 5.3 MB |
| Paint | 72k | 108k (19) | 2.34 | 0.00 | 0.00 | 0.03 | **2.57** | 0.00 | 2.6 | 2.3 | 10.7 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 15.9M, after edits) | 1551 |
| Higher Res (back up) | 25 |
| Lower Res again (no edits) | 76 |
| Higher Res again (no edits) | 21 |
| Save project (2633 MB) | 846 |
| Open project | 6307 |
| Open project: memory (RSS) | +6822 MB, peak +7821 MB |

#### After, CPU only, 104M (no project round trip)

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

##### 104.0M triangles (52.0M vertices, 8 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4889 |
| BVH build | 5896 |
| Mesh (incl. topology) | 4011 MB = 39 B/triangle |
| BVH | 2043 MB = 19.6 B/triangle |
| Subdivision levels (stack) | 3068 MB = 29 B/triangle |
| Process memory (RSS) | 8974 MB (+8964 MB = 86 B/triangle) |
| Peak while dividing (RSS) | 10982 MB above the start |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 324k | 407k (43) | 9.69 | 7.10 | 5.00 | 0.00 | **22.61** | 0.00 | 70.3 | 68.2 | 35.6 MB |
| Standard (large, r 0.4) | 3.1M | 3.2M (114) | 54.36 | 60.70 | 42.49 | 0.02 | **161.53** | 0.00 | 262.4 | 261.4 | 121.1 MB |
| Clay | 181k | 242k (32) | 6.10 | 4.26 | 2.59 | 0.03 | **13.52** | 0.00 | 47.7 | 51.7 | 22.6 MB |
| Clay Block | 193k | 256k (34) | 6.94 | 4.91 | 3.10 | 0.04 | **15.59** | 0.00 | 51.3 | 52.3 | 24.4 MB |
| Smooth | 115k | 164k (28) | 4.76 | 3.13 | 1.60 | 0.05 | **9.96** | 0.00 | 35.2 | 35.1 | 17.4 MB |
| Inflate | 113k | 161k (27) | 4.43 | 3.27 | 1.85 | 0.05 | **10.01** | 0.00 | 34.7 | 36.1 | 17.2 MB |
| Sharp Crease | 111k | 159k (28) | 4.08 | 3.26 | 1.67 | 0.05 | **9.44** | 0.00 | 37.3 | 39.0 | 16.7 MB |
| Layer | 114k | 163k (28) | 5.86 | 3.11 | 1.69 | 0.05 | **11.11** | 0.00 | 41.4 | 38.9 | 17.4 MB |
| Move | 105k | 152k (30) | 0.78 | 1.57 | 1.83 | 0.05 | **4.76** | 0.00 | 4.1 | 4.1 | 1.7 MB |
| Recipe (pattern) | 117k | 167k (28) | 6.53 | 3.10 | 1.49 | 0.05 | **11.58** | 0.00 | 37.2 | 36.8 | 17.2 MB |
| Mask | 118k | 166k (28) | 3.85 | 0.00 | 0.00 | 0.05 | **4.26** | 0.00 | 2.6 | 2.3 | 8.6 MB |
| Paint | 118k | 166k (28) | 3.95 | 0.00 | 0.00 | 0.05 | **4.36** | 0.00 | 4.2 | 4.0 | 17.3 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 26.0M, after edits) | 2776 |
| Higher Res (back up) | 32 |
| Lower Res again (no edits) | 133 |
| Higher Res again (no edits) | 32 |


The v0.6 performance batch ran as three branches side by side (mem, brush, view); each adds its own subsection
here. Same machine and benchmark as above.

### Brushes: vertex sets, normals, BVH refits (feat/brush)

Goal: big Move / Smooth / Inflate / Standard strokes fluid on huge meshes (a large dab under about 25 ms at 33M
triangles, a medium one under about 5 ms, undo of a large stroke at least twice as fast), with results still
identical with multithreading on or off. "Before" is main at aac5f68 with the two large-radius rows added to the
benchmark (28fb707), "after" is feat/brush; both binaries were run in turn, three times each, at 4, 16 and 33M
(CPU only, no project round trip, 40 dabs per stroke, X symmetry), keeping each row's best run, because the
other branches' builds and tests kept the machine busy (load 8-20). 64M: best of two runs each.

| 33M triangles, X symmetry | before | after | |
|---|---:|---:|---|
| Medium dab (Standard, radius 0.12: 100k vertices per step) | 8.6 ms | **3.6 ms** | 2.4x |
| Large dab (Standard, radius 0.4: 1M vertices per step) | 49.4 ms | **21.0 ms** | 2.3x |
| Large Smooth | 27.8 ms | **9.8 ms** | 2.8x |
| Large Move | 16.5 ms | **5.2 ms** | 3.2x |
| Undo of the 40-dab large Standard stroke (2.4M vertices) | 69.5 ms | **35.0 ms** | 2.0x |
| Undo of a medium stroke | 21.7 ms | **10.5 ms** | 2.1x |
| BVH | 19.5 B/triangle | **17.1** | |
| Mesh | 62 B/triangle | **60** | |
| Stroke recorder (allocated by the first stroke) | 12 B/vertex | **3 bits** | |

At 64M triangles (CPU only): medium dab 14.2 -> **7.4 ms**, large 95.9 -> **57.5 ms**, large Smooth 48.2 -> **25.8 ms**,
large Move 34.9 -> **14.7 ms**, undo of the large stroke 138 -> **98 ms**.

The v0.5 summary's 93 ms for the large dab includes the GPU upload; CPU only, the v0.5 "after" table above
has 53 ms and 77 ms for its undo, in line with today's "before" runs of the same code (49 and 70 ms).

With the GPU (`sculptforge --bench`, before and after run in turn, best of two, the upload of what each dab changed
included; the upload itself is the view branch's work and unchanged here), at 33M: medium dab 11.4 -> **6.5 ms**,
large 62.8 -> **38.7 ms** (15 ms of it the upload), large Move 22.1 -> **11.9 ms**. The CPU-side target (large
dab under 25 ms) is met; with the upload the large dab is still above it.

#### What changed

- **Vertex sets as bitmaps, listed in index order** (core/IndexBits.h): one bit per vertex (or BVH leaf) plus a
  summary bit per 4096, so listing a set visits only the chunks that hold members. The BVH vertex query, the
  normal update and the refit used 32-bit stamps per vertex and listed vertices in the order threads found
  them; now every dab's vertex list is in increasing index order. On a divided mesh a dab's vertices lie in a few
  dozen runs of the numbering, so the kernel, the normal update and the undo record walk positions, normals and
  adjacency front to back instead of jumping between subdivision levels. The order doesn't depend on the thread
  count, so results stay identical with multithreading on or off (the tests compare bit for bit).
- **Each thread marks a bitmap of its own** (IndexBits::insertParallel, scratch sets shared process-wide): with
  one shared bitmap, threads fought over its cache lines and the query scaled 3x on 10 cores; merging per-job
  sets by chunk made it twice as fast. The query also splits the sphere's subtrees breadth first until it has 128
  tasks (it used a fixed depth, which gave a medium dab fewer than 8 tasks, so it ran on one thread), skips the
  distance tests for leaves wholly inside the sphere, and stays on one thread for small dabs.
- **Refit from the query**: the query already visits the BVH leaves holding the vertices it returns, so a dab
  refits exactly those (BVH::refitQueried, BVH::refitLeaves) instead of looking every moved vertex's triangles
  up; the Move brush keeps its stroke's leaves. Generic refits (undo, transforms) use a leaf bitmap. At 33M the
  large dab's refit went from 11.7 to 5.5 ms.
- **Normals**: the vertices to update (moved ones and their ring) are a bitmap, listed in order and ORed straight
  into the mesh's dirty bits; each vertex sums its faces as before (identical values), with the faces of
  vertices 16 ahead and their corners 8 ahead prefetched (core/Prefetch.h), about 30% less time on one thread.
  Symmetric dabs update the normals once after all mirrors moved, so a vertex in both halves (near the symmetry
  plane) is updated once; each mirror still refits the BVH before the next one queries it.
- **Undo record and Move in parallel**: displaceAll counts per block which vertices the stroke moves for the
  first time and writes their undo entries at those offsets (same order as before) while moving them; the
  Sculptor's per-vertex stamps for positions, mask and colours are bitmaps cleared from the stroke's own lists.
  The Move brush moves each mirror's region in parallel (it was a serial loop). Undo and redo swap position
  channels in parallel (each lists a vertex once).
- **Benchmark**: Smooth and Move at radius 0.4, and `--bench-brushes TEXT` to run only the rows whose name
  contains TEXT.

Tried and dropped: computing each face normal once into a compact array (gathering the faces as a set and
finding each by its rank) was slower than summing the faces per vertex; prefetching in the refit's leaf loop and
in the Smooth kernel, longer prefetch distances, and skipping repeated leaves in the refit's lookup made no
difference or were slower.

Behaviour: results are identical with multithreading on or off. Compared with 0.5 they differ by rounding (a
brush's area normal and centre are summed over its vertices in index order instead of BVH order) and, where the
two halves of a symmetric dab overlap, both halves now see the normals from before the dab (the second one used
to see the first one's).

#### Known limits

- Normal updates and refits still wait on memory: a vertex's faces and their corners are scattered over the
  triangle and position arrays. Per-leaf contiguous vertex lists would let a leaf's box be recomputed from its
  15 or so vertices instead of 48 corners, but cost about 4 bytes per triangle; quantised BVH nodes and SoA
  positions were not tried.
- At 64M the gains are smaller (1.4x for undo of a large stroke, 98 ms): the generic refit's per-vertex leaf
  lookup and the normal update over 4.6M vertices dominate.
- Undo of a Mask or Paint stroke is about 0.4 ms slower at 33M (1.2 instead of 0.8 ms): its entries are now in
  index order per dab rather than in BVH order.
- Prefetching is a no-op with MSVC (core/Prefetch.h uses the GCC / Clang builtin).

#### CPU only, 4.0M triangles

| Brush | dab before | dab after | kernel | normals | BVH refit | undo before | undo after |
|---|---:|---:|---:|---:|---:|---:|---:|
| Standard | 1.40 | **1.09** | 0.49 -> 0.44 | 0.48 -> 0.38 | 0.39 -> 0.24 | 2.9 | **1.2** |
| Standard (large, r 0.4) | 8.04 | **3.51** | 2.95 -> 1.44 | 3.06 -> 0.93 | 1.80 -> 0.89 | 10.0 | **4.1** |
| Clay | 0.79 | **0.68** | 0.30 -> 0.30 | 0.27 -> 0.22 | 0.20 -> 0.13 | 2.4 | **1.0** |
| Clay Block | 1.03 | **0.82** | 0.43 -> 0.38 | 0.33 -> 0.25 | 0.25 -> 0.16 | 2.5 | **0.8** |
| Smooth | 0.55 | **0.48** | 0.24 -> 0.23 | 0.17 -> 0.15 | 0.12 -> 0.08 | 2.0 | **1.1** |
| Smooth (large, r 0.4) | 5.24 | **2.42** | 2.15 -> 1.18 | 2.04 -> 0.68 | 0.93 -> 0.43 | 6.3 | **2.6** |
| Inflate | 0.51 | **0.47** | 0.19 -> 0.20 | 0.17 -> 0.16 | 0.12 -> 0.08 | 1.9 | **1.1** |
| Sharp Crease | 0.49 | **0.45** | 0.19 -> 0.21 | 0.16 -> 0.15 | 0.11 -> 0.08 | 2.4 | **1.0** |
| Layer | 0.58 | **0.55** | 0.28 -> 0.29 | 0.17 -> 0.15 | 0.11 -> 0.08 | 1.9 | **1.2** |
| Move | 0.26 | **0.22** | 0.02 -> 0.02 | 0.10 -> 0.12 | 0.12 -> 0.07 | 0.3 | **0.2** |
| Move (large, r 0.4) | 2.17 | **1.13** | 0.41 -> 0.21 | 0.73 -> 0.55 | 0.86 -> 0.27 | 2.4 | **0.9** |
| Recipe (pattern) | 0.77 | **0.73** | 0.46 -> 0.48 | 0.17 -> 0.15 | 0.12 -> 0.08 | 2.0 | **0.8** |
| Mask | 0.19 | **0.21** | 0.17 -> 0.19 | 0.00 -> 0.00 | 0.00 -> 0.00 | 0.1 | **0.1** |
| Paint | 0.20 | **0.19** | 0.17 -> 0.17 | 0.00 -> 0.00 | 0.00 -> 0.00 | 0.1 | **0.1** |

#### CPU only, 15.9M triangles

| Brush | dab before | dab after | kernel | normals | BVH refit | undo before | undo after |
|---|---:|---:|---:|---:|---:|---:|---:|
| Standard | 4.91 | **2.49** | 1.88 -> 1.03 | 1.82 -> 0.69 | 1.07 -> 0.59 | 10.8 | **4.7** |
| Standard (large, r 0.4) | 24.70 | **10.29** | 8.98 -> 3.29 | 9.29 -> 3.14 | 5.72 -> 2.90 | 33.8 | **16.9** |
| Clay | 3.09 | **1.89** | 1.16 -> 0.94 | 1.13 -> 0.48 | 0.71 -> 0.33 | 7.1 | **3.4** |
| Clay Block | 3.35 | **2.18** | 1.33 -> 0.96 | 1.20 -> 0.67 | 0.72 -> 0.43 | 7.6 | **3.4** |
| Smooth | 2.15 | **1.55** | 0.89 -> 0.78 | 0.69 -> 0.39 | 0.50 -> 0.30 | 5.5 | **2.7** |
| Smooth (large, r 0.4) | 12.95 | **5.43** | 5.48 -> 2.33 | 4.75 -> 1.51 | 2.30 -> 1.09 | 20.2 | **10.0** |
| Inflate | 2.12 | **1.54** | 0.77 -> 0.68 | 0.74 -> 0.43 | 0.53 -> 0.33 | 5.4 | **2.6** |
| Sharp Crease | 2.07 | **1.54** | 0.76 -> 0.71 | 0.73 -> 0.42 | 0.50 -> 0.31 | 5.4 | **2.5** |
| Layer | 2.41 | **1.73** | 1.04 -> 0.92 | 0.78 -> 0.44 | 0.51 -> 0.29 | 6.5 | **2.7** |
| Move | 1.15 | **0.68** | 0.14 -> 0.11 | 0.42 -> 0.37 | 0.51 -> 0.12 | 0.9 | **0.6** |
| Move (large, r 0.4) | 7.39 | **2.80** | 1.57 -> 0.34 | 2.37 -> 1.23 | 2.67 -> 0.77 | 5.6 | **2.4** |
| Recipe (pattern) | 3.17 | **2.65** | 1.78 -> 1.81 | 0.80 -> 0.45 | 0.50 -> 0.28 | 6.6 | **2.6** |
| Mask | 0.76 | **0.54** | 0.69 -> 0.47 | 0.00 -> 0.00 | 0.00 -> 0.00 | 0.3 | **0.5** |
| Paint | 0.69 | **0.58** | 0.62 -> 0.51 | 0.00 -> 0.00 | 0.00 -> 0.00 | 0.5 | **0.8** |

#### CPU only, 33.2M triangles

| Brush | dab before | dab after | kernel | normals | BVH refit | undo before | undo after |
|---|---:|---:|---:|---:|---:|---:|---:|
| Standard | 8.59 | **3.64** | 3.66 -> 1.47 | 2.87 -> 0.98 | 1.76 -> 0.85 | 21.7 | **10.5** |
| Standard (large, r 0.4) | 49.42 | **21.03** | 18.08 -> 6.06 | 18.22 -> 7.63 | 11.67 -> 5.53 | 69.5 | **35.0** |
| Clay | 5.63 | **2.70** | 2.36 -> 1.22 | 2.00 -> 0.76 | 1.03 -> 0.51 | 14.3 | **6.7** |
| Clay Block | 6.32 | **2.96** | 2.60 -> 1.28 | 2.26 -> 0.80 | 1.22 -> 0.59 | 15.8 | **7.5** |
| Smooth | 3.95 | **2.14** | 1.62 -> 1.00 | 1.44 -> 0.64 | 0.73 -> 0.30 | 12.1 | **5.3** |
| Smooth (large, r 0.4) | 27.81 | **9.78** | 12.07 -> 3.80 | 9.97 -> 3.14 | 4.98 -> 1.84 | 40.5 | **18.2** |
| Inflate | 4.39 | **2.02** | 1.72 -> 0.87 | 1.67 -> 0.63 | 0.83 -> 0.37 | 11.1 | **4.9** |
| Sharp Crease | 4.28 | **2.41** | 1.64 -> 1.04 | 1.74 -> 0.74 | 0.73 -> 0.38 | 11.4 | **4.8** |
| Layer | 4.85 | **2.80** | 2.14 -> 1.47 | 1.73 -> 0.74 | 0.81 -> 0.36 | 11.5 | **5.0** |
| Move | 2.14 | **1.26** | 0.28 -> 0.28 | 0.90 -> 0.62 | 0.78 -> 0.23 | 1.8 | **1.1** |
| Move (large, r 0.4) | 16.52 | **5.23** | 3.23 -> 0.55 | 5.88 -> 2.34 | 5.98 -> 1.53 | 10.7 | **4.4** |
| Recipe (pattern) | 5.05 | **3.33** | 2.57 -> 2.15 | 1.53 -> 0.64 | 0.77 -> 0.33 | 11.1 | **5.1** |
| Mask | 1.39 | **1.08** | 1.26 -> 0.90 | 0.00 -> 0.00 | 0.00 -> 0.00 | 0.8 | **1.2** |
| Paint | 1.34 | **1.06** | 1.21 -> 0.90 | 0.00 -> 0.00 | 0.00 -> 0.00 | 1.3 | **1.8** |

#### With the GPU, 4.0M triangles

| Brush | dab before | dab after | of which GPU upload (after) | undo before | undo after |
|---|---:|---:|---:|---:|---:|
| Standard | 1.77 | **1.47** | 0.36 | 3.4 | **1.6** |
| Standard (large, r 0.4) | 10.76 | **5.97** | 2.28 | 9.8 | **4.2** |
| Smooth (large, r 0.4) | 6.98 | **3.83** | 1.21 | 5.7 | **3.1** |
| Move (large, r 0.4) | 3.03 | **2.12** | 0.78 | 2.0 | **1.2** |
| Clay | 1.04 | **0.90** | 0.22 | 2.6 | **1.6** |
| Inflate | 0.67 | **0.62** | 0.16 | 1.5 | **1.1** |

#### With the GPU, 15.9M triangles

| Brush | dab before | dab after | of which GPU upload (after) | undo before | undo after |
|---|---:|---:|---:|---:|---:|
| Standard | 6.80 | **3.68** | 1.29 | 12.9 | **5.2** |
| Standard (large, r 0.4) | 32.72 | **18.57** | 7.68 | 33.2 | **17.4** |
| Smooth (large, r 0.4) | 18.38 | **9.54** | 4.00 | 20.6 | **10.3** |
| Move (large, r 0.4) | 10.35 | **5.82** | 3.03 | 5.6 | **2.5** |
| Clay | 3.80 | **2.60** | 0.81 | 6.9 | **2.9** |
| Inflate | 2.64 | **2.23** | 0.66 | 5.3 | **2.2** |

#### With the GPU, 33.2M triangles

| Brush | dab before | dab after | of which GPU upload (after) | undo before | undo after |
|---|---:|---:|---:|---:|---:|
| Standard | 11.42 | **6.50** | 2.64 | 21.7 | **11.8** |
| Standard (large, r 0.4) | 62.83 | **38.74** | 14.97 | 68.7 | **36.7** |
| Smooth (large, r 0.4) | 32.19 | **20.94** | 8.62 | 36.6 | **19.8** |
| Move (large, r 0.4) | 22.07 | **11.88** | 5.98 | 11.1 | **5.8** |
| Clay | 6.60 | **4.44** | 1.74 | 13.2 | **6.6** |
| Inflate | 5.10 | **3.68** | 1.47 | 10.5 | **5.6** |

The v0.6 performance work ran on three branches at once (mesh memory, brushes, viewport), each measured
against main at aac5f68 with `sculptforge --bench --bench-sizes 4,16,33 --bench-dabs 10 --bench-no-project`, on
the same machine as below, one benchmark at a time; every figure is the better of two runs. The sections after
this one (from "Summary") are v0.5's.

### Viewport (feat/view)

"Before" is main at aac5f68 plus the benchmark rows below; "after" is feat/view. The 33M benchmark sphere has a
few cracks on main (makeCube welded badly at 26 quads per side; fixed in feat/mem), so the 33M "after" runs
use that one-line fix, as the merged branches will; without it, the 33M sphere counts as open and draws its back
faces too (last column). 104M ran with `--bench-view` (the full brush suite at that size doesn't fit in memory);
neither 64M nor 104M was drawn before (v0.5 only measured 64M on the CPU). Full reports are at the end.

| | 4M | 16M | 33M | 64M | 104M | 33M, open (no fix) |
|---|---|---|---|---|---|---|
| Orbit, 4x antialiasing (ms) | 4.8 -> **2.9** | 11.4 -> **4.9** | 20.6 -> **8.7** | **14.0** | **19.6** (51 fps) | 15.3 |
| Orbit, no antialiasing | 3.0 -> **1.6** | 8.2 -> **3.2** | 14.3 -> **6.1** | **11.3** | **16.9** (59 fps) | 12.2 |
| Orbit, Fast Navigation (no AA, motion proxy) | 3.0 -> **1.1** | 8.2 -> **1.9** | 14.3 -> **3.2** (314 fps) | **6.5** | **9.9** (101 fps) | 4.6 |
| Triangles drawn after culling (proxy) | 53% (19%) | 49% (19%) | 48% (19%) | 47% (19%) | 46% (19%) | 100% (19%) |
| First GPU upload (ms) | 87 -> **52** | 337 -> **199** | 701 -> **422** | **784** | **1283** | 571 |
| GPU memory, bytes per triangle | 46 -> **14.8** | 46 -> **14.8** | 46 -> **14.8** (1.53 -> 0.49 GB) | **14.8** | **14.8** (1.5 GB) | 14.8 |
| Motion proxy build, first move after an upload (ms) | 16 | 60 | 126 | 241 | 408 | 127 |
| GPU upload per medium dab: Standard / Mask / Paint (ms) | 0.28 / 0.39 / 0.62 -> 0.57 / 0.34 / 0.70 | 0.95 / 1.33 / 2.16 -> 1.19 / 1.05 / 1.76 | 2.19 / 2.89 / 4.64 -> **2.08 / 1.89 / 3.69** | 3.8 / 3.7 / 7.3 | 6.6 / 6.5 / 12.8 | |

"Before" GPU memory is the old buffers' size, which the old renderer didn't report: positions, normals and
curvature as floats (28 bytes per vertex, 14 per triangle), 32-bit indices (12), 32-bit face groups (4) and, on
meshes with quads like this one, the wireframe's edges, always kept, every edge twice (16): 46 bytes per
triangle (26 without the last two). After: 12-byte vertices with about 10% copies on cluster seams (6.6 per
triangle), 16-bit indices (6), 16-bit face groups (2), cluster tables (0.1). Vertex Paint adds 2.2, a mask 0.6,
the motion proxy 1.5 once the view has moved, the wireframe about 4 while it is on.

Targets from the brief: GPU memory down at least a third (down 68%, or 43% against the 26 bytes counted
before); 60 fps at 100M (101 fps with Fast Navigation, 59 without antialiasing, 51 with 4x); orbit at 33M under
about 8 ms with 4x antialiasing (8.7 ms, from 20.6: not quite); first upload at 33M under about 300 ms (422,
from 701: not reached).

#### What changed

- **Clusters** (core/UploadOrder.h). The triangles are cut into runs of 64 consecutive triangles (compact on
  any mesh whose triangle order isn't random: a divided mesh keeps each cage face's triangles together), the
  runs sorted along a Morton curve of their centres, and every 64 runs make a cluster of up to 4096 triangles.
  Each cluster has its own block-aligned range of GPU vertices, numbered by first use, so its indices are 16-bit
  with a base vertex; vertices on seams between clusters are copied into each cluster that uses them (about
  10%). The mesh's own vertex numbering is untouched. Without the Morton step, clusters on meshes with small cage
  faces (a remesh divided three times) were long strips whose normals point every way: a test sphere culled 9%
  of its clusters, 28% with it.
- **Culling** (render/Clusters.h), on the CPU every frame: clusters outside the view, and clusters whose
  triangles all face away (a normal cone per cluster against a bounding sphere). Back faces may only be skipped
  where they can't show: on a mesh that is closed and wound consistently (every directed edge has its reverse;
  found with a 64-bit hash sum while the layout is built, with a fallback that accepts unwelded seams) and
  outwards (positive signed volume), with nothing hidden, the eye outside its bounds and nothing reaching into
  the near plane's slab. Then every back face lies behind a front face, so the picture is the same; the GPU
  also culls the back faces left in clusters along the silhouette. The visible clusters go in one
  `glMultiDrawElementsBaseVertex`. About half the triangles of a sphere are drawn.
- **12-byte vertices**: the position on a 21-bit grid over the mesh's bounds plus a margin (one grid for the
  whole mesh, so a vertex copied into two clusters decodes to exactly the same point and seams can't crack; a
  stroke that leaves the grid re-sends the mesh on a new one), an octahedral normal (2 x 11 bits, within 0.12
  degrees) and the curvature (10 bits, square-root companded over its whole range, within 0.001 where the
  cavity shading ramps). Vertex Paint is RGBA8 with gamma 2.2 (decoded in the shader), the mask 8-bit. The
  fragment shader works out the view direction from the pixel instead of an interpolated position, so a vertex
  passes only a normal and the curvature (plus colour and mask when the mesh has them; shader variants per
  feature).
- **Motion proxy**: while a scene of 10M+ triangles moves with Fast Navigation on, each cluster of a mesh of
  256k+ triangles draws a proxy: its vertices snapped to the one nearest the centre of each cell of a grid 2.5
  average edges wide, collapsed triangles dropped, seam vertices kept, using the same vertex buffer (indices
  only, up to a quarter of the cluster's triangles; 19% in practice). The proxies of a closed mesh form a closed
  mesh (a simplicial map of it), so the same culling applies to them with their own normal cones and no
  hole can open between clusters. Proxies are built for the clusters that changed, when the view starts moving
  (126 ms at 33M after an upload, a few clusters after a stroke). Face Group and texture display and the
  wireframe keep the full mesh.
- **Uploads**: a full upload writes the index buffer straight into the mapped buffer from the threads building
  the layout, and whole vertex arrays mapped in parallel; dab uploads gather all their runs in parallel. Each
  128-vertex block keeps a hash of what it last sent: blocks a dab marked but didn't change (paint, mask) aren't
  sent again, and clusters whose positions didn't move keep their bounds and proxy.
- **Wireframe** edges are built when the wireframe is first shown, each edge once (from the lowest-numbered
  drawn triangle using it), at the opacity two overlapping lines had.
- Per-triangle data (face groups, texture UVs, baked-normal tangents) is found from `gl_PrimitiveID` plus the
  cluster's first drawn triangle, read through `gl_VertexID` (which includes the base vertex) from a table per
  vertex block. A start-up check draws one point to see that the driver does include it; where it doesn't, those
  meshes draw one cluster at a time with the base as a uniform (`SF_NO_VERTEXID_BASE=1` forces that path).

#### Looks the same

Every UI test's screenshots were compared pixel by pixel with main's, and the benchmark's frames too. The
viewport matches except for: outline pixels of closed meshes, where back faces used to cover a few
antialiasing samples and show as a faint light rim (now the background shows there, as it would with a closed
surface seen exactly edge-on); a few silhouette pixels moved by the position grid (a millionth of the model);
and the wireframe on quad meshes, where line ends overlap a little differently now that each edge is drawn once.
Shelf and Library thumbnails differ in the UI tests only because they arrive sooner.

#### Known limits

- Back faces are drawn (twice the work on huge meshes) on open meshes, with hidden polygons, when the eye is
  inside the mesh's bounds or the near plane cuts it: the cases where they can show. On a closed mesh, a lobe
  pushed through the surface so that it is inside out (winding below zero) would show a hole where its back
  faces are culled; ZBrush culls back faces always.
- The proxy is coarse on a mesh with fine detail (detail under 2.5 edges disappears while moving), and the first
  move after opening or dividing a big mesh waits for it (126 ms at 33M, 408 ms at 104M).
- 33M orbiting is 8.7 ms with antialiasing: what's left is 16M sub-pixel triangles a frame, which only a level
  of detail at rest would remove.
- The first upload at 33M (422 ms) spends most of its time building the layout (about 250 ms: two passes over
  every cluster's triangles) and packing vertices (about 100 ms, mostly curvature).
- The per-cluster fallback for drivers without the base vertex in `gl_VertexID` was checked by forcing it on
  Mesa; macOS (GL 4.1) wasn't tried on hardware.
- Upload per dab at 4M and 16M is a little slower (0.3-0.6 ms) than before: runs are finer (128 vertices) and
  the clusters a dab touches get new bounds.


#### Before: main at aac5f68 with the benchmark

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 268 |
| BVH build | 222 |
| First GPU upload | 90 |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 718 MB (+648 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 9k | 20k (12) | 0.36 | 0.36 | 0.30 | 0.00 | 0.28 | **1.33** | 0.00 | 1.3 | 1.3 | 0.3 MB |
| Standard (large, r 0.4) | 147k | 183k (53) | 3.70 | 4.00 | 2.07 | 0.00 | 2.85 | **12.88** | 0.00 | 9.3 | 9.5 | 4.2 MB |
| Clay | 8k | 17k (11) | 0.33 | 0.30 | 0.24 | 0.00 | 0.23 | **1.13** | 0.00 | 1.0 | 0.9 | 0.3 MB |
| Clay Block | 13k | 24k (13) | 0.54 | 0.44 | 0.40 | 0.00 | 0.32 | **1.73** | 0.00 | 2.1 | 2.2 | 0.4 MB |
| Smooth | 7k | 16k (10) | 0.34 | 0.26 | 0.19 | 0.00 | 0.22 | **1.03** | 0.00 | 0.8 | 1.4 | 0.2 MB |
| Inflate | 7k | 16k (10) | 0.28 | 0.25 | 0.20 | 0.00 | 0.21 | **0.96** | 0.00 | 0.6 | 1.6 | 0.2 MB |
| Sharp Crease | 7k | 16k (10) | 0.29 | 0.25 | 0.19 | 0.00 | 0.21 | **0.96** | 0.00 | 1.0 | 2.1 | 0.2 MB |
| Layer | 7k | 16k (10) | 0.59 | 0.30 | 0.20 | 0.00 | 0.22 | **1.34** | 0.00 | 1.3 | 2.1 | 0.2 MB |
| Move | 6k | 14k (9) | 0.03 | 0.15 | 0.18 | 0.00 | 0.18 | **0.59** | 0.00 | 0.4 | 0.4 | 0.1 MB |
| Recipe (pattern) | 8k | 17k (11) | 0.72 | 0.27 | 0.20 | 0.00 | 0.22 | **1.44** | 0.00 | 0.7 | 1.4 | 0.2 MB |
| Mask | 8k | 16k (10) | 0.30 | 0.00 | 0.00 | 0.00 | 0.39 | **0.71** | 0.00 | 0.0 | 0.0 | 0.1 MB |
| Paint | 8k | 16k (10) | 0.25 | 0.00 | 0.00 | 0.00 | 0.65 | **0.93** | 0.00 | 0.0 | 0.0 | 0.2 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 184 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Orbit: frame time, 4x antialiasing | 5.0 (199 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 3.2 (308 fps) |

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1189 |
| BVH build | 794 |
| First GPU upload | 337 |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2712 MB (+2172 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 37k | 60k (12) | 1.55 | 1.65 | 1.09 | 0.00 | 0.95 | **5.35** | 0.00 | 3.1 | 3.1 | 1.3 MB |
| Standard (large, r 0.4) | 589k | 650k (53) | 11.12 | 11.62 | 7.06 | 0.01 | 9.93 | **40.64** | 0.00 | 30.7 | 30.4 | 17.0 MB |
| Clay | 32k | 53k (11) | 1.31 | 1.46 | 0.87 | 0.00 | 0.83 | **4.58** | 0.00 | 2.2 | 2.6 | 1.1 MB |
| Clay Block | 51k | 77k (13) | 1.87 | 1.54 | 1.01 | 0.00 | 1.20 | **5.76** | 0.00 | 3.1 | 3.0 | 1.5 MB |
| Smooth | 30k | 51k (10) | 1.17 | 1.18 | 0.54 | 0.00 | 1.02 | **4.02** | 0.00 | 2.3 | 2.4 | 1.0 MB |
| Inflate | 29k | 50k (10) | 1.24 | 1.30 | 0.86 | 0.00 | 0.76 | **4.25** | 0.00 | 2.5 | 2.4 | 1.0 MB |
| Sharp Crease | 29k | 49k (10) | 1.27 | 1.38 | 0.66 | 0.00 | 0.99 | **4.42** | 0.00 | 3.0 | 2.7 | 1.0 MB |
| Layer | 30k | 51k (10) | 2.29 | 1.42 | 0.59 | 0.00 | 1.06 | **5.49** | 0.00 | 2.9 | 3.2 | 1.0 MB |
| Move | 23k | 42k (9) | 0.19 | 0.64 | 0.66 | 0.00 | 0.63 | **2.37** | 0.00 | 1.6 | 1.8 | 0.4 MB |
| Recipe (pattern) | 31k | 52k (11) | 1.69 | 1.20 | 0.50 | 0.00 | 0.80 | **4.31** | 0.00 | 2.0 | 2.5 | 1.0 MB |
| Mask | 31k | 52k (11) | 1.29 | 0.00 | 0.00 | 0.01 | 1.44 | **2.82** | 0.00 | 0.1 | 0.1 | 0.5 MB |
| Paint | 31k | 52k (11) | 0.98 | 0.00 | 0.00 | 0.00 | 2.64 | **3.71** | 0.00 | 0.2 | 0.1 | 1.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 766 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 21 |
| Higher Res again (no edits) | 5 |
| Orbit: frame time, 4x antialiasing | 11.8 (84 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 8.7 (115 fps) |

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2469 |
| BVH build | 1655 |
| First GPU upload | 701 |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5642 MB (+4820 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 117k (18) | 3.34 | 3.86 | 1.84 | 0.00 | 2.62 | **11.91** | 0.00 | 6.9 | 5.6 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 1.3M (67) | 21.60 | 22.79 | 14.70 | 0.01 | 17.56 | **78.51** | 0.00 | 63.7 | 62.8 | 35.5 MB |
| Clay | 68k | 104k (17) | 2.63 | 2.26 | 1.34 | 0.00 | 1.76 | **8.20** | 0.00 | 4.4 | 4.6 | 2.4 MB |
| Clay Block | 107k | 151k (21) | 4.31 | 2.84 | 1.68 | 0.01 | 2.87 | **12.06** | 0.00 | 6.0 | 6.3 | 3.1 MB |
| Smooth | 63k | 98k (17) | 2.55 | 1.91 | 1.05 | 0.01 | 1.70 | **7.46** | 0.00 | 4.3 | 4.6 | 2.1 MB |
| Inflate | 61k | 96k (16) | 2.36 | 2.27 | 1.31 | 0.01 | 1.67 | **7.82** | 0.00 | 4.4 | 4.0 | 2.1 MB |
| Sharp Crease | 61k | 96k (16) | 2.30 | 1.85 | 1.15 | 0.01 | 1.42 | **6.92** | 0.00 | 4.2 | 3.9 | 2.0 MB |
| Layer | 63k | 99k (17) | 4.53 | 2.18 | 1.14 | 0.01 | 1.60 | **9.66** | 0.00 | 3.7 | 3.8 | 2.1 MB |
| Move | 49k | 79k (14) | 0.39 | 0.83 | 1.02 | 0.01 | 1.39 | **4.17** | 0.00 | 2.1 | 1.7 | 0.9 MB |
| Recipe (pattern) | 65k | 102k (18) | 4.06 | 2.12 | 1.13 | 0.01 | 1.84 | **9.39** | 0.00 | 4.0 | 4.3 | 2.1 MB |
| Mask | 66k | 101k (18) | 2.54 | 0.00 | 0.00 | 0.01 | 2.89 | **5.63** | 0.00 | 0.3 | 0.2 | 1.0 MB |
| Paint | 66k | 101k (18) | 2.19 | 0.00 | 0.00 | 0.01 | 4.64 | **7.03** | 0.00 | 0.4 | 0.3 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1588 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 40 |
| Higher Res again (no edits) | 10 |
| Orbit: frame time, 4x antialiasing | 20.6 (49 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 14.8 (68 fps) |

#### After: feat/view (33M with the makeCube fix from feat/mem)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 268 |
| BVH build | 227 |
| First GPU upload | 52 |
| GPU memory (buffers) | 59 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 721 MB (+650 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 9k | 21k (14) | 0.40 | 0.43 | 0.30 | 0.00 | 0.58 | **1.79** | 0.00 | 1.1 | 1.1 | 0.3 MB |
| Standard (large, r 0.4) | 147k | 191k (39) | 3.58 | 3.34 | 2.04 | 0.01 | 2.89 | **12.78** | 0.00 | 9.3 | 8.1 | 4.2 MB |
| Clay | 8k | 18k (10) | 0.38 | 0.36 | 0.25 | 0.00 | 0.52 | **1.58** | 0.00 | 1.1 | 1.2 | 0.3 MB |
| Clay Block | 13k | 25k (15) | 0.68 | 0.55 | 0.42 | 0.00 | 0.57 | **2.33** | 0.00 | 1.8 | 1.4 | 0.4 MB |
| Smooth | 7k | 17k (10) | 0.36 | 0.30 | 0.19 | 0.00 | 0.47 | **1.38** | 0.00 | 0.9 | 1.6 | 0.2 MB |
| Inflate | 7k | 16k (10) | 0.34 | 0.34 | 0.21 | 0.00 | 0.48 | **1.44** | 0.00 | 0.9 | 0.7 | 0.2 MB |
| Sharp Crease | 7k | 16k (10) | 0.40 | 0.35 | 0.21 | 0.00 | 0.48 | **1.50** | 0.00 | 1.4 | 1.1 | 0.2 MB |
| Layer | 7k | 17k (10) | 0.69 | 0.34 | 0.20 | 0.00 | 0.55 | **1.85** | 0.00 | 1.0 | 1.2 | 0.2 MB |
| Move | 6k | 12k (10) | 0.05 | 0.17 | 0.18 | 0.00 | 0.40 | **0.87** | 0.00 | 0.5 | 0.6 | 0.1 MB |
| Recipe (pattern) | 8k | 17k (10) | 0.95 | 0.41 | 0.25 | 0.00 | 0.50 | **2.18** | 0.00 | 1.3 | 1.2 | 0.2 MB |
| Mask | 8k | 17k (10) | 0.37 | 0.00 | 0.00 | 0.01 | 0.42 | **0.87** | 0.00 | 0.0 | 0.0 | 0.1 MB |
| Paint | 8k | 17k (10) | 0.49 | 0.00 | 0.00 | 0.01 | 0.70 | **1.29** | 0.00 | 0.1 | 0.3 | 0.2 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 175 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Motion proxy build (first move after an upload) | 16 |
| Orbit: frame time, 4x antialiasing | 2.9 (347 fps) |
| Orbit: frame time, no antialiasing | 1.7 (605 fps) |
| Orbit: triangles drawn | 2.1M (53%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 1.1 (932 fps) |
| Orbit: triangles drawn (motion proxy) | 771k (19%) |
| GPU memory with the motion proxy | 76 MB = 19.1 B/triangle |

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1145 |
| BVH build | 787 |
| First GPU upload | 200 |
| GPU memory (buffers) | 235 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2786 MB (+2286 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 37k | 61k (25) | 1.47 | 1.23 | 0.87 | 0.00 | 1.19 | **5.10** | 0.00 | 2.9 | 2.5 | 1.3 MB |
| Standard (large, r 0.4) | 589k | 699k (74) | 10.90 | 11.26 | 7.20 | 0.01 | 10.53 | **43.38** | 0.00 | 31.2 | 30.8 | 17.0 MB |
| Clay | 32k | 54k (21) | 1.22 | 1.28 | 0.73 | 0.00 | 1.03 | **4.54** | 0.00 | 2.1 | 2.5 | 1.1 MB |
| Clay Block | 51k | 78k (25) | 2.37 | 1.92 | 1.04 | 0.01 | 1.39 | **7.14** | 0.00 | 3.2 | 4.0 | 1.5 MB |
| Smooth | 30k | 51k (19) | 1.36 | 1.29 | 0.75 | 0.00 | 0.97 | **4.63** | 0.00 | 2.2 | 2.1 | 1.0 MB |
| Inflate | 29k | 50k (19) | 1.21 | 1.21 | 0.63 | 0.01 | 0.96 | **4.26** | 0.00 | 2.5 | 2.3 | 1.0 MB |
| Sharp Crease | 29k | 50k (19) | 1.15 | 1.18 | 0.59 | 0.01 | 1.06 | **4.23** | 0.00 | 2.3 | 1.9 | 1.0 MB |
| Layer | 30k | 51k (20) | 2.01 | 1.10 | 0.52 | 0.01 | 0.86 | **4.74** | 0.00 | 2.3 | 2.3 | 1.0 MB |
| Move | 23k | 40k (11) | 0.21 | 0.38 | 0.45 | 0.01 | 0.72 | **2.13** | 0.00 | 0.9 | 1.0 | 0.4 MB |
| Recipe (pattern) | 31k | 53k (19) | 1.76 | 1.25 | 0.55 | 0.01 | 1.14 | **4.98** | 0.00 | 2.9 | 2.4 | 1.0 MB |
| Mask | 31k | 52k (19) | 1.26 | 0.00 | 0.00 | 0.01 | 1.08 | **2.60** | 0.00 | 0.1 | 0.1 | 0.5 MB |
| Paint | 31k | 52k (19) | 1.12 | 0.00 | 0.00 | 0.01 | 1.80 | **3.17** | 0.00 | 0.2 | 0.2 | 1.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 748 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 25 |
| Higher Res again (no edits) | 7 |
| Motion proxy build (first move after an upload) | 68 |
| Orbit: frame time, 4x antialiasing | 4.9 (202 fps) |
| Orbit: frame time, no antialiasing | 3.2 (317 fps) |
| Orbit: triangles drawn | 7.8M (49%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 1.9 (525 fps) |
| Orbit: triangles drawn (motion proxy) | 3.1M (19%) |
| GPU memory with the motion proxy | 304 MB = 19.1 B/triangle |

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 3030 |
| BVH build | 1693 |
| First GPU upload | 461 |
| GPU memory (buffers) | 490 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5757 MB (+4345 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 114k (35) | 2.95 | 2.73 | 1.55 | 0.00 | 2.35 | **10.25** | 0.00 | 6.4 | 5.0 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 1.4M (104) | 22.30 | 24.75 | 15.38 | 0.01 | 22.64 | **92.39** | 0.00 | 67.3 | 68.8 | 35.5 MB |
| Clay | 68k | 101k (30) | 2.67 | 2.49 | 1.42 | 0.01 | 2.07 | **9.26** | 0.00 | 5.4 | 4.9 | 2.4 MB |
| Clay Block | 107k | 150k (35) | 4.00 | 3.00 | 1.58 | 0.01 | 2.77 | **12.20** | 0.00 | 6.2 | 6.9 | 3.1 MB |
| Smooth | 63k | 95k (28) | 2.88 | 2.22 | 1.10 | 0.01 | 2.17 | **8.93** | 0.00 | 5.2 | 5.3 | 2.1 MB |
| Inflate | 61k | 92k (28) | 2.52 | 2.68 | 1.34 | 0.01 | 1.88 | **8.98** | 0.00 | 4.8 | 4.8 | 2.1 MB |
| Sharp Crease | 61k | 93k (27) | 2.35 | 2.42 | 1.22 | 0.01 | 1.90 | **8.46** | 0.00 | 4.4 | 4.3 | 2.0 MB |
| Layer | 63k | 96k (27) | 4.21 | 1.98 | 0.97 | 0.01 | 1.72 | **9.48** | 0.00 | 4.6 | 5.0 | 2.1 MB |
| Move | 49k | 77k (15) | 0.39 | 1.13 | 1.32 | 0.01 | 1.51 | **5.16** | 0.00 | 2.1 | 2.1 | 0.9 MB |
| Recipe (pattern) | 65k | 99k (28) | 4.60 | 2.42 | 1.15 | 0.01 | 2.19 | **10.95** | 0.00 | 5.0 | 5.8 | 2.1 MB |
| Mask | 66k | 97k (28) | 2.43 | 0.00 | 0.00 | 0.01 | 1.89 | **4.87** | 0.00 | 0.3 | 0.3 | 1.0 MB |
| Paint | 66k | 97k (28) | 2.33 | 0.00 | 0.00 | 0.01 | 3.69 | **6.58** | 0.00 | 0.5 | 0.5 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1557 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 41 |
| Higher Res again (no edits) | 10 |
| Motion proxy build (first move after an upload) | 142 |
| Orbit: frame time, 4x antialiasing | 8.7 (115 fps) |
| Orbit: frame time, no antialiasing | 6.1 (163 fps) |
| Orbit: triangles drawn | 15.9M (48%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 3.2 (314 fps) |
| Orbit: triangles drawn (motion proxy) | 6.4M (19%) |
| GPU memory with the motion proxy | 633 MB = 19.1 B/triangle |

#### After: feat/view, 33M without the makeCube fix (the sphere has cracks, so it counts as open)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2463 |
| BVH build | 1653 |
| First GPU upload | 571 |
| GPU memory (buffers) | 490 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5497 MB (+5427 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 114k (35) | 3.14 | 2.73 | 1.44 | 0.00 | 2.25 | **10.23** | 0.00 | 5.5 | 5.5 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 1.4M (104) | 22.73 | 22.99 | 14.65 | 0.01 | 21.52 | **89.23** | 0.00 | 64.1 | 63.2 | 35.5 MB |
| Clay | 68k | 101k (30) | 3.13 | 2.15 | 1.24 | 0.01 | 1.95 | **9.06** | 0.00 | 4.9 | 5.0 | 2.4 MB |
| Clay Block | 107k | 150k (35) | 4.85 | 2.71 | 1.74 | 0.01 | 2.74 | **13.00** | 0.00 | 7.2 | 7.1 | 3.1 MB |
| Smooth | 63k | 95k (28) | 2.82 | 1.77 | 1.03 | 0.01 | 1.80 | **7.96** | 0.00 | 4.7 | 4.7 | 2.1 MB |
| Inflate | 61k | 92k (28) | 2.89 | 1.96 | 1.22 | 0.01 | 1.83 | **8.43** | 0.00 | 4.2 | 4.8 | 2.1 MB |
| Sharp Crease | 61k | 93k (27) | 2.80 | 1.97 | 1.17 | 0.01 | 1.82 | **8.32** | 0.00 | 4.1 | 4.1 | 2.0 MB |
| Layer | 63k | 96k (27) | 4.58 | 2.05 | 1.07 | 0.01 | 1.83 | **10.10** | 0.01 | 4.5 | 4.1 | 2.1 MB |
| Move | 49k | 77k (15) | 0.41 | 0.79 | 1.01 | 0.01 | 1.40 | **4.43** | 0.00 | 2.4 | 2.1 | 0.9 MB |
| Recipe (pattern) | 65k | 99k (28) | 3.28 | 1.91 | 1.01 | 0.01 | 2.04 | **8.85** | 0.00 | 4.3 | 4.4 | 2.1 MB |
| Mask | 66k | 97k (28) | 2.58 | 0.00 | 0.00 | 0.01 | 2.04 | **5.21** | 0.01 | 0.4 | 0.3 | 1.0 MB |
| Paint | 66k | 97k (28) | 2.84 | 0.00 | 0.00 | 0.01 | 3.67 | **7.11** | 0.01 | 0.8 | 1.0 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1541 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 40 |
| Higher Res again (no edits) | 10 |
| Motion proxy build (first move after an upload) | 127 |
| Orbit: frame time, 4x antialiasing | 15.3 (66 fps) |
| Orbit: frame time, no antialiasing | 12.2 (82 fps) |
| Orbit: triangles drawn | 33.2M (100%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 4.6 (218 fps) |
| Orbit: triangles drawn (motion proxy) | 6.5M (19%) |
| GPU memory with the motion proxy | 633 MB = 19.1 B/triangle |

#### After: feat/view, 64M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 63.7M triangles (31.9M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4596 |
| BVH build | 3209 |
| First GPU upload | 784 |
| GPU memory (buffers) | 940 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 3953 MB = 62 B/triangle |
| BVH | 1244 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 4756 MB = 75 B/triangle |
| Process memory (RSS) | 10407 MB (+10337 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 149k | 204k (46) | 5.29 | 3.40 | 2.23 | 0.00 | 3.79 | **16.17** | 0.00 | 10.5 | 10.2 | 5.3 MB |
| Standard (large, r 0.4) | 2.4M | 2.7M (141) | 42.11 | 43.62 | 28.13 | 0.01 | 40.24 | **167.87** | 0.00 | 125.7 | 123.4 | 67.9 MB |
| Clay | 129k | 180k (40) | 4.91 | 3.28 | 1.88 | 0.01 | 3.40 | **14.67** | 0.00 | 9.4 | 10.1 | 4.5 MB |
| Clay Block | 204k | 271k (46) | 7.79 | 4.76 | 2.66 | 0.01 | 4.79 | **21.48** | 0.00 | 11.7 | 12.6 | 6.0 MB |
| Smooth | 120k | 167k (37) | 4.84 | 3.12 | 1.42 | 0.01 | 3.01 | **13.37** | 0.01 | 10.0 | 10.1 | 4.0 MB |
| Inflate | 117k | 164k (37) | 4.65 | 3.14 | 1.93 | 0.01 | 3.13 | **13.81** | 0.01 | 8.1 | 8.1 | 3.9 MB |
| Sharp Crease | 116k | 165k (37) | 4.61 | 3.07 | 1.62 | 0.01 | 3.27 | **13.70** | 0.00 | 7.5 | 7.7 | 3.9 MB |
| Layer | 120k | 169k (39) | 8.03 | 3.36 | 1.44 | 0.01 | 3.09 | **16.93** | 0.01 | 10.5 | 11.0 | 4.0 MB |
| Move | 93k | 130k (29) | 0.74 | 1.46 | 1.60 | 0.01 | 2.43 | **7.76** | 0.00 | 3.7 | 3.8 | 1.6 MB |
| Recipe (pattern) | 125k | 175k (40) | 7.13 | 3.07 | 1.37 | 0.02 | 3.43 | **16.15** | 0.01 | 10.4 | 10.4 | 4.0 MB |
| Mask | 126k | 174k (39) | 4.59 | 0.00 | 0.00 | 0.02 | 3.66 | **9.19** | 0.00 | 0.6 | 0.6 | 2.0 MB |
| Paint | 126k | 174k (39) | 4.64 | 0.00 | 0.00 | 0.01 | 7.28 | **12.88** | 0.00 | 0.9 | 0.9 | 4.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 15.9M, after edits) | 2842 |
| Higher Res (back up) | 19 |
| Lower Res again (no edits) | 77 |
| Higher Res again (no edits) | 19 |
| Motion proxy build (first move after an upload) | 241 |
| Orbit: frame time, 4x antialiasing | 14.0 (71 fps) |
| Orbit: frame time, no antialiasing | 11.3 (88 fps) |
| Orbit: triangles drawn | 29.9M (47%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 6.5 (154 fps) |
| Orbit: triangles drawn (motion proxy) | 12.4M (19%) |
| GPU memory with the motion proxy | 1214 MB = 19.1 B/triangle |

#### After: feat/view, 104M (--bench-view)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 104.0M triangles (52.0M vertices, 8 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 7638 |
| BVH build | 5301 |
| First GPU upload | 1283 |
| GPU memory (buffers) | 1535 MB = 14.8 B/triangle |
| Mesh (incl. topology) | 6455 MB = 62 B/triangle |
| BVH | 2043 MB = 19.6 B/triangle |
| Subdivision levels (stack) | 7765 MB = 75 B/triangle |
| Process memory (RSS) | 16656 MB (+16586 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 244k | 321k (59) | 8.15 | 5.73 | 3.85 | 0.01 | 6.60 | **26.45** | 0.00 | 17.3 | 17.0 | 8.6 MB |
| Mask | 237k | 309k (58) | 8.68 | 0.00 | 0.00 | 0.01 | 6.54 | **17.23** | 0.00 | 1.2 | 1.2 | 4.2 MB |
| Paint | 237k | 309k (58) | 8.79 | 0.00 | 0.00 | 0.01 | 12.77 | **23.43** | 0.00 | 2.0 | 1.9 | 8.4 MB |

| Operation | ms |
|---|---:|
| Motion proxy build (first move after an upload) | 408 |
| Orbit: frame time, 4x antialiasing | 19.6 (51 fps) |
| Orbit: frame time, no antialiasing | 16.9 (59 fps) |
| Orbit: triangles drawn | 48.1M (46%) |
| Orbit: frame time, Fast Navigation (no antialiasing, motion proxy) | 9.9 (101 fps) |
| Orbit: triangles drawn (motion proxy) | 20.1M (19%) |
| GPU memory with the motion proxy | 1982 MB = 19.1 B/triangle |

## v0.5

Measurements for the v0.5 performance work ("Performance at 20-100M polygons, multithreaded brushes, fast undo").
Every number here comes from the benchmark built into SculptForge, so anyone can reproduce it:

```
sculptforge --bench [--bench-sizes 4,16,32] [--bench-dabs 40] [--bench-no-project] [--bench-out report.md]
sculptforge-cli --bench ...      # the same without the GPU (CI, servers)
```

The benchmark (core/Benchmark.cpp, app/Bench.cpp) builds a sphere the way a sculptor gets one: a quad cube
projected onto a sphere and divided (Catmull-Clark) to about 4, 16 and 33 million triangles, so the mesh has
subdivision levels and the vertex numbering of a divided mesh. It then runs a stroke with each brush, with X
symmetry on as in the app, a medium Draw Size (radius 0.12 on the unit sphere, about 64 pixels on a 1080p view
framing the model) and dabs 0.15 radius apart, and times per dab: the brush kernel (finding and moving the
vertices), the normal update, the BVH refit, the pick, and with the GPU the upload of what the dab changed
(`glFinish` included). Per stroke it times the undo record, undo and redo; then stepping subdivision levels,
saving and opening the project, and 120 frames of orbiting in a 1600 x 960 window (no vsync).

Machine: Apple M1 Pro (8 performance + 2 efficiency cores, 32 GB unified memory; 30 GB usable under Asahi
Linux), Mesa 26.1 (OpenGL 4.6). Another process kept one core busy during all runs. "Before" is main at
00c4989 with only the benchmark added (the same instrumentation, no other change); "after" is feat/perf.
GPU runs use 10 dabs per stroke (the old upload made 40 too slow), CPU-only runs 40. The 33M GPU runs skip
the project round trip: opening a second copy of a 33M project next to the first ran out of memory on this
machine before the changes.

### Summary

| | 4M triangles | 16M triangles | 33M triangles |
|---|---|---|---|
| Medium brush (Standard), one dab, upload included | 23.6 -> **1.3 ms** | 166 -> **5.9 ms** | 390 -> **10.3 ms** |
| Large brush (radius 0.4: 1.2M vertices per dab at 33M) | 44 -> **13 ms** | 572 -> **43 ms** | 548 -> **93 ms** |
| Vertices uploaded per medium dab (changed: 9k / 37k / 78k) | most of the index range -> **20k** | most of the range -> **60k** | most of the range -> **117k** |
| Orbit, 4x antialiasing | 5.1 -> **5.0 ms** (200 fps) | 12.7 -> **11.8 ms** (85 fps) | 30.4 -> **20.6 ms** (49 fps) |
| Orbit, Fast Navigation (no AA while moving) | **3.1 ms** | **8.0 ms** | **14.0 ms (71 fps)** |
| Undo / redo of a 40-dab medium stroke | 7.7 -> **3.4 ms** | 33 -> **12 ms** | 71 -> **24 ms** |
| Undo of a 40-dab large stroke | 28 -> **11 ms** | 116 -> **38 ms** | 286 -> **77 ms** |
| Lower Res after sculpting (folds the edits into the levels) | 300 -> **175 ms** | 1338 -> **718 ms** | 4245 -> **1539 ms** |
| Higher Res / Lower Res without edits | 2-5 -> **1-5 ms** | 5-20 -> **5-20 ms** | 37-320 -> **10-41 ms** |
| Save project | 768 -> **81 ms** | 3060 -> **321 ms** | 8115 -> **677 ms** (1.9 GB) |
| Open project | 1696 -> **550 ms** | 8130 -> **2278 ms** | 15487 -> **5679 ms** |
| BVH build (open, import, level change without cache) | 693 -> **215 ms** | 2912 -> **829 ms** | 6285 -> **1747 ms** |
| First GPU upload | 98 -> **90 ms** | 514 -> **344 ms** | 907 -> **705 ms** |
| Mesh memory (with topology) | 90 -> **62 bytes per triangle** | | 2990 -> **2062 MB** |

Targets from the brief: a dab under 16 ms at 16M triangles with a medium brush is reached (5.9 ms, and 10.3 ms
at 33M). Orbiting at 60 fps at 32M is reached while the view moves with Fast Navigation (71 fps); with 4x
antialiasing it is 49 fps on this integrated GPU (it was 33).

### What changed

- **Thread pool** (core/ThreadPool.h): a persistent pool (one thread per core, up to 16) with `poolFor`, which
  splits work into fixed blocks of 4096; sums are made per block and added in block order, so results don't
  depend on the thread count and a region of one block is computed exactly like before. Ranges under 4 blocks
  run inline. It runs every brush kernel (all types, recipes, mask, paint weights), normal updates, the BVH
  vertex query (in traversal order, so the vertex list is identical), refits and build, renderer gathers and
  project CRCs. Undo records stay serial, in order. `set_multithreading enabled=false` (or the checkbox) runs
  everything on one thread; tests compare both bit for bit.
- **Partial GPU updates.** The old renderer uploaded one index range from the lowest to the highest changed
  vertex. On a divided mesh, whose vertices are numbered level by level, a dab with symmetry covers most of
  that range: every dab re-sent nearly the whole mesh. Meshes now mark changed vertices in a bitset (plus
  2048-vertex blocks to find them fast), and the renderer stores vertices in first-use order of the triangle list
  (core/UploadOrder.h), which keeps any region of a divided mesh together; a dab uploads a few 512-vertex
  blocks (15-20 `glBufferSubData` calls per array). The same order improves the GPU's vertex cache: orbiting at
  33M went from 30 to 21 ms with the same shaders.
- **Normals** are summed per vertex from its faces (same sums as before) instead of through a per-triangle face
  normal cache, which saves 16 bytes per triangle (cache and stamps); the adjacency list no longer keeps
  spare capacity (12 bytes per triangle); the renderer no longer keeps a CPU copy of the curvature.
- **Undo** already stored only the touched vertices per stroke; it is faster because the normal and BVH updates
  it runs are. The first vertex paint stroke on an unpainted mesh no longer copies the whole mesh for undo (it
  keeps the empty paint stack): at 33M that copy was over 2 GB.
- **Projects**: the ZIP writer streamed the archive into a second in-memory copy and the reader read the file
  through a byte iterator; now entries go straight to the file, are read in place, CRCs use slicing-by-8 in
  parallel, and arrays are copied (not converted byte by byte) on little-endian CPUs. Archives over 4 GB use
  ZIP64 (the old writer refused them); smaller archives are byte-identical to before.
- **Viewport**: the window has no multisampling; the 3D view draws into its own multisampled target and resolves
  it (Renderer::drawView). That allows **Fast Navigation** (scenes of 10M+ triangles drop antialiasing while the
  view moves) and falling back to fewer samples when the target can't be allocated. A mesh whose buffers run the
  GPU out of memory is skipped with a status message rather than drawn from incomplete buffers.

### Memory per triangle (33M benchmark sphere, after)

| | bytes per triangle |
|---|---|
| Mesh: positions, normals, triangles, quad pairs, face groups, adjacency (vertex-vertex and vertex-triangle), stamps, dirty bits | 62 (was 90) |
| BVH | 19.5 |
| Subdivision levels (the level stack keeps every level's data, detail and topology) | 75 |
| GPU: positions, normals, curvature per vertex; indices | about 26 |

A 33M sculpt with levels holds about 5.4 GB (RSS); the 64M benchmark sphere 10 GB, where a medium dab still
takes 14.6 ms (without the upload) and undo of a 40-dab stroke 42 ms (last table).

### Known limits

- The subdivision level stack (75 bytes per triangle) keeps a copy of the current level's positions and
  triangles next to the working mesh; sharing them would save about 18 bytes per triangle but needs changes to
  the level code. Divide stops at 50M triangles (core/Multires.h).
- Normal updates and refits scale about 2-3x on 10 cores: they read vertices scattered over memory (the same
  numbering issue as the uploads), so they wait on memory, not arithmetic.
- Very large brushes on 33M meshes (a tenth of the model per dab) take about 90 ms per dab.
- Opening a project needs about twice the memory of the scene while it loads.

### Before (main at 00c4989 plus the benchmark)

#### CPU only

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 411 |
| BVH build | 693 |
| Mesh (incl. topology) | 358 MB = 90 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 708 MB (+698 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 0.41 | 0.56 | 0.31 | 0.00 | **1.29** | 0.00 | 7.7 | 7.3 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 4.27 | 6.18 | 3.33 | 0.00 | **13.78** | 0.00 | 28.2 | 27.9 | 4.6 MB |
| Clay | 7k | 0.26 | 0.31 | 0.17 | 0.00 | **0.74** | 0.00 | 4.4 | 4.0 | 0.9 MB |
| Clay Block | 8k | 0.34 | 0.37 | 0.21 | 0.00 | **0.92** | 0.00 | 4.0 | 3.9 | 0.8 MB |
| Smooth | 5k | 0.21 | 0.21 | 0.11 | 0.00 | **0.53** | 0.00 | 2.6 | 2.5 | 0.6 MB |
| Inflate | 5k | 0.17 | 0.20 | 0.11 | 0.00 | **0.48** | 0.00 | 2.5 | 2.5 | 0.6 MB |
| Sharp Crease | 4k | 0.17 | 0.19 | 0.11 | 0.00 | **0.47** | 0.00 | 2.4 | 2.3 | 0.5 MB |
| Layer | 5k | 0.25 | 0.20 | 0.11 | 0.00 | **0.56** | 0.00 | 2.6 | 2.5 | 0.6 MB |
| Move | 4k | 0.02 | 0.10 | 0.09 | 0.00 | **0.21** | 0.00 | 0.3 | 0.3 | 0.1 MB |
| Recipe (pattern) | 5k | 0.41 | 0.20 | 0.11 | 0.00 | **0.73** | 0.00 | 2.6 | 2.4 | 0.6 MB |
| Mask | 5k | 0.15 | 0.00 | 0.00 | 0.00 | **0.15** | 0.00 | 0.0 | 0.0 | 0.3 MB |
| Paint | 5k | 0.15 | 0.00 | 0.00 | 0.00 | **0.15** | 0.00 | 0.1 | 0.1 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 300 |
| Higher Res (back up) | 2 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 2 |
| Save project (228 MB) | 768 |
| Open project | 1696 |

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1846 |
| BVH build | 2912 |
| Mesh (incl. topology) | 1433 MB = 90 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2778 MB (+2377 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 1.76 | 2.45 | 1.31 | 0.00 | **5.52** | 0.00 | 33.3 | 32.6 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 17.82 | 27.90 | 16.01 | 0.01 | **61.73** | 0.00 | 116.2 | 114.7 | 18.5 MB |
| Clay | 28k | 0.98 | 1.28 | 0.67 | 0.01 | **2.94** | 0.00 | 20.6 | 20.6 | 3.5 MB |
| Clay Block | 33k | 1.34 | 1.56 | 0.82 | 0.01 | **3.73** | 0.00 | 23.1 | 22.7 | 3.8 MB |
| Smooth | 19k | 0.86 | 0.88 | 0.44 | 0.01 | **2.19** | 0.00 | 15.5 | 15.4 | 2.7 MB |
| Inflate | 18k | 0.66 | 0.86 | 0.46 | 0.01 | **1.98** | 0.00 | 15.4 | 14.9 | 2.6 MB |
| Sharp Crease | 18k | 0.68 | 0.83 | 0.42 | 0.01 | **1.94** | 0.00 | 14.8 | 14.4 | 2.5 MB |
| Layer | 19k | 0.99 | 0.86 | 0.43 | 0.01 | **2.29** | 0.00 | 15.2 | 15.2 | 2.6 MB |
| Move | 16k | 0.07 | 0.41 | 0.36 | 0.01 | **0.87** | 0.00 | 1.1 | 1.1 | 0.3 MB |
| Recipe (pattern) | 19k | 1.73 | 0.89 | 0.43 | 0.01 | **3.07** | 0.00 | 15.2 | 15.2 | 2.6 MB |
| Mask | 19k | 0.57 | 0.00 | 0.00 | 0.01 | **0.58** | 0.00 | 0.3 | 0.2 | 1.3 MB |
| Paint | 19k | 0.58 | 0.00 | 0.00 | 0.01 | **0.59** | 0.00 | 0.5 | 0.3 | 2.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 1338 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 20 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 3060 |
| Open project | 8130 |

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4302 |
| BVH build | 6285 |
| Mesh (incl. topology) | 2990 MB = 90 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5749 MB (+5268 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 3.96 | 5.49 | 2.94 | 0.00 | **12.40** | 0.00 | 70.8 | 71.1 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 39.60 | 60.23 | 35.90 | 0.01 | **135.74** | 0.00 | 286.0 | 283.7 | 38.7 MB |
| Clay | 58k | 2.66 | 3.60 | 1.92 | 0.01 | **8.19** | 0.00 | 51.2 | 50.7 | 7.2 MB |
| Clay Block | 66k | 3.29 | 3.96 | 2.13 | 0.02 | **9.41** | 0.00 | 56.5 | 56.7 | 8.0 MB |
| Smooth | 39k | 2.58 | 2.65 | 1.32 | 0.02 | **6.58** | 0.00 | 55.5 | 40.1 | 5.7 MB |
| Inflate | 38k | 1.79 | 2.24 | 1.17 | 0.02 | **5.23** | 0.00 | 39.8 | 40.7 | 5.6 MB |
| Sharp Crease | 37k | 1.95 | 2.30 | 1.18 | 0.02 | **5.46** | 0.00 | 40.0 | 38.6 | 5.4 MB |
| Layer | 37k | 2.72 | 2.33 | 1.18 | 0.02 | **6.26** | 0.00 | 45.8 | 41.4 | 5.6 MB |
| Move | 33k | 0.23 | 1.27 | 1.19 | 0.02 | **2.78** | 0.00 | 3.1 | 2.9 | 0.5 MB |
| Recipe (pattern) | 38k | 4.32 | 2.29 | 1.11 | 0.02 | **7.74** | 0.00 | 37.2 | 37.8 | 5.4 MB |
| Mask | 39k | 1.23 | 0.00 | 0.00 | 0.02 | **1.25** | 0.00 | 0.7 | 0.6 | 2.7 MB |
| Paint | 39k | 1.30 | 0.00 | 0.00 | 0.02 | **1.32** | 0.00 | 1.3 | 1.1 | 5.5 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 4245 |
| Higher Res (back up) | 37 |
| Lower Res again (no edits) | 320 |
| Higher Res again (no edits) | 64 |
| Save project (1905 MB) | 8115 |
| Open project | 15487 |

#### With the GPU, 4M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 463 |
| BVH build | 767 |
| First GPU upload | 98 |
| Mesh (incl. topology) | 358 MB = 90 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 801 MB (+730 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 9k | 0.42 | 0.52 | 0.25 | 0.00 | 22.36 | **23.57** | 0.00 | 1.9 | 1.8 | 0.3 MB |
| Standard (large, r 0.4) | 147k | 6.78 | 9.16 | 5.13 | 0.00 | 23.38 | **44.48** | 0.00 | 28.4 | 36.0 | 4.2 MB |
| Clay | 8k | 0.75 | 0.57 | 0.27 | 0.01 | 26.43 | **28.05** | 0.00 | 1.5 | 1.5 | 0.3 MB |
| Clay Block | 13k | 0.64 | 0.75 | 0.34 | 0.01 | 23.57 | **25.32** | 0.00 | 2.1 | 2.1 | 0.4 MB |
| Smooth | 7k | 0.49 | 0.42 | 0.32 | 0.01 | 23.69 | **24.94** | 0.00 | 1.4 | 1.3 | 0.2 MB |
| Inflate | 7k | 0.34 | 0.40 | 0.20 | 0.01 | 21.50 | **22.45** | 0.00 | 1.3 | 1.5 | 0.2 MB |
| Sharp Crease | 7k | 0.35 | 0.42 | 0.18 | 0.01 | 21.82 | **22.78** | 0.00 | 1.3 | 1.4 | 0.2 MB |
| Layer | 7k | 0.64 | 0.43 | 0.19 | 0.01 | 22.58 | **23.85** | 0.00 | 1.6 | 1.7 | 0.2 MB |
| Move | 6k | 0.05 | 0.27 | 0.17 | 0.01 | 19.98 | **20.53** | 0.00 | 0.6 | 0.6 | 0.1 MB |
| Recipe (pattern) | 8k | 0.89 | 0.44 | 0.19 | 0.01 | 23.07 | **24.60** | 0.00 | 1.4 | 1.4 | 0.2 MB |
| Mask | 8k | 0.35 | 0.00 | 0.00 | 0.01 | 23.60 | **23.97** | 0.00 | 0.0 | 0.0 | 0.1 MB |
| Paint | 8k | 0.35 | 0.00 | 0.00 | 0.01 | 24.31 | **24.68** | 0.00 | 0.1 | 0.1 | 0.2 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 475 |
| Higher Res (back up) | 2 |
| Lower Res again (no edits) | 7 |
| Higher Res again (no edits) | 2 |
| Save project (228 MB) | 908 |
| Open project | 2026 |
| Orbit: frame time | 5.1 (194 fps) |

#### With the GPU, 16M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2281 |
| BVH build | 3320 |
| First GPU upload | 514 |
| Mesh (incl. topology) | 1433 MB = 90 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2828 MB (+2757 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 37k | 1.62 | 2.14 | 1.09 | 0.01 | 161.12 | **165.99** | 0.00 | 26.0 | 16.9 | 1.3 MB |
| Standard (large, r 0.4) | 589k | 53.96 | 87.81 | 50.14 | 0.01 | 379.68 | **571.63** | 0.00 | 124.1 | 112.5 | 17.0 MB |
| Clay | 32k | 1.45 | 1.77 | 0.86 | 0.01 | 148.08 | **152.18** | 0.00 | 6.2 | 6.1 | 1.1 MB |
| Clay Block | 51k | 2.45 | 2.91 | 1.54 | 0.01 | 150.24 | **157.18** | 0.01 | 8.7 | 9.2 | 1.5 MB |
| Smooth | 30k | 1.94 | 1.82 | 0.99 | 0.01 | 154.28 | **159.06** | 0.01 | 5.4 | 5.3 | 1.0 MB |
| Inflate | 29k | 1.36 | 1.68 | 0.86 | 0.01 | 152.70 | **156.62** | 0.00 | 6.2 | 6.0 | 1.0 MB |
| Sharp Crease | 29k | 1.39 | 1.70 | 0.81 | 0.01 | 163.71 | **167.64** | 0.01 | 6.9 | 11.5 | 1.0 MB |
| Layer | 30k | 2.48 | 1.66 | 0.77 | 0.01 | 149.94 | **154.88** | 0.00 | 5.4 | 5.4 | 1.0 MB |
| Move | 23k | 0.22 | 0.89 | 0.67 | 0.01 | 141.54 | **143.53** | 0.01 | 2.3 | 2.2 | 0.4 MB |
| Recipe (pattern) | 31k | 3.35 | 1.72 | 1.49 | 0.01 | 159.51 | **166.10** | 0.01 | 5.7 | 6.3 | 1.0 MB |
| Mask | 31k | 1.68 | 0.00 | 0.00 | 0.01 | 161.04 | **162.75** | 0.00 | 0.1 | 0.2 | 0.5 MB |
| Paint | 31k | 1.27 | 0.00 | 0.00 | 0.01 | 153.57 | **154.86** | 0.01 | 0.3 | 0.2 | 1.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 1492 |
| Higher Res (back up) | 7 |
| Lower Res again (no edits) | 20 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 3134 |
| Open project | 8582 |
| Orbit: frame time | 12.7 (79 fps) |

#### With the GPU, 33M (no project round trip)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4091 |
| BVH build | 6244 |
| First GPU upload | 907 |
| Mesh (incl. topology) | 2990 MB = 90 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5833 MB (+5763 MB) |

| Brush | verts/dab | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 3.14 | 4.18 | 2.09 | 0.01 | 380.44 | **389.88** | 0.00 | 16.2 | 16.1 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 48.26 | 73.61 | 43.62 | 0.01 | 382.34 | **547.84** | 0.00 | 220.1 | 219.8 | 35.5 MB |
| Clay | 68k | 2.82 | 3.50 | 1.76 | 0.01 | 380.26 | **388.35** | 0.00 | 13.0 | 13.3 | 2.4 MB |
| Clay Block | 107k | 4.81 | 5.55 | 2.92 | 0.01 | 380.61 | **393.90** | 0.00 | 17.8 | 17.8 | 3.1 MB |
| Smooth | 63k | 3.40 | 3.27 | 1.52 | 0.01 | 379.44 | **387.64** | 0.00 | 11.5 | 11.9 | 2.1 MB |
| Inflate | 61k | 2.52 | 3.18 | 1.57 | 0.01 | 380.18 | **387.47** | 0.00 | 12.3 | 11.3 | 2.1 MB |
| Sharp Crease | 61k | 2.63 | 3.19 | 1.52 | 0.01 | 381.24 | **388.60** | 0.00 | 11.3 | 11.1 | 2.0 MB |
| Layer | 63k | 4.98 | 3.28 | 1.49 | 0.01 | 379.55 | **389.33** | 0.00 | 12.3 | 11.6 | 2.1 MB |
| Move | 49k | 0.39 | 1.73 | 1.43 | 0.01 | 341.09 | **345.07** | 0.00 | 4.5 | 4.5 | 0.9 MB |
| Recipe (pattern) | 65k | 6.55 | 3.40 | 1.54 | 0.01 | 380.30 | **391.80** | 0.00 | 11.3 | 11.4 | 2.1 MB |
| Mask | 66k | 2.52 | 0.00 | 0.00 | 0.01 | 382.21 | **384.75** | 0.00 | 0.3 | 0.3 | 1.0 MB |
| Paint | 66k | 2.37 | 0.00 | 0.00 | 0.01 | 389.76 | **392.16** | 0.00 | 0.5 | 0.5 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 2846 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 43 |
| Higher Res again (no edits) | 10 |
| Orbit: frame time | 30.4 (33 fps) |

### After (feat/perf)

#### CPU only

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 277 |
| BVH build | 215 |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 643 MB (+633 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 12k | 24k (15) | 0.49 | 0.48 | 0.40 | 0.00 | **1.41** | 0.00 | 3.4 | 3.5 | 1.4 MB |
| Standard (large, r 0.4) | 118k | 148k (45) | 3.19 | 3.35 | 1.85 | 0.00 | **8.60** | 0.00 | 10.6 | 10.3 | 4.6 MB |
| Clay | 7k | 16k (11) | 0.31 | 0.27 | 0.20 | 0.00 | **0.81** | 0.00 | 2.7 | 2.4 | 0.9 MB |
| Clay Block | 8k | 18k (12) | 0.40 | 0.31 | 0.25 | 0.00 | **0.99** | 0.00 | 2.4 | 2.4 | 0.8 MB |
| Smooth | 5k | 12k (9) | 0.24 | 0.17 | 0.12 | 0.00 | **0.56** | 0.00 | 1.9 | 2.0 | 0.6 MB |
| Inflate | 5k | 12k (9) | 0.19 | 0.17 | 0.12 | 0.00 | **0.51** | 0.00 | 2.0 | 2.3 | 0.6 MB |
| Sharp Crease | 4k | 12k (9) | 0.20 | 0.17 | 0.11 | 0.00 | **0.50** | 0.00 | 2.0 | 1.9 | 0.5 MB |
| Layer | 5k | 12k (9) | 0.29 | 0.18 | 0.12 | 0.00 | **0.61** | 0.00 | 1.8 | 1.7 | 0.6 MB |
| Move | 4k | 10k (8) | 0.02 | 0.10 | 0.09 | 0.00 | **0.24** | 0.00 | 0.3 | 0.2 | 0.1 MB |
| Recipe (pattern) | 5k | 13k (9) | 0.47 | 0.17 | 0.12 | 0.00 | **0.78** | 0.00 | 1.9 | 1.6 | 0.6 MB |
| Mask | 5k | 12k (9) | 0.21 | 0.00 | 0.00 | 0.00 | **0.23** | 0.00 | 0.1 | 0.1 | 0.3 MB |
| Paint | 5k | 12k (9) | 0.17 | 0.00 | 0.00 | 0.00 | **0.19** | 0.00 | 0.1 | 0.1 | 0.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 175 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Save project (228 MB) | 81 |
| Open project | 550 |

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1140 |
| BVH build | 829 |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2589 MB (+2173 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 50k | 76k (15) | 1.99 | 1.95 | 1.15 | 0.00 | **5.25** | 0.00 | 11.6 | 11.9 | 5.4 MB |
| Standard (large, r 0.4) | 472k | 525k (45) | 9.76 | 10.47 | 6.11 | 0.01 | **27.04** | 0.00 | 37.6 | 37.0 | 18.5 MB |
| Clay | 28k | 47k (11) | 1.19 | 1.15 | 0.69 | 0.01 | **3.14** | 0.00 | 7.7 | 7.6 | 3.5 MB |
| Clay Block | 33k | 55k (12) | 1.44 | 1.37 | 0.85 | 0.01 | **3.78** | 0.00 | 8.1 | 12.2 | 3.8 MB |
| Smooth | 19k | 37k (9) | 0.99 | 0.75 | 0.54 | 0.01 | **2.36** | 0.00 | 6.2 | 6.3 | 2.7 MB |
| Inflate | 18k | 36k (9) | 0.76 | 0.75 | 0.55 | 0.01 | **2.14** | 0.00 | 5.9 | 6.0 | 2.6 MB |
| Sharp Crease | 18k | 35k (9) | 0.80 | 0.73 | 0.52 | 0.01 | **2.12** | 0.00 | 5.5 | 5.9 | 2.5 MB |
| Layer | 19k | 36k (9) | 1.06 | 0.77 | 0.51 | 0.01 | **2.42** | 0.00 | 6.0 | 6.0 | 2.6 MB |
| Move | 16k | 29k (8) | 0.14 | 0.48 | 0.55 | 0.01 | **1.26** | 0.00 | 0.8 | 1.3 | 0.3 MB |
| Recipe (pattern) | 19k | 37k (9) | 1.71 | 0.77 | 0.51 | 0.01 | **3.06** | 0.00 | 5.8 | 5.7 | 2.6 MB |
| Mask | 19k | 36k (9) | 0.66 | 0.00 | 0.00 | 0.01 | **0.73** | 0.00 | 0.4 | 0.3 | 1.3 MB |
| Paint | 19k | 36k (9) | 0.65 | 0.00 | 0.00 | 0.01 | **0.72** | 0.00 | 0.5 | 0.4 | 2.6 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 718 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 20 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 321 |
| Open project | 2278 |

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2526 |
| BVH build | 1747 |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5366 MB (+4797 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 103k | 149k (21) | 3.84 | 3.01 | 1.79 | 0.00 | **8.97** | 0.00 | 23.7 | 22.2 | 11.4 MB |
| Standard (large, r 0.4) | 987k | 1.1M (57) | 19.07 | 20.32 | 12.46 | 0.01 | **53.27** | 0.00 | 76.6 | 76.7 | 38.7 MB |
| Clay | 58k | 91k (16) | 2.26 | 2.16 | 1.08 | 0.01 | **5.72** | 0.00 | 15.3 | 15.1 | 7.2 MB |
| Clay Block | 66k | 102k (17) | 2.68 | 2.38 | 1.31 | 0.01 | **6.63** | 0.00 | 16.5 | 16.2 | 8.0 MB |
| Smooth | 39k | 67k (14) | 1.78 | 1.54 | 0.82 | 0.02 | **4.32** | 0.00 | 11.9 | 11.7 | 5.7 MB |
| Inflate | 38k | 66k (14) | 1.65 | 1.69 | 0.90 | 0.02 | **4.41** | 0.00 | 12.0 | 12.8 | 5.6 MB |
| Sharp Crease | 37k | 65k (15) | 1.65 | 1.74 | 0.84 | 0.02 | **4.41** | 0.00 | 11.8 | 11.5 | 5.4 MB |
| Layer | 37k | 65k (14) | 2.12 | 1.74 | 0.88 | 0.02 | **4.92** | 0.00 | 12.8 | 11.8 | 5.6 MB |
| Move | 33k | 59k (10) | 0.27 | 0.77 | 0.77 | 0.02 | **1.99** | 0.00 | 1.8 | 1.8 | 0.5 MB |
| Recipe (pattern) | 38k | 68k (15) | 2.61 | 1.63 | 0.81 | 0.02 | **5.22** | 0.00 | 12.1 | 11.6 | 5.4 MB |
| Mask | 39k | 67k (15) | 1.24 | 0.00 | 0.00 | 0.02 | **1.37** | 0.00 | 1.0 | 0.7 | 2.7 MB |
| Paint | 39k | 67k (15) | 1.25 | 0.00 | 0.00 | 0.02 | **1.38** | 0.00 | 1.3 | 1.1 | 5.5 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1539 |
| Higher Res (back up) | 10 |
| Lower Res again (no edits) | 41 |
| Higher Res again (no edits) | 10 |
| Save project (1905 MB) | 677 |
| Open project | 5679 |

#### With the GPU, 4M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 4.0M triangles (2.0M vertices, 5 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 301 |
| BVH build | 238 |
| First GPU upload | 90 |
| Mesh (incl. topology) | 247 MB = 62 B/triangle |
| BVH | 78 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 297 MB = 74 B/triangle |
| Process memory (RSS) | 720 MB (+650 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 9k | 20k (12) | 0.36 | 0.36 | 0.30 | 0.00 | 0.28 | **1.33** | 0.00 | 1.2 | 1.4 | 0.3 MB |
| Standard (large, r 0.4) | 147k | 183k (53) | 3.77 | 3.99 | 2.15 | 0.00 | 2.99 | **13.17** | 0.00 | 9.3 | 9.4 | 4.2 MB |
| Clay | 8k | 17k (11) | 0.33 | 0.31 | 0.24 | 0.00 | 0.24 | **1.15** | 0.00 | 1.6 | 1.5 | 0.3 MB |
| Clay Block | 13k | 24k (13) | 0.56 | 0.48 | 0.40 | 0.00 | 0.34 | **1.82** | 0.00 | 1.7 | 1.8 | 0.4 MB |
| Smooth | 7k | 16k (10) | 0.35 | 0.28 | 0.19 | 0.00 | 0.23 | **1.06** | 0.00 | 1.0 | 0.9 | 0.2 MB |
| Inflate | 7k | 16k (10) | 0.28 | 0.26 | 0.20 | 0.00 | 0.22 | **0.98** | 0.00 | 0.8 | 1.1 | 0.2 MB |
| Sharp Crease | 7k | 16k (10) | 0.30 | 0.26 | 0.19 | 0.00 | 0.21 | **0.98** | 0.00 | 0.8 | 1.2 | 0.2 MB |
| Layer | 7k | 16k (10) | 0.60 | 0.27 | 0.18 | 0.00 | 0.22 | **1.31** | 0.00 | 0.7 | 0.7 | 0.2 MB |
| Move | 6k | 14k (9) | 0.03 | 0.15 | 0.17 | 0.00 | 0.18 | **0.59** | 0.00 | 0.4 | 0.4 | 0.1 MB |
| Recipe (pattern) | 8k | 17k (11) | 0.83 | 0.28 | 0.20 | 0.00 | 0.23 | **1.57** | 0.00 | 1.5 | 1.0 | 0.2 MB |
| Mask | 8k | 16k (10) | 0.30 | 0.00 | 0.00 | 0.00 | 0.39 | **0.72** | 0.00 | 0.0 | 0.0 | 0.1 MB |
| Paint | 8k | 16k (10) | 0.25 | 0.00 | 0.00 | 0.00 | 0.60 | **0.88** | 0.00 | 0.0 | 0.0 | 0.2 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 995k, after edits) | 170 |
| Higher Res (back up) | 1 |
| Lower Res again (no edits) | 5 |
| Higher Res again (no edits) | 1 |
| Save project (228 MB) | 81 |
| Open project | 572 |
| Orbit: frame time, 4x antialiasing | 5.0 (201 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 3.1 (325 fps) |

#### With the GPU, 16M

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 15.9M triangles (8.0M vertices, 6 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 1206 |
| BVH build | 846 |
| First GPU upload | 344 |
| Mesh (incl. topology) | 988 MB = 62 B/triangle |
| BVH | 311 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 1188 MB = 75 B/triangle |
| Process memory (RSS) | 2670 MB (+2600 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 37k | 60k (12) | 1.75 | 1.89 | 1.10 | 0.00 | 1.04 | **5.93** | 0.00 | 3.2 | 4.2 | 1.3 MB |
| Standard (large, r 0.4) | 589k | 650k (53) | 11.94 | 13.01 | 7.83 | 0.01 | 9.43 | **43.06** | 0.00 | 34.7 | 36.6 | 17.0 MB |
| Clay | 32k | 53k (11) | 1.48 | 1.48 | 0.82 | 0.00 | 0.87 | **4.77** | 0.00 | 2.7 | 3.3 | 1.1 MB |
| Clay Block | 51k | 77k (13) | 2.11 | 1.87 | 1.15 | 0.00 | 1.27 | **6.57** | 0.00 | 4.1 | 4.4 | 1.5 MB |
| Smooth | 30k | 51k (10) | 1.37 | 1.48 | 0.79 | 0.00 | 0.86 | **4.62** | 0.00 | 2.5 | 2.8 | 1.0 MB |
| Inflate | 29k | 50k (10) | 1.21 | 1.36 | 0.70 | 0.00 | 0.84 | **4.24** | 0.00 | 2.3 | 2.4 | 1.0 MB |
| Sharp Crease | 29k | 49k (10) | 1.18 | 1.39 | 0.76 | 0.00 | 0.81 | **4.29** | 0.00 | 2.4 | 2.6 | 1.0 MB |
| Layer | 30k | 51k (10) | 2.25 | 1.54 | 0.64 | 0.00 | 0.83 | **5.37** | 0.00 | 2.5 | 2.3 | 1.0 MB |
| Move | 23k | 42k (9) | 0.20 | 0.53 | 0.65 | 0.00 | 0.90 | **2.55** | 0.00 | 2.0 | 1.1 | 0.4 MB |
| Recipe (pattern) | 31k | 52k (11) | 2.03 | 1.41 | 0.76 | 0.00 | 0.87 | **5.20** | 0.00 | 2.4 | 2.7 | 1.0 MB |
| Mask | 31k | 52k (11) | 1.41 | 0.00 | 0.00 | 0.01 | 1.83 | **3.37** | 0.00 | 0.1 | 0.1 | 0.5 MB |
| Paint | 31k | 52k (11) | 1.01 | 0.00 | 0.00 | 0.00 | 2.25 | **3.36** | 0.00 | 0.2 | 0.1 | 1.0 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 4.0M, after edits) | 717 |
| Higher Res (back up) | 5 |
| Lower Res again (no edits) | 20 |
| Higher Res again (no edits) | 5 |
| Save project (913 MB) | 357 |
| Open project | 2279 |
| Orbit: frame time, 4x antialiasing | 11.8 (85 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 8.0 (125 fps) |

#### With the GPU, 33M (no project round trip)

Threads: 10, GPU: Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 10 dabs per stroke. Times in milliseconds.

##### 33.2M triangles (16.6M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 2570 |
| BVH build | 1748 |
| First GPU upload | 705 |
| Mesh (incl. topology) | 2062 MB = 62 B/triangle |
| BVH | 648 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 2481 MB = 75 B/triangle |
| Process memory (RSS) | 5486 MB (+5415 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | GPU upload | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 78k | 117k (18) | 3.10 | 2.88 | 1.58 | 0.00 | 2.49 | **10.32** | 0.00 | 5.5 | 5.9 | 2.8 MB |
| Standard (large, r 0.4) | 1.2M | 1.3M (67) | 26.54 | 27.08 | 17.16 | 0.02 | 20.26 | **92.91** | 0.00 | 69.1 | 68.2 | 35.5 MB |
| Clay | 68k | 104k (17) | 2.77 | 2.45 | 1.56 | 0.00 | 1.69 | **8.71** | 0.00 | 5.7 | 5.3 | 2.4 MB |
| Clay Block | 107k | 151k (21) | 3.98 | 3.02 | 1.62 | 0.01 | 2.51 | **11.46** | 0.00 | 6.5 | 6.6 | 3.1 MB |
| Smooth | 63k | 98k (17) | 2.93 | 2.28 | 1.18 | 0.01 | 1.77 | **8.39** | 0.00 | 4.5 | 5.6 | 2.1 MB |
| Inflate | 61k | 96k (16) | 2.50 | 2.17 | 1.19 | 0.01 | 1.87 | **7.97** | 0.00 | 4.8 | 4.8 | 2.1 MB |
| Sharp Crease | 61k | 96k (16) | 2.62 | 2.24 | 1.28 | 0.01 | 1.59 | **8.01** | 0.00 | 4.6 | 4.6 | 2.0 MB |
| Layer | 63k | 99k (17) | 4.87 | 2.34 | 1.26 | 0.01 | 1.81 | **10.57** | 0.00 | 4.7 | 4.9 | 2.1 MB |
| Move | 49k | 79k (14) | 0.42 | 0.91 | 1.05 | 0.01 | 1.65 | **4.61** | 0.00 | 2.2 | 1.9 | 0.9 MB |
| Recipe (pattern) | 65k | 102k (18) | 4.29 | 2.39 | 1.18 | 0.01 | 2.08 | **10.19** | 0.00 | 4.9 | 5.1 | 2.1 MB |
| Mask | 66k | 101k (18) | 2.52 | 0.00 | 0.00 | 0.01 | 3.03 | **5.76** | 0.01 | 0.3 | 0.2 | 1.0 MB |
| Paint | 66k | 101k (18) | 2.47 | 0.00 | 0.00 | 0.05 | 5.08 | **7.82** | 0.01 | 0.4 | 0.4 | 2.1 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 8.3M, after edits) | 1529 |
| Higher Res (back up) | 11 |
| Lower Res again (no edits) | 55 |
| Higher Res again (no edits) | 14 |
| Orbit: frame time, 4x antialiasing | 20.6 (49 fps) |
| Orbit: frame time, Fast Navigation (no antialiasing) | 14.0 (71 fps) |

#### CPU only, 64M (no project round trip)

Threads: 10. Brushes use X symmetry (two dabs per step), radius 0.12 on a unit sphere unless noted, 40 dabs per stroke. Times in milliseconds.

##### 63.7M triangles (31.9M vertices, 7 subdivision levels)

| Setup | |
|---|---|
| Divide up to this size | 4788 |
| BVH build | 3266 |
| Mesh (incl. topology) | 3953 MB = 62 B/triangle |
| BVH | 1244 MB = 19.5 B/triangle |
| Subdivision levels (stack) | 4756 MB = 75 B/triangle |
| Process memory (RSS) | 10081 MB (+10070 MB) |

| Brush | verts/dab | uploaded verts (ranges) | kernel | normals | BVH refit | pick | **dab total** | stroke end (undo record) | undo | redo | undo step |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Standard | 198k | 259k (28) | 6.61 | 4.61 | 2.82 | 0.00 | **14.58** | 0.00 | 42.3 | 42.2 | 21.8 MB |
| Standard (large, r 0.4) | 1.9M | 2.0M (79) | 33.95 | 38.18 | 23.82 | 0.01 | **98.42** | 0.00 | 148.1 | 147.0 | 74.0 MB |
| Clay | 110k | 155k (21) | 4.07 | 3.18 | 1.76 | 0.02 | **9.41** | 0.00 | 27.8 | 27.2 | 13.8 MB |
| Clay Block | 120k | 166k (23) | 4.52 | 3.34 | 1.79 | 0.03 | **10.06** | 0.00 | 30.7 | 30.3 | 15.1 MB |
| Smooth | 71k | 108k (19) | 3.22 | 2.54 | 1.28 | 0.03 | **7.35** | 0.00 | 22.7 | 22.6 | 10.8 MB |
| Inflate | 69k | 106k (19) | 2.73 | 2.62 | 1.37 | 0.03 | **7.02** | 0.00 | 22.5 | 22.5 | 10.7 MB |
| Sharp Crease | 68k | 104k (18) | 2.76 | 2.60 | 1.27 | 0.03 | **6.94** | 0.00 | 22.5 | 22.5 | 10.4 MB |
| Layer | 70k | 107k (19) | 3.75 | 2.64 | 1.32 | 0.03 | **8.00** | 0.00 | 21.5 | 22.9 | 10.8 MB |
| Move | 64k | 98k (13) | 0.50 | 1.06 | 1.12 | 0.03 | **3.04** | 0.00 | 2.5 | 2.5 | 1.0 MB |
| Recipe (pattern) | 72k | 109k (19) | 4.24 | 2.53 | 1.24 | 0.03 | **8.32** | 0.00 | 22.0 | 22.7 | 10.6 MB |
| Mask | 72k | 108k (19) | 2.35 | 0.00 | 0.00 | 0.03 | **2.59** | 0.00 | 1.7 | 1.3 | 5.3 MB |
| Paint | 72k | 108k (19) | 2.41 | 0.00 | 0.00 | 0.03 | **2.65** | 0.00 | 2.6 | 2.4 | 10.7 MB |

| Operation | ms |
|---|---:|
| Lower Res (to 15.9M, after edits) | 2793 |
| Higher Res (back up) | 20 |
| Lower Res again (no edits) | 79 |
| Higher Res again (no edits) | 20 |
