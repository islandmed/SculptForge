# Performance moonshots: sculpting that costs pixels, not polygons

Research branch `research/perf-moonshots` (2026-11-01). Goal: the cost of a stroke grows with the pixels on screen
and the brush footprint, never with the total polygon count. This note surveys recent work, rates eight ideas,
reports three prototypes measured with `--benchmark standard-33m`, and recommends what to ship.

Starting point (Tony's clean 33M run, M1 Pro, Asahi, Mesa GL 4.6, unified memory): orbit 89 fps; Clay small p99
38 ms; Clay large p99 119 ms; 240 px brush p99 338 ms; Move 18 fps. The branch is built on `perf/bigbrush-p99`
(dabs of meshes from 2M vertices go to the solver thread without the preview), which already takes the stroke
p99s to about 30 ms; what it leaves is the surface trailing the pen (each 240 px dab at 33M still costs
100-200 ms of CPU) and Move, which never used the solver path.

## 0. Hard requirement: the floor tier (Tony, 11-03)

Weak GPUs must get great performance too, not only big cards. Every idea, prototype and default below is held to
this; an experiment that misses it stays off for that tier or doesn't ship.

1. **Floor hardware.** Integrated GPUs: Intel UHD 620 / Iris Xe, AMD Vega / 680M iGPU, Apple M1 base. OpenGL 3.3,
   8 GB RAM, about 1-2 GB of shared video memory, 4 cores. Windows first (the biggest install base; real-hardware
   checks by testers).
2. **Costs bounded by screen pixels and brush footprint, never total polygon count.** The cluster-LOD pixel budget
   and the draw budget come from the hardware profile (calibration: `renderTriangleBudget()`,
   `motionProxyThreshold()`, `TierConfig::uploadBudgetMBPerFrame`). Under load they degrade gracefully: fewer drawn
   triangles, a lower view resolution while the view moves, full quality on release. They never stutter: a budget
   is cut before a frame is late, not after.
3. **GPU work is optional.** Sculpting, normals, picking and undo stay on the CPU, multithreaded; the GPU only draws
   a budgeted set. No feature may require compute shaders, SSBOs, mesh shaders or big VRAM; anything newer than
   GL 3.3 is an optional fast path with a GL 3.3 fallback that produces the same picture. A CPU raster path for
   sub-pixel clusters (and for the canvas, §7 E) is part of the plan.
4. **Memory.** Compact per-vertex storage so 20M+ triangles fit in 8 GB with headroom. Today the scene is about
   90 bytes per triangle (docs/performance.md): 20M = 1.8 GB plus undo, which fits; 50M = 4.5 GB, which does not
   leave headroom on 8 GB. Target: 60 bytes per triangle at the top level (quantized positions in the cluster grid,
   octahedral normals, implicit adjacency), and micro levels (1.25 bytes per triangle) above ~8M.
5. **Benchmarks.** Every number is reported twice: this laptop and the **iGPU-simulated floor profile** (below),
   with p99 frame time and stroke latency at 5, 20 and 50M triangles. A beta must not regress the floor tier.

**The iGPU-simulated profile** (`SF_HW_SIMULATE=floor`, core/HardwareProfile.h; applied where the hardware
profile is applied, so it is the same code a real floor machine runs): Laptop tier; 4 worker threads;
render triangle budget 2M (1M while the view moves); motion proxy from 3M visible triangles; upload budget
16 MB a frame; no GL 4.4 buffer storage (Fast Upload falls back); resident caches capped at 1 GB; the governor
may lower the budget, never raise it. View resolution 75% while the view moves, full
when it stops (`SF_MOTION_RESOLUTION=25..100` sets it on any profile).

**Frame split.** `--benchmark` now writes `frame_split_ms` per phase (p50 / p95 / p99 of the main thread's
events, sculpt, UI, upload = ghost + scene sync, draw = GL calls, swap_wait = UI draw + swap, where the frame
waits for the GPU). It decides whether a native backend (D3D11/12, Metal) behind a thin render interface should
move earlier: only if draw + swap_wait dominate on the floor profile at 20M while sculpt and upload are within
budget. Run: `--benchmark --benchmark-sizes 5,20,50`, with and without `SF_HW_SIMULATE=floor`.

**First measurement** (11-03, M1 Pro, one run each at load < 2 at start; p99 frame / stroke latency p50):

| 20M | laptop | floor (simulated) |
|---|---|---|
| 60 px stroke | 30.0 ms / 89 ms | 36.2 ms / 58 ms |
| 240 px stroke | 30.2 ms / 414 ms | 30.6 ms / 311 ms |
| orbit | 25.4 ms | 21.8 ms |
| GL draw p99 (any phase) | <= 4.7 ms | <= 6.1 ms |

The floor misses its stroke targets by far (latency 58-311 ms against < 16), while orbit at 20M meets < 33 ms. The
GL calls are a few ms, so the time goes to CPU stroke throughput and scene-sync spikes (the motion proxy's build at
the start of an orbit), not the driver: a native backend would not fix these. At 50M on the floor, orbit p99 is
44 ms with draw at 32 ms p99, the one place the draw side shows. It simulates the budgets, not the GPU's fill
rate, so real iGPU runs by testers (Windows UHD 620 / Iris Xe first) confirm it.

**Targets, floor profile** (laptop targets in brackets):

| | 5M | 20M | 50M |
|---|---|---|---|
| p99 frame, orbit | < 16 ms [< 8] | < 33 ms [< 11] | < 33 ms, proxy [< 16] |
| p99 frame, 240 px stroke | < 33 ms [< 16] | < 33 ms [< 33] | < 50 ms [< 33] |
| stroke latency (pen to visible surface), p99 | < 16 ms | < 16 ms (canvas track, §7) | < 33 ms |
| RSS after load + 20 strokes | < 1 GB | < 2.5 GB | < 5 GB (needs item 4) |

**The three prototypes against the floor:**

| Prototype | GL needed | Floor tier |
|---|---|---|
| Live Move | GL 3.3 (vertex shader, uniform arrays) | works; takes per-frame grab and upload work off the CPU, so it helps most here. The commit at pen lift is one grab on the CPU (cost is the brush footprint) |
| Background Preview | GL 3.3 | works; draws only the clusters the dabs reach |
| Fast Upload | GL 4.4 buffer storage (optional) | an optional fast path: without the extension (GL 3.3 contexts, macOS GL 4.1) it is off and uploads go the old way, same picture; on Windows iGPUs (GL 4.6 drivers) it needs the floor-profile numbers before it may default on |

## 1. What SculptForge already does (so the ideas below are measured against the right thing)

| Piece | Where | State |
|---|---|---|
| Solver thread for dabs, two-part upload (pack on the thread, `glBufferSubData` on the GL thread) | `app/AsyncStroke.cpp`, `Renderer::prepareStrokeUpload` | on (bigbrush: from 2M vertices) |
| GPU preview of the dabs not solved yet ("ghost"), analytic per brush kernel in the vertex shader, normals by forward differentiation | `render/Ghost.h`, `kMeshVS` GHOST | only with Predictive Strokes, which is **off** by default |
| Pen prediction: alpha-beta-gamma filter, 25 ms ahead | `core/PenPredictor.h` | with the ghost |
| Draft strokes: positions only, normals at pen lift | `Sculptor::draftStrokes` | off; measured no gain on M1 |
| Cluster culling + motion proxy | `render/Clusters.h` | on |
| Micro levels: 16-bit scalar height per point above the top level, evaluated in the vertex shader; stride-sampled brushes | `core/Micro.h`, `render/MicroRenderer.h` | on; 1.02B triangles in 1.7 GB RSS |
| Kelvinlet brushes (grab, twist, scale, snake) | `core/Kelvinlet.h` | on |

So "GPU preview from a dab list" (idea 1) and "quantized scalar displacement" (idea 6) exist already; the
question for them is where they are not used yet.

## 2. Research (2022-2026, plus the classics they build on)

**Micro-meshes and displacement micro-maps.** NVIDIA's micro-mesh primitive is a base mesh plus a compressed,
UV-free displacement per base face (11-bit scalars, four watertight levels), with hardware support on recent GPUs
for ray tracing [1]. Maggiordomo, Moreton and Tarini, *Micro-Mesh Construction* (SIGGRAPH 2023 / TOG 42(4)) build
them automatically from dense meshes with a base simplification tuned for tessellation and displacement sampling,
with an open reference implementation [2]. Our micro levels are the same idea (16-bit heights, barycentric grid
per base triangle) for editing rather than for ray tracing; [2]'s base-mesh simplification is relevant to
"Divide to micro" on scanned meshes.

**Cluster LOD / virtual geometry.** Nanite (Karis, Stubbe, Wihlidal, SIGGRAPH 2021 Advances [3]): ~128-triangle
clusters, a DAG of cluster groups simplified with locked group borders, LOD chosen per group by projected error,
visibility buffer and a software rasterizer for small triangles, streaming of pages. The 2021 design assumed rigid
meshes; UE 5.4 added tessellation with programmable displacement and UE 5.5 skinned Nanite meshes (deformed on
the GPU before culling) [4] (secondary sources; Epic's own notes should be checked before quoting).
NVIDIA's RTX Mega Geometry (2025) builds cluster acceleration structures, *cluster templates* (topology reused,
only vertices new), per-frame cluster tessellation of subdivision surfaces at about one triangle per pixel, and a
continuous cluster-LOD library [5]. The common lesson for an editor: keep topology-derived data (clusters, LOD
DAG, templates) fixed while vertices change, and rebuild only the cluster groups an edit touched.

**Sculpting engines.** Blender 4.3's sculpt refactor (2024) reports brushes about 8x faster and 30% less memory
on a 16M-face mesh, from data-oriented attribute arrays, a face (not triangle) BVH, removing per-element call
layers and writing deformation directly per symmetry pass [6]. That is the "make the CPU kernel lean" path; we
are already there in structure (flat arrays, parallel kernels); our remaining dab cost is proportional to the
vertices under the brush, which is the term this note attacks.

**Analytic deformers.** Regularized Kelvinlets (de Goes and James, SIGGRAPH 2017) give closed-form, volume-aware
grab/twist/scale/pinch brushes from regularized fundamental solutions of linear elasticity [7]; Dynamic
Kelvinlets (2018) and Sharp Kelvinlets (DigiPro 2019, multi-scale cusps) extend them [7]. Blender's Elastic Deform
and Unreal's modeling tools ship them. Key property for us: a closed-form displacement of the *start* position
is exactly what a vertex shader can evaluate; so are our Move and Standard kernels.

**Progressive refinement.** Hoppe's progressive meshes (SIGGRAPH 1996) and their GPU successors (cluster DAGs
above) refine coarse-to-fine by screen error. For editing, the exact final geometry is the constraint: coarse
first is useful as a *display* order, not as a different result.

