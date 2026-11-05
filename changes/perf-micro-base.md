---
fixed: **Big brushes on scans with micro detail**: on a micro level over a dense scan, a large Standard, Clay or
  Inflate brush now moves the base surface the way it does on a plain mesh, and the micro detail rides along
  untouched (one undo step, as before). A big Clay stroke there could freeze the app for seconds, throw spikes
  hundreds of units long and coarsen the whole level's detail; orbiting after such strokes could drop below one
  frame a second. Masks, hidden faces and alphas keep the detail brush.
---
Testers: open a large scan, add a micro level (Micro 1 or 2), and sculpt with a 120-240 px Clay brush: strokes
should follow the pen, orbit right after should stay smooth, and Ctrl+Z should undo each stroke whole.
