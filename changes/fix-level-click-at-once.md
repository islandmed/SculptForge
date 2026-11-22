---
whats_new: **Level slider**: a click switches the level at once again, in every layout; a double-click still opens the number field.
changed: The Tool > Geometry Level slider applies a plain click at once, as before 0.13.7 (it waited about 0.35 s for a possible
  double-click). A double-click still types a number: its first click has already switched, Esc leaves that level.
testers:
  - Any OS, Classic and SculptForge layouts: on a Part with 3+ levels, click the Level slider at another level; it switches on the click, no pause.
  - Double-click the slider: the number field opens; Esc leaves the level, Enter switches to the number.
---
Audit: ~/Work/.sf/state/classic-drift (row 2). No setting: a click that applies at once suits both layouts.
