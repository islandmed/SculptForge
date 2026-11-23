---
fixed: Carving brushes that pull the surface toward the stroke (Engrave, Crease Line, Panel Groove and the others in the Brush Lab packs) left a ruffled, sawtooth wall when one stroke went up and down the same line a few times (a member's report): every pass pulled the walls closer until the triangles crossed. A vertex now slides toward the stroke only so far in one stroke (about 0.15 of the brush radius, easing off from there), so more passes deepen the groove without squeezing it. A line or two looks as before.
testers:
  - Windows: pick Engrave, Size about 150 px, Intensity 50; in ONE stroke (pen or mouse never lifted) drag up and down the same line five times on a Sphere (81.9k). The walls stay smooth (before: a feathery, ruffled wall); one or two passes look as before.
  - Same with Crease Line; and a short Engrave line on a head: unchanged.
---
Reviewers: src/core/Sculpt.cpp (dabRecipeSingle: pinchStart_/pinchStamp_ per stroke, kPinchReach), tests/test_carve_pinch.cpp (3 laps of one stroke for every plain pinching recipe, Engrave 8 laps, Engrave on a Micro level). Not fixed here: separate strokes stacked in one groove, and the pattern recipes (Wrinkle Bunch, Deep Fissure, ...), which still fold when stacked deep; a groove deeper than about two brush radii still leans its wall past vertical (the cut itself).
