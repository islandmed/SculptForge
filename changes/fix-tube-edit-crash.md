---
whats_new: **Tube point edits no longer crash**: moving or resizing the points of a long Tube (many points) closed the
  app; and a Rope or Braid that would be too heavy is drawn with longer edges instead of running away.
fixed: Moving or resizing Tube points (Fixes #215, thanks @maratcgartist) closed the app on a long tube (one whose full
  mesh has over 60,000 triangles): while a point is dragged the Part shows a lighter preview, and the World Size cursor
  still cast its pick ray into the picking tree of the full mesh, reading triangles past the end of the preview. A stale
  tree is now skipped and World Size waits for the rebuild. Also, one tube is swept within 2 million triangles (a Rope or
  Braid with a point dragged far could run to tens of millions), a drag or edit that cannot be swept keeps the last
  good shape, and autosave waits while a Tube point is being dragged.
testers:
  - Windows: Draw Tube, 150+ points (a long, kinked stroke); hover the tube and drag points, Ctrl+drag to resize, many times;
    the app stays open and Undo works. Then a Braid with a point dragged far away.
---
Cause (reproduced under ASan, tests/test_tube.cpp tube_invariant_drag_preview_with_the_full_density_tree_is_safe_to_pick):
BVH::raycast walked a tree built for the full mesh against the smaller preview mesh (syncWorldSize picks every frame,
without the bvhDirty check the surface cursor has): heap-buffer-overflow, a segfault with enough triangles. Second, smaller
cause: tubeSweep had no size limit (strands x rings x sides). ASan found nothing else in several thousand random edits.
