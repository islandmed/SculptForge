---
fixed: **Big brushes on scans**: a vertex whose faces have no area (a collapsed sliver in a scan) kept the shading normal a stroke gave it, also after undo; the normal is now always the one its faces give (the fallback when they have none). Undo gives the mesh back exactly, normals included, and the same strokes from the same mesh come out identical on meshes of tens of millions of vertices.
testers:
  - Open a dense scan (10M+ vertices), draw one stroke with a 200+ px brush over most of it, undo: the surface and its shading are as before.
---
Reviewers: found by rnd-9a / artist-qa-3c on a 28M-vertex scan (--measure-stroke 3,240, SF_MEASURE_VERIFY=1): positions are byte-equal after undo (never data loss); the
replay differences were 1-ulp roundings at positions near 800 units, from one vertex's stale normal in the area-plane sums. Mesh::finishNormals now writes the same fallback as recomputeAllNormals.
