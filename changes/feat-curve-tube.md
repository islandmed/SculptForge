---
whats_new: **Tube**: draw clay tubes in the air like a 3D pen. **Tool > Tube > Draw Tube**, then drag anywhere: on the
  model, off it, or out of it. Pen pressure thins the tube; drag its points afterwards to reshape it, then **Bake** to
  sculpt it, or draw straight into the active Part with **Merge** (even edges, ready for dynamic topology). Thanks to
  GABO on Discord for the idea.
highlight: Tube: draw clay tubes in 3D space
added: **Tube** (Tool > Tube): draw tubes freely in 3D space, like a 3D pen. Strokes lie on a plane facing the view
  (at the surface where the stroke starts, the pivot, or a fixed distance), optionally snapped onto surfaces; pen
  pressure sets the radius and the stroke is smoothed into a curve. The tube is swept along it with rings placed
  closer in bends and frames that never twist, round or flat ends, and Symmetry X. Each stroke is a Tube Part whose
  points are dragged (Ctrl: thickness, Alt+click: delete), inserted and deleted until **Bake**, or goes straight into
  the active Part with even edge lengths for dynamic topology; one undo step each. Script and AI commands `tube_new`,
  `tube_list`, `tube_set_point`, `tube_insert_point`, `tube_delete_point`, `tube_set`, `tube_bake`. Guide chapter
  "Tube: draw in 3D space". Requested by GABO on Discord.
testers:
  - Any OS, with a pen: Tool > Tube > Draw Tube, start a stroke on the model and pull it out into the air; a light
    touch gives a thinner tube. Ctrl+Z removes it. Good: smooth tube, no twisting or pinching in bends.
  - Drag a dot of the new tube: it selects the Tube Part and the tube follows the dot smoothly (also with long
    strokes). Ctrl+drag a dot: thicker / thinner there. Bake, then sculpt it.
  - Turn Symmetry X on and draw off-centre: the mirror copy appears. Result > Merge with dynamic topology on: draw over
    the model, then sculpt across the join.
---
Clay tube drawn in the air (Discord #wip-and-feedback 1568627563562274836, GABO / gabrielabdala_). Profiles other than
round (square, custom) are a later step.
