---
whats_new: **Make sculptable** (Render > Gaussian Splats): smooth curves such as a forehead come out smooth, without contour rings.
fixed: **Make sculptable**: the scan surface follows the splat density between voxels, so gentle curves no longer show
  voxel terraces (contour rings) at any Detail; floaters, filled holes and closed gaps behave as before.
testers:
  - Any OS: load a splat head capture (or Render > Gaussian Splats > Try a test head), turn on Review, Make sculptable; the forehead and cheeks of the scan are smooth, no rings.
---
