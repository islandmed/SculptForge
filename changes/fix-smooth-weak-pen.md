---
whats_new: **Smooth on dense meshes**: Smooth (and holding Shift) now takes the same amount off a stroke at any mesh density; on a mesh of a million triangles it barely changed the surface before.
fixed: **Smooth** (and Shift-smooth) is about the same strength on a dense mesh as on a light one. A dab relaxed each point toward its neighbours once, which reaches one edge, so on a mesh with many edges under the brush a stroke did almost nothing (1.3M triangles: 2% of what 20k triangles took off). A dab now repeats that step in proportion to the edges under the brush (what it did with edges a sixth of the brush radius), also with Keep volume; Intensity and pen pressure still apply once. Micro levels are not changed yet.
testers:
  - Any OS: a 1M+ triangle mesh, hold Shift (or pick Smooth) and stroke over bumps with the mouse, then a pen (light and firm). Bumps should go within a few strokes at any density; Intensity and pen pressure should still change how much.
  - A Smooth stroke over a masked area leaves the mask where it was; Keep volume keeps the form.
---
Reporter's clip: pen display, Shift held, 0.13.9. The orange ring is the normal brush: Shift pressed after the pen touched down starts the stroke with the main brush (blue ring = Smooth).
