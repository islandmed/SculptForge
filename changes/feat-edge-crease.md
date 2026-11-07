---
whats_new: **Edges mode** (**Tool > Geometry > Edges**): select edges of the base level on the model (click, Shift
  adds, Ctrl removes, double-click takes the edge loop), every hard edge at once with **Select by Angle**, open
  borders, Grow and Shrink. The first step towards edge creases for subdivision (#108).
highlight: Select edges by angle, loop or click
added: **Edges mode** (Tool > Geometry > Edges, #108): picks edges of a Part's base level (SDiv 1; a Part with levels
  steps down to it) on the model: click selects the edge under the cursor, Shift+click adds, Ctrl+click removes, a
  double-click takes the edge loop (across quads, or along an open border). **Select by Angle** selects the edges
  whose faces meet at more than the Angle (30 degrees by default), optionally **only within the selection**;
  **Grow**, **Shrink**, **Open Edges**, **Invert** and **Deselect All**. Quad diagonals are never edges. The `edge_select`
  command does the same from scripts. Thanks _tfx on Discord for asking for select by angle; the plan for edge creases is
  @NicTanghe's (#108). Creases themselves come next.
testers:
  - Any OS: Tool > Geometry > Edges mode on a new Cube; Select by Angle selects the 12 sides (orange), nothing else;
    Invert, then Deselect All.
  - Click an edge, Shift+click another, Ctrl+click it again: 1, 2, 1 selected; the edge under the cursor lights up
    (red while Ctrl is held). Nothing gets sculpted.
  - Double-click an edge in the middle of a face: the loop round the cube. Grow, then Shrink: back to the loop.
  - File > Starter meshes > Creature Head (with levels): Edges mode steps down to SDiv 1; double-click loops on the
    skull. Press D: clicks say to step down again.
  - A big base (Tool > Remesh > Voxel Remesh a sphere to about 1M vertices): Edges mode starts in well
    under a second; clicks, double-clicks and Select by Angle stay instant while orbiting.
  - Windows at 150% / 200%: the Edges buttons and slider fit the Geometry panel; lines are thick enough to see.
---
Slice 1 of #108 (edge selection only). Slices 2-4 (crease values in the project, Catmull-Clark semi-sharp creases,
USD export/import) follow on their own branch.
