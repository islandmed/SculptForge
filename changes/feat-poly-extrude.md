---
whats_new: **Extrude** (Polygon mode): push selected polygons out along their normals, as one region or one by one, with an optional inset; extrude open border edges into a strip.
added: **Extrude** (Polygon mode, members' top wish): selected polygons move out along their normals (or in, with a
  negative distance) and get side walls, as one connected region or as individual polygons with a groove between
  them; an optional inset narrows the cap first; open border edges extrude into a strip of quads along the surface,
  the normals or an axis. Caps keep their face groups, UVs, creases, mask and paint; the walls are quads in a new
  face group. The whole op is one undo step.
member_request: true
testers:
  - Any OS: Polygon mode on a box, select one side, Extrude 0.5; the side stands out as a block with quad walls in a new face group; Undo takes it back in one step.
  - Any OS: select two touching sides, Extrude along normals: each side moves the same distance (the shared edge goes out diagonally); Region keeps the shape and moves both along one direction.
  - Any OS: a plane, select its border edges, Extrude edges: the strip continues the plane, corners go out diagonally.
---
Core op in src/core/PolyExtrude.h (pure: mesh + selection in, mesh + new selection out); the panel entry comes with
the Polygon mode foundation (feat/poly-mode). Tests: tests/test_poly_extrude.cpp.
