---
whats_new: **Symmetry with textured brushes**: Dragon Scales, Skin Pores, Wood Grain and the other textured and turning-stamp brushes now sculpt the same pattern on both sides of X symmetry.
fixed: **Symmetry with textured brushes** (#197): with X or radial symmetry the mirrored strokes of the brushes that stamp a pattern or an alpha (Dragon Scales, Skin Pores, Stone Carve, Wood Grain, Woven Cloth and about 25 more) now carry the pattern's mirror image, where they used to lay a different pattern on each side.
testers:
  - Any OS: turn on X symmetry, pick Dragon Scales (or Skin Pores), stroke across one side of a sphere; both sides show the same scales, mirrored.
---
Root causes: recipe brushes sampled the pattern at the world position (each copy a different one) and a mirror copy's alpha frame turned
instead of reflecting (a mirror swaps the handedness of a cross).
Tests: the 30 #197 brushes left tests/test_geom_invariants.cpp's kKnown list.
Not changed (same handedness pattern, no invariant covers them yet): Texture.cpp and VertexPaint.cpp alpha frames, MicroSculpt recipes' alpha.
