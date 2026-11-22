---
whats_new: **Gizmo on heavy Parts**: pressing and dragging a handle (arrows, balls, rings, squares) stays as smooth as turning the view, also on multi-million triangle Parts.
fixed: **Gizmo slow on dense Parts**: grabbing and dragging a Gizmo handle on a 5M triangle Part took 20-90 ms per frame (balls, which scale along one axis, worst), against about 1 ms now. Every drag frame no longer walks the whole mesh for its bounds, an axis scale is drawn by one matrix like a move, a turn or an even scale (its shading corrected, the surface after the release is exactly what it was), and the press does its setup in parallel.
testers:
  - A Part of 3M triangles or more (a dense sculpt or a Voxel Remesh). Shift+W (Gizmo All): hover and drag a move arrow, a scale ball, a ring, a plane square and the centre; the view should stay as smooth as orbiting.
  - Drag a scale ball and let go: the surface and its shading look the same before and after the release. Ctrl+Z restores the Part exactly.
---
Reviewers: src/app/Gizmo.cpp (grid size read once and only with Shift; axis scale joins the matrix path; BVH taken at the release),
src/core/MeshTransform.cpp (begin: no search for an unweighted Part, threaded copies), src/render/Renderer.cpp (uNormalFix:
the normals of a stretched Part, identity otherwise), src/app/Demo.cpp (SF_UI_FRAME_LOG per-frame log for measuring).
Tests: tests/test_transform.cpp transform_axis_scale_drawn_by_one_matrix, tests/ui/gizmo_gpu.sfui (axis scale by matrix + same surface as SF_GIZMO_GPU=0).
