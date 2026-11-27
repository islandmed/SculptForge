---
fixed: **Micro levels: both sides of a stroke show while you sculpt**. When a stroke changed more patches per frame than the shading budget takes, the budget always went to the same patches first, so with X symmetry one side showed live and the other only once you lifted the pen. The budget now moves through the changed patches in turn, so every one is shaded within a few frames and both sides update together; the total shading per frame is the same. Reported by @HG4Science.
testers:
  - Any OS (Standard or Basic tier): a Micro level 3 on a dense mesh (about 2M triangles), Clay with an alpha, X symmetry on, a 80 px brush: drag slowly. Both the side under the brush and its mirror pick up the shading while the pen is down, not only on release.
---
Test: tests/ui/micro_shade_fair_invariant.sfui (squeezed shading budget, per-side counts of patches shaded during the stroke: both sides above zero and within 2x; everything shaded after the release).
