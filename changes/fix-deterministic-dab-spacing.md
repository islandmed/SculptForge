---
whats_new: **Brushes behave the same at any frame rate**: a stroke places the same dabs whether the app runs at 30, 60 or 144 frames a second or stalls for a moment, so a big brush on a big mesh no longer gives a stronger or weaker stroke depending on how fast the screen draws.
fixed: Under load, which dabs a stroke skipped was decided by how long the dab queue was when a frame came round, so
  the same stroke placed different dabs at 30, 60 or 144 Hz (one scripted stroke: 0.49-1.33x the volume) and lost
  dabs through a short stall. Dabs now stay on the brush's even spacing, and which are kept is decided on the input's
  own timestamps; the backlog limit (and the 150 ms floor for slow dabs) still keeps the brush near the pen, and the
  stroke ends where the pen did.
testers:
  - Windows or a 4-core laptop, a 20M+ mesh: the same fast stroke with a 240 px Clay brush at a high and a low frame
    rate (a busy second monitor, or vsync on and off); the strokes should look alike (no gaps, the same depth), and
    the brush stays close behind the pen.
---
Cause found by dab-normals (frame-time thinning in strokeTo). Unit replay at 30/60/144 Hz + a 120 ms stall: identical dabs; a pinned count for a fixed stroke. The floor bench now feeds the same samples every run (timed from the first sample, not the phase start). Data: ~/Work/.sf/dabspacing/.
