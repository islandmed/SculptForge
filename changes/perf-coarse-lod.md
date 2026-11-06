---
whats_new: **Smoother view on very dense meshes**: a scan or import with more triangles than your computer draws comfortably now draws a lighter version of itself while you turn the view, and the real detail as soon as the view rests. Parts you sculpt always show in full detail. Orbiting a 28-million-triangle scan on an entry-level laptop goes from about 70 to about 180 frames a second.
highlight: Dense scans orbit smoothly on entry-level machines
added: Coarse levels for dense meshes: a plain mesh over the hardware tier's triangle budget draws all of it at one
  lighter level while the view moves (the finest that fits the budget), and in full detail once the view has rested
  for 150 ms, when one full frame fits the still-frame budget (100 ms); otherwise it stays light and the info line says
  **Preview detail**. A stroke draws the mesh in full detail; its edits reach the lighter level right after it.
  `SF_COARSE_LOD=0` turns it off.
testers:
  - Open a scan of 20M+ triangles (e.g. a Stanford-style statue): orbit; it should stay smooth. Stop: within a moment the view shows the full detail (no holes, no seams, no jump you notice).
  - Sculpt on it: the stroke draws in full detail; orbit right after it and the stroke is there from every side.
  - On a low-end machine (integrated graphics), a very dense scan may stay light at rest: the info line then says Preview detail.
  - Windows 8 GB laptop: open the same scan; the app should not use noticeably more memory than 0.11.6.
---
Design: docs/design/micro-coarse-lod.md (clean-room rules; pre-2000 sources). Micro levels are not covered yet.
