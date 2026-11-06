---
whats_new: **Paint stays while the view moves**: on dense meshes, vertex paint, paint layers and the mask no longer
  vanish while you orbit or paint and pop back at rest.
fixed: Dense meshes over the triangle budget lost their vertex colour (paint layers included) and mask while the view
  moved (the coarse level 0.11.7 draws then had none) and showed them again at rest. The coarse level now carries each
  cell's mean colour and mask from the full mesh, updated when a paint stroke ends.
testers:
  - Windows or macOS: open a 10M+ triangle scan, Paint > Fill a colour, then orbit: the colour stays while moving.
  - Paint a stroke, then orbit at once: the stroke's colour shows while moving (it may draw in full for the first frames).
  - Mask part of it (Ctrl-drag), orbit: the masked area stays dark while moving.
---
Face groups and textures already drew in full detail while moving (per-triangle data never takes the coarse path).
