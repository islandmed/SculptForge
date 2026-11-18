---
fixed: **Ctrl+W and Mask by ... on micro detail**: Ctrl+W (Group from Mask) and Tool > Masking > Mask by Cavity, Peaks,
  Colour, Facing, Face Group and Border now work on a micro level's own mask, the one its brushes and the viewport
  use (before, they read or wrote only the base mesh's mask, so nothing happened on a micro level). Cavity, Peaks and
  Facing follow the micro surface itself, down to its pores and ridges. Ctrl+W puts a base triangle in the new group
  when most of its micro detail is masked, then clears the mask. Each op is one undo step.
fixed: **Face groups vanished after Add Micro**: with Show Face Groups on, a model with a micro level now shows its face
  group colours, including groups made with Ctrl+W on the micro level (before, every group drew in one colour).
fixed: **Move on micro detail, slow drags**: a Move dragged over a micro level in many small steps (a pen moving slowly,
  most of all a second Move next to a mask) could leave contour rings and a stepped band across the falloff, because
  each small step was rounded on its own. The surface now comes out the same however many steps the drag takes.
testers:
  - Any OS: add a micro level (Tool > Geometry > Add Micro), paint a mask with Ctrl, press Ctrl+W: a new face group
    colour appears where you painted and the mask goes away; Ctrl+Z steps back.
  - Same micro level: Tool > Masking > Mask by Cavity (after some detail strokes), Mask by Facing, Mask by Face Group:
    the mask appears on the micro detail at once; Ctrl+Z takes it off.
  - Same micro level with a mask painted: Move slowly next to the mask, twice; the surface stays smooth (no rings,
    no steps) up close in a shiny material.
---
Known, not in this branch: a big Move across a soft mask edge on micro detail can still show a faint grain there (the
mask's 256 levels scale the move in small steps); tests/test_micro_move_terrace.cpp holds the rounding fix itself.
