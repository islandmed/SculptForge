---
whats_new: **Cloth brush**: drag the surface like fabric and it folds where it bunches up; **Sculpt > Brush > Cloth** sets Drag,
  Push or Settle, the fabric (Silk, Cotton, Denim, Leather), gravity and reach. It rests on other Parts and the mask pins it.
highlight: Cloth brush: folds that behave like fabric
added: **Cloth brush**: simulates the surface near the stroke as fabric (our own XPBD cloth solver, docs/design/cloth.md):
  Drag pulls it along and it buckles into folds that keep their length, Push presses it in (Alt pulls), Settle drapes it
  under gravity. Fabric presets Silk, Cotton, Denim and Leather; collides with the other visible Parts; masked areas
  hold like pins; symmetry, sculpt layers, one undo step per stroke. Asked for on YouTube.
testers:
  - Any OS: pick Cloth from the brush shelf, drag across a flat area of a Part: folds form ahead of the brush, nothing
    beyond a few brush radii moves, Ctrl+Z undoes the whole stroke.
  - Brush > Cloth > Settle over a body Part with a shirt Part above it: the shirt sags onto the body, never through it.
  - Windows: a 2M-triangle Part: the Cloth stroke keeps up with the pen like other brushes.
---
Phase 1 of docs/design/cloth.md (solver + Cloth brush). Drape and Garments follow on their own branches.
