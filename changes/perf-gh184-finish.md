---
whats_new: **Big micro levels settle after strokes**: on a base of millions of triangles at micro level 4, the view
  held still no longer stays at a few frames a second after you sculpt; the full detail fills in over a second or two
  and the view is smooth again.
fixed: **Micro level 4 slow after strokes on big bases** (#184): after strokes on a base of about 2.5M triangles or
  more at micro level 4, every frame went through each base triangle again and loaded only a small batch of full
  detail, so even a view held still stayed at a few frames a second. Unchanged frames now skip that pass, as much
  of the wanted detail loads as fits the frame's time budget, and a full detail budget stops loading instead of
  trying again each frame. On a 2.5M base at micro level 4 the view held still after Clay strokes is smooth again
  within a couple of seconds.
testers:
  - Linux with a pen tablet (Wayland and X11), and any OS: a base of 2.5M+ triangles (Voxel Remesh high, then
    Subdivide twice), Tool > Geometry > Add Micro up to level 4; draw a few large Clay strokes, then hold still: the
    view is smooth again within a couple of seconds and the full detail appears where you look, with no cracks between tiles.
  - The same model: orbit after the strokes, then Ctrl+Z and Ctrl+Shift+Z: the detail follows each step.
---
Not in this branch: turning the view after such strokes is still slow (each new view looks at every base triangle),
and a large Clay stroke at micro 4 on a 2.5M base draws at about 3 fps (0.13.7 the same).
Fixes #184.
