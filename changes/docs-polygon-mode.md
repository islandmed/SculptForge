---
whats_new: **Extrude** (Polygon mode): push selected faces out or in with quad side walls, as one region or face by face; selected open border edges extrude into a strip.
whats_new: **Inset** (Polygon mode): shrink selected faces inward with a rim of quads, and sink them in or raise them with Depth, as one shape or each face by itself.
whats_new: **Edge loops** (Polygon mode): add one or more loops across the ring of quads through the selected edge, with a live preview of where they go.
whats_new: **Span** (Polygon mode): join two edge loops or two sides of a model with a band of quads, a tunnel through a block or a handle between two shells.
added: **Guide: Polygon mode**: a chapter for Tool > Polygon (selecting points, edges and faces, moving the selection, Extrude, Inset, Bevel, Edge loops, Span, block out then sculpt).
testers:
  - Any OS: open Help > User Guide, Polygon mode: every button and slider the page names (Tool > Polygon, Operations) is in the panel under that name.
---
Docs lane. The chapter is docs/guide/polygon-mode.md: one clearly marked block per operation (`<!-- BEGIN op: x -->` ...
`<!-- END op: x -->`), so an operation that misses the cut is removed in one edit, together with its line above. Polygon mode
and Bevel have their What's New lines in changes/feat-poly-mode.md.
