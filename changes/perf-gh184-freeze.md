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
Widen mid-stroke: a widen that takes its frame's dab budget (MicroMesh::widenMs; SF_WIDEN_SPLIT_MS overrides) ends its frame's
dabs and MicroRenderer::sync keeps the GPU's old heights and step for it (resendLater); the next frame re-sends them and places
no dabs (App::drainStrokeDabs, budgeted only). A quicker widen re-sends in its own frame as before. MicroStroke::widenAhead
widens at a frame's start when two of the last dab's additions would pass the range; the undo copies rescale on the pool.
Test: tests/ui/micro_widen_live_stroke.sfui (interactive strokes; micro_widen_deferrals, view_stale 0 once idle).
Re-land for 0.13.13 (with fix/gh184-reshade-win, both reverted from 0.13.12): a stroke's own release no longer counts as a base
moved outside a stroke (MicroMesh::strokeEnded, skipped by MicroFocus shadeAll and MicroRenderer's budget-0 rule; that whole-shade
on a big Clay stroke's release doubled peak RAM at random on core-400m..800m with a 1-2 s stall); budget-0 shadePending runs in
16k-patch slices and drops staging over two slices; App::wantsFrames counts Renderer::microShadingWantsFrame so a plain micro
level's backlog drains with no input. Tests: micro_freeze_stroke_release_base_move_is_the_strokes, tests/ui/micro_shade_rest.sfui.
Focus Detail memory (re-land): reshade-win's shadeAll took the whole coarse queue at once on Undo/Redo/Move, and the coarse
MicroShader's partial sums, shadeStaging_ and the base-sized coarseShade_ copy stayed at that size (streamed lion +1.8 GB over
0.13.12). MicroFocus::shadeCoarse now shades in 16k-tile slices straight into slot-ordered staging (shadeLevel's prepare hook;
coarseShade_ removed), sendPages is sliced the same way (a widen's re-send), and MicroShader/MicroFocus::trimScratch free big-batch
scratch afterwards. Streamed lion undo/redo/move session peak 5385 -> 3146 MB (0.13.12: 3554).
MicroRenderer::shadePending holds its staging through a budgeted catch-up (freed once the backlog is empty or after an unbudgeted sync;
freed every drain frame it was allocated again each time) and slices the stale pass too: lucy-112m without Focus Detail (Windows' path),
worst frame per 2 s after two 240 px strokes 84 84 91 62 62 -> 61 56 64 62 16 ms (0.13.12: 45 56 70 16 12).
