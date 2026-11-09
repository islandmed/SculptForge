---
whats_new: **Voxel Remesh up to 4096**: the **Tool > Remesh** Resolution slider now goes to 4096 (two thirds of it
  still covers 32-512), the line under it estimates the triangles it makes, and above 512 it asks once and never
  starts a remesh that won't fit in the free memory.
highlight: Voxel Remesh up to 4096, with a live triangle estimate
added: **Voxel Remesh up to 4096** (foirgairwolf on Discord): **Tool > Remesh > Resolution** and the `remesh` command go
  from 32 to 4096 (was 512). The slider keeps two thirds of its travel for 32-512 and a sixth each for 528-2048 and
  2112-4096; Ctrl+click types a number. Above 512 only the voxels near the surface are stored (8x8x8 bricks), so
  memory grows with the surface, not the volume, and the result is the same surface the full grid would make. A
  sculpted head remeshes at 1024 into about 5.6M triangles in about 3 s, at 2048 into about 22M in about 9 s.
added: **Triangle estimate** under the Voxel Remesh Resolution slider (*About 1.6M triangles*), live as it moves; above
  512 it also shows the memory needed, in red when more than is free.
added: **High resolution remesh** confirmation: the first remesh above 512 shows the estimated triangles and memory
  (Remesh / Cancel); it asks again after the Resolution goes back to 512 or less. A remesh that won't fit in the free
  memory doesn't start (the app and the `remesh` command both say how much it needs); it is cancellable with progress
  throughout.
testers:
  - Any OS, 8 GB computer: Tool > Remesh, drag Resolution to the right end; the line under it turns red and Voxel
    Remesh says how much memory it needs instead of starting. Nothing freezes.
  - Any OS: a sculpted head, Resolution 1024 (Ctrl+click, type 1024): Voxel Remesh asks once with the estimate, then
    remeshes with a progress bar; the result is closed and close to the estimate. Remesh again: no question. Set 512,
    then 1024 again: it asks again.
  - Windows: Resolution 2048 on a head, Voxel Remesh, then Cancel within a few seconds: the Part is unchanged.
---
Core: src/core/VoxelSparse.cpp mirrors the dense steps of src/core/Voxel.cpp sample for sample;
tests/test_voxel_highres.cpp checks the brick grid gives bit-identical points and triangles to the dense grid on 7
meshes (merged and buried parts, big triangles, an open mesh), a 4096 end-to-end run, the slider mapping, the
estimate and the memory refusal. UI test voxel_highres. Floor bench: bench.sh remesh.
