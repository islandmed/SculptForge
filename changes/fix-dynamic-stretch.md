---
whats_new: **Dynamic and Move**: a stretch made with Move now gets the triangles it needs when you let go, so pulled shapes stay smooth instead of turning into a few long triangles.
fixed: **Dynamic topology and Move**: Move did not remesh, so a stretched area kept its coarse triangles (a pulled
  horn was a few long triangles). At the end of the stroke the moved region is split towards the Detail's edge
  length (splits only, finer triangles stay; with symmetry, mirrored; one undo step). Parts with subdivision levels
  still skip Dynamic. The UV notice after a remesh now says when the Part's texture stops showing.
testers:
  - Any OS: New sphere, Dynamic on, Wire on, Move: pull a horn out. On release the horn's triangles get smaller; Ctrl+Z puts the coarse sphere back in one step.
  - Windows: same on a Part you remeshed earlier (Detail 20): nothing already finer than the Detail gets coarser.
---
Move was left out of Dynamic's per-dab remesh because it drags a fixed set of vertices; Sculptor::refineGrab now
refines that set's bounding sphere at the stroke end (DynamicTopology::refineOnly: no collapses).
