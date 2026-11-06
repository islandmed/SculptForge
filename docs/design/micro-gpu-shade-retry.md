# perf/micro-gpu-shade: GPU micro shading, rebased and re-measured. PARKED (input 11-06: off on every tier, not merged)

Branch: perf/micro-gpu-shade. HEAD a0061556 = research/micro-gpu-shade f7817622 merged onto origin/main +
perf/frame-overlap c77f98b8 + micro-upload (53fbb340), as input asked.
T0: PC full suite green except ui_micro_gpu_shade (the known WSL D3D12 "Removing Device" crash; now labelled
no_d3d12 like ui_micro_levels). All 7 no_d3d12 tests pass on the laptop, ui_micro_gpu_shade_parity (png_diff, CPU vs GPU shots) included.
**Still off by default.** No glFinish anywhere on the app's path (bench-only).

## Port
- GpuFrameTimer: frame-overlap's fences + the research's query parts with pause/resume.
- shadePending: the GPU path (seeds + vertex ring, whole) first; the CPU path (micro-upload: seeds alone, ring
  stale until the stroke ends) as the fallback.
- The GPU pass's own timer query is read behind a fence (asking a query's availability stalled on Mesa's Apple
  driver; app/Calibration.h).
- New knob SF_MICRO_GPU_SHADE_MS (the GPU time target per pass, default 4).

## Floor tier (laptop, SF_HW_SIMULATE=floor, x3 interleaved, load 1.77-1.99): core-200m, p50s (medians of 3)
| phase | CPU shading | GPU shading 4 ms | GPU 1.5 ms (x1) | GPU 1.0 ms (x1) |
|---|---|---|---|---|
| 120 px fps / frame | 225.6 / 3.91 | 100.7 / 9.63 | 135.9 / 7.37 | 147.2 / 7.21 |
| 120 px lag p50/p99 | 16.5 / 32.5 | 21.6 / **42.7** | 18.5 / 39.3 | 18.5 / 37.3 |
| 240 px fps | 203 | 87.6 | 111.6 | 126.8 |
| 240 px lag p50/p99 | 26.2 / 58.3 | 32.0 / **68.6** | 30.9 / 65.4 | 29.0 / 63.6 |
| uploads per dab, 240 px | 24.3 | **3.6** | 3.5 | 3.2 |
| orbit fps | 194 | 190 | 183 | 173 |
GPU passes: 3.6 ms GPU + 0.6 ms CPU each (4 ms), 1.3 + 0.4 (1 ms); 1.5 ns a point on the M1 GPU.
Result: the stroke p99 bar fails at every target (+15% to +31% at 120 px). The frame time grows far more than the
pass's own GPU time (1.3 ms of pass, +3.3 ms of frame). Nothing on the main thread grows; the solver's
kernel+normals do (~690 -> ~1200 ms a phase at 120 px). That fits GPU and CPU competing for the M1's unified memory,
but it isn't proven.

## Windows RTX 3060 (win.sh, CI's packages build of a0061556): core-200m
x1 pair only (a 0.3 ms target and pairs 2-3 cancelled: the PC queue was ~1 h behind).
| phase | CPU shading | GPU shading 4 ms |
|---|---|---|
| 120 px fps / frame | 171.2 / 4.56 | 152.9 / 6.01 |
| 120 px lag p50/p99 | 18.3 / 33.2 | 18.4 / 34.1 |
| 240 px fps / lag p99 | 179.5 / 58.7 | 177.8 / 59.1 |
| upload per frame (main thread) | 3.08 | 4.76 |
| release_done 120 / 240 px | 6.7 / 6.5 | 3.9 / 3.4 |
| orbit fps | 443 | 524 |
GPU passes: **1.52 ms GPU but 4.24 ms CPU each** (0.2 ns a point on the GPU). The pass is cheap on the GPU. Its
CPU side (vertex rings, slot lists, row tables for up to 8M points: the budget clamp) costs more than the CPU
shading it replaces. Lag is flat, fps lower, pen-up settles sooner.

## Proposal per tier
Off on every tier (input agreed 11-06). The branch is parked, not merged dark (unused paths rot).
Preconditions for a retry:
1. **Find the floor cause.** With GPU shading on, frames grow ~3x the pass's own GPU time and the solver slows
   ~1.7x. Unified-memory contention on the M1 is the leading hypothesis, unproven. Check it against a real iGPU or
   with the GPU pass's memory traffic cut (a smaller target, fewer disp reads), and look at frame-overlap's
   in-flight frame meeting the pass's copies into shade buffers it reads.
2. **Make the point lists cheap** (3060: 4.2 ms CPU a pass): build them incrementally (keep the ring/slot tables
   across syncs, append only new seeds), or on the GPU (seeds -> rings in a pass), and cap a pass by CPU time
   too, not only by GPU time.
Meanwhile the CPU path from perf/micro-upload (shipping in 0.11.8) is the one that runs.

Results and scripts: ~/Work/.sf/bench/micro-upload/gpu (floor), .../win (3060); summ.py prints the rows.
