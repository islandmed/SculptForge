---
fixed: **Predictive strokes** (Preferences > Performance, experimental): on a dense mesh the stroke preview's head no
  longer jumps off the stroke when the pen's samples arrive in a burst. Pen and mouse samples are now timed when they
  happened, not when they were handled, and samples under 2 ms apart count as one instant. On the floor tier with a 240 px
  brush on a 28M-triangle mesh, the preview now trails the pen by about 35 ms at p99 instead of about 230 ms. Thanks @Calavera00 (#216).
---
Event timestamps through App (motion, press) and Tablet (pen -> mouse); ProofMode stamps its scripted samples the same way.
