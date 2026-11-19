---
whats_new: **Dense meshes move smoothly**: on a mesh of millions of triangles (a high Voxel Remesh, say), the Gizmo's
  Move, Rotate and Scale and the Move brush now follow the pen; before, a 7M-triangle mesh dragged at under one frame a
  second.
highlight: Gizmo and Move brush smooth on 7M+ triangle meshes
fixed: The Gizmo and the Move brush were very slow on dense meshes (millions of triangles without a micro level, e.g.
  a Voxel Remesh at a high resolution): every pointer sample moved the whole region, so a slow frame took more samples
  and the next was slower. A Gizmo drag of a whole Part (no mask, nothing hidden) is now drawn by the graphics card and
  the vertices move once when you let go; masked Gizmo drags and the Move brush move the vertices once a frame,
  leaving the same surface as before. Big Move drags on meshes of a million vertices or more use Live Move
  (Preferences > Performance) automatically. Floor-tier laptop, 7M triangles: Gizmo move 0.2 to 121 fps, Move brush
  240 px 1.1 to 206 fps (the surface 12 ms behind the pen instead of 4 s). The move is applied when you lift the pen,
  so a big drag on such a mesh ends with a short pause (about 0.1-0.5 s at 3-14M triangles).
testers:
  - Any OS (macOS especially): Tool > Voxel Remesh at a resolution that gives 7M+ triangles; press W and drag the
    centre, then R and turn a ring: the model follows the mouse smoothly; let go and Ctrl+Z puts it back.
  - The same mesh: Move brush at a large size (half the head): the surface follows the pen, and lifting the pen
    leaves it where you dragged it.
  - Mask half the mesh, Gizmo move: only the unmasked half moves, as before.
---
Bench rows: floor tier, x3 vs 0.13.7 with the same bench phases (bench.sh dense).