**Zero-copy buffers.** `GL_ARB_buffer_storage` (core in 4.4) allows immutable storage that stays mapped while the
GPU uses it (`MAP_PERSISTENT`), with writes visible without flushes (`MAP_COHERENT`); it is the core of the
"Approaching Zero Driver Overhead" techniques (Everitt, McDonald et al., GDC 2014 [8]). It does not remove
synchronisation: the application must fence what the GPU may still read [8].

**Pen-latency prediction.** Henze et al. (MobileHCI 2016/2017) extrapolate touch with small neural networks;
predicting 33 ms ahead improved task performance, beyond that it did not [9]. A GRU-CNN stylus predictor
(Samsung Research, HCII 2020) reports 0.07/0.24/0.47 mm error at ~16/33/50 ms, several times below LSTM
baselines [10]. TurboTouch (UIST 2018) uses finite-time derivative estimation and predicts 2-3x further with few
side effects [11]. Our alpha-beta-gamma filter is the classical baseline these improve on.

**What the Asahi driver offers (checked on this machine).** Mesa 26.1.8 reports OpenGL 4.6 core with
`GL_ARB_buffer_storage`, `GL_ARB_compute_shader`, `GL_ARB_shader_storage_buffer_object`,
`GL_ARB_multi_draw_indirect`, `GL_ARB_indirect_parameters` and `GL_ARB_shader_draw_parameters` (glxinfo). The
Asahi drivers are conformant for OpenGL 4.6, OpenGL ES 3.2, OpenCL 3.0 and Vulkan 1.4 (Honeykrisp, in Mesa since
24.3); the hardware has no geometry or tessellation shaders, which the driver emulates with compute [12]
(so our texture-painting geometry shader is a compute pass on this GPU). No source says Honeykrisp exposes
`VK_EXT_mesh_shader`, and `vulkaninfo` is not installed here; treat mesh shaders as unavailable. GPU-driven
culling therefore means compute + multi-draw-indirect, which the driver has.

