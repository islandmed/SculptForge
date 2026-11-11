---
whats_new: **Faster Undo and Redo on dense models**: Ctrl+Z / Ctrl+Shift+Z of a brush stroke on a model of several million
  triangles no longer stalls for half a second; it takes a few milliseconds. Thanks @TheLycanFenrir and @Devon0209 (#181).
fixed: **Undo / Redo of a stroke on a dense model (#181).** Every Undo and Redo made the viewport send the whole model to
  the graphics card again and rebuild its distance-view cache, so the time grew with the model instead of the stroke
  (5.4M triangles with 3 subdivision levels: 470-600 ms per Ctrl+Z, now 20-30 ms in the frame; the upload itself 500 ms,
  now 4-8 ms). Only the vertices the stroke changed are sent now. Thanks @TheLycanFenrir and @Devon0209 for the reports.
testers:
  - A model of 3M+ triangles (or 2-3 Divide levels on a 80k base): three strokes, then Ctrl+Z three times and Ctrl+Shift+Z
    three times; each step is instant and the surface returns exactly.
---
Found with SF_SYNC_LOG on the floor-tier laptop (5.4M triangles, 2.95M vertices, benchmark take tests/takes/undo_levels_stroke.sfui):
App::runCommand cleared the LODs after Undo / Redo and that reset the renderer's shown mesh, so the next sync was a full
upload (2.95M slots, 500-640 ms, of which the proxy rebuild 470-520) even with no LOD. Now ranged (31-37 runs, about 190k slots,
4-8 ms). The core undo was already O(touched): 1.5 ms, step 360 KB for 23k vertices. tests/ui/undo_sends_only_the_stroke.sfui
(Renderer::fullUploads), tests/test_undo_dense.cpp.
