---
whats_new: **Shift smooths every channel you paint**: with the Paint brush, Shift now softens Roughness, Metallic, Normal and Height strokes as well as the colour, across UV seams too.
fixed: Texture painting: Shift with the Paint brush (smooth) only softened Base Color, so a stroke painted with Roughness, Metallic, Normal or Height ticked kept hard, streaky edges that Shift could not blend. It now smooths every channel the brush paints (Paint > Material), on layers by their coverage; a Normal is turned into the neighbouring UV island's tangent frame before it is blended and renormalised after.
testers:
  - Any OS: paint a few strokes with the Paint brush on a textured Part with Base Color, Roughness (0.85) and Metallic ticked, then hold Shift and drag over their edges: the edges blend in the colour and in the shine, also where the strokes cross a UV seam.
  - Any OS: with Normal or Height ticked, paint a stroke, Shift over it: the relief softens; undo brings it back in one step.
---
Reviewers: src/core/Texture.cpp (TexturePainter::smoothDab: one blur per painted channel on the same cached tile windows; tangent frames per seam border in buildSmoothWindow), src/app/PanelVertexPaint.cpp (the Shift stroke begins with the brush's channels). Tests: tests/test_texture_smooth_channels.cpp, a five-channel row in test_texture.cpp's smooth bench: a Shift dab with five channels ticked costs about 3.4x the colour-only dab (still interactive at 4k on a million triangles); a stroke with only Base Color ticked costs what it did.
