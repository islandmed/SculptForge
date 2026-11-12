---
whats_new: **Nudge and Palette Knife fold the surface less**: a second stroke across a dragged area leaves fewer creases, and Nudge's two halves agree under symmetry.
fixed: **Nudge and Palette Knife folded the surface over itself** (#198, #199): a second stroke across an area they had
  dragged, and a single Nudge stroke, left thousands of triangles folded over their neighbours (dark creases and specks).
  The material they push is now spread out again behind the brush; Palette Knife drags a little less per dab when its
  steps are long (at the default spacing it drags as far as before); Nudge slides along the plane of the brush instead of
  each vertex's own, noisy normal (which also keeps the two halves of a mirrored stroke identical), and holds back a move
  that would turn a triangle over while its brush covers up to about 12,000 vertices. In the close-up session the folded
  triangles fall from 1,444 to 607 (Palette Knife) and Nudge adds none (it added 4,972); on a replay of the same strokes
  Nudge goes from 5,347 to 0 and Palette Knife from 1,381 to about 620. Palette Knife still leaves creases where two
  strokes overlap. Cost: Palette Knife takes about 0.2 ms more per dab with a small brush and 40 to 50% more time with a
  very large one; Nudge about 1.2 ms more per dab with a small brush (the fold check), little more with a large one.
  Nudge reaches about 6% less far on a long stroke. Tendril is unchanged.
testers:
  - Any OS: Palette Knife and Nudge, two strokes crossing; close-up with a wireframe or a dark Lit Sphere. Good = fewer black creases than before; some remain on Palette Knife.
  - Any OS: Nudge with X symmetry on; both sides look the same.
  - Any OS: Palette Knife and Nudge strokes with a very large brush on a multi-million-triangle mesh: the brush follows the pen as smoothly as before (it costs 40 to 50% more per dab).
  - Any OS: Palette Knife with a wide spacing (Spacing 25% or more): it drags less per dab than at the default spacing, by design.
---
Geometric invariants (tests/test_geom_invariants.cpp): palette_knife and brush_nudge are normal tests now; the crossings are
replayed from recorded close-up strokes (tests/data/drag_paths, tests/GeomInvariants.h replayStrokes, the PathStroke pressure ramp).
