---
whats_new: **Edge creases** (**Tool > Geometry > Edges**): give the selected edges a crease from 0 to 1 with
  **Crease**, or remove it with **Uncrease**. Creased edges show in blue on the base level and are saved in the
  project (#108).
highlight: Crease edges for subdivision
added: **Crease** / **Uncrease** in Tool > Geometry > Edges (#108): a crease value (0..1, 1 = fully sharp) on the
  selected edges of the base level, one undo step each, saved in the project (an optional blob: older versions open
  the file without creases) and kept on the base while stepping levels. Creased edges are drawn in blue on the base
  level in every mode. The `edge_crease` command does the same from scripts. Thanks _tfx on Discord for asking; the
  plan for edge creases is @NicTanghe's (#108).
testers:
  - Any OS: new Cube, Tool > Geometry > Edges mode, Select by Angle, Crease (1.00): the 12 sides turn a thick orange;
    Deselect All: they show blue. The line under the buttons counts the creased edges.
  - Crease 0.25 on one loop: a thinner, dimmer blue line than the sharp ones. Undo / Redo flips it back and forth.
  - Leave Edges mode (pick a brush): the blue lines stay on the base level; Divide: they go away (they live on SDiv 1);
    Lower Level: back.
  - Save, reopen: the creases are there. Open the file in 0.11.13: it opens (without creases).
  - Windows at 150% / 200%: the Crease slider and buttons fit the Geometry panel.
---
Slice 2 of #108 (creases stored). Subdivision that honours them is slice 3; USD export/import slice 4.
