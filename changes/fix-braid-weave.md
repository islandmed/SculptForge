---
whats_new: **Tube Braid** is now two round strands twisted over and under each other, and **Spread** opens and closes the weave from a tight rope to a loose twist.
highlight: Round, woven Braid strands
changed: **Braid** (Tool > Tube) now builds round strands (two to begin with) that twist over and under each other round the curve, instead of flat ribbons. **Spread** sets the weave: 0 packs the strands together so they touch, 1 opens them up with longer turns. Saved braids look different. Requested by Artizmo.
changed: Tool > Tube sliders for Rope, Braid and the other profiles are now Twist, Spin, Spread, Strands. **Turn** is gone: **Spin** does what Turn did (turns the cross-section about the curve). A saved Turn loads as Spin; a Spin saved before this (degrees per radius of length) is dropped. `tube_set` and `tube_new` take `spin` (`turn` still works). Requested by Artizmo.
testers:
  - Any OS: Tool > Tube, draw a curve, pick Tube from curve and Braid; two round strands twist round the curve. Drag Spread from 0 (touching, tight) to 1 (loose, open gaps).
  - Any OS: Twist and Spin still turn the strands; the sliders read Twist, Spin, Spread, Strands from top to bottom.
---
Member request (Artizmo, Discord DM): a round, woven braid with Spread from loose to tight, and Spin in place of Turn.
