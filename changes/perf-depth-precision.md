---
whats_new: **Layered Parts at rest**: a garment, Shell, Drape result or Duplicate lying close over or under the body no longer flickers or shows through in patches when the view stands still.
fixed: Parts lying close together (a garment, Shell, Drape result or Duplicate 0.2-2% off the body) showed patches of
  each other at rest, worst on thin curved forms: each Part's surface was drawn pushed back by its own slope (for the
  wireframe's lines), more than the gap between them. Only the wireframe view still does that.
testers:
  - Any OS: a thin limb or rod, Duplicate it, Transform > scale 0.995 and paint it dark; at rest and while turning, no dark patches show through the body.
  - Wireframe on (Shift+F): the lines still sit cleanly on the surface.
---
Cause: glPolygonOffset(1,1) on every fill (Renderer.cpp), not depth precision; near/far + 24-bit depth resolve a 0.2% gap.
Regression: tests/ui/coplanar_parts_invariant.sfui (view_others_marks: 2647/1652/2300 px on the old code, 0 now).
