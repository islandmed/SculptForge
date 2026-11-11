---
whats_new: **Clay on big brushes** leaves one even band, not a row of ring edges along the stroke.
fixed: **Large Clay strokes showed each dab as a ring** (#196): on a form that curves away under the brush (a 120 px
  brush on a sphere) the middle of the dab stood above Clay's plane and stayed untouched, the dab lifted a ring, and the
  ring's edge was a crease, one per dab along the stroke. The plane now reaches the surface under the cursor and the pull
  is scaled by how much higher that is, so the middle lifts as it does on a flat surface and a stroke builds up about as
  much as before (a flat surface is unchanged). Light Clay strokes no longer leave a raised edge: on a strongly curved form they lift a little less than
  they did, because the ring they used to lift is gone (a heavy stroke moves the same amount of clay). Brush-pack Clay brushes follow the same plane.
fixed: **Symmetry strokes whose two sides overlap** (a big brush near the mirror plane): the second side's plane
  now reads the surface the first side just left, so a Fast Big Brushes stroke matches the exact one.
testers:
  - Any OS: New sphere, Divide three times, Clay at size 120; one stroke across the front, then turn the view 30 degrees.
    It should be one even band with no row of ring edges (before: a ring edge per dab).
  - Any OS with a tablet: Clay at size 40, a light pen stroke and a heavy one on the same sphere; the heavy one should
    lift clearly more, as on a flat surface.
---
Tests: tests/test_clay_dab.cpp (clay_stroke_invariant_no_ring_walls_across_overlapping_dabs: every dab of an overlapping
stroke on a dense sphere lifts a mound, no wall and no bare middle; the single-dab profile; the plane on a flat surface
unchanged); tests/test_beta_boost.cpp adaptive/fused match tests unchanged (Clay 0.05% and 0.23%, bars 1% and 2.5%).
Measured (laptop, 1.3M sphere, Clay, no symmetry), peak height of a stroke across the front, before -> after: 30 px 28 -> 30 mm,
60 px 54 -> 53, 120 px 69 -> 64, 160 px 86 -> 66; the second difference's high-pass peak (a crease) 0.18/0.53/0.85/1.3 -> 0.11/0.05/0.15/0.18.
Artist-pass pen row (Clay 40 px, 5.2M): light 0.0014 -> 0.0011, heavy 0.0164 -> 0.0184: x11.6 -> x16.4 (bar > x2; a flat surface gives x15.5: the old x11.6 was the ring wall).
Displaced volume of the pen stroke (Clay 40 px, 1.3M sphere), before -> after: heavy 0.0%, mid -4%, light -25%.
