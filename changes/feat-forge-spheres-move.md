---
whats_new: **Forge Spheres move smoothly**: a dragged sphere stays under the pointer at its depth, even on long chains; hold **Shift** to swing it around the sphere it grows from (its link keeps its length), **Tool > Forge Spheres > Snap to Surface** seats it on the sphere under the pointer, and **Move Branch** brings what grows from it along.
changed: **Forge Spheres**: moving a sphere follows the pointer smoothly on any armature size (the pointer's moves are taken once a frame and only the moved spheres and their links are redrawn, instead of the whole armature on every move); a moved sphere keeps its depth in the view; Draw's resize band is a thin rim (none on spheres drawn small), so most of a sphere moves it. Reported in a public review.
added: **Forge Spheres**: hold **Shift** while moving a sphere to swing it around its parent with the link keeping its length; **Snap to Surface** (Tool > Forge Spheres) puts a moved sphere's centre on the surface of the sphere under the pointer; **Move Branch** (Tool > Forge Spheres) carries everything growing from a moved sphere along, keeping its shape.
testers:
  - Windows, mouse: Tool > Forge Spheres > New Forge Sphere; grow a body, head, tail and four legs (X symmetry). Drag spheres around fast: each stays under the pointer and its twin follows with no lag or hitch.
  - Any OS, pen or trackpad: with Move, drag a foot while holding Shift: it circles its leg sphere, the link length stays the same. Let go of Shift mid-drag: it moves freely again.
  - Any OS: Snap to Surface on, drag the head onto the body: it sits on the body's surface. Turn it off again.
  - Any OS: Move Branch on, drag a leg: its foot comes along; drag the body: the whole figure moves.
  - Draw mode: a press anywhere but the very edge of a sphere moves it; only the thin outer rim resizes (the outline thickens there).
---
Reviewers: per-event cost before vs after is printed by forgespheres_drag_cost_on_dense_chain; frame times by ui_forge_spheres_move.
