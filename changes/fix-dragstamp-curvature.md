---
whats_new: **Big stamps on curved surfaces**: Drag Stamp, Slide Stamp and every alpha brush now cover their whole footprint on a curved surface, with no untouched gaps.
fixed: **Alpha stamps leaving gaps on curved meshes** (#174, thanks @Devon0209): a large Drag Stamp or Slide Stamp (any alpha, sculpt or paint) only reached the vertices within its radius in space, so where the surface curved away from the view part of the stamp's footprint stayed untouched. The stamp now also takes the visible surface under its footprint.
testers:
  - Any OS: a dense head or sphere, Drag Stamp with a square alpha, drag a stamp larger than the curved area (sculpt, then Vertex Paint): the whole square is stamped, no gaps or splotches.
  - Same with Slide Stamp and a plain Stamp click; the stamp still does not reach the far side of the model. Dragging shows the stamp as before and the release fills the footprint.
  - On a 28M-triangle model at the floor tier, the largest alpha stamps take about 100 ms longer, once per stamp.
---
