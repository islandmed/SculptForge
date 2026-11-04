---
whats_new: **Fast Big Brushes**: big brushes on dense meshes keep up with the pen. A 240 px Clay stroke on a 15 million
  triangle head trails the pen by a third to a quarter of what it did (most on modest hardware), and every dab is
  placed. On by default: **Preferences > Performance > Fast Big Brushes**.
highlight: Big brushes keep up on dense meshes
added: **Fast Big Brushes** (Preferences > Performance, on by default): a dab that reaches more than 150,000 vertices
  takes its plane from an even sample of them and turns the shading along with the move it makes (from the brush's
  own slope, no neighbour walks), and the dabs waiting for a frame go through the vertices together in one pass, so
  the normals, the picking structure and the GPU copy are updated once a frame instead of once a dab. Every vertex
  still moves by the brush itself; the exact shading follows when the pen rests and when it lifts. Standard, Clay,
  Flatten, Crease, Pinch and Dam Standard; Trim takes the sampled plane and shading but not the fused pass. Small
  brushes, Smooth, Layer, Inflate, Nudge, Clay Block, alphas and masked areas are placed dab by dab as before.
  The result matches the dab-by-dab stroke to within about 2% of how far it moved the surface
  (tests/test_beta_boost.cpp).
testers:
  - Windows: open a dense model (or Help > Benchmark, standard-15m) and sculpt with a 200-300 px Clay or Standard
    brush across the face, fast: the surface should follow the pen closely, without the stroke falling behind.
  - Any OS: same stroke with Preferences > Performance > Fast Big Brushes off, then on: the result should look the
    same; the shading under the brush should be smooth while you drag and after you lift the pen.
  - Any OS: X symmetry on, a big brush back and forth across the middle of the face: no seam or step at the mirror line.
  - macOS / Linux: undo and redo a big stroke: the surface and its shading come back exactly.
---
Numbers before/after (laptop, floor profile, Windows PC): REPORT.md on perf/beta-boost.
