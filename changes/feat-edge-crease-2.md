---
whats_new: **Edge creases** (**Tool > Geometry > Edges**): give the selected edges a crease from 0 to 1 with
  **Crease**, or remove it with **Uncrease**, and Add Level keeps them sharp: hard rims, panel lines and corners on a
  smooth subdivided model (#108).
highlight: Creased edges stay sharp as you add levels
added: **Crease** / **Uncrease** in Tool > Geometry > Edges (#108): a crease value (0..1, 1 = fully sharp) on the
  selected edges of the base level, one undo step each, saved in the project (an optional blob: older versions open
  the file without creases) and kept on the base while stepping levels. Creased edges are drawn in blue on the base
  level in every mode. The `edge_crease` command does the same from scripts. **Add Level honours creases**
  (Catmull-Clark and Loop semi-sharp creases: sharpness 10 x crease², s - 1 per level, fractional blending):
  creasing at Level 1 under existing levels reshapes them with their detail; Remove Below keeps what is left of the
  creases on the new base. Without creases subdivision is bit-for-bit unchanged. **USD** export / import carries
  them (`creaseIndices` / `creaseLengths` / `creaseSharpnesses`, `subdivisionScheme` catmullClark or loop on a
  creased cage); glTF, OBJ, FBX, PLY and STL have none (documented). Thanks _tfx on Discord for asking; the
  plan for edge creases is @NicTanghe's (#108).
testers:
  - Any OS: new Cube, Tool > Geometry > Edges mode, Select by Angle, Crease (1.00): the 12 sides turn a thick orange;
    Deselect All: they show blue. The line under the buttons counts the creased edges.
  - Crease 0.25 on one loop: a thinner, dimmer blue line than the sharp ones. Undo / Redo flips it back and forth.
  - Leave Edges mode (pick a brush): the blue lines stay on the base level; Add Level: they go away (they live on Level 1);
    Lower Level: back.
  - Save, reopen: the creases are there. Open the file in 0.11.13: it opens (without creases).
  - Windows at 150% / 200%: the Crease slider and buttons fit the Geometry panel.
  - Crease a new Cube's 12 sides (Select by Angle, Crease 1.00), Add Level 4 times: still a cube, flat faces
    and razor-sharp edges. Crease 0.5 instead: crisp edges with a small round-off.
  - Creature Head with levels: Level 1, crease a loop round the eye, step up: a crisp line round the eye, the
    sculpted detail still there. Uncrease, step up: smooth again.
  - Crease a Cube's sides, File > Export USD at Level 1, open it in Blender (or usdview with subdivision on): the
    same sharp edges. Open the .usda in SculptForge again: the creases are back (blue lines).
---
Slices 2-4 of #108: creases stored, subdivision honours them, USD export/import.
