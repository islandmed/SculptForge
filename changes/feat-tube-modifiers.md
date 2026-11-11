---
whats_new: **Tube**: Braid now weaves over and under, Rope and Braid take 1 to 8 **Strands** (one is a spring), and **Repeat along curve** lays copies of another Part along a tube or curve (chain links, beads, scales).
highlight: Repeat a Part along a curve, real braids and springs
added: **Strands** (Tool > Tube): Rope and Braid take 1 to 8 strands (3 as before); a rope of one strand is a spring, and
  Spread opens it. `tube_set` and `tube_new` take `strands`.
added: **Repeat along curve** (Tool > Tube, with a Tube or Curve Part active): pick another Part and its copies are laid end
  to end along the curve as a new Part, with Spacing (1 end to end, less overlaps them like chain links) and Turn (each copy
  turned that many degrees more than the one before; 90 for chain links). One undo step; the curve and the Part are kept.
  New command `tube_repeat`. Requested by Artizmo on Discord.
changed: **Braid** strands are now flat ribbons that pass over and under each other instead of three round strands like a
  rope. Saved braids look different. Requested by Artizmo on Discord.
testers:
  - Any OS: Tool > Tube, draw a tube, pick Braid; the three strands weave over and under (Strands 5 for a wider plait).
  - Any OS: pick Rope, Strands 1; it is a spring, Spread opens it, Spin changes how tightly it winds.
  - Any OS: add a small Part (a flattened cube as a link), draw a Curve, Tool > Tube > Repeat a Part along the curve, pick the link, Turn 90, Spacing 0.8; Ctrl+Z removes it.
---
Member request (Artizmo): twist, spin and spread modifiers, rope / braid / sleeve profiles, and repeating a premade shape along a curve.
Left for later: modifier scopes per point (start + end, individual points) and a live (non-baked) repeat.
