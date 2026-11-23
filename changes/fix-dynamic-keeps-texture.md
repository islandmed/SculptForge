---
whats_new: **Dynamic** keeps a painted Part's texture: sculpting with Dynamic on no longer erases the paint.
fixed: Dynamic topology no longer erases a Part's painted texture: the UVs are kept through the stroke (new triangles get UVs from the ones they split; a UV seam's vertices are never merged), so the paint stays in place and Undo restores the UVs exactly.
testers:
  - Windows or macOS: paint a Part into its texture (Paint into Texture), turn Dynamic on and sculpt over the paint, big and small strokes, across a seam if the Part has one; the paint stays. Undo, then redo; save and reopen.
---
Reviewers: src/core/Dyntopo.cpp (UVs per triangle slot in TopologyTriangleValues; split/collapse/flip/relax), tests/test_dyntopo.cpp.
