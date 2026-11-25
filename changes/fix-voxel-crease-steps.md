---
whats_new: **Voxel Remesh** keeps creases and hard edges 1.5-3 voxels wide cleaner; finer creases need a higher Resolution, and Remesh now tells you which.
fixed: **Voxel Remesh: creases came out stair-stepped** (#240). A crease about 1.5-3 voxels wide (wrinkles, eyelid and
  lip lines, hard edges) came out serrated, or faded in patches along its length, because the surface was rebuilt from
  samples a voxel apart. Voxel Remesh now moves the points along such a crease back onto the sculpt's own surface. On a
  test sphere with creases 1.6 voxels wide at the default Resolution (200), the crease's depth varies 40% less along its
  length, its sides are tilted half as far off (normal error 44 to 24 degrees) and its cross-section sits 43% closer to
  the original; a cube's edges come out sharper. Smooth surfaces and fine bumps come out exactly as before, there are no
  new self-intersections, and the remesh takes about 1% longer. Creases finer than that can't be held by a remesh at
  that Resolution: when a remesh loses a good share of them, the status line says which Resolution keeps them and
  about how many triangles it makes, first on the line (only when it fits the free memory, once per Part and
  Resolution in a session, and not once you have raised the Resolution above the default); in the title bar, a status
  message too long to show whole now shows it all on hover. Creases no remesh at that Resolution can hold (a closed
  slit such as the lips, or a crease under a voxel wide) are left as before. Reported by @nino3thd.
testers:
  - Any OS: Tool > Remesh > Voxel Remesh at the default Resolution on a sculpt with creases a little wider than the
    voxels (carve lines with Detail Crease on a dense head, then Decimate it). Close up, the creases are continuous
    lines of an even depth.
  - On a head with fine wrinkles, the status line after the remesh may start "Resolution N keeps fine creases (about M
    triangles)". Remesh again (Undo first) at that Resolution: the wrinkles come through, and the hint is gone.
  - The lip line and other closed slits look as they did in the previous version (no new shards or notches).
  - Remesh a smooth sphere and a bumpy sculpt: the result looks the same as in the previous version, with no hint.
---
Design (cto-9d 11-25): Voxel.cpp snapCreases after surface nets + Taubin, dense and brick (> 512) paths. Crease vertices:
their cell's nearest source triangles turn > 35 degrees and they sit > 0.1 voxel off the source; they move along their
normal onto the weighted planes of the 27 samples' nearest triangles (5 rounds, light smoothing); guards for flips, new
folds, new self-intersections and moves that don't get nearer; touched quads take the diagonal along the crease. The hint
(noteFineCreases): >= 64 crease vertices whose result still faces > 30 degrees off the source, and >= 1 in 20 of them,
median width (from the flank planes) 1-2 voxels -> the Resolution for 2 voxels, slider-rounded, memory-gated (Fits only).
Brick path keeps its nearest triangles: memory estimate 11 -> 15 bytes per stored sample. Probe: tests/CreaseProbe.h,
`sf_remesh_bench --shape creased [--no-crease-snap]`; tests/test_voxel_crease.cpp.
