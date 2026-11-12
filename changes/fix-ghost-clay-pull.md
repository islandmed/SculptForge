---
whats_new: **Clay:** the live stroke preview now matches the final result (it was drawn too strong).
fixed: **The Clay preview drew too much while you sculpted** (below the default Intensity, up to 1.4x the final pull): the stroke changed when the result caught up. The live preview now uses the same pull and plane as the final result. Clay Block's preview now follows its square tip and pull too, so it changes less when the result catches up.
testers:
  - Any OS: New sphere, Divide until the mesh is dense (7+ levels), Clay at Intensity 35; drag a fast stroke. The shape should not change when you lift the pen.
---
Tests: tests/test_ghost.cpp ghost_matches_solver_per_vertex_at_every_intensity (every modelled brush, Intensity 0.1/0.35/1.0, pressure 1 and 0.6, bumpy sphere): red before for Clay and Clay Block only; ghost_matches_a_stroke_of_overlapping_dabs: a run of dabs composes as the solver's. App probe (SF_MEASURE_POP, 1.3M sphere, 2 s circle at 80 px; SF_MEASURE_STRENGTH sets Intensity), view change when the solver catches up, before -> after: Clay 0.088 -> 0.015 (Intensity 35: 0.005), Clay Block 0.49 -> 0.33 (35: 0.16); what is left in Clay Block is the estimated plane of dabs not placed yet.