## 3. How the established desktop sculpting app does it (publicly documented approaches only)

From its vendor's public reference manual only (no binaries inspected):
(a) **2.5D canvas**: the document is "a 2.5D canvas", not a 3D scene; each canvas pixel stores depth,
orientation and material [13]. The heritage is a CPU renderer whose cost follows pixels.
(b) **Subdivision levels**: the classic multires stack (ours: subdivision levels).
(c) **Local high-resolution levels**: extra levels above the top subdivision level are sculpted only in a circular
or square region around the cursor [14]; only that
region is at the extra resolution while the rest stays at the regular top level.
(d) **Per-mesh caps**: a memory preference limits polygons per mesh (in millions), with a separate
cap for the local high-resolution preview [15]; dense work is split into many objects, hidden ones costing little.

What it implies: their interactivity comes from bounding the *active* point count (cap per object, a local HD
region) on a CPU renderer. Our equivalents must bound the same thing with our own design: micro levels (bounded
bytes per triangle), a focus region (idea 8) and GPU drawing whose cost follows the screen.

## 4. Ideas, rated

Gain is for the 33M standard scene unless said; effort in agent-days; fit = how well it suits our engine.

| # | Idea | Expected gain | Risk | Effort | Fit |
|---|---|---|---|---|---|
| 1 | GPU stroke preview from the dab list, async commit | surface under the pen within a few px at any brush size (measured below); Move frame cost independent of the region | low: preview only, the solver's mesh is unchanged | 1-2 (exists; extend) | **high** |
| 2 | Coarse-first progressive dab application | none on the final mesh (same work); as a display order (visible clusters packed first) maybe 30-50% sooner visible surface on big symmetric brushes | medium: order changes must stay bit-identical | 2-3 | low once (1) is on: the preview already shows the result |
| 3 | Strokes as data (analytic fields, replay, re-tune, cheap undo) | undo of a 240 px stroke at 33M: tens of MB of positions -> ~10 KB of dabs; re-tune strength after the fact | high: Clay, Smooth, Flatten read the surface, so replay is sequential and as costly as the stroke; determinism across versions | 10+ | medium (Sculpt Timeline is the natural home) |
| 4 | Zero-copy persistent mapped buffers on unified memory | the GL thread's commit (2-10 ms per batch) -> a GPU copy command; measured below | low-medium: fences; not on macOS GL 4.1 (fallback kept) | 1 (prototyped) | medium |
| 5 | Cluster LOD with a per-cluster pixel budget (Nanite-style DAG on an editable mesh) | orbit cost bounded by screen (as micro levels already are: 3-4M triangles drawn at 299M, 8-9 ms); 33M orbit 89 -> ~110+ fps | high: cracks at group borders, rebuild the edited groups after each stroke | 10-15 | medium (compute + MDI available, no mesh shaders) |
| 6 | Quantized scalar displacement + GPU normals | memory 90 -> 1.25 B per triangle above the top level (micro levels, measured in docs/performance.md) | low (exists) | 2-3 to make it automatic ("Divide" past ~8M goes to a micro level) | **high** |
| 7 | Learned pen prediction | at most 1-3 px of the 1-4 px preview lag left; literature: no gain beyond 33 ms ahead [9] | medium: training data, per-device tuning | 3-5 | low |
| 8 | **Focus Detail** (below) | billion-triangle assets in a few GB of RAM and ~0.3-0.5 GB of GPU, editable locally at the speed of a 30-60M mesh | medium-high: streaming latency, seams between levels | 8-12 (data side started on `research/focus-detail`) | **high** |

