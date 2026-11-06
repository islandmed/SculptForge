---
whats_new: **Reconstruct Subdiv** works on open meshes such as UV-tiled panels, and the lower level keeps each face's UVs.
fixed: **Reconstruct Subdiv** (Tool > Geometry) on open meshes (a border, e.g. a UDIM-tiled panel) no longer reads
  the mesh's corners and face centres the wrong way round, which failed with "a face point joins 1 quads" or gave a
  wrong lower level; the lower level keeps every face's UVs across seams and UDIM tiles, and stepping back up gives
  back the imported UVs exactly (#145, thanks @RodTDai).
testers:
  - Import a subdivided mesh with UDIM UVs and an open border, Tool > Geometry > Reconstruct Subdiv: the lower level's UV islands stay in their tiles; step up, the UVs are the imported ones.
---
Regression test: multires_reconstruct_keeps_udim_uvs (tests/test_multires.cpp).
