---
fixed: **Sculpting on very dense meshes no longer flickers between full detail and a smoother look.** On a mesh of several
  million triangles that is too heavy to show in full detail while you work, a stroke used to draw the whole surface in full
  detail and, as soon as it ended, drop back to the lighter preview, so quick strokes made the wrinkles come and go (and every
  stroke frame was a full-detail frame). The surface now keeps one look while you sculpt: the lighter preview everywhere,
  with the part you are editing in full detail, and no gaps between the two. Turning Wire on and off is no longer needed.
  A user report.
testers:
  - Any machine: open a mesh over about 3 million triangles (Subdivide a sphere to level 7), make fast Clay strokes back and
    forth. The surface keeps the same look from frame to frame (no whole-surface flicker), the edited part shows full detail, and
    no dark gaps appear along the edge of the edited part.
---
