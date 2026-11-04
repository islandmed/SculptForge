---
whats_new: **Faster Voxel Remesh**: **Tool > Remesh > Voxel Remesh** is about 3-4 times faster (a 500,000-triangle
  sculpt at the default resolution in about a third of a second) and runs in the background with a progress bar and
  Cancel, so the app never freezes while it works.
highlight: Voxel Remesh 3-4x faster, never freezes the app
changed: **Voxel Remesh** is 3-4 times faster with the same result (Discord, ThinkingPolygons and GABO): every stage
  runs on all cores, the distance to the old surface is only computed in a narrow band around it, and the flood fills
  work a run at a time. A 500,000-triangle sculpt at the default resolution 200 takes about 0.3 s instead of 1.1 s;
  2 million triangles about 0.6 s instead of 2 s; resolution 400 about 1.1 s instead of 6 s (10-core laptop).
fixed: Voxel Remesh no longer freezes the app: it always runs in the background with a progress bar and Cancel (before,
  only when keeping a painted texture). Smoothing a remeshed surface and carrying mask and colour over also run in
  parallel.
testers:
  - Windows: Tool > Remesh > Voxel Remesh on a sculpt of 500k triangles or more at resolution 200; it finishes in
    about a second or less, the viewport keeps turning while it runs, and Undo brings the old mesh back.
  - Any OS: resolution 400 on a dense sculpt, press Cancel while the bar moves; the Part stays as it was.
  - Two overlapping spheres (Append), Voxel Remesh: one closed surface, the same look as in 0.10.10.
---
Branch fix/0.10.11-voxel. Output is bit-identical to 0.10.10 (positions, triangles and mask compared on 100k and 500k
triangles at resolutions 200 and 400); numbers and method in REPORT.md.