### Idea 8: Focus Detail (our design)

The region under and near the brush is promoted to full micro resolution; everything else stays at a coarse
micro level (or compressed on disk), and the GPU draws each tile at its own level.

- **Data** (`core/FocusDetail`, branch `research/focus-detail`, in progress): micro tiles (one per base
  triangle) stored zstd-compressed in a tile file; a focus region (brush footprint plus a margin, plus what the
  view needs at one micro triangle per pixel) is decompressed to full level; tiles outside hold an upsampled coarse
  grid (level 3) so the MicroRenderer works unchanged today. Promotion and demotion `markDirty(tri)`; budget the
  promoted tiles by bytes (e.g. 512 MB) and demote least recently used.
- **GPU** (follow-up here): per-tile resident level. Coarse tiles keep 45 points (level 3) instead of 8385
  (level 7), drawn with the same vertex-shader evaluation at that level; the data side exposes
  `FocusDetail::tileLevel(tri)`. Edge stitching between a level-3 and a level-7 tile: snap the fine tile's edge
  points to the coarse edge (as micro levels already stitch shared edges).
- **What it enables, estimated** for 1.0B triangles (micro level 7 on 61K base triangles; 8385 points a tile):
  all tiles at full level are 511M points: 1.0 GB of heights on the CPU, ~3 GB on the GPU (2 B height + 4 B
  shading per point). With Focus Detail: coarse everywhere 2.7M points (16 MB GPU); a focus of ~2% of the tiles
  (a 240 px brush plus margin on a full figure) 1.2K tiles x 8385 points = 10M points (60 MB GPU, 20 MB CPU);
  compressed tile file perhaps 0.4-0.6 GB on disk (16-bit heights of sculpted detail compress 2-3x; to be
  measured). Dab cost depends on the points under the brush at full level, the same as on a 30-60M mesh today
  (docs/performance.md: Standard 64 px on 1.02B Micro 5 is 39 ms a step, 300 px 283 ms); drawing stays bounded
  by the screen. **A billion-triangle asset, editable locally at interactive rates on a 16 GB laptop**, is the
  claim to test; nothing here is measured yet, so no public claim until it is.
