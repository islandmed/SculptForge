---
whats_new: **Stencil**: lay an image over the view and sculpt or paint through it (**Sculpt > Alpha > Stencil**). Move,
  scale and turn it on the canvas with **Adjust**, stroke over it, turn the model and repeat. Sculpt brushes take its
  brightness, Vertex Paint its colours; Tile repeats it across the view.
highlight: Stencil: sculpt and paint through an image (thanks @kenttumanda123-netizen, #155)
added: **Stencil** (Sculpt > Alpha > Stencil, #155, thanks @kenttumanda123-netizen): an image over the view (loaded
  like an alpha, or the brush's alpha) that the brushes work through. It stays put on screen while the model turns
  under it; every brush that takes an alpha (built-in, custom and Community brushes, the Paint brush, micro levels)
  takes its brightness (sculpt) or its colours (Vertex Paint, or its brightness masking the paint colour) at the place
  each vertex shows, and nothing off the image. Adjust: drag to move, a corner to scale, the round handle to turn;
  arrows, - =, , . on the keyboard. Invert, Tile, Opacity, Hide while navigating. Symmetry mirrors it. Commands
  `stencil_load`, `stencil_set`, `stencil_clear`. Texture painting doesn't take it yet.
testers:
  - Any OS: Sculpt > Alpha > Stencil > Load image... (a photo or a pattern), Adjust, move / scale / turn it, Enter;
    stroke Standard across its edge: the surface changes only under the image, in its pattern.
  - Paint brush over a colour photo stencil: the model takes the photo's colours where it covers it, nothing elsewhere.
  - Windows at 150% scaling: the handles sit on the stencil's corners and are easy to grab.
---
Design: docs/design/stencil.md. Core sampling tests: tests/test_stencil.cpp; UI: tests/ui/stencil.sfui.
