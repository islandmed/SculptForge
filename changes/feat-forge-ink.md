---
added: **Forge Ink** engine (no tool in the app yet): relief strokes that stay editable after they are painted. Each
  stroke keeps its path, width, height, profile and operator (add, raise to, carve), and an edit re-evaluates only the
  surface it touches: about 1.6 ms per edit on a 200-million-triangle Micro level with 4 threads. `sf_ink_micro_bench`
  measures it on the core-200m head.
---
Engine, tests and bench only; the artist-facing tool follows on its own branch.