- **Measured on the data side** (`research/focus-detail` @ 6184559, docs/design/focus-detail.md §5.1; 1B
  triangles, shared machine, load ~4): promoting one tile 0.041 ms p50 / 0.051 ms p99, 65.5K tiles/s in parallel;
  RSS 1147 MB with the focus on against 1526 MB with every tile at full level; GPU memory (estimated) 3130 MB with
  today's renderer against 597 MB with per-tile resident levels (`FocusDetail::tileLevel(tri)` is in). So
  promotion is far below a frame (a few hundred tiles a frame while orbiting is ~10 ms on one thread, ~5 ms spread
  over the pool); the GPU side (per-tile levels in the MicroRenderer) is what turns the estimate into the 5x saving.
- **Difference from (c)**: the focus follows the brush automatically (no mode to enter), the coarse remainder is
  the same surface at a lower micro level (not a separate HD layer), and GPU memory follows focus + screen.

## 5. Prototypes (feature flags, off by default)

All three are Experimental toggles in Preferences > Performance (Live Move, Background Preview, Fast Upload), off
by default, with environment overrides (`=0` / `=1`) for benchmarks; they cost nothing when off. None changes what a stroke leaves on the mesh
except Live Move's commit (one grab with the summed move instead of one per frame; tested equal to 1e-6).

1. **Background preview** (`SF_BG_GHOST=1`, idea 1): the ghost (already written for predictive strokes) also
   draws the dabs of bigbrush's background path, and the frame no longer waits 12.5 ms for a batch. The solver's
   dabs are unchanged (same dabs, same order), so the final mesh is identical by construction.
2. **Live Move** (`SF_LIVE_MOVE=1`, ideas 1 and 3): a Move drag is not applied frame by frame. The GPU draws it
   from the grab's start positions (`GhostKind::Move`: x0 + d w(|x0 - c|), normals by the rank-one inverse
   transpose), per symmetry mirror, and one `Sculptor::grab` with the whole move is applied when the stroke ends.
   Falls back to the old path with masks, hidden faces, sculpt layers, micro levels, connected-only or view
   falloff. Test `live_move_matches_the_grab`: the preview matches the grab to 1e-5, one grab matches the
   per-frame grabs to 1e-6 (X symmetry, focal shift).
3. **Persistent upload** (Fast Upload, `SF_PERSISTENT_UPLOAD=1`, idea 4): two persistently mapped, coherent staging buffers
   (48 MB each, `glBufferStorage`); the solver thread packs straight into one, the GL thread issues
   `glCopyBufferSubData` for each range and a fence; a buffer is reused only after its fence. Batches larger than
   4M vertices pack into the old vector as before.

