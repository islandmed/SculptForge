---
fixed: Picking a brush, Draw, Move, Scale or Rotate leaves **Color > Line Gradient** (a gradient just painted stays as
  it is); before, drags on the model kept drawing gradient lines after another brush was picked.
---
Found by the mode matrix and the monkey (seed 53, shrunk to 2 steps); its paint.line_gradient known lines are deleted.
