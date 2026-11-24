---
whats_new: **Blockout primitives with settings** (Tool > Parts > Append): Cylinder, Cone, Torus, Capsule and the new Pipe come with Segments, Rings, Cap rings, Quad caps, Radius and Height sliders that stay editable until you sculpt; a **Low-poly** switch in the Append menu gives the clean quad cages Polygon mode starts from.
whats_new: **Solidify** (Tool > Parts > Extract): gives a flat, single-sided Part real thickness in place, both sides joined by a rim of quads.
highlight: Blockout primitives with live settings, and Solidify
added: **Primitive settings** (Tool > Parts): a Part from **Append** (Sphere, Cube, Plane, Cylinder, Cone, Torus, Capsule,
  the new **Pipe**, Icosphere, Quad Sphere) keeps its settings editable below the Parts list for as long as it is still the
  generated mesh: Segments (cells or subdivisions for cubes, planes and spheres), Rings, Cap rings (0 = open ends), Quad
  caps (an all-quad cap with no pole), Radius, a second radius (a cone's top, a torus's tube, a pipe's inside),
  Height and Axis (Y, X or Z), plus Low-poly and Dense presets. Each change rebuilds the Part as one undo step. Sculpting, remeshing or
  editing faces turns it into an ordinary mesh; undo brings the settings back, and moving the whole Part with the Gizmo
  keeps them. Saved in projects. Appended primitives are unchanged by default; the **Low-poly (Polygon mode)** switch at
  the top of the Append menu (a saved preference) gives 12-segment, 1-ring cages with quad caps instead, the clean
  quads polygon modelling starts from. Commands: `part_append` takes `low_poly=`, `segments=`, `rings=`, `caps=`, `quad_caps=`,
  `radius=`, `radius2=`, `height=`; `part_primitive` changes them later. In the Classic layout the Primitive group shows
  with **Preferences > Interface > Show newer menu rows** (Classic keeps its old rows unless asked). Requested by members.
added: **Solidify** (Tool > Parts > Extract, beside Make Shell): gives a flat, single-sided Part thickness in place: a
  copy of the surface pushed along the normals becomes the back, a rim of quads joins the two along every open edge, and
  the result is one closed mesh. Thickness is a fraction of the Part's size (negative grows in front; Centred puts half
  each way). Face groups, UVs and paint stay on both sides; the rim gets a new face group. Pairs with Extract's
  Thickness 0: extract the flat surface first, give it thickness later. Command: `solidify thickness= both=`. In the
  Classic layout the rows show with **Preferences > Interface > Show newer menu rows**. Requested by members.
testers:
  - Any OS: Tool > Parts > Append > tick Low-poly (Polygon mode), Append > Cylinder; a Primitive group appears under the Parts buttons, the ends are a grid of quads with no centre pole (wireframe W). Drag Segments and Rings; the cylinder rebuilds, Ctrl+Z steps back through the settings. Untick Quad caps for fans; click Axis Z: it lies along Z.
  - Any OS: with the switch off, Append > Cylinder looks exactly as before (dense); click Low-poly in the Primitive group to get the cage, Dense to go back.
  - Any OS: Append a Cone, raise Top radius; it becomes a truncated cone. Append a Pipe; Inner radius sets the wall.
  - Any OS: move a fresh primitive with the Gizmo (W), then change Segments: it rebuilds where it is. Sculpt one stroke on it: the Primitive group disappears; Ctrl+Z brings it back. Save the project, reopen: the settings are still editable.
  - Any OS: Append > Plane, then Tool > Parts > Extract > Solidify: the plane gets a back and a rim (orbit under it). Thickness negative grows upward; Centred splits it. On a closed Part (a sphere) Solidify says why it did nothing.
---
Builds on #31's primitives: makeCylinder & co stay as they were (the Classic dense defaults are byte-for-byte the same
meshes, tested); the parametric path is core/PrimitiveKit.h. Freshness is judged on the mesh (a regeneration compared
within a tolerance, cached per undo revision), not on the history, so undo/redo and whole-Part Gizmo moves keep the settings.
"Pipe" rather than "tube" because Tube is already the curve Part type. Polygon mode (feat/poly-mode) takes these meshes as
they are (quad pairs sharing a face group); poly-mode-37 confirmed nothing more is needed.
