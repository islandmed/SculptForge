---
added: **Benchmark takes** (`sculptforge --benchmark-script take.sfui [project]`): a UI test script's strokes, orbits,
  keys and waits played in real time (wall-clock pacing, 240 Hz stroke samples) in the interactive loop, with the
  benchmark HUD, the benchmark video path and a per-frame CSV (frame ms, stroke lag) beside it. Steps a take can't
  play in real time (shots, expectations) are skipped; steps that run commands or read other files are refused.
  docs/benchmark.md.
---
Dev tooling (creator-kit performance clips), so no What's New. The benchmark video's encoder and pixel-buffer ring
moved from ProofMode.cpp to BenchVideo.cpp unchanged, except that finishing now flushes the last frames still in the
ring (the clip runs to the last frame shown). Test: bench_take_sphere.
