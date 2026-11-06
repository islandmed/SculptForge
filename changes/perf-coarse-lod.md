---
whats_new: **Smoother view on very dense meshes**: a scan or import with more triangles than your computer draws comfortably now draws far parts in lighter versions of themselves, sharp where you look and full detail as soon as it all fits. Orbiting a 28-million-triangle scan on an entry-level laptop goes from about 70 to about 170 frames a second.
highlight: Dense scans orbit smoothly on entry-level machines
added: Coarse levels for dense meshes: a plain mesh over the hardware tier's triangle budget draws each part of it at a
  lighter level picked from its size on screen (under a pixel of error at rest), so a frame stays within the budget;
  full detail when it all fits. Sculpting keeps the edited area in full detail and updates its levels after the stroke.
  `SF_COARSE_LOD=0` turns it off.
testers:
  - Open a scan of 20M+ triangles (e.g. a Stanford-style statue): orbit; it should stay smooth, and when you stop the view should look the same as before (no holes, no seams).
  - Sculpt on it: the stroke area stays sharp; after the stroke, orbit again and the stroke is still there from afar.
  - Windows 8 GB laptop: open the same scan; the app should not use noticeably more memory than 0.11.4.
---
Design: docs/design/micro-coarse-lod.md (clean-room rules; pre-2000 sources). Micro levels are not covered yet.