### Measurements

Correctness (all in ctest): `live_move_matches_the_grab` (preview vs grab 1e-5, one grab vs per-frame grabs
1e-6); `ui_live_move` (the same drag with Live Move on and off moves the mesh by the same sum and maximum to 0.1%;
mid-drag the mesh is untouched and the GPU draws the move: screenshot `live_move_dragging` matches the committed
`live_move`; undo/redo); `ui_stroke_background_preview` and `ui_stroke_persistent_upload` (`SF_MEASURE_VERIFY=1`:
the background path's mesh is bit-identical to the plain path's with the preview and the persistent upload on).

Speed: **not measured yet.** The before/after runs need the shared machine at load < 2 (`--benchmark standard`,
33M scene, toggles off vs each on vs all on, interleaved), and the load stayed between 7 and 20 through every
session of this work. Expected from the design, to be checked by those runs: Live Move's frame cost no longer
depends on the region moved (today Move at 33M is 18 fps); Background Preview takes the 12.5 ms batch wait out of
the frame; Fast Upload moves the GL thread's 2-10 ms commit copy to a GPU copy. No public numbers until measured.

## 6. Recommendation

- **Ship now, Experimental, off by default (0.11.0-beta.1):** Live Move, Background Preview, Fast Upload. They are
  tested equal to the plain path, so the beta risk is in drawing (driver differences), which is what the
  #testers checklist asks about. Promote one to on-by-default only with the 33M numbers above and clean tester
  reports on Windows, Linux and macOS.
- **Build next:** Focus Detail's GPU side (per-tile resident levels in the MicroRenderer, idea 8): the data side's
  measurements (promotion 0.05 ms a tile, GPU 3130 -> 597 MB at 1B) make it the biggest lever toward
  billion-triangle assets. Then idea 6 made automatic (Divide past ~8M goes to a micro level).
- **Later or not at all:** cluster LOD (5) only if orbit at 33M+ stays below target after Focus Detail; strokes as
  data (3) belongs with the Sculpt Timeline; coarse-first (2) and learned prediction (7) don't pay once the
  preview is on.

## 7. Canvas track (the cached screen; Tony, 11-03)

Generic techniques, designed here from first principles (clean room: no decompiling, no copied code or docs). Goal:
a dab costs about the brush's pixels, whatever the mesh size, so the floor tier sculpts 20M like a small mesh.

- **A. Cached screen canvas.** Keep a G-buffer between frames: depth, normal (octahedral, 2x16 bit), material and
  colour per pixel. When nothing moves, nothing is redrawn (the canvas is composited as one quad). While sculpting,
  only the clusters overlapping the brush's dirty screen rectangle (the dab's screen bound, grown by the move) are
  re-rasterized into the canvas, scissored to that rectangle, and only that rectangle is re-shaded. The rectangle
  comes from the dab list we already have (the solver's dabs and the ghost's bounds); the clusters come from the
  existing cluster bounds. Cost per dab: brush pixels plus the clusters under them. Correctness: the depth test
  inside the scissor against the cached depth outside it; clusters straddling the edge are drawn whole and clipped.
- **B. Deferred Lit Sphere shading.** Shade the canvas per pixel from its cached normals with the Lit Sphere lookup
  (one texture fetch, no lights, no shadows) in the interactive path; the full material (PBR, cavity, shadows) is
  applied when the view is still. Re-shading a rectangle is then a texture pass, not a geometry pass.
- **C. Motion.** While orbiting or zooming: draw a lower level or budget (the motion proxy, micro levels' coarse
  level), or reproject the cached canvas for the first frames of a move; full detail returns on release. Input is
  never stalled: the frame takes what the budget allows, never waits for geometry.
- **D. Compact data, levels as offsets.** Multires levels stored as offsets from the level below (the micro levels'
  16-bit heights already are), so a large brush works at a coarse level and the detail rides along; with §0 item 4
  this is how 50M fits the floor.
