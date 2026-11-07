---
whats_new: **Deformation stack** (requested by foirgairwolf on Discord): **Tool > Deformation > Stack** bends, twists, tapers,
  shears, turns, moves, scales, squashes, rounds, boxes, flattens, sags or puts depth on the whole Part, along any of
  X, Y and Z, with a live preview. Stack steps up, switch them on and off, repeat one, then **Apply** (one undo step),
  **Apply Selected** or **Reset**. Every step is a command too: `deform_stack ops="bend 45 y soft; twist 90 y"`.
whats_new: **Exact size** (requested by foirgairwolf on Discord): **Tool > Deformation > Size** shows the Part's width, height
  and depth in scene units; drag one or type an exact value to resize it, with **Uniform** keeping its proportions.
highlight: Deformation stack: bend, twist, taper and more on the whole Part, live
added: **Deformation stack** (Tool > Deformation > Stack, requested by foirgairwolf on Discord): thirteen whole-Part shape
  operations in four groups (Transform: Move, Turn, Scale, Squash; Bend & Twist: Bend, Twist, Taper, Shear; Shape:
  Round, Box, Flatten, Sag, Depth; plus the Surface ones), each along any of X, Y and Z about the Part's centre (or
  the gizmo's placed pivot), with Direction for Bend and Shear and Soft for an eased profile. The preview is the
  stack applied as one undo step and follows each change; Apply, Apply Selected, Reset and Repeat. The mask holds
  what it covers. Commands `deform_move` ... `deform_depth` and `deform_stack` for scripts and the AI.
added: **Size** (Tool > Deformation, requested by foirgairwolf on Discord): the active Part's X, Y and Z dimensions in scene
  units, to drag or type exactly; it resizes about its centre (Uniform: in proportion), one undo step. Command
  `deform_size`.
testers:
  - Any OS: New Sphere, Tool > Deformation > Stack > Add step > Bend; drag its amount: the sphere bends live. Add
    Twist, switch Bend off and on, then Apply: one Ctrl+Z takes the whole stack back.
  - Any OS: mask the top half (Ctrl+drag), add Taper 60 on Y: the masked half stays put.
  - Any OS: Tool > Deformation > Size: double-click X, type 4, Enter: the Part is 4 wide and, with Uniform on,
    keeps its proportions; Ctrl+Z puts it back.
  - Windows: a 5M+ triangle Part: the preview follows each slider when you let go; Apply stays under a second.
---
Slice 1 of the Deform panel (docs/design/deform-panel.md). Slice 2: Noise, Contrast, Inflate, re-symmetrize.
