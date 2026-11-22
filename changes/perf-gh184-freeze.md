---
fixed: **Freezes at the start and end of strokes on a dense streamed Micro level** (#184, thanks @HG4Science): letting go
  of a stroke held the window for up to a second or more while its undo step was packed, the first Draw stroke on a new
  Micro level froze for one to two seconds while every tile's shading was redone, and with Tool > Geometry open every
  stroke re-counted Merge by Distance over the whole base mesh. The undo step is now packed in the background (Undo
  right after still restores it exactly), the shading catches up tile by tile within the frame's budget, and Merge by
  Distance counts again only when the base mesh changes.
fixed: After a long stroke on a streamed Micro level, the edited detail leaving memory no longer stalls the next frames
  for seconds on lower-spec computers: it goes a little each frame (#184).
fixed: Big brushes on a high Micro level lag less: each dab writes only the part of a triangle under the brush, not the
  whole of every triangle it touches (#184).
---
Reviewers: MicroStroke::end keeps the Morton order and Forge Ink hashes on the main thread and hands the change and its
packing to a background thread (MicroUndo::pending; settle() waits in swapWith and the Timeline sidecar; bytes() counts the
unpacked size meanwhile; PoolInline keeps the work off the shared pool). MicroMesh::widen: FocusDetail::rescale no longer
marks everything dirty; MicroRenderer re-sends every tile's heights (MicroFocus::resendHeights) and a budgeted sweep
re-shades the coarse tiles when the queue is empty, within its budget. UndoStack::baseRevision skips micro-only
steps (Merge by Distance / Find Problems key, the size bounds). FocusDetail::update demotes within its time budget (a batch
at least), and not while the view moves unless over twice the budget (checked before the LRU list is built). Tests: tests/test_micro_freeze.cpp, focus_detail_demotions_over_budget_spread_over_frames,
tests/ui/micro_widen_keeps_shading.sfui (view_stale 0 once idle against a full re-shade).
Brush lag: interpolateCoarse writes coarse cell by coarse cell and skips the cells whose corners are all 0 (bit-identical
to the old per-point loop: the same expression per point, only the write order differs).
