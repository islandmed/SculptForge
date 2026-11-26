---
whats_new: **Voxel Remesh** now follows deep, narrow creases (wrinkles, eye bags, folds) all the way down, and a crease's two sides are two clean sheets, so the speckled rim of extra voxels along it is gone.
fixed: **Voxel Remesh filled narrow creases and left speckled bits along them** (#240). On a sculpt with deep, narrow
  creases (face wrinkles, eye bags, nasolabial folds, especially a decimated mesh), the remesh rebuilt a crease only down
  to where its two walls came within about four voxels of each other, then filled the rest and left stray sheets and
  one-voxel bits along it, at any Resolution. The remesh now follows the crease on down to where it is under a voxel wide.
  On a test slab with V-shaped creases 24 voxels deep it now reaches the tip at Resolution 200 (it stopped 4 to 24 voxels
  short before) and makes one clean surface (it left up to 85 loose pieces), and a smooth or bumpy surface comes out
  as before. With the crease cleaning of 0.14.1, the test sphere with creases 1.6 voxels wide at the default
  Resolution (200) measures: depth variation along the crease 40% lower than without the cleaning, its sides tilted 44 to
  24 degrees off, its cross-section 41% closer to the original. Where two sides of a crease or a closed lip line touch, the
  surface is now two sheets instead of one pinched seam (the starter head at Resolution 200 and 400 had 20 and 6 pinched
  edges, now none), and the loose voxels that seam used to hide along a rim are dropped: on a head with deep creases at
  Resolution 752, points of the result more than a voxel off the original along the face went from 1311 to 368. Reported
  by @nino3thd.
testers:
  - Any OS: take a dense head with deep wrinkles (carve lines with Detail Crease, or import one), Decimate it, then Tool >
    Remesh > Voxel Remesh at the default Resolution and at a higher one. Close up on the eye bags and the folds beside the nose:
    the creases are continuous and no speckled rim or loose bits sit along them.
  - Windows: the same at Resolution 752 or higher (an imported, decimated head): the eye bags and nasolabial folds have no speckled
    rim; the closed lips of the starter head stay one seam, no chips along it.
  - Remesh two overlapping spheres (or any sculpt built from overlapping blobs): still one closed surface, no slits at the
    seam where they cross.
  - Remesh a smooth sphere and a bumpy sculpt: the result looks as it did in the previous version.
---
Cause: the wall closing (the shell dilated by a voxel before the flood from outside) seals a crease under about four
voxels wide, so the flood never enters and its air counts as solid; the sign pass only trusted the nearest triangle
within two samples of the outside, so below the crease mouth everything stayed solid and the surface between the two
came out as sheets. Fix (core/Voxel.cpp, core/VoxelSparse.cpp, both grids): the nearest triangle decides the sign of every
solid sample within 2.5 samples of the surface too, and a probe that lands on a gap's far wall (the surface nearest the probe
has the sample on its outer side) is not a surface buried in another part. Specks of under 64 triangles or under 64
voxels of volume are dropped when a sealed crease was opened or a cell was split (a groove tip under a voxel wide, the loose
bits along a rim). Surface nets (core/SurfaceNetsCell.h, both grids) now makes one vertex per connected piece of a cell's
surface instead of one per cell: a cell with two inside corners opposite on a face (two walls touching along a line, a
1-3 voxel slit) used to weld its walls at one vertex and give an edge with 4 faces; now they are two sheets that touch.
Tests: tests/test_voxel_remesh_sign.cpp (slabs at 100, 200 and 752; starter head non-manifold edges).
