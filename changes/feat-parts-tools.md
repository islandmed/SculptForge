---
whats_new: **Split > Hidden / Masked / Unmasked** (**Tool > Parts**): move the hidden, masked or unmasked triangles
  of a Part into a new Part just below it, in one undo step. Asked for by foirgairwolf on Discord.
whats_new: **Delete masked** (**Tool > Visibility**) deletes what is masked, as **Delete hidden** does for what is
  hidden. Undoable.
whats_new: **Smooth or flat per Part, right in the list**: click the mark left of a Part's eye to go from the scene's
  shading to Smooth to Flat.
highlight: Split masked, hidden or unmasked into a new Part
added: **Split > Hidden / Masked / Unmasked** (Tool > Parts; `part_split_hidden`, `part_split_masked`,
  `part_split_unmasked`): the triangles leave the active Part for a new Part below it (same folder, visibility and
  shading), one undo step that also removes the new Part. A triangle is masked when its corners' average mask is
  above 50%. UVs, face groups, vertex paint and sculpt layers carry over; subdivision levels are cut at the current
  level (undo restores them). Thanks foirgairwolf on Discord.
added: **Delete masked** (Tool > Visibility; `delete_masked`), the mirror of Delete hidden. Thanks foirgairwolf on
  Discord.
added: A per-Part shading mark in the Parts list (Scene / Smooth / Flat; `part_shading`). Thanks foirgairwolf on
  Discord.
whats_new: **Merge by Distance** (**Tool > Geometry**): merge points closer than a distance, with a live count and the
  points shown on the model before you click. For unwelded imports. Asked for by foirgairwolf on Discord.
whats_new: **Check Mesh** (**Tool > Geometry**): counts non-manifold edges, holes, duplicate, degenerate, zero-area and
  flipped triangles and isolated points, with a **Fix** per row and **Fix all**. Checking never changes the mesh.
added: **Merge by Distance** (Tool > Geometry; `weld_points distance=... preview=true`): the points of the active Part
  within the distance merge into the nearest kept one; collapsed triangles go; one undo step. The count follows the
  slider and the points that would merge show in orange. Thanks foirgairwolf on Discord.
added: **Check Mesh** (Tool > Geometry; `mesh_check`, `mesh_fix issue=...`): non-manifold edges (Fix loosens the extra
  triangles), holes (capped as Close Holes), duplicate and degenerate triangles (deleted), zero-area triangles
  (shortest edge collapsed), flipped triangles and inside-out closed pieces (rewound), isolated points (removed).
  Fix all fixes everything except holes, as one undo step. Thanks foirgairwolf on Discord.
testers:
  - Any OS: mask half a sphere, Tool > Parts > Split > Masked: a new "<name> masked" Part below, unmasked; Ctrl+Z
    once puts the half back and removes the Part.
  - Hide part of a Part (Ctrl+Shift+drag), Split > Hidden: the new Part shows those triangles.
  - Tool > Visibility > Delete masked, then Ctrl+Z.
  - Parts list: click the mark left of the eye three times: Scene (dim), Smooth (disc), Flat (hexagon), back.
  - Open tests/data/check_mesh_cube.obj (each face has its own points): Tool > Geometry > Merge by Distance shows
    "16 points will merge" and orange dots at the corners; Merge leaves 8 points. Ctrl+Z.
  - Check Mesh on a clean sphere: "No problems found". On a broken import: rows with counts; Fix one row, it goes;
    Fix all, then Ctrl+Z once brings every problem back.
  - Windows at 150% scaling: the Check Mesh rows' counts and Fix buttons line up at the right edge.
---
The Discord #ideas request (1568553579323588630) by foirgairwolf: Geometry / Parts panel additions.