- **E. CPU rasterizer for the canvas.** A tiled, multithreaded CPU rasterizer (binning by 64x64 tiles, SIMD edge
  functions, one thread per tile) that writes the same G-buffer, so a GL 3.3 iGPU only composites one textured
  quad. Also the path for sub-pixel clusters (§0 item 3). Compare CPU vs GPU fill per machine on the floor profile;
  calibration measures both and the hardware profile picks one (`TierConfig`, a new `canvasRaster` field).

**Targets (floor profile, 20M):** stroke latency < 16 ms, p99 frame < 33 ms; idle frames ~0 ms of GPU (composite
only). Measured as in §0 (5/20/50M, laptop and floor).

**Order of experiments**, each behind a flag, off by default, with numbers before it ships: (1) A + B on the GPU
(the canvas, dirty rectangles, Lit Sphere re-shade); (2) C reprojection of the canvas; (3) E the CPU rasterizer
writing the same canvas, and the per-machine pick; (4) D with Focus Detail's per-tile levels. Risks: the canvas
must match a full redraw (a screenshot test compares them pixel for pixel at rest); transparency, X-Ray and
wireframe overlays draw on top of the canvas, not into it.

## References

1. NVIDIA Research, *Micro-Mesh Construction* project page and micro-mesh format: https://research.nvidia.com/publication/2023-08_micro-mesh-construction
2. A. Maggiordomo, H. Moreton, M. Tarini, *Micro-Mesh Construction*, ACM TOG 42(4), SIGGRAPH 2023: https://micromesh.di.unimi.it/
3. B. Karis, R. Stubbe, G. Wihlidal, *Nanite: A Deep Dive*, SIGGRAPH 2021 Advances: https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
4. UE 5.4 Nanite tessellation (Puget Systems): https://www.pugetsystems.com/blog/2024/04/16/exploring-unreal-engine-5-4-whats-new-and-whats-next/ ; UE 5.5 skinned Nanite (secondary): https://wnhub.io/news/engines/item-45659
5. NVIDIA, *Fast Ray Tracing of Dynamic Scenes Using OptiX 9 and RTX Mega Geometry*: https://developer.nvidia.com/blog/fast-ray-tracing-of-dynamic-scenes-using-nvidia-optix-9-and-nvidia-rtx-mega-geometry ; Vulkan samples: https://developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples
6. Blender developers, *This Summer's Sculpt Mode Refactor* (2024): https://code.blender.org/2024/11/this-summers-sculpt-mode-refactor/
7. F. de Goes, D. L. James, *Regularized Kelvinlets* (SIGGRAPH 2017): https://graphics.pixar.com/library/Kelvinlets ; *Sharp Kelvinlets* (DigiPro 2019): https://graphics.pixar.com/library/SharpKelvinlets
8. `ARB_buffer_storage` specification: https://developer.download.nvidia.com/opengl/specs/GL_ARB_buffer_storage.txt ; C. Everitt, J. McDonald, *Beyond Porting* (AZDO): https://www.ozone3d.net/dl/201401/NVIDIA_OpenGL_beyond_porting.pdf ; persistent mapping notes: https://cppstories.com/2015/01/persistent-mapped-buffers-in-opengl
9. N. Henze, H. V. Le, S. Mayer, V. Schwind, *Improving Software-Reduced Touchscreen Latency*, MobileHCI 2017: https://www.medien.informatik.uni-muenchen.de/pubdb/publications/pub/henze2017improving/henze2017improving.pdf
10. Samsung Research, *Active Stylus Input Latency Compensation on Touch Screen Mobile Devices* (HCII 2020): https://research.samsung.com/research-papers/Active-Stylus-Input-Latency-Compensation-on-Touch-Screen-Mobile-Devices
11. TurboTouch predictor (UIST 2018): https://github.com/LokiResearch/TurboTouchPredictor ; https://hal.archives-ouvertes.fr/hal-01893310
12. A. Rosenzweig, Asahi GPU driver status (OpenGL 4.6, Vulkan 1.4, geometry/tessellation emulation): https://rosenzweig.io/blog/asahi-gpu-part-n.html ; Honeykrisp in Mesa 24.3: https://www.phoronix.com/news/Apple-Vulkan-Driver-Mesa-24.3
13. The vendor's public reference manual, chapters on the canvas document and its pixels.
14. The same manual, the chapter on local high-resolution levels.
15. The same manual, the memory preferences chapter.
