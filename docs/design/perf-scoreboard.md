# Perf scoreboard (0.11.0 beta.3)

One table for every performance claim in the beta.3 push. A number goes here only if it was measured with the
method below. Claims in notes, the site or the guide quote this table, and quote the floor first. Owner: core200m
(it updates the table as branches land). Public text states our own numbers only, never another app's.

## Targets (the maintainer, 11-04: performance is the #1 priority until they're met)

Our own numbers only. Each must hold on the floor tier first, then the E-core floor, the laptop and the Windows 3060.

| | Target |
|---|---|
| (a) Base mesh 15-30M | committed lag p50 < 50 ms and p99 < 100 ms at 120 and 240 px; release < 100 ms |
| (b) Micro levels 100-450M (Lucy 112M / 448M, core-200m) | the same lag targets; orbit >= 30 fps right after strokes; first frame < 3 s |
| (c) First dab | no stall: < 50 ms (NVIDIA first-dab stall) |

## Gap to the targets (11-04)

p50 / p99 / release in ms (240 px, the worse of 120 and 240 px); x = how far off the p50 target. E-core and laptop
for 15M and core-200m are the beta.2 baseline below; Lucy is the laptop run of 11-04 (offscreen) with
fix/micro-pick-reach. Floor iGPU: no machine.

| Target | Scene | E-core floor | Laptop | Windows 3060 | Owner / lever |
|---|---|---|---|---|---|
| (a) | 15M head | 552 / 2036 / 3355 (11x) | 405 / 577 / 230 (8x) | 447 / 737 / 249 (9x) | betaboost: Fast Big Brushes, batches capped by time |
| (a) | Lucy 28M | - | 359 / 623 / 900 (7x) | running | betaboost (same path) |
| (b) lag | core-200m | 1498 / 1735 / 4675 (30x) | 319 / 511 / 452 (6x) | 440 / 565 / 827 (9x) | n2stroke: N2 + big brushes on the base level |
| (b) lag | Lucy 112M / 448M | - | 1700-2656 / 3240-3448 / 10-11 s (34-53x) | running | n2stroke: base-level edits (the 256x rescale); core200m: picks (fix/micro-pick-reach) |
| (b) orbit | core-200m | 10 fps (3x) | 27 fps (1.1x) | 32 fps (met) | core200m: micro renderer and catch-up |
| (b) orbit | Lucy 112M / 448M | - | 1.6-1.7 fps (18x) | running | core200m: micro renderer GPU cost on a dense base (63-70 ms/frame before any stroke) |
| (b) first frame | core-200m | - | 0.7 s (met) | 0.7 s (met) | - |
| (b) first frame | Lucy 112M / 448M | - | 9.0 / 11.1 s (3-4x) | running | core200m: micro open path on a dense base |
| (c) | first dab, NVIDIA | - | - | betaboost's numbers | betaboost |

## Tiers, in the order a claim must pass them

1. **Floor iGPU**: a real Windows laptop with integrated graphics (Intel UHD / Iris Xe class, or a small AMD APU).
   No such machine yet; the tester request is drafted in research/micro-gpu-shade REPORT.md. Until it reports,
   this tier is empty and nothing claims it.
2. **E-core floor** (the gate today): `SF_HW_SIMULATE=floor SF_GOVERNOR=0 taskset -c 0,1` on this laptop (M1,
   Asahi Linux): the floor's budgets, GL 3.3, on the two efficiency cores (about half a fast core each). This is the
   pessimistic CPU bound; a CPU-bound claim counts only if it holds here (perf-moonshots §0 item 6).
