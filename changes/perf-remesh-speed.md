---
whats_new: **Faster remeshing**: **Tool > Geometry > Quad Remesh** at the defaults is 1.4 to 1.7 times faster (a
  1,000-triangle sculpt: 1.24 s before, 0.74 s now on our floor-tier laptop), and **Voxel Remesh** on a 10 million
  triangle sculpt takes 1.6 s instead of 4.0 s. The quality is unchanged. Thanks @factoberplay for the report.
highlight: Quad Remesh up to 1.7x faster
changed: **Quad Remesh is faster**, most of all on small models (reported by @factoberplay): most of it used to run on
  one core. Its solvers (the multigrid setup, the conjugate-gradient passes, the grid map's matrix assembly), the proxy
  surface's new vertices and the BVH build of the sculpt now run on all threads. Measured on the floor-tier laptop at
  the defaults (5000 quads, median of 5): 1k triangles 1242 to 740 ms, 10k 1544 to 1001 ms, 100k 1524 to 1087 ms, 1M
  2562 to 1793 ms, 10M 11.9 to 7.3 s. Quality unchanged: the same mesh bit for bit on the test sculpts, and the same
  quality numbers (distance to the sculpt, poles, angles) where a sum's rounding moved.
changed: **Voxel Remesh and the legacy Quad Remesh are faster on big sculpts**: the open-border check, the shell
  rasterisation and the BVH build run on all threads (Voxel Remesh at 10M triangles 4.0 to 1.6 s, 1M 546 to 352 ms;
  legacy Quad Remesh at 10M 11.0 to 7.1 s). Same results.
testers:
  - Windows: Tool > Geometry > Quad Remesh at the defaults on a starter head and on a small model of your own; note
    the seconds in the status line next to 0.11.12's on the same model. Good: faster, and the same quad layout.
  - Any OS: Voxel Remesh on a dense sculpt (several million triangles): faster than 0.11.12, same surface.
---
Timings per stage, the fitted scaling exponents and what is left: REPORT.md on perf/remesh-speed and
~/Work/.sf/remesh-bench/results/after-vs-before.md (sf_remesh_bench, tests/tools/remesh_bench.cpp).
