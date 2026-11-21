---
changed: Help > Benchmark: stroke phases also report `lag_on_model_ms` (the lag over frames whose newest dab reached
  the surface) and `dabs_off_model`; `lag_ms` is unchanged (#184).
---
Reviewers: bench-only. A dab off the model costs nothing, so the frames after it are quick and their lag small; on the
floor 1B "clay large" phase that alone flipped lag_ms p50 between 24 and 370 ms run to run (same dab work in v0.13.9 and
v0.13.10). Probe: App::strokeHitProbe_ / DabView::hitProbe beside strokeProbe_, set in dabAt.
