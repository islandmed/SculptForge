---
whats_new: **Palette Knife no longer creases where strokes overlap**: crossing and stacked strokes, big brushes and mirrored strokes too, stay clean.
fixed: **Palette Knife folded the surface where two strokes overlapped** (Fixes #198): a long knife stroke dragged its vertices
  along with the clay, so the band along each side of the stroke was sheared into thin slivers (about four times over on a stroke
  a brush and a half long), and the next stroke across them turned them over into dark creases. The knife now carries the shape
  and leaves the vertices spread as they were: each dab flattens first, then drags, and after the drag every vertex goes back to
  where the dab found it, on the dragged surface (where a very big brush presses the surface flat against the way back, that
  dab moves the vertices with the clay as before). Six overlapping strokes with X symmetry on a 1.3M-triangle sphere fold 0
  triangles (2,343 folded and 860 turned inside out before) and leave no slivers (up to 37,000 before); five stacked strokes
  with brushes up to most of the sphere's front (Size 200) fold none either, with far fewer slivers (8,000 against 278,000 at
  the largest); the dragging-brush close-up session goes from 607 folded triangles to 0. Where a stroke runs near the mirror
  plane the two halves' drags now blend instead of adding up (they used to drag the clay between them twice as far), and the
  halves match to about half an edge (about 39 edges apart before). A drag alone carries the surface where it did before
  (within a tenth of an edge); with its flattening the knife's ridges stand a little higher than before (5.8 against 4.4 edges
  on the test strokes), as the old vertices bunched ahead of the tip had pulled its plane down. Cost: a knife stroke takes about
  1.7 times as long to compute (at a normal size on a 1.3M-triangle mesh its slowest frame goes from about 8 to 11 ms; at Size
  200 from about 75 to 90 ms); other brushes are unchanged. Vertex paint under the knife now stays where it is instead of being
  smeared along. Tendril is tracked in #204.
testers:
  - Any OS: Palette Knife, three or more strokes crossing and stacked, with X symmetry on and one stroke ending near the middle; close-up with a dark Lit Sphere or wireframe. Good = no dark creases or specks, the two halves alike.
  - Any OS: Palette Knife at Size 200 on a multi-million-triangle mesh, three to five strokes stacked at different pressures: no crumpled patch or speckled trail, and the stroke follows the pen about as well as before.
  - Any OS: Palette Knife over vertex paint: the paint stays in place (by design).
---
Geometric invariants (tests/test_geom_invariants.cpp): palette_knife_overlaps_never_fold (six overlapping strokes, X symmetry: no
fold, no inside-out triangle, no sliver under quality 0.2; the drag alone's surface against the vertex-moving drag's; the ridges no
lower), palette_knife_big_brush_stack (radius 0.3, 0.7, 0.95, five stacked strokes at varied pressure, X symmetry: no fold, a few
dozen slivers at most up to 0.7 and no more than the vertex-moving drag's at 0.95, the fifth stroke no slower than the first),
drag_brushes_replay_session (the knife folds nothing off the ground Tendril dragged). SF_DRAG_SLIDE=0 runs the old drag for A/B.
