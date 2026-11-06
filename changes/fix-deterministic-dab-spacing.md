---
whats_new: **Brushes behave the same twice**: the same stroke places the same dabs whatever the frame rate, so a big brush on a big mesh no longer gives a stronger or weaker stroke from one try to the next.
fixed: Under load, which dabs a stroke skips was decided by how long the dab queue was when a frame came round, so
  the same stroke placed different dabs at 30, 60 or 144 Hz (one scripted stroke: 0.49-1.33x the volume from run to
  run) and lost dabs through a stall. Dabs now stay on the brush's even spacing, and which are kept is decided on the
  input's own timestamps from what the stroke's first dabs cost (rounded, so timing noise doesn't move dabs): the
  same input gives the same dabs at any frame rate, the backlog
  limit (and the 150 ms floor for slow dabs) still keeps the brush near the pen, and the stroke ends where the pen did.
testers:
  - Windows or a 4-core laptop, a 20M+ mesh: the same fast stroke with a 240 px Clay brush three times; the three
    strokes should look alike (no gaps, the same depth), and the brush stays close behind the pen.
---
Cause found by dab-normals (frame-time thinning in strokeTo). Unit replay at 30/60/144 Hz + 250 ms stall: identical dabs. Data: ~/Work/.sf/dabspacing/.
