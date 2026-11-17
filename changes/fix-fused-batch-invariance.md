---
fixed: **Fast Big Brushes (Experimental)**: the same stroke could come out slightly different every time (Clay, Standard,
  Flatten, the Crease family), because where the dabs were split into batches depended on how fast the machine was. The
  result is now the same however the dabs are batched: a recorded stroke replays to the identical surface, a dab's plane
  no longer waits on the exact normals catching up, and the dabs of a batch no longer miss vertices near the rim of a
  dab picked before earlier dabs moved the surface. Fast Big Brushes strokes also follow the exact path more closely.
testers:
  - Windows or Linux with a dense head (10M+ triangles): turn on Preferences > Performance > Fast Big Brushes, Clay with a
    big radius, one long stroke; Ctrl+Z; the same stroke again. Both strokes should look the same.
---
tests/test_fused_invariance.cpp: one recorded dab list placed one dab at a time, in batches of 2, 8 and 32, in one batch
and in mixed batches gives the same sha256 of the positions (Standard, Clay, Flatten, Crease, Pinch, Sharp Crease,
symmetry on and off; Clay Block, Crisp Flatten and Smooth, which always take the exact path, too).
