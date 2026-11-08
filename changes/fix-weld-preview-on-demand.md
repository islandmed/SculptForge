---
fixed: **Merge by Distance** (Tool > Geometry): the orange dots for the points that would merge show only while
  the distance is dragged, hovered or focused with the keyboard, and fade out after. Before, they showed whenever
  the panel was open, which on a dense model looked like a rendering fault.
testers:
  - Any OS: open a dense model (millions of points), open Tool > Geometry and leave it: no orange dots on the model.
    Drag the Merge by Distance distance: the dots show; let go and move the pointer away: they fade out.
---
UI test: tests/ui/mesh_check.sfui (weld_preview_points; drag_item gains `keep`).
