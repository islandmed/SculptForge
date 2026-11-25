---
whats_new: **Holes while turning a heavy Part you moved**: on a dense Part (several million triangles) that had been pushed a long way with Move or the like, turning the view could show blocky holes in the surface. The pushed areas now draw in full detail while you turn.
fixed: Holes in a dense Part while the view turned, after big Move strokes: the lighter levels the view draws while it moves kept the shape the Part had when they were built, so a long push stretched and folded them and the folds were culled. Clusters pushed more than a few cells from where the levels were built, and their neighbours, now draw in full detail (more triangles in a moving frame until the levels are rebuilt).
testers:
  - A Part of 5M+ triangles: push an arm out with Move (big and small brush), then orbit: no blocky holes, same surface as when you stop. With a garment Part under the skin the garment must not show through.
  - Same Part, orbit before and after the pushes: the frame rate stays close (the pushed areas draw in full).
---
Stopgap: stale clusters draw in full (Clusters.h CoarseLevels::stale / unsafe). A background rebuild of their levels follows as its own change.
