# Perf scoreboard (0.11.0 beta.3)

One table for every performance claim in the beta.3 push. A number goes here only if it was measured with the
method below. Claims in notes, the site or the guide quote this table, and quote the floor first. Owner: core200m
(it updates the table as branches land). Public text states our own numbers only, never another app's.

## Targets

| Measure | Target | Where it comes from |
|---|---|---|
| Committed lag p50, beyond the spacing floor | <= 16 ms | docs/design/core-200m.md, the two latencies |
| Committed lag p99 | < 33 ms | two frames at 60 Hz |
| Release (pen up to the stroke fully in the mesh) | < 100 ms | feels instant |
| Orbit | >= 60 fps | the display |
| 200M micro triangles on a 16 GB integrated-GPU laptop | opens, sculpts, orbits | perf-moonshots §0 |

The spacing floor: a dab lands every 0.15 x radius of pointer travel, so at the scripted pointer speed the newest dab
is on average ~18 ms old at 240 px before any work runs (perf-moonshots §0). Raw lag is reported; the adjusted
figure is raw minus that floor.

## Tiers, in the order a claim must pass them

1. **Floor iGPU**: a real Windows laptop with integrated graphics (Intel UHD / Iris Xe class, or a small AMD APU).
   No such machine yet; the tester request is drafted in research/micro-gpu-shade REPORT.md. Until it reports,
   this tier is empty and nothing claims it.
2. **E-core floor** (the gate today): `SF_HW_SIMULATE=floor SF_GOVERNOR=0 taskset -c 0,1` on this laptop (M1,
   Asahi Linux): the floor's budgets, GL 3.3, on the two efficiency cores (about half a fast core each). This is the
   pessimistic CPU bound; a CPU-bound claim counts only if it holds here (perf-moonshots §0 item 6).
3. **Laptop**: the same machine, no simulation, all cores (Apple M1 Pro GPU through Asahi's GL driver).
4. **Windows RTX 3060** (Ryzen 5600X, 32 GB): `~/Work/.sf/win.sh`, PC idle (win.sh holds the PC lock). A sanity
   check: a result that holds only here hasn't hit its target.

## Method

- Scenes: `--benchmark standard-15m` (the 15M head, multires mesh path), `--benchmark core-100m` and
  `--benchmark core-200m` (the same head at micro level 5 with procedural wrinkles and pores, 100M and 200M micro
  triangles in memory). Each runs Clay at 25, 120 and 240 px, then orbits for 10 s (core) or 3 s (15M).
- Committed lag: pointer sample to the swap of the frame whose mesh contains that sample's dab (`lag_ms`).
  Release: pen up to the last dab in the mesh and shaded (`release_done_ms`). Orbit: frames per second over the
  orbit phase. Memory: peak resident RAM plus GPU buffers.
- Defaults of the build under test (no toggles), load < 2 on the laptop, two interleaved runs per cell; a cell shows
  the range of the runs.
- Rows from `python3 tools/perf_scoreboard.py TIER results/*.json`.

## Shipping today: main 0.11.0-beta.2 defaults

| Mesh | Brush | Tier | Committed lag p50 / p99 | Release | Orbit fps | Memory (peak RAM + GPU) | Commit |
|---|---|---|---|---|---|---|---|
| (filled below as runs complete) | | | | | | | |

## Candidates for beta.3

Each candidate is measured against the row above on the same tier, interleaved, before it can be turned on.

| Branch (owner) | What | Status |
|---|---|---|
| perf/n2-wavelet (n2stroke) | N2 wavelet strokes on micro levels, default on; GPU shading (fragment shader) | in progress |
| perf/beta-boost-2 (betaboost) | Fast Big Brushes with fused batches capped by time; NVIDIA first-dab stall | in progress |
| research/core-200m (core200m) | N1 screen-space strokes under 1 ms a dab | in progress |
