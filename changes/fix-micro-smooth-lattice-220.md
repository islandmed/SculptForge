---
whats_new: **Smooth on a Micro level**: a big Smooth no longer leaves a diamond lattice of fine creases (after a big Flatten, for example).
fixed: **Smooth and Smooth Peaks on a Micro level with a big brush** left a regular lattice of fine creases along the
  micro triangles (most visible after a big Flatten). The brush now smooths every point of the level, as a small brush
  and a plain mesh do. A big Smooth on a Micro level now works at the level's own fine scale, like a plain mesh: it
  also smooths fine detail under the brush, which it used to leave as it was, while big forms smooth as on a plain mesh.
  Fixes #220.
testers:
  - New Sphere, Geometry > Add Micro (4); Flatten at about 240 px, Intensity 50, two dabs side by side; then Smooth at
    240 px over them, a few passes. Zoom in: the flattened area is smooth, no criss-cross lattice.
  - Same with Smooth Peaks; and a big Smooth over sculpted detail on a Micro level still feels as before (no lag).
---
Kernel change in core/MicroSculpt.cpp only (dab record format untouched). Test: test_micro_smooth.cpp (GeomInvariants microUmbrella).
