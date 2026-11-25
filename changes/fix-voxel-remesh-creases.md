---
whats_new: **Voxel Remesh** now follows deep, narrow creases (wrinkles, eye bags, folds) all the way down instead of filling them in with a ridge of extra voxels.
fixed: **Voxel Remesh filled narrow creases and left speckled bits along them** (#240). On a sculpt with deep, narrow
  creases (face wrinkles, eye bags, nasolabial folds, especially a decimated mesh), the remesh rebuilt a crease only down
  to where its two walls came within about four voxels of each other, then filled the rest and left stray sheets and
  one-voxel bits along it, at any Resolution. The remesh now follows the crease on down to where it is under a voxel wide.
  On a test slab with V-shaped creases 24 voxels deep it now reaches the tip at Resolution 200 (it stopped 4 to 24 voxels
  short before) and makes one clean surface (it left up to 85 loose pieces), and a smooth or bumpy surface comes out
  as before. With the crease cleaning of the same release, the test sphere with creases 1.6 voxels wide at the default
  Resolution (200) measures: depth variation along the crease 40% lower than without the cleaning, its sides tilted 44 to
  24 degrees off, its cross-section 41% closer to the original. Reported by @nino3thd.
testers:
  - Any OS: take a dense head with deep wrinkles (carve lines with Detail Crease, or import one), Decimate it, then Tool >
    Remesh > Voxel Remesh at the default Resolution and at a higher one. Close up on the eye bags and the folds beside the nose:
    the creases are continuous and no speckled rim or loose bits sit along them.
  - Remesh two overlapping spheres (or any sculpt built from overlapping blobs): still one closed surface, no slits at the
    seam where they cross.
  - Remesh a smooth sphere and a bumpy sculpt: the result looks as it did in the previous version.
---
Cause: the wall closing (the shell dilated by a voxel before the flood from outside) seals a crease under about four
voxels wide, so the flood never enters and its air counts as solid; the sign pass only trusted the nearest triangle
within two samples of the outside, so below the crease mouth everything stayed solid and the surface between the two
came out as sheets. Fix (core/Voxel.cpp, core/VoxelSparse.cpp, both grids): the nearest triangle decides the sign of every
solid sample within 2.5 samples of the surface too, and a probe that lands on a gap's far wall (the surface nearest the probe
has the sample on its outer side) is not a surface buried in another part. Specks of under 64 triangles are dropped when
a sealed crease was opened (a groove tip under a voxel wide). Test: tests/test_voxel_remesh_sign.cpp.
