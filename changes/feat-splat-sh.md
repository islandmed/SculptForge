---
whats_new: **Splats with view-dependent colour**: captures with spherical harmonics (SH 1-3) now show the sheen and
  highlights that change as you orbit, as splat viewers do, and keep them when you turn and save them.
added: Gaussian splats show their view-dependent colour (spherical harmonics, degree 1-3, the 3D Gaussian Splatting
  layout) in the viewport, evaluated for the direction each splat is seen from (not on the Essential hardware tier,
  where splats keep their plain colour).
changed: Turning splats (Flip, Turn 90) and saving them as PLY or .splat, or exporting a turned splat object from
  Blender with Apply Object Transform, now turns their view-dependent colour with them instead of dropping it to the
  plain colour.
testers:
  - Open a splat capture .ply with SH 3 (the palette says "SH 3"): orbit; highlights and tints shift with the view.
  - Flip it, save it as .ply, open the saved file in SuperSplat or reopen it here: it looks the same as before saving,
    from every side.
  - Blender 5.3: rotate a splat object, File > Export > Gaussian Splats (.ply) with Apply Object Transform on; the
    export looks like the object in Blender, sheen included.
---
SH rotation: per band, the exact matrix by least squares on 32 directions in the 3DGS basis (core/Splats.cpp
ShRotation; the add-on's splats.rotated_sh is the same). Renderer: bands 1-3 as half floats in a second texture buffer
(96 B a splat at SH 3), evaluated once a splat in the cloud's own frame by a fragment pre-pass into an RGBA32F texture
when the eye moves (feat/splat-sh-prepass; per quad corner before: 1.5M splats SH 3, orbit p95 75 -> 49 ms, p99 90 ->
53 ms). Floor tier: off and not uploaded (SH 3 still p50 17.6 -> 31 ms there with the pre-pass).
