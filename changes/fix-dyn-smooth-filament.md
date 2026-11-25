---
whats_new: **Smooth with Dynamic on** now melts away a hair-thin strand (a Tendril pulled long and thin) instead of leaving a line of triangles standing out of the surface.
fixed: **Dynamic + Smooth leaves a hair-thin filament** (#238, reported by @nino3thd): a strand whose cross-section had shrunk to a hair kept its long edges, which Dynamic never collapses, and smoothing a straight line of vertices moves nothing, so it stood out of the surface after any number of Smooth strokes (16 strokes: 650 sliver triangles still out). Smooth strokes with Dynamic on now merge away a vertex whose every triangle is a needle (height under 5% of the Detail's edge length), a strand at a time from its end; a solid form, a Part with even triangles, and Smooth without Dynamic are untouched.
testers:
  - Windows or Linux: Dynamic on, Tendril, pull a long thin horn out of a sphere (small brush, long drag), then Smooth over it from the tip down a few times; no thin line of triangles is left standing out of the surface.
  - Smooth over a normal, solid area with Dynamic on: it looks and behaves as before.
---
Test: tests/test_dyn_smooth_filament.cpp (a strand squashed to 5% and 0.5% of its width, 1 to 16 Smooth strokes at two brush sizes; the same
strokes on even spheres, a torus, a capsule and a quad sphere merge nothing). Dynamic Smooth per dab on a 308k-triangle sphere: +1% (the
collapse phase 58.3 -> 59.0 ms per 300 dabs).
