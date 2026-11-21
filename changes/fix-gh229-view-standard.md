---
fixed: With **View falloff** on (Brush palette), Standard, Inflate, Crease, Pinch, Sharp Crease and Brush Lab recipes that
  build along the surface also dented the far side of the model, which showed after orbiting round it. These brushes now
  leave what lies far behind the surface under the cursor (more than three brush widths, fading out smoothly), so the
  far side stays; Move and the grab brushes still carry
  everything inside the brush circle, through the model, which is what View falloff is for; its tooltip says so in every language (Fixes #229).
testers:
  - Any OS: New > Sphere, Standard brush, **Brush > View falloff** on, a few strokes, then orbit to the back: it is smooth. With the Move brush and View falloff the back still follows.
---
Reviewers: Sculpt.cpp viewAdditiveFade (dabSingle's offset, dabRecipe's depth term; a facing-based gate tore a head's nose and lips, artist-qa 11-21); tests/test_view_falloff_planes.cpp. Layer, Nudge, Snake Hook, Smooth are not gated (not along-the-normal adds).
