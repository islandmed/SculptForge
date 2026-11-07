---
highlight: Stencil: clean, sharp groove edges when you sculpt through a stencil, and detailed photos stay even on lighter meshes
fixed: **Stencil** edges sculpt as clean grooves instead of a beaded zipper: a built-in pattern made into a Stencil
  (**Use alpha**) is generated again at 1024 px instead of the alpha's 128, whose pixels showed as stair-steps
  along every edge it sculpted.
---
Found reviewing the What's New shots of 0.11.11's Stencil. tests/test_stencil.cpp stencil_groove_walls_are_smooth_along_the_groove:
roughness along a 30-degree Cobblestone-profile groove (per-bin line fit, p99 / depth) < 1% at 1024 px; the 128 px
source measures more than twice that. (Tried and dropped: reading box-filtered mip levels by the vertices' spacing made
it worse, 8.4% vs 0.2%: the coarse levels' square pixels stair-step diagonal edges again.)
