---
whats_new: **Record a performance trace** (Help menu, #127): turn it on, do what feels slow, turn it off and attach sculptforge-trace.log (Help > Open log folder) to your report.
added: **Help > Record a performance trace** (#127, thanks RodTDai): while on, `sculptforge-trace.log` beside
  `sculptforge.log` records every frame over 33 ms with where its time went (events, dabs, UI, GPU uploads, drawing,
  GPU time), each stroke's timings, commands that held up the frames, background jobs starting and ending, the mesh
  size, subdivision and micro level, and the GPU, driver and CPU. Off by default and costs nothing when off; two files
  of 2 MB at most (`sculptforge-trace-previous.log`); a local file only, nothing is sent anywhere.
testers:
  - Windows: Help > Record a performance trace, sculpt on a big mesh and orbit, run Voxel Remesh, turn it off; Help > Open log folder shows sculptforge-trace.log with "long frame", "stroke", "command" and "jobs" lines and your GPU on the first line.
  - Any OS: restart; the trace is off again.
---
Reuses the main loop's phase marks (SF_FRAME_LOG), the stroke's SculptTimings and the frame governor's GPU timer.
