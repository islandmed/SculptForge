---
whats_new: Micro levels on very dense models orbit smoothly: while the view moves, a lighter version of the surface is drawn, and the full detail returns as soon as you stop.
changed: Micro levels on dense bases (a scan of tens of millions of triangles with a micro level) no longer draw the
  whole base every frame: while the view moves, or when the frame can't hold it, they draw coarse levels of their
  sculpted surface within the hardware tier's triangle budget, as plain meshes already do. At rest the full micro
  detail comes back when a frame of it fits; otherwise the info line says "Preview detail". A stroke draws the full
  micro level, and the coarse levels follow when it ends. The mask shows on the coarse levels too.
testers:
  - A dense scan (10M+ triangles) with Divide to Micro: orbit; it stays smooth, and the surface looks right while moving (no holes, no dark specks); stop: the full detail comes back within a moment, or the info line says "Preview detail".
  - Same model: a Clay stroke, then orbit: the stroke shows while moving too.
---
Tests: micro_coarse_levels_on_displaced_corners, ui micro_coarse_lod. Numbers (Lucy 112M floor A/B) in REPORT.md.
