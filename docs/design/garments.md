# Garments: design notes

Pattern panels drawn flat at real size (millimetres), sewn edge to edge, draped on a body by the cloth solver
(docs/design/cloth.md, core/Cloth.h) and turned into a Part whose UVs are the flat layout. The code is
src/core/Garment.h (the model, its notes in the header), GarmentMesh.cpp (even triangulation, in-house constrained
Delaunay), GarmentFiles.cpp (SVG / DXF), GarmentCommands.cpp (garment_*) and src/app/PanelGarments.cpp (the
Pattern window).

## Placement

The user places each panel: a click on the body says where it goes (part 3). There is no automatic body analysis,
no body-part slots, bounding volumes or arrangement points, and no initial position computed from the body's
shape. (Removed from part 1 on 11-06 before it shipped; see "Do not build".)

## Sewing

The user picks both edges of every seam. A seam joins whole edges (or ranges along them the user chose); the
window shows both lengths, green when they match, red when one side is eased in. Nothing snaps a seam's ends to
make the lengths equal.

## Do not build (patents)

Active patents read on these (search: ~/Work/.sf/reports/garment-patents.md, 2026-11-06; not legal advice).
None of them may be added, in any form, without the owner's explicit OK:

- Automatic placement from the body: feature points, per-body-part bounding volumes, arrangement points, initial
  pattern positions computed from the avatar.
- Automatic sewing from placement (sewing what ends up near each other).
- Seam suggestions by 3D distance or orientation.
- Auto-grading from avatar-to-avatar strain.
- Matching patterns against a database of pattern blocks and sewing from it.
- 3D duplicate (or mirror) of a panel that adds its seams automatically. Duplicate and Mirror Copy stay 2D with no
  seams added.
- A measuring tape across several pieces.
- A shirring tool.
- Snapping a seam's end points so the two lengths come out equal.
