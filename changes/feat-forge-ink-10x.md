---
whats_new: **Close-ups stay smooth**: zoom as close as you like into a model with micro detail; the surface no longer shows the stored grid as facets.
highlight: Close-ups of micro-detail models stay smooth at any zoom
added: Close-ups of models with a micro level stay smooth at any zoom. The viewport now draws up to two levels finer than the
  stored micro grid when the camera is close (three on Extreme tier hardware), the surface between stored points following
  the model's curvature, with no new data in memory or on disk and within the usual triangle budget; picks and brushes are
  unchanged. Also for Focus Detail models. SF_TIER_MICRO_DRAW_ABOVE sets the extra levels (0 = as before). Credit: Aymen G (idea).
testers:
  - Any OS: New Sphere, Add Micro level 4, Micro noise; zoom until the sphere fills the view many times over. Good = a smooth
    wavy surface with no flat triangles; bad = visible facets, cracks between patches, or a frame-rate drop.
  - A head with a micro level from an earlier version: open, zoom in on a pore. The detail reads the same from arm's length.
---
Harness for the 10x scoreboard rides along (no user-visible change): `--benchmark core-<N>m` scenes at any size, `sf_detail_bench`
(tests/tools/detail_bench.cpp), `bench.sh tenx` in ~/Work/.sf. Design + numbers: the branch's REPORT.md.
