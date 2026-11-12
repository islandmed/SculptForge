---
whats_new: **Clay:** the live stroke preview now matches the final result (it was drawn too strong).
fixed: **Clay and Clay Block previews drew too much while you sculpted** (below the default Intensity, up to 1.4x the final pull): the stroke changed when the result caught up. The live preview now uses the same pull, plane and tip measure as the final result.
testers:
  - Any OS: New sphere, Divide until the mesh is dense (7+ levels), Clay at Intensity 35; drag a fast stroke. The shape should not change when you lift the pen.
---
Tests: tests/test_ghost.cpp ghost_matches_solver_per_vertex_at_every_intensity (every modelled brush, Intensity 0.1/0.35/1.0, pressure 1 and 0.6, bumpy sphere): red before for Clay and Clay Block only.
