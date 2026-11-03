# 0.10.9 testers: big brushes, Move and memory on dense meshes
- Divide a model to 15-35M triangles (or Help > Benchmark > Standard 33M) and sculpt with a big Clay brush (200+ px, symmetry on): the view should stay smooth and lifting the pen instant; the surface may trail the pen a little on huge brushes. Tell us if any frame freezes for more than a moment.
- Same mesh, Move brush, fast strokes with a pen: should keep up (no multi-second freezes), then orbit right after a long stroke: no hitch.
- With other heavy apps open (little free RAM), run Help > Benchmark: a scene that doesn't fit should be skipped with a "needs ~X GB free" message instead of the system running out of memory.
- On a big mesh, try Subdivide / Divide / Voxel Remesh at a high setting with little free memory: a "Not enough free memory" dialog should ask first; Cancel leaves the mesh, Continue does it.
- Please include your RAM, GPU and OS, and Help > Benchmark's numbers if you ran it.
