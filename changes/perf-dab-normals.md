---
whats_new: **Big brushes on big meshes** follow the pen closer on 4-core machines and settle sooner after pen-up.
changed: Big brushes on dense meshes keep a shorter queue of pending dabs when each dab is slow (about 150 ms of work,
  at least one dab, instead of always four): a big Clay stroke on a dense scan lags less and settles sooner after
  pen-up, with the same stroke; small brushes are unchanged.
testers:
  - 4-core laptop, a 20M+ mesh: a long fast stroke with a 240 px Clay brush; the stroke stays close behind the pen,
    no gaps, same depth as before.
---
Lever: the stroke backlog floor (kStrokeBacklogMinDabs = 4) times a 70 ms dab was most of the lag. 100 ms (one dab)
cut more lag (262 ms) but placed fewer dabs and a weaker stroke on average (0.90x volume); 150 ms keeps it (0.97x).
Analytic normals stay gated on the floor tier (release +170 ms, no lag gain there). Data: ~/Work/.sf/bench-dabnormals/.
