---
fixed: **The surface lagging a second or two behind the brush on a dense Micro level** (#184, thanks @HG4Science): each dab
  re-shaded every patch within reach of the brush, not just the ones it changed. With an alpha (whose square reaches well
  past what it moves) that was more than twice the work, and the shading fell behind the stroke. A dab now re-shades only
  what it changed and the patches next to it.
fixed: **A stroke on a Micro level showing only when released** (#184, thanks @HG4Science): on a level that isn't
  streamed (Micro 3 on a 2.6M base, for one), each frame shaded fewer patches than its dabs changed, and the surface behind
  the brush kept its old shading until you let go. On computers at the High or Extreme hardware tier every frame now shades
  what it changed, so the stroke shows as you draw. On the Essential and Standard tiers that cost a fifth to a third of the
  stroke's frame rate, so there the shading still follows the stroke and catches up when you release, with the smaller
  per-dab shading above.
testers:
  - Linux or Windows on a High or Extreme tier computer (Preferences > Performance shows the tier), a 2M+ base at Micro 3: Clay brush,
    Bolt alpha, about 40 px, one long slow stroke without letting go. The relief shows behind the brush while you draw,
    not only when you release.
  - Linux, a 2M+ base at Micro 4: Standard brush, Bolt alpha, size about 90 px, one long stroke. The relief shows right behind
    the brush, not a second or two later.
  - Any OS, Micro 3: the same stroke, then orbit. No dark or flat patches at the stroke's edges.
---
Reviewers: MicroStroke::changed_ (0b2d89b81: stitch marks only the candidates the kernel or the stitch changed) was never
filled on main, so stitch marked every candidate. The kernel now sets it per candidate it writes. The neighbours' shading is
still redone: the renderer's shadeSeeds puts each shaded patch's vertex ring in stale_ (MicroShader::shadeSet ring2_).
Floor replay of the reporter's Draw + Bolt flow (2.58M base, Micro 4, 27 px, 8 threads): 41% of candidates change; the
stroke's mean coarse-shading queue goes from 93.6K to 11.3K tiles, fps 22.9 -> 26.3, dab time unchanged.
Tests: micro_alpha_dab_marks_only_what_changed (fails without the fill), ui micro_alpha_stroke_shading (view_stale 0 after
the stroke drains, at Micro 3 and 4, with and without Focus Detail). SF_STROKE_LOG adds one "micro dab:" line per dab:
triangles changed of those in reach, the stride, and find / plane / kernel / stitch ms.
Widen sweep (own commit): without Focus Detail, a range widen re-sent the heights and kept the shading, a step's rounding
off (±2/255 on a few pixels, until those patches were next shaded). MicroRenderer now sweeps every patch's shading
afterwards, as MicroFocus does: within the rest budget, only once nothing else waits and no stroke runs; pendingShading()
counts it, so the app keeps drawing until it is done and then rests. Test: ui micro_widen_sweep.
Keep pace (own commit): MicroRenderer::shadePending takes at least this sync's changed patches while a stroke runs
(keepPace), whatever the budget. Without it, at Micro 3 on the reporter's 2.58M base (Clay + Bolt 39 px, 8 threads), a frame
changed 18-35K patches, the budget took ~7K (the measured ns per point already counts the ring, and the limit divides by 3
for it again: ~3 ms of strokeShadeMs 10), and the backlog reached 560K patches by the release. With it: no backlog, mid-stroke
view_stale on that scene 0.041 -> 0.0007; cost fps 44 -> 36, frame p50 22 -> 27 ms, pointer lag p50 51 -> 57 ms (x2 each).
Focus Detail levels unchanged on purpose: their close view comes from pages (mid-stroke view_stale 0.00001 at Micro 4 there),
and shading every changed coarse tile each frame cost a third of the fps on big strokes. Test: ui micro_stroke_shows_midstroke
(squeezed budget, held live stroke: 0.647 without, 0.00007 with).
Tier split (own commit): keepPace only on High and Extreme (TierConfig::strokeShadeKeepPace; SF_STROKE_KEEP_PACE=0|1 pins it).
His Micro 3 flow (Clay + Bolt 39 px, 2.58M base, x3 interleaved, 30c50787 -> 206666d2): floor long fps 34 -> 26 (-24%),
second 22 -> 14; standard long 43 -> 34 (-21%), second 27 -> 18 (-33%), frame p50 23 -> 29 ms, lag p50 58 -> 66 ms; peak RAM
equal (1307-1318 vs 1308-1321 MB). Both over the 15% bar: off on Floor and Standard. Test: hw_tier_stroke_shading_keeps_pace_on_high_and_extreme.
