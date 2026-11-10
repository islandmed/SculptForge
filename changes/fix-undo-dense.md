---
whats_new: **Faster Undo after a remesh**: undoing or redoing a Voxel Remesh (or another whole-model step) on a very dense
  model no longer stops for a second while the model's search tree is rebuilt. Thanks @TheLycanFenrir (#181).
fixed: **Undo / Redo of a whole-model step on a very dense mesh (#181).** A step that replaces the mesh (Voxel Remesh and
  the other tools that remesh) now keeps the old mesh's BVH with it, so undo and redo swap the two instead of building a
  tree again (0.7 s of a 12M-triangle undo, now 0 ms; the tree isn't counted against the undo byte budget, so it doesn't
  push the remesh step out of the history). Stroke undo and redo were already O(touched vertices); tests now hold that
  (time and step size). Thanks @TheLycanFenrir for the report.
testers:
  - A model of 8M+ triangles (Voxel Remesh at a high resolution): Ctrl+Z the remesh, then Ctrl+Shift+Z; the model switches
    back and forth without a long freeze, and picking / sculpting right after works on the right surface.
  - On the same model, a few strokes and Ctrl+Z each: instant, and the surface returns exactly.
---
Measured on the floor-tier laptop (aarch64, core only; the GPU upload after a whole-mesh step is unchanged): 4M / 8M / 12M triangles,
remesh undo 217 / 439 / 707 ms before, 0 ms after; stroke undo 0.6 / 1.7 / 1.9 ms (50k touched vertices) and 163 ms for a
2.6M-vertex stroke; UploadOrder::build (CPU part of the full upload) 118 ms at 12M. tests/test_undo_dense.cpp.
