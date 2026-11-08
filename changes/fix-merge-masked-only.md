---
whats_new: **Masked only** in **Tool > Geometry > Merge by Distance**: weld just the masked points, for example one seam,
  and leave the rest of the Part alone.
added: **Masked only** (Tool > Geometry > Merge by Distance): only masked points merge, with each other; the count
  and the orange preview dots follow it. `weld_points masked_only=true` for scripts. Thanks foirgairwolf on Discord.
whats_new: **Hidden faces**: Quad Remesh, Voxel Remesh, Decimate and Merge by Distance ask first when part of the Part is
  hidden, with **Unhide and continue** or **Cancel**.
added: Quad Remesh, Voxel Remesh, Decimate and Merge by Distance ask before working on a partly hidden Part, in the
  app: **Unhide and continue** shows everything and goes on (Undo hides it again), **Cancel** changes nothing. Merge by
  Distance with Masked only and the mask shown doesn't ask. Thanks @TheLycanFenrir (#163).
testers:
  - Any OS: hide part of a sphere, then Tool > Geometry > Decimate: the prompt shows; Cancel leaves it, Unhide and continue
    shows everything and decimates. Quad Remesh and Voxel Remesh ask the same way.
  - Any OS: mask one seam of an unwelded model, tick Masked only in Merge by Distance: the count and the dots cover only
    the masked points, and Merge welds only them (one Ctrl+Z undoes it).
---
Discord #ideas msg 1568861977042755767; GitHub #163.
