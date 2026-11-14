---
fixed: **No hitch when the view turns after a big stroke on a dense mesh**: the distance levels a stroke changed are
  now updated a few milliseconds a frame instead of all in the first frame after it. A 33-million-triangle head's
  slowest frame orbiting after a 240 px stroke: 85 ms to 18 ms at full detail, 103 ms to 21 ms on the entry-level tier.
testers:
  - Windows, a dense mesh (30M triangles or more): a long, large brush stroke, then turn the view at once; no hitch
    as it starts, and the far view looks the same as before once it settles.
---
Cause: Renderer::updateCoarse rebuilt every pending cluster's coarse levels in one sync (memory bound: slower on the
Ryzen 5600X than on the M1 Pro). Now budgeted like buildProxies; updateCoarseLevels' cost follows the batch.
