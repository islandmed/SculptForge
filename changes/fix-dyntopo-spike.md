---
whats_new: **Dynamic topology** no longer shows a long spike and a patch of broken faces after zooming in and sculpting
  (only the display was wrong: Undo or the next strokes put it right) (thanks @editordenis4-collab, #153).
fixed: Dynamic topology: after strokes in several places (zooming in between them makes many more triangles), the
  view could draw a long spike out of the mesh and a patch of dark, broken or missing faces until the next Undo or a
  full redraw. When the renderer's buffers for the remeshed areas grew, the areas drawn before kept their vertices
  but lost their triangles, which then drew whatever the new buffer held. The mesh itself was never damaged. Thanks
  @editordenis4-collab (#153).
testers:
  - Any OS (Intel graphics especially): New Sphere, Dynamic on (Detail 50), X symmetry on, Wire on. Zoom in, make
    short strokes in 8-10 different places, zoom in again and stroke more. No spike, no dark or missing faces.
---
Renderer::preparePatches marks every patch stale when uploadPatches is about to reallocate the patch buffers
(patches > patchParts), so each patch's indices, edges and face groups are rebuilt and re-sent. Regression:
tests/ui/dyntopo_patches.sfui reads the patch index buffers back (render_patch_errors) after each stroke.
