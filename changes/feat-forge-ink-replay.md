---
added: **Forge Ink** editable strokes, core (not switched on in the app yet): every stroke on a Micro level can keep the
  dabs it was made of, so it can be edited or removed later and the strokes on top of it are re-run exactly as they
  were (Smooth, Clay and the other surface-reading brushes included). A stroke stays editable until 20 overlapping
  strokes sit on it, then it is baked in. `sf_ink_replay_bench` measures an edit against how deep the stroke is.
---
Core + tests + bench; the default sculpt path, undo, saving and the stroke editing UI follow on their own branches.
