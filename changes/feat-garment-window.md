---
added: **Garments, part 2: the Pattern window** (Tool > Garments > Pattern): the garment's panels flat at real
  size over a 10 / 100 mm grid with a scale bar. Start from a T-shirt or a rectangle, or import an SVG; drag
  points (snapping to the 5 mm grid and other panels' points; Ctrl drags freely), curve handles or whole panels;
  Alt+click an edge to add a point, Delete removes it, C curves an edge. Sew by clicking two edges: every seam is
  listed by its edges' names ("Front left side <-> Back left side") with both lengths, green when they match, red
  when one side is eased in; hover a seam or an edge to see what it is sewn to. Duplicate, Mirror Copy (the
  other sleeve), Mirror, rename, particle distance, fabric and thickness; export SVG or
  DXF. Ctrl+Z / Ctrl+Shift+Z over the window undo and redo pattern edits. In English, Spanish and Chinese.
testers:
  - Any OS: Tool > Garments > T-Shirt; drag a corner of the front: the side seam turns red with both lengths.
    Ctrl+Z over the window puts it back. Export SVG and print it at 100%: the front is 52 cm wide.
---
Part 2 of 3 of Garments. src/app/PanelGarments.cpp; every edit is a garment_* command (the new garment_edit
from this branch), so scripts and the AI assistant can do what the window does. The window keeps its own undo of
the pattern (projectSettings is outside the per-Part mesh undo). UI test tests/ui/garments.sfui. Drape and
Convert to Part come in part 3 with the cloth solver.
