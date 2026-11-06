---
whats_new: **Remeshing an open Part stays solid**: **Quick Remesh** and **Tool > Remesh > Voxel Remesh** cap open
  borders (the Stylised Head's neck, a cut) before remeshing, so the result is one closed surface, not a shell full of
  holes. A Part that can't be made solid stays as it is, and the status bar says why.
whats_new: **UV Editor at large UI scales**: the toolbar wraps on a 4K screen at 240 %, so **On model: Colours /
  Checker** and **Frame** are all visible and each takes its own click.
fixed: **Quick Remesh** and **Tool > Remesh > Voxel Remesh** of an open Part (the Stylised Head starter, open at the
  neck, especially after a Move pull near the neck) gave a thin shell full of holes: the outside flowed in through the
  opening. Open borders are now capped before the voxels are made (only for the remesh; the Part's own surface is
  not touched), and a Part that still isn't a closed solid (borders that can't be capped) is left unchanged with a
  message pointing to **Tool > Geometry > Close Holes**, instead of a broken result.
fixed: **UV Editor** at UI scale 2.4 (a 4K screen): the **On model: Colours / Checker** row was cut off and **Frame**
  was drawn over it, so clicking Frame toggled Colours. The toolbar now measures each control (in the interface
  language) and wraps onto more lines when it needs to; every control is where it is drawn.
testers:
  - Any OS: File > New from starter mesh > Stylised Head; Move brush, pull the front of the neck down and forward;
    Tool > Remesh > Voxel Remesh at Resolution 123: a closed head, no holes (turn the view to look into the neck).
    Ctrl+Z, then Quick Remesh on and Shift+R: the same.
  - Windows / Linux on a 4K screen: Edit > Preferences > Interface > UI scale 240 %; Alt+U opens the UV Editor:
    the toolbar wraps, nothing is cut off; click Colours, Checker and Frame: each does its own thing.
---
Reviewer notes:
- Voxel.cpp voxelRemesh: open edges (undirected count 1, so flipped triangles don't hide them) -> closeHoles on a
  positions+triangles copy (refine off: no new vertices; attributes still transfer from the original). Then, only for
  open input, a leak check: solid cells < half the capped mesh's volume in cells -> empty result + reason. Closed
  meshes take exactly the old path.
- tests: test_quick_remesh.cpp voxel_remesh_open_starter_head_stays_solid (Stylised Head, unpulled and Move-pulled,
  res 64/123/200: closed, one piece, volume within 8 % of the capped head) and voxel_remesh_uncappable_open_mesh_says_why;
  tests/ui/remesh_open_mesh.sfui (both buttons in the app), tests/ui/uv_editor_scales.sfui (+ _4k at 3840x2160).
