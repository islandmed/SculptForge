---
whats_new: **Polygon mode Bevel on faces** rounds only the outline of the selected faces (one side of a gridded box keeps its inner cells), and **Extrude > Inset** is now a share of the selected faces' width.
fixed: Polygon mode, Bevel with Faces picked bevelled every edge inside the selection (one side of a 4x4 cage box turned into a waffle, all faces into a mess). It now bevels the selection's border only; a selection with no border (all faces of a closed Part) bevels the sharp edges (over 30 degrees), so a gridded box rounds its 12 box edges. A smooth Part with everything selected says there is no edge to round.
changed: Polygon mode, Extrude **Inset** is a fraction of the selected region's narrowest width (per separate region; same 0 to 0.25 range, same `inset` parameter for poly_op and MCP) instead of a fraction of the Part's size, so 0.1 takes a tenth off each side of one cell of a grid and the cap no longer collapses into a stub. Scripts that passed a larger inset for big regions get proportionally more inset on small cells and less on large regions.
testers:
  - Tool > Polygon > Polygon mode, Parts > Append > Low-poly > Cube with Segments 4: double-click one side, Bevel; only the side's outline rounds, the cells inside stay flat.
  - Same cube, Polygon > All, Bevel: a rounded box, the cell grid unchanged.
  - Click one cell, Extrude with Inset 0.1: the cap keeps four fifths of the cell's width, a clean tapered boss.
---
Reviewer note: PolyBevel.cpp faceBevelEdges (border, else >30 degree edges), PolyExtrude.cpp pieceExtents (per piece narrowest width,
candidates = the border edges' in-plane normals, one per degree) and ExtrudePlan::Moved::insetExtent.
