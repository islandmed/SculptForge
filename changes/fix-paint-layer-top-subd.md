---
whats_new: **Full detail after a new paint layer**: on dense meshes, adding a paint layer no longer leaves the top
  subdivision level looking unsubdivided until a restart.
fixed: Adding a paint layer on a dense top subdivision level (4M+ triangles) could leave the model drawn at its coarse
  preview level at rest, looking like level 1, through level changes and project reloads until a restart (#151, thanks
  @fxrtst). The GPU time used to decide whether full detail fits a still frame no longer counts the CPU work of the
  frame that added the layer, and one slow frame can no longer tip it.
testers:
  - Windows: a 4M+ triangle mesh with 3 subdivision levels, on the top level open Tool > Paint Layers > New. Let the
    view rest: full detail (wrinkles sharp), not the blurred preview; the status bar doesn't say "Preview detail".
  - Lower Level, Higher Level, and reopen the project from the Library: full detail at rest each time.
---
Cause: the frame governor's GPU timer brackets the whole frame, so the ~400 ms the CPU spent on the add (paint stack,
colour upload, every cluster's coarse levels) counted as 3D-view GPU time over the 4.28M triangles drawn in full that
frame. The per-triangle estimate (renderer-wide, so it outlived reloads) then put a full still frame over its 100 ms
budget ("Preview detail"), and coarse frames never measure the full mesh to bring it down again.