3. **Laptop**: the same machine, no simulation, all cores (Apple M1 Pro, 32 GB shared, through Asahi's GL driver).
4. **Windows RTX 3060** (Ryzen 5600X, 32 GB): `~/Work/.sf/win.sh`, PC idle (win.sh holds the PC lock). A sanity
   check: a result that holds only here hasn't hit its target.

## Method

- Scenes: `--benchmark standard-15m` (the 15M head, multires mesh path), `--benchmark core-100m` and
  `--benchmark core-200m` (the same head at micro level 5 with procedural wrinkles and pores, 100M and 200M micro
  triangles in memory). Each runs Clay at 25, 120 and 240 px, then orbits for 10 s (core) or 3 s (15M).
- Committed lag: pointer sample to the swap of the frame whose mesh contains that sample's dab (`lag_ms`).
  Release: pen up to the last dab in the mesh and shaded (`release_done_ms`). Orbit: frames per second over the
  orbit phase. Memory: peak resident RAM plus GPU buffers.
- Defaults of the build under test (no toggles), 6 s strokes, each run started at load < 2 on the laptop, at least
  two interleaved runs per cell; a cell shows the median.
- Rows from `python3 tools/perf_scoreboard.py TIER results/*.json`.
- From 11-04 every laptop run is offscreen (`SDL_VIDEO_DRIVER=offscreen`, the same GPU, no window or compositor).
  The beta.2 baseline below was windowed, so each candidate is re-measured against main offscreen, interleaved, and
  the baseline rows are replaced by those offscreen runs.

## Shipping today: main 0.11.0-beta.2 defaults (11-04)

Committed lag p50 / p99, then release (pen up to the stroke in the mesh), in ms; median of the runs. Floor iGPU:
no machine yet. E-core and laptop: 3 runs each (perf/scoreboard e30349e sources; the build reports its parent
8d89d61c); Windows: 2 runs on an idle PC (e30349e). Bold: meets the target (p50 within 16 ms of the spacing floor,
p99 < 33 ms, release < 100 ms).

| Mesh | Brush | Floor iGPU | E-core floor (the gate) | Laptop | Windows RTX 3060 |
|---|---|---|---|---|---|
| 15M | 25 px | - | 19 / 174, 156 | **14** / 109, **51** | **8** / 59, **45** |
| 15M | 120 px | - | 550 / 884, 608 | 177 / 259, **81** | 170 / 255, **73** |
| 15M | 240 px | - | 552 / 2036, 3355 | 405 / 577, 230 | 447 / 737, 249 |
| 100M micro | 25 px | - | 24 / 79, 131 | **14** / **28**, **23** | **6** / **24**, **33** |
| 100M micro | 120 px | - | 240 / 390, 521 | 113 / 186, **68** | 102 / 183, 107 |
| 100M micro | 240 px | - | 829 / 1254, 2747 | 286 / 479, 338 | 298 / 437, 404 |
| 200M micro | 25 px | - | 25 / 90, 109 | **18** / 60, **42** | **6** / 46, **49** |
| 200M micro | 120 px | - | 466 / 582, 960 | 134 / 225, 171 | 122 / 215, 312 |
| 200M micro | 240 px | - | 1498 / 1735, 4675 | 319 / 511, 452 | 440 / 565, 827 |

Orbit (fps, the orbit phase after the strokes) and memory (peak RAM + GPU buffers):

| Mesh | E-core floor | Laptop | Windows RTX 3060 | Memory (laptop / Windows) |
|---|---|---|---|---|
| 15M | **90** | **182** | **319** | 2.3 GB + 233 MB / 2.4 GB + 211 MB |
| 100M micro | 13 | 32 | 47 | **0.8 GB + 330 MB** / 0.9 GB + 330 MB |
| 200M micro | 10 | 27 | 32 | **1.3 GB + 640 MB** / 1.4 GB + 640 MB |

What it says:

- **Small brushes keep up at p50, not at p99.** 25 px p50 is 6-25 ms on every tier (the target or within a few ms
  of it on the E-core floor); p99 meets 33 ms only on 100M (laptop, 3060) and is 46-174 ms elsewhere.
- **Big brushes fail everywhere, the 3060 included.** 120 px is 100-550 ms behind the pen, 240 px 290-1500 ms.
  On the micro scenes the E-core floor places 9-22 dabs in a 6 s stroke at 120-240 px (the brush can't keep up,
  so dabs merge): the stroke lands, but late and coarse. This is what N2 (micro levels) and Fast Big Brushes
  (15M) are for.
- **Orbit after sculpting on micro levels is 10-47 fps** (it opens and orbits at 60+ fps before any stroke, per
  docs/design/core-200m.md §8): the shading catch-up after the strokes runs during the orbit. 15M orbits at 90+.
- **200M fits a 16 GB integrated-GPU laptop:** 1.3 GB RAM + 640 MB GPU.
- **Noise:** the laptop is shared with other sessions (load 4-13 during runs; the start waits for load < 2). One
  E-core round of three was 2-7x worse on the micro scenes and is outvoted by the median; ranges per cell are in
  the raw rows (`tools/perf_scoreboard.py`). The Windows runs agree within 10%.
- **Windows harness note:** the micro scenes have to start through `cmd /c` (win.sh with any `SFENV`); a direct
  launch from the scheduled task exits silently before writing the scene (not yet understood).

## Lucy on the floor: 0.11.0 vs 0.11.1 (11-05)

Does 0.11.1's micro-level speed-up (measured on the 3060) hold on the floor? Laptop, `SF_HW_SIMULATE=floor
SF_GOVERNOR=0`, offscreen, `--benchmark-file` (Clay 25 / 120 / 240 px, then a 10 s orbit), each run under
`~/Work/.sf/bench.lock` at load < 2. Builds: v0.11.0 and 0.11.1 (d98be72f), each with fix/bench-load-stall applied
(without it E-core runs hang at "settling"), and v0.11.0 with the `--benchmark-file` and `SF_GOVERNOR` harness
commits as well. 2 interleaved runs per cell unless the cell says otherwise. Committed lag p50 / p99 in ms, then
release.

| Scene | Tier | Build | 25 px | 120 px | 240 px | Orbit after strokes | First frame | RAM + GPU |
|---|---|---|---|---|---|---|---|---|
| Lucy 112M (micro) | floor, P-cores | 0.11.0 | 89 / 268 | 1076 / 2230 | 5540 / 5540, 14.4 s | 0.3 fps | - | 5.7 GB + 336 MB |
| Lucy 112M (micro) | floor, P-cores | 0.11.1 | 55 / 148 | 151 / 258 | 325 / 829, 1.4 s | 13.1 fps | 8.7 s | 5.6 GB + 2245 MB |
| Lucy 112M (micro) | floor, P-cores | 0.11.1 + fix/floor-orbit-shade | 55 / 163 | 149 / 267 | 313 / 824, 1.5 s | 19.1 fps | 8.7 s | 5.6 GB + 2245 MB |
| Lucy 112M (micro) | E-core floor | 0.11.0 (1 run) | 83 / 902 | 4 dabs in 6 s | 5 dabs, 61.5 s | 0.1 fps | - | 5.4 GB + 336 MB |
| Lucy 112M (micro) | E-core floor | 0.11.1 (3 runs) | 54 / 180 | 232 / 846 | 2258 / 2434, 6.7 s | 7.5 fps | 46.4 s | 5.4 GB + 2245 MB |
| Lucy 112M (micro) | E-core floor | 0.11.1 + fix/floor-orbit-shade | 53 / 181 | 232 / 864 | 2265 / 2467, 6.7 s | 19.2 fps | 46.3 s | 5.4 GB + 2245 MB |
| Lucy 28M (plain) | floor, P-cores | 0.11.0 | 21 / 133 | 128 / 296 | 347 / 622, 0.7 s | 64 fps | - | 4.0 GB + 336 MB |
| Lucy 28M (plain) | floor, P-cores | 0.11.1 | 20 / 138 | 128 / 317 | 327 / 655, 0.8 s | 63 fps | 4.8 s | 3.9 GB + 500 MB |
| Lucy 28M (plain) | E-core floor | 0.11.0 | 21 / 306 | 372 / 1592 | 2750 / 4570, 8.1 s | 53 fps | - | 3.6 GB + 336 MB |
| Lucy 28M (plain) | E-core floor | 0.11.1 | 21 / 294 | 460 / 1752 | 2050 / 4009, 7.1 s | 54 fps | 23.8 s | 3.6 GB + 500 MB |

What it says:

- **Lucy 112M doesn't fit an 8 GB floor machine:** 5.6 GB RAM + 2.2 GB GPU, and an integrated GPU takes both from
  the same memory. Lucy 28M (3.9 GB + 500 MB) is the largest Lucy that fits.
- **0.11.1 is a real step on the floor for micro levels** (112M, P-cores): 25 px p99 268 → 148 ms, 120 px p50
  1076 → 151 ms, 240 px release 14.4 → 1.4 s, orbit 0.3 → 13 fps. On E-cores, 0.11.0 can't sculpt 112M at all
  (4-5 dabs per 6 s big stroke, 7-19 s frames); 0.11.1 can.
- **The orbit claim doesn't hold on the floor:** 13 fps (P-cores) and 7.5 fps (E-cores) after strokes, against the
  3060's 48-51 and the target of 30. Two costs: shading catch-up after strokes ran without a time cap while the view
  moved (36 ms a frame on P-cores, 93 ms on E-cores), and drawing the 28M base triangles (the micro renderer's
  coarsest LOD is the base; the floor's budget is 2M), about 33 ms of GPU a frame here. fix/floor-orbit-shade caps
  the first at 10 ms a frame (orbit 13 → 19 fps, 7.5 → 19 fps; strokes unchanged). The second needs LOD below the
  base: cluster LOD (perf-plan item 8), off until the patent opinion. A real integrated GPU is slower than this
  M1 Pro's GPU.
- **The 3060 "before" numbers aren't v0.11.0:** "312 ms p99 at 25 px" was main efe8bb2f (an unreleased micro-level
  regression) and "orbit 0.7 fps" was main 8e67e3f7. Against the v0.11.0 tag on the floor the 25 px p99 gain is
  268 → 148 ms.
- **Plain meshes (28M) didn't change** between 0.11.0 and 0.11.1 (within noise); big brushes there are still 3-55x
  off target (a).
- **First frame:** 112M opens in 8.7 s on P-cores, 46 s on E-cores (target < 3 s).

## Candidates for beta.3

Each candidate is measured against the row above on the same tier, interleaved, before it can be turned on.

| Branch (owner) | What | Status |
|---|---|---|
| perf/n2-wavelet (n2stroke) | N2 wavelet strokes on micro levels, default on; GPU shading (fragment shader) | in progress |
| perf/beta-boost-2 (betaboost) | Fast Big Brushes with fused batches capped by time; NVIDIA first-dab stall | in progress |
| research/core-200m (core200m) | N1 screen-space strokes under 1 ms a dab | 6d410e1f: 0.58-0.73 ms GPU per 240 px dab on the laptop (was 4.2-4.8), live on the floor 9.6 vs the ghost's 8.3 ms GPU; research, off (SF_SCREEN_STROKE=1) |
