---
whats_new: **Cut** caps and walls come out as clean, even triangles about the size of the mesh's own, ready to sculpt without a remesh.
highlight: Cut caps are clean, even triangles instead of thin slivers (thanks dm4nd on Discord)
fixed: Cut: the flat cap and the walls a cut leaves were fans of long thin triangles between the outline's points (the
  cut looked messy in Wire and only a remesh fixed it). They are now filled with even triangles about the mesh's own
  edge length, and the triangles inside each cut mesh triangle are rearranged the same way. The outline stays exactly
  where it was, so the result is as watertight as before (thanks dm4nd on Discord).
testers:
  - Any OS: New sphere (low poly, e.g. Subdivisions 2, and the default), Wire on. Cut Line through the middle, then
    turn to the cap: even triangles, no fan of needles from one corner. Cut Rectangle and Cut In Lasso the same way;
    sculpt a stroke across the cap: it deforms like the rest of the surface.
---
