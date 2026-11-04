---
whats_new: **Fast Big Brushes (Experimental)**: a new way to place big brushes on dense meshes, off by default. Try it
  on a heavy model with a big Clay or Standard brush and tell us how it feels:
  **Preferences > Performance > Fast Big Brushes (Experimental)**.
added: **Fast Big Brushes (Experimental)** (Preferences > Performance, off by default): a dab that reaches more than
  150,000 vertices takes its plane from an even sample of them and turns the shading along with the move it makes
  (from the brush's own slope, no neighbour walks), and the dabs waiting for a frame go through the vertices together
  in one pass, so the normals, the picking structure and the GPU copy are updated once a frame instead of once a dab.
  Every vertex still moves by the brush itself; the exact shading follows behind the brush and when the pen lifts.
  Standard, Clay, Flatten, Crease, Pinch and Sharp Crease; Trim takes the sampled plane and shading but not the fused
  pass. Small brushes, Smooth, Layer, Inflate, Nudge, Clay Block, alphas and masked areas are placed dab by dab as
  before. The result matches the dab-by-dab stroke to within about 2% of how far it moved the surface
  (tests/test_beta_boost.cpp).
testers:
  - Any OS: Preferences > Performance > Fast Big Brushes (Experimental) on; on a dense model (or Help > Benchmark,
    standard-15m) sculpt with a 200-300 px Clay or Standard brush across the face, then the same with it off: the
    result should look the same; the shading under the brush should be smooth while you drag and after you lift.
  - Any OS, with it on: X symmetry on, a big brush back and forth across the middle of the face: no seam or step at
    the mirror line; undo and redo a big stroke: the surface and its shading come back exactly.
  - Any OS, with it on, integrated graphics first: lifting the pen after a long big-brush stroke may finish a little
    later (the exact shading lands then); say if it feels like a hitch.
---
Off by default for 0.11.0-beta.1 while we tune it (perf/beta-boost REPORT.md).
