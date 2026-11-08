---
whats_new: **Find Problems**: **Fix all** now repairs messy meshes (a rough remesh, touching solids) completely, and anything it can't fix is listed under **Not fixed**, with why.
fixed: **Find Problems > Fix all** left non-manifold edges and opened holes on messy meshes, such as a rough remesh
  (foirgairwolf on Discord). Solids touching along an edge now come apart closed, Fix all repeats until only holes
  are left, the Holes Fix never makes other problems or leaves more holes than it found, and the panel says what
  couldn't be fixed and why. The inner wall of a hollow (or an eye inside a head) is no longer counted as flipped.
testers:
  - Any OS: remesh a thin or spiky model at low resolution, then Tool > Geometry > Find Problems > Check, then Fix all.
    Good: only Holes (or nothing) is left, and a "Not fixed:" line says why the holes stay.
  - Any OS: a hollow shell (an outer and an inner wall): Check finds no flipped triangles.
---
Repro and measurements: 3000 messy meshes (random voxel grids, welded spheres, broken spheres). Before: 422 left
non-manifold edges after Fix all, and Fix all opened 4437 holes. After: none left anything but holes, and 382 holes opened (loose fins).
