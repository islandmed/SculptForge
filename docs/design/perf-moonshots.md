# Performance moonshots: sculpting that costs pixels, not polygons

Research branch `research/perf-moonshots` (2026-11-01). Goal: the cost of a stroke grows with the pixels on screen
and the brush footprint, never with the total polygon count. This note surveys recent work, rates eight ideas,
reports three prototypes measured with `--benchmark standard-33m`, and recommends what to ship.

Starting point (Tony's clean 33M run, M1 Pro, Asahi, Mesa GL 4.6, unified memory): orbit 89 fps; Clay small p99
38 ms; Clay large p99 119 ms; 240 px brush p99 338 ms; Move 18 fps. The branch is built on `perf/bigbrush-p99`
(dabs of meshes from 2M vertices go to the solver thread without the preview), which already takes the stroke
p99s to about 30 ms; what it leaves is the surface trailing the pen (each 240 px dab at 33M still costs
100-200 ms of CPU) and Move, which never used the solver path.

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

## 3. How ZBrush does it (publicly documented approaches only)

From Maxon's documentation and public material only (no binaries inspected):
(a) **2.5D pixol canvas**: ZBrush has "a 2.5D canvas document", not a 3D scene; a pixol is a pixel with depth,
orientation and material [13]. The heritage is a CPU renderer whose cost follows pixels.
(b) **Subdivision levels**: the classic multires stack (ours: subdivision levels).
(c) **HD Geometry**: DivideHD adds levels beyond the highest SDiv; Sculpt HD (the A hotkey) "selects an area of
your model around the current mouse position for high-definition sculpting", circular or square [14]; only that
region is at HD resolution while the rest stays at the regular top level.
(d) **Per-mesh caps**: Preferences > Mem > MaxPolyPerMesh limits polygons per mesh (in millions), with a separate
HD preview cap [15]; dense work is split into many objects, hidden ones costing little.

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

All three are environment flags so they cost nothing when off. None changes what a stroke leaves on the mesh
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
3. **Persistent upload** (`SF_PERSISTENT_UPLOAD=1`, idea 4): two persistently mapped, coherent staging buffers
   (48 MB each, `glBufferStorage`); the solver thread packs straight into one, the GL thread issues
   `glCopyBufferSubData` for each range and a fence; a buffer is reused only after its fence. Batches larger than
   4M vertices pack into the old vector as before.

### Measurements

MEASUREMENTS

## 6. Recommendation

RECOMMENDATION

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
13. Maxon ZBrush docs, *The Canvas Document* and *The Pixol*: https://help.maxon.net/zbr/en-us/Content/html/getting-started/basic-concepts/the-canvas-document/the-canvas-document.html ; https://help.maxon.net/zbr/en-us/Content/html/getting-started/basic-concepts/the-pixol/the-pixol.html
14. Maxon ZBrush docs, *Geometry HD*: https://help.maxon.net/zbr/en-us/Content/html/reference-guide/tool/polymesh/geometry-hd/geometry-hd.html
15. Maxon ZBrush docs, *Preferences > Mem*: https://help.maxon.net/zbr/en-us/Content/html/reference-guide/preferences/mem/mem.html

ZBrush is a trademark of Maxon; SculptForge is not affiliated with or endorsed by Maxon.
