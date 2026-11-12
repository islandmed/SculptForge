---
whats_new: **Voxel Remesh keeps your sculpt smooth**: Clay and other strokes no longer come out grainy after a remesh, and face groups now come through it.
fixed: **Voxel Remesh** (Tool > Remesh, #193): areas laid in with Clay came out grainy. The tiny facets a stroke leaves in the mesh
  (invisible under smooth shading) were sampled by the grid and showed up as blotches; the distance field is now low-passed
  where the surface is smooth, keeping tips, creases and thin parts sharp, and the volume is held (0.02% or less). Face groups
  are carried to the new surface as well (they used to reset); mask and vertex paint already were.
testers:
  - Any OS: sculpt a few overlapping Clay strokes (Dynamic on), zoom in, Tool > Remesh at the default; the stroked area looks as smooth as before the remesh.
  - Any OS: give two halves of a Part different face groups (Groups view), Remesh: the halves keep their groups.
---
Resolutions above 512 use the brick grid, which has no low-pass: its cells are already finer than the facets.
