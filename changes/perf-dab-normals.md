---
whats_new: **Big brushes on big meshes** follow the pen closer on 4-core machines: a 240 px brush on a 28M-triangle scan lags about a third less.
changed: Big brushes on dense meshes keep a shorter queue of pending dabs when each dab is slow (about 100 ms of work
  instead of always four dabs): a 240 px Clay stroke on a 28M scan with 4 threads lags 233 ms instead of 360 (median)
  and finishes 0.3 s sooner after pen-up; small brushes are unchanged.
testers:
  - 4-core laptop, a 20M+ mesh: a long fast stroke with a 240 px Clay brush; the stroke stays close behind the pen,
    no gaps, same depth as before.
---
Lever: the stroke backlog floor (kStrokeBacklogMinDabs = 4) times a 70 ms dab was most of the lag. Analytic normals
stay gated on the floor tier (release +170 ms, no lag gain there).
