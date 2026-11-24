---
whats_new: **Edge loops**: **Tool > Geometry > Edges > Insert Loop** adds new loops of edges across the ring of quads through the selected edge (Loops, Slide); **Select Ring** picks that ring.
highlight: Insert edge loops
added: **Edge loops** (Tool > Geometry > Edges): **Insert Loop** splits the ring of quads through the selected edge with one or
  more new edge loops, evenly spaced, **Slide** moving them towards either side; **Select Ring** selects that ring of edges. The
  loop runs round the model or stops at a border, a triangle (split in two) or a quad it already crossed, and keeps face
  groups, UVs, seams, creases, vertex paint, mask and layers. One undo step; the new loops are selected afterwards. Scripts:
  `edge_loop count= slide= edge=` and `edge_select op=ring`. Members' top request (#225).
member_request: true
testers:
  - Any OS: Tool > Geometry > Edges mode, click one edge of a cube, Select Ring; the ring goes round the cube.
  - Insert Loop with Loops 3 and Slide 0.5; the three new loops sit towards one side, selected; Ctrl+Z removes them in one step.
  - A cylinder: click a rim edge, Insert Loop; the loop runs down the side and splits the two cap triangles it reaches, no holes.
---
Core: src/core/EdgeLoopInsert.{h,cpp} (pure: edgeRing, edgeLoopPreview, insertEdgeLoops; edge_loop command), op=ring in edge_select,
tests/test_edge_loop_insert.cpp (Euler characteristic and manifold checks on cube, grid, cylinder, torus, Moebius strip, a
self-meeting ring, pure triangles, non-manifold and empty input), tests/ui/edge_loop.sfui.
