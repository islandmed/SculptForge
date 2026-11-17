---
added: Micro close-ups: in smooth shading the viewport can draw up to two levels finer than the stored micro grid when the
  camera is close (three on Extreme tier hardware), the surface between stored points following the model's curvature, with
  no new data in memory or on disk and within the usual triangle budget; flat shading shows the stored grid as before, and
  picks and brushes are unchanged. Head-on, the default view looks the same as before; measured on the floor tier's
  billion-triangle Focus Detail scene (x3): 240 px stroke p99 156 -> 116 ms, orbit 62.7 -> 72.0 fps, memory unchanged
  (2.2 GB). SF_TIER_MICRO_DRAW_ABOVE sets the extra levels (0 = as before). Credit: Aymen G on Discord (idea).
testers:
  - Any OS: New Sphere, Add Micro level 4, Micro noise; zoom until the sphere fills the view many times over, in smooth and
    in flat shading. Good = flat shading shows one grid of facets with no seams or dark strips, smooth shading shows no
    cracks, and orbiting close is no slower than before; bad = a seam between patches, or a frame-rate drop.
  - A head with a micro level from an earlier version: open, zoom in on a pore. The detail reads the same from arm's length.
---
Also on the branch (no user-visible change): `--benchmark core-<N>m` scenes at any size and the sf_detail_bench tool
(tests/tools/detail_bench.cpp), the measurement harness for the Forge Ink work. Based on main (0.13.6 is out); feat/forge-ink-tiles stacks on this branch.
