---
fixed: **Inflate on a Cylinder Part** (and any thin round form with flat or beveled ends): a brush wider than the shape took the beveled end rings for a packed grid and moved the whole dab along one direction, so strokes bent the tube sideways into S-shapes and lumps instead of swelling it evenly, worst with X symmetry on. Each point now follows its own normal; grooves cut by Crease or Pinch still swell whole, also across a tube.
testers:
  - Any OS: New > Cylinder (Library), X symmetry on, Inflate, Size about three times the tube's width, stroke along the tube a few times: it thickens evenly, no S-bend or lump. Then Crease a groove across the tube and Inflate across it (Size 2 to 5 times the width): the groove stays whole, no rip.
---
Reviewers: Sculpt.cpp dabSingle (a packed dab: mean normal >= 0.7 keeps the one area normal, <= 0.4 uses the normals smoothed over the dab, blended between; a hard 0.5 gate tore a groove across a tube, artist-qa 11-21); tests/test_inflate_cylinder.cpp (shares along the own normals; groove across the stretched Cylinder: 0 flipped).
