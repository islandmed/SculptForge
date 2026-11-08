---
whats_new: **Tube and Insert let go**: picking any brush or tool (**Draw**, **Move**, **Scale**, **Rotate**), or
  pressing **Esc**, now ends **Draw Tube** and **Parts > Append > Insert on the model**; the status bar says how.
fixed: Draw Tube (Tool > Tube) and Parts > Append > Insert on the model stayed on after picking another brush or tool,
  so the next drag still drew a tube or inserted a mesh and the other tools seemed dead. Any brush (shelf, picker,
  Library, its key, the same brush again), Draw / Move / Scale / Rotate, or Esc now leaves both; with a Tube Part
  selected by its points, a brush goes back to the Part before it. Reported by GABO on Discord.
testers:
  - Tool > Tube > Draw Tube, draw a tube, then click Clay on the shelf: the next drag sculpts, the status says "Left Draw Tube".
  - Parts > Append > Insert on the model > Cube, then press 2 (or W): the next drag sculpts (or moves), no cube.
  - Draw Tube on, Esc: off. Insert on, Esc: off.
---
