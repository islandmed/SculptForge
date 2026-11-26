---
whats_new: **Crease Line, Engrave and the other pinching cuts**: repeated passes over one line no longer saw the groove's walls into teeth. The groove stops deepening once the mesh can't hold it; untick **Brush > Edit recipe > Stop at mesh depth** or turn on Dynamic to cut deeper.
fixed: Pinching carve brushes (Crease Line, Engrave, Fold Valley, Panel Groove, Scratches, Check Split, Board Gap): ten up-and-down passes over one line, with Mirror X on or off and Contact facing on or off, left serrated, sawtooth walls inside the groove (its walls squeezed past the mesh's own triangle rows). The groove now stops deepening and tightening at about a brush radius and a quarter under its rim, and a stroke on the mirror plane is one dab instead of two done one after the other. The first passes dig as before.
added: **Stop at mesh depth** (Brush > Edit recipe, pinching carve brushes; Classic shows it with Show newer menu rows): on by default; untick it to cut on, as before. With Dynamic on the groove keeps deepening (it adds the triangles it needs) and only its pinch stops.
testers:
  - Any OS: Crease Line, Size about 150, Mirror X on, ten up-and-down strokes over one line on the sphere; the groove deepens for the first passes, then holds a smooth wall (no teeth). Repeat with Contact facing on.
  - Brush > Edit recipe > untick Stop at mesh depth, repeat: it cuts on, deeper.
  - Dynamic on, repeat: the groove keeps deepening without teeth.
---
Not a regression of 0.13.13 (measured: 0.13.13 pinches the same or less than 0.13.12; the depth per pass is identical). The teeth came from digging past what the mesh holds; Contact facing never changed these grooves (its weights are all 1 there).
