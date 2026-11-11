---
whats_new: **Crisp Flatten** (Hard Surface pack; thanks _tfx on Discord): flattens to the plane the stroke starts on with a clean, sharp edge instead of a soft fade, for hard planes and crisp bevels. In Brush Lab, any custom brush can use the new **Lock plane** and **Crisp edge** options.
highlight: Crisp Flatten: hard planes with a sharp edge
added: **Crisp Flatten** brush in the Hard Surface pack (thanks _tfx on Discord): the plane is taken where the
  stroke starts and held for the whole stroke, and the edge is hard with a thin anti-aliased band (about 2.5 polygons
  wide), so one stroke cuts one flat facet and two meet in a crisp bevel. It cuts what is above the plane and fills
  only shallow dips, so it never builds a wall past a curving form. Works with symmetry, masks and Dynamic
  topology. Brush Lab recipes get **Lock plane** (off: the plane follows the surface) and **Crisp edge**, also as
  `planeLock` / `crispEdge` in .sfbrush files and `brush_create`.
testers:
  - Windows at 100% and 150%: Sculpt > Brush > Crisp Flatten on a Rounded Box starter mesh; drag across a rounded
    edge: a flat facet with a sharp, smooth boundary (no jagged steps). Drag again on the side: a crisp bevel.
  - Symmetry X on: both sides get the same facet. Mask half the box: the masked half doesn't move.
  - Dynamic topology on: the stroke still leaves a flat facet with a clean edge.
  - Brush Lab > Recipe on a copy: Lock plane off, the plane follows the curve; Crisp edge off, the soft edge is back.
---
Clean-room: our own design on the existing recipe kernel (core/Sculpt.cpp dabRecipeSingle); no outside brush pack was
looked up or copied.
