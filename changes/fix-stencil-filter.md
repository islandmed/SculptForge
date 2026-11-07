---
fixed: **Stencil** photos on lighter meshes sculpt and paint evenly instead of as noise: where a dab's vertices
  are many image pixels apart, each reads the average of a disc as wide as their spacing (from the image or a
  half / quarter size copy, never below 512 px), not the one pixel it lands on, so a fine photo doesn't speckle or
  shimmer as you move the stencil.
---
Follow-up to fix-stencil-edges (merge-0106-4b review). tests/test_stencil.cpp stencil_fine_image_on_a_light_mesh_sculpts_evenly:
a 2048 px one-pixel checker on 40- and 160-cell planes; plateau sd/mean within 0.01 of the same stroke without a stencil
(0.041 vs 0.038, 0.026 vs 0.025; point by point it was 0.065 and 0.17). The groove test (< 1%) still passes: the
coarsest level is 512 px, so no square-pixel stair-steps.
