---
fixed: **Inflate on a Cylinder Part** (and any thin round form with flat or beveled ends): a brush wider than the shape took the beveled end rings for a packed grid and moved the whole dab along one direction, so strokes bent the tube sideways into S-shapes and lumps instead of swelling it evenly, worst with X symmetry on. Each point now moves along its own normal; the one-direction rule stays for grooves cut by Crease or Pinch, where the surface faces one way.
testers:
  - Any OS: New > Cylinder (Library), X symmetry on, Inflate, Size about three times the tube's width, stroke along the tube a few times: it thickens evenly, no S-bend or lump. Then Crease a groove and Inflate across it: no tearing, as before.
---
Reviewers: Sculpt.cpp dabSingle (mean normal of the dab's sample under 0.5 keeps the per-vertex rule; groove dabs measure 0.72 to 0.98, wrapped-tube dabs 0.02 to 0.25); tests/test_inflate_cylinder.cpp.
