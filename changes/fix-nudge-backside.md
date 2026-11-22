---
whats_new: **Nudge and Layer with View falloff** only work the surface facing you: the far side of a thick part no longer moves with it. Move and Grab still carry everything inside the circle.
fixed: Nudge and Layer with **View falloff** (Brush palette) also moved the far side of the model, which showed after orbiting (a member's report): like Standard, Inflate, Crease and Pinch (0.13.11), they leave what lies more than 3 brush radii behind the cursor along the view, fading in from 1.5. A thin part (an ear, a horn) is still reached through. Move and Grab are unchanged.
testers:
  - Windows: Brush palette > View falloff on, Nudge a few strokes across the front of a thick part, orbit to the back; the back is untouched (before: it was swirled too).
  - Same with Layer; and Nudge on a thin part (an ear): both sides still move.
---
Reviewers: src/core/Sculpt.cpp (depthReach in dabSingle gains Layer and Nudge), tests/test_nudge_view_falloff.cpp. Tendril has the same gap and is left to tendril-sheets-9e (cto 11-22).
