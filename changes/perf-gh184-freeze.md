---
fixed: **Freezes at the start and end of strokes on a dense streamed Micro level** (#184, thanks @HG4Science): letting go
  of a stroke held the window for up to a second or more while its undo step was packed, the first Draw stroke on a new
  Micro level froze for one to two seconds while every tile's shading was redone, and with Tool > Geometry open every
  stroke re-counted Merge by Distance over the whole base mesh. The undo step is now packed in the background (Undo
  right after still restores it exactly), the shading catches up tile by tile within the frame's budget, and Merge by
  Distance counts again only when the base mesh changes.
fixed: After a long stroke on a streamed Micro level, the edited detail leaving memory no longer stalls the next frames
  for seconds on lower-spec computers: it goes a little each frame (#184).
fixed: Big brushes on a high Micro level lag less: each dab no longer reads and writes the whole of every triangle it
  touches, only the part under the brush (#184).
fixed: No pause in the middle of a long stroke on a streamed Micro level when the stroke grows the level's height
  range: the work is spread over two frames (#184).
---
Reviewers: MicroStroke::end keeps the Morton order and Forge Ink hashes on the main thread and hands the change and its
packing to a background thread (MicroUndo::pending; settle() waits in swapWith and the Timeline sidecar; bytes() counts the
unpacked size meanwhile; PoolInline keeps the work off the shared pool). MicroMesh::widen: FocusDetail::rescale no longer
marks everything dirty; MicroRenderer re-sends every tile's heights (MicroFocus::resendHeights) and a budgeted sweep
re-shades the coarse tiles when the queue is empty, within its budget. UndoStack::baseRevision skips micro-only
steps (Merge by Distance / Find Problems key, the size bounds). FocusDetail::update demotes within its time budget (a batch
at least), and not while the view moves unless over twice the budget (checked before the LRU list is built). Tests: tests/test_micro_freeze.cpp, focus_detail_demotions_over_budget_spread_over_frames,
tests/ui/micro_widen_keeps_shading.sfui (view_stale 0 once idle against a full re-shade).
Brush lag: MicroStroke::dab's plane samples at the kernel's stride while thousands of points stay under the brush
(planeSamples, else every second point as before; micro_big_plane_brush_on_a_coarse_plane_stride_matches), and
interpolateCoarse skips coarse cells whose corners are all 0 (bit-identical to the old per-point loop).
Widen mid-stroke: the frame a dab widens ends its dabs and MicroRenderer::sync keeps the GPU's old heights and step for it
(rescaleDeferred_); the next frame re-sends them and places no dabs (App::drainStrokeDabs, budgeted only). MicroStroke::widenAhead
widens at a frame's start when two of the last dab's additions would pass the range; the undo copies rescale on the pool.
Test: tests/ui/micro_widen_live_stroke.sfui (interactive strokes; micro_widen_deferrals, view_stale 0 once idle).
