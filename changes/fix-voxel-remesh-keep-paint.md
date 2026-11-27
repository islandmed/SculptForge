---
whats_new: **Voxel Remesh and vertex paint**: the paint now follows the old surface smoothly (edges no longer come out in blocks), and the status line says when a coarser result softens fine paint.
fixed: **Voxel Remesh: vertex paint edges came out in blocks** when the new surface is denser than the old one, because each new vertex copied the colour of the nearest old vertex. Each new vertex now takes the colour at the closest point of the old surface, interpolated across its triangle. When the result has fewer vertices than the Part had, the status line says that paint finer than the new vertices is softened and that a higher Resolution keeps it. Reported by a member.
testers:
  - Any OS: paint a Part with the Paint brush (a hard-edged blob), Tool > Remesh > Voxel Remesh at the default Resolution; the blob's edge is smooth and the colour is where it was. Undo brings the old paint back.
  - Any OS: Voxel Remesh the same Part at Resolution 32; the status line says the paint is softened and names the Resolution.
---
Test: tests/test_remesh_keep_paint.cpp (vertex paint plain, in layers, Keep Masked + Keep Face Groups, sparse path, coarse source, the note) and tests/ui/remesh_keep_paint.sfui. The texture and its material channels were already carried (tests/test_texture_transfer.cpp); the Lit Sphere material is the scene's, a remesh doesn't touch it.
