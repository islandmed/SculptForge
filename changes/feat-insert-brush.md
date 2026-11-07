---
whats_new: **Insert brush**: a brush on the bottom shelf that drags shapes onto the model in one step: a primitive, a
  Creature bones piece or your own insert set's, standing on the surface, sized and turned by the drag, as a new Part
  (or joining the active Part). Its palette picks the shape; one Ctrl+Z each. Thanks @foirgairwolf.
whats_new: **Forge Stamps**: a seventh brush pack of ten stamps that press a shaped form in or raise it with one click:
  nostrils, ear whorls, scales, rivets, screws, hatch seams, louvres, suction cups, gill slits and sockets, from ten
  new alphas of our own. Thanks @foirgairwolf.
highlight: Insert brush: drag shapes onto the model in one step
added: **Insert brush** (bottom shelf, brush picker, command palette; bindable to a key): drag on the model to place
  the palette's shape (Sphere, Cube, Cylinder, Cone, Torus, Capsule, Quad Sphere, a Creature bones piece or an item of
  your own insert set) on the surface along its normal, the drag setting size and turn; each one a new Part below the
  model (its mirrored copies with it) or, with New Part off, a face group of the active Part. One undo step each;
  [ and ] step through the shapes, Esc stops. `insert_primitive` and `insert_mesh` take `new_part=true`. Requested
  by @foirgairwolf.
added: **Mask the rest** for inserts that join the Part (the Insert brush with New Part off, on by default;
  `mask_rest=true` on `insert_primitive` / `insert_mesh`): everything that was there is masked and the new piece left
  free, ready to sculpt and blend in, in the same undo step. Requested by @foirgairwolf.
added: **Forge Stamps** brush pack (Sculpt > Brush, sorted by pack): Nostril Press, Ear Whorl Press, Scale Plate Stamp,
  Rivet Head Stamp, Hatch Seam Cut, Louvre Cut, Cross Screw Stamp, Suction Cup Stamp, Gill Slit Cut and Socket Press,
  with ten new procedural alphas (Nostril, Ear Whorl, Scale Plate, Rivet Head, Hatch Seam, Louvre, Cross Screw,
  Suction Cup, Gill Slits, Socket) that work with any brush. Requested by @foirgairwolf.
testers:
  - Any OS: click Insert on the bottom shelf, pick Cone in its palette, drag on the sphere; a Cone Part appears
    standing on the surface (and its X mirror), the model stays active. Ctrl+Z removes it in one step.
  - Any OS: in the Insert palette pick a Creature bones piece, drag; untick New Part, drag a Cube: it joins the model,
    everything else masked (brushes only move the cube); Ctrl+Z takes the cube and the mask back.
  - Any OS: Sculpt > Brush > Sort: Pack > Forge Stamps: click once with Nostril Press and Rivet Head Stamp on a
    subdivided head; each leaves one clean form.
---
Discord #ideas 1568513006118113321 and 1568553579323588630 (Mask the rest). Insert brush: src/app/InsertBrush.cpp, new_part in core/Parts.cpp insertPiece.
