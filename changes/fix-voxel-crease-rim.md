---
fixed: **Voxel Remesh left a dotted rim along the edge of a deep crease** (#240). Where a crease was cut deep with repeated Crease strokes,
  the remesh at Resolution 752 put a line of extra voxels along its edge: points that stood out from the original as small bumps in a
  row. A voxel next to the surface that the sign test counted as inside while the triangle under it faced out is now turned out. On the test box with a deep crease, points more than 0.7 voxel off the original along the crease went from 159
  to 0 (more than a voxel: 19 to 0). The rim is a short, faint dashed line now, not a row of dots, and the one-voxel grain along a very sharp crease stays; that is the Resolution's own grain.
  Reported by @nino3thd.
testers:
  - Any OS (the reporter's steps): subdivide a sphere, cut a crease with the Crease brush, repeat the stroke until it is deep, then Tool >
    Remesh > Voxel Remesh at Resolution 752 and at the default: the crease edge has no row of bumps (a faint dashed line and a one-voxel grain can stay).
  - Remesh a smooth sphere, a bumpy sculpt and the starter head: the result looks as it did before.
---
Cause: next to a deep crease, samples inside the solid whose nearest source triangle faces out (they sit in the air of the slot)
had stayed inside: their surface-nets vertices made the dotted rim. Fix (core/Voxel.cpp and core/VoxelSparse.cpp, both grids): after the
sign pass, an inside sample within 3 cells of the surface with at most 3 of its 6 neighbours inside, which the nearest triangle puts
outside, is turned out. Tests: tests/test_voxel_remesh_sign.cpp (exact distances on the repro box: 0 over 0.7 voxel off).
