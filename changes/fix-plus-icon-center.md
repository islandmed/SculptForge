---
fixed: The bottom shelf's **Add a brush** button draws its + exactly in the middle of the circle at every UI scale
  and on Retina screens; it was a font glyph that sat off centre (#150, thanks @tristratos).
testers:
  - macOS Retina, and Windows at 125% / 150% scaling: the circled + at the end of the bottom shelf has the + dead
    centre, horizontally and vertically, hovered or not.
---
Drawn from pixel-snapped rects sharing the circle's centre (circledPlus in ShellGeometry); test_ui_scale checks
scales 1-2 at fractional positions. Other circled pluses (Stroke popup Add) are a single icon-font glyph, ink-centred.
