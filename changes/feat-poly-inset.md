---
whats_new: **Inset** (Polygon mode): shrink the selected faces inward by a thickness and push them in or out by a depth; the rim between keeps the faces' groups.
added: **Inset** (Polygon mode): insets the selected faces, every connected piece as one shape or every face by itself,
  with a thickness (how far the border moves in) and a depth (along the normal). The rim quads take the face group of the
  face they came from (or one new group, as an option), UVs stay continuous, creases and UV seams stay on the old edge, and
  the thickness is held below the point where a face would turn inside out. Members' top request (polygon modelling).
member_request: true
testers:
  - Any OS: Polygon mode on a cube, select one side, Inset with thickness 0.2 and depth -0.1; the side gets a sunken panel, wireframe shows quads in the rim, no gaps.
  - Any OS: select every face of a sphere, Inset individually with a thickness larger than the faces; nothing turns inside out, every face shrinks evenly.
  - Any OS: paint a texture, then Inset a region; the paint stays where it was on the inner faces and the rim.
---
Core op only in this note's first version: src/core/PolyInset.h (pure: mesh + triangle selection in, mesh + selection out);
the panel entry, preview and undo step come with the wiring into Polygon mode's Ops API (feat/poly-mode).
