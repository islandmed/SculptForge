---
whats_new: **Move on a Micro level** next to a mask no longer leaves crumpled speckle or folded, leaf-like lumps around the stroke.
fixed: Move on a Micro level with a mask nearby moves the surface underneath smoothly again, mouse and pen alike: it folded
  the surface into leaf-like lumps and left crumpled speckle wherever the mesh's own forms were smaller than the drag. The
  mask still holds exactly.
testers:
  - Any OS: a dense mesh with a Micro level (Geometry > Micro), Ctrl-paint a mask band, then Move across beside it with the
    mouse and with a pen at light and full pressure. Good: the free surface follows the drag smoothly, no lumps or speckle;
    the masked band does not move.
---
Cause: since 0.12.13 a Move with a mask near took the micro grid's along-the-normal rule (and rescaled the whole grid's
range each stroke). Fix: it stays on the base cage, which already holds masked samples bit-exact.
