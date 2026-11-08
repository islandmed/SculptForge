---
fixed: **Tube**: Ctrl+Z (or Redo) in the middle of a tube stroke or a point drag left the tube's mesh and its points
  out of step (two tubes on screen, one set of points, and later edits refused). Esc, Undo and Redo now drop the
  stroke or put the point back. Esc during a stroke also drops it now instead of making the tube.
fixed: **Tube**: with a pen, a tube always started at full thickness however lightly it touched down (the pressure
  from before the touch was used); it starts at the pen's first real pressure now. Reported by GABO on Discord.
testers:
  - Pen: Tool > Tube > Draw Tube, Pressure 1, draw from a light touch to firm: the start is thin.
  - Any OS: drag a tube point and press Ctrl+Z before letting go: the point goes back; dragging it again works.
---
Functional pass of Tube (scripted: tests/ui/tube_fn_*.sfui). Stacked on fix/tube-mode-exit.
