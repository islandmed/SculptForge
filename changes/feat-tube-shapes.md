---
whats_new: **Tube shapes**: **Tool > Tube > Shape** draws square, triangular and flat ribbon tubes as well as round
  ones, turned and tapered to a point at either end, and changes them after drawing. **Point spacing** draws with
  fewer points, and **Simplify** takes points out of a tube you already drew.
highlight: Tube: square, triangle and ribbon shapes, tapers, fewer points (thanks GABO on Discord)
added: **Tube shapes** (Tool > Tube > Shape): Circle, Square (sharp corners), Triangle and Ribbon cross-sections,
  Thickness for ribbons, Turn, and Taper start / Taper end; they set new tubes and the selected Tube Part (live while a
  slider is held, one undo step), and the Symmetry X copy is the exact mirror image. `tube_new` / `tube_set` take
  `profile`, `thickness`, `turn`, `up`, `taper_start`, `taper_end`. Requested by GABO on Discord.
added: **Point spacing** (Tool > Tube > Stroke) sets how far apart a drawn tube's points are, and **Simplify**
  (Tool > Tube > Edit, `tube_simplify`) takes about a third of a tube's points out per press, the ones the curve needs
  least. Requested by GABO on Discord.
changed: **Tube**: **Snap to surfaces** is on by default, so a stroke drawn across a model follows its surface instead
  of sinking into it, and pulled off the model it carries on into the air from the edge. With **Draw Tube** on, a
  plain drag always draws, even over an old tube's dot; Shift+drag (or Ctrl, Alt) edits the dot. Reported by GABO on
  Discord.
testers:
  - Any OS: Draw Tube, drag across the sphere and off it: the tube lies on the surface, then goes on into the air.
  - Any OS: start a stroke on an old tube's end dot: a new tube is drawn; Shift+drag on the dot moves it.
  - Any OS: Tool > Tube > Draw Tube; Shape > Ribbon, draw a stroke: a flat strip facing you. Drag Turn and Taper end:
    the tube follows while dragging; Ctrl+Z undoes each in one step.
  - Any OS: Symmetry X on, Shape > Square, Turn 30, draw off the centre: the copy is the mirror image.
  - Any OS: Point spacing 4, draw a long stroke: far fewer dots; Simplify on a dense tube removes about a third.
  - Any OS: click without dragging: a ball; with Merge, it goes into the active Part.
---
Slices 1 and 2 of docs/design/tube-shapes.md. Saved round tubes sweep exactly as before (old projects stay editable).
