---
whats_new: **Reshaped scans keep their colour**: **Sculpt to splats** (Render > Gaussian Splats) brings a scan back in its own colour, ready to send to Blender; new surface takes your vertex paint.
highlight: Reshape a scan, send it back in its own colour
added: **Sculpt to splats keeps a scan's colour** (Render > Gaussian Splats, `mesh_to_splats`): with the capture still loaded, its splats that still lie on the sculpt's surface are kept as they were (colour, all spherical-harmonics bands, size, opacity) and only reshaped or new surface gets new splats, painted from the Part's vertex paint or texture; soft clay is used only for a model with no colour. The result keeps the capture's placement, so Send to Blender puts it back where the scan was. New `from_capture` option; the splat count defaults to the capture's.
testers:
  - Any OS, with a splat capture (.ply): Make sculptable, push the nose out with Draw, Render > Gaussian Splats > Sculpt to splats; away from the nose the splats look exactly like the capture, not grey.
  - Same, then paint the nose a new colour first: the new surface comes back in that colour with no grey line at the edge.
  - With Blender 5.3 and the add-on: File > Blender > Send to Blender after Sculpt to splats; the object shows the same colours.
---
Colour carry from the capture (kept splats) plus vertex paint for new surface; tests/test_splat_color.cpp measures it.
