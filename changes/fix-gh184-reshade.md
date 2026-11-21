---
fixed: **Undo, Redo and Move on a dense Micro level show patches of stale shading** (#184, thanks @HG4Science): since
  0.13.10 their shading caught up over several frames, the way a long Clay stroke's does, so undone strokes and moved
  areas kept their old shading for up to a few seconds. Undo, Redo and Move now show the new shading on the next frame, on Windows and macOS
  levels as well as streamed ones; only a Clay-type stroke's own shading still catches up within the per-frame budget.
changed: `SF_FRAME_LOG=100` (start SculptForge with it set) now also logs, for a streamed Micro level, where a slow frame's
  Micro work went and how many tiles still wait for shading, to help find rare freezes.
---
Reviewers: MicroFocus::sync and, without Focus Detail (Windows, macOS: Focus Detail is POSIX only; 0.13.11's stage went red on
Windows CI only), MicroRenderer::sync take the whole shading queue when MicroMesh::shadeNow (MicroUndo::swapWith, the end of
a Move) or grabbing (a Move stroke) is set, or the base moved outside a stroke; tests: tests/ui/micro_shade_now.sfui (no Focus
Detail, every platform) and micro_focus_shade_now.sfui (view_stale < 0.02 right after Undo, Redo and a Move),
tests/test_micro_shade_now.cpp (flags; Undo/Redo bit-identical). gh184-freeze edits nearby MicroFocus code.
