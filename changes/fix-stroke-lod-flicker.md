---
fixed: **Sculpting on very dense meshes no longer flickers between full detail and a smoother look.** On a mesh of several
  million triangles that is too heavy to show in full detail while you work, a stroke used to draw the whole surface in full
  detail and, as soon as it ended, drop back to the lighter preview, so quick strokes made the wrinkles come and go (and every
  stroke frame was a full-detail frame). The surface now keeps one look while you sculpt: the lighter preview everywhere,
  with the part you are editing in full detail, and no gaps between the two. Turning Wire on and off is no longer needed.
  A user report.
changed: **Tool > Geometry > Micro Detail**: before Add Micro, the slider reads **New Micro 2 (83M)**: what Add Micro would make,
  not a level the mesh already has.
added: Windows laptops with two graphics chips (Intel and NVIDIA or AMD) now start SculptForge on the faster one without
  a setting; **Preferences > Performance > Graphics card** and Windows' own graphics settings still decide if you pick one.
testers:
  - Windows laptop with Intel graphics plus an NVIDIA or AMD card (GTX 1650 class), on a fresh install with no Windows
    graphics setting for SculptForge: start it from a terminal or open the log; the `OpenGL ... on ...` line names the NVIDIA
    or AMD card, not the Intel one. Then set **Preferences > Performance > Graphics card** to the Intel one and restart: the line
    names Intel.
  - Any machine: open a mesh over about 3 million triangles (Subdivide a sphere to level 7), make fast Clay strokes back and
    forth. The surface keeps the same look from frame to frame (no whole-surface flicker), the edited part shows full detail, and
    no dark gaps appear along the edge of the edited part.
---
