---
changed: Help > Benchmark: stroke phases also report `lag_on_model_ms` / `lag_on_model_px` (the lag over frames whose
  newest dab reached the surface) and `dabs_off_model`; the stroke path is fitted to the model on screen (at most 2% of
  it off the model, at the same pointer speed), and each dab's lag is matched to its own lap of the path. Small-brush lag
  is now measured on the surface; earlier results read low where the path left the model (#184).
changed: The benchmark results list shows only on-model lag, and none for results from builds without the off-model
  count (#184).
---
Reviewers: bench-only. A dab off the model costs nothing, so the frames after it are quick and their lag small; on the
floor 1B "clay large" phase that alone flipped lag_ms p50 between 24 and 370 ms run to run (same dab work in v0.13.9 and
v0.13.10), and 77-90% of every scene's 25 px dabs missed the model. Probe: App::strokeHitProbe_ / DabView::hitProbe
beside strokeProbe_, set in dabAt. Matcher: core/StrokePath.h dabSampleIndex (tests/test_stroke_path.cpp).
