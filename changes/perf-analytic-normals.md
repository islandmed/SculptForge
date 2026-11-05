---
changed: **Big brushes keep up better on 6+ core machines**: a large brush's lighting now follows the brush's own
  shape while you drag, and the exact shading lands behind it and at pen-up, so big strokes stay closer to the pen.
  Machines with fewer cores keep the previous behaviour.
testers:
  - A desktop or a recent laptop (6+ cores): a 15M+ mesh, Clay at 240 px, long strokes. The stroke stays near the
    pen and the shading looks right after release (no faceting, no seams).
---
Perf plan item 2. Gate: !simulatedFloorTier() && hardware_concurrency() >= 6; SF_ANALYTIC_NORMALS=0/1 forces it.
Scoreboard A/B in docs/design/perf-scoreboard.md (floor tier: identical to main by construction; laptop + 3060 measured).
