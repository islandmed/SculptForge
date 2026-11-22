---
fixed: **Inflate** with a brush much wider than a thin tube swells the tube evenly again instead of bending it into S-shapes
  and lumps: after a bulb, the thin tube beside it no longer counts as a packed groove, so each vertex keeps moving along its
  own normal (with or without X symmetry, at any scene scale or mesh density). A groove cut by Crease or Pinch still swells
  whole, without tearing.
testers:
  - Any OS: Library > Cylinder, scale it long and thin (Transform), X symmetry on. Inflate a bulb with a small brush, then
    stroke along the tube with a circle many times wider than the tube: it thickens evenly and its axis stays straight.
  - Crease a groove across the same tube, then Inflate across it with a small and then a wide brush, stacked: no dark specks,
    needles or torn triangles along the groove.
---
Residual of fix/inflate-cylinder. Test: tests/test_inflate_wide_brush.cpp. Known limit: at intensity 50 a brush 8 tube radii
wide with X symmetry on the 49k stretched Cylinder still takes the area normal on some dabs (own normals would fold the pulled
cap there).
