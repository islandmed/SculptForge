---
whats_new: **Booleans between Parts**: in **Tool > Parts**, click the mark in a Part's row for **Add**, **Subtract** or
  **Intersect**; the view shows the result live, and **Make Boolean Mesh** turns it into a new Part (the sources stay,
  hidden). Exact and watertight, with face groups, vertex paint and UVs kept.
highlight: Exact Booleans between Parts, with a live preview
added: **Booleans between Parts** (Tool > Parts): each Part's **Boolean** (None, Add, Subtract, Intersect) is set from
  the mark in its row or its right-click menu; the shown Parts with one combine in list order, previewed live in the
  view (as you sculpt or move them on lighter meshes, on release on heavy ones). **Make Boolean Mesh** writes the
  result into a new Part, one undo step, with options to weld the seams, keep each source's face groups and give the cut
  faces a face group of their own. The Boolean is exact (watertight when the Parts are; faces away from the cut, with
  their face groups, vertex paint and UVs, are kept as they are; flush and coplanar faces handled), with a voxel
  fallback for Parts that aren't closed (the status line says which it used). Saved in projects. Commands
  `part_boolean`, `make_boolean_mesh`, `boolean_parts` (scripts, Python, MCP). Requested by foirgairwolf and GABO on
  Discord.
testers:
  - Any OS: Append a Sphere and a Cube, move the cube into the sphere, set the sphere to Add and the cube to Subtract
    in the Parts list. The view shows the sphere with the cube cut out; move the cube with the Gizmo and the cut follows.
  - Make Boolean Mesh: a new Part "Boolean" appears, active, the two sources hidden; Ctrl+Z takes it away and shows
    them again. Tool > Geometry > Find Problems > Check on the result: no holes.
  - A cube cutter whose top is flush with a box's top: Subtract opens a clean pocket (no film across it).
  - Windows: two 1M-triangle Parts (Divide a sphere and a cube a few times): Make Boolean Mesh finishes in a few
    seconds and the UI stays responsive while the preview works.
---
Exact core: src/core/MeshBoolean.cpp (mesh vs mesh, sharing Cut's planar arrangement code, now in
src/core/Arrangement.h); scene, commands and undo: src/core/PartBoolean.cpp; live preview: src/app/BooleanPreview.cpp.
