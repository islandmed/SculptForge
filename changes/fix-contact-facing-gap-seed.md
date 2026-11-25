---
fixed: **Contact facing** with Trim: in a narrow gap the brush could start measuring from the wall across it once the stroke's own cut had lowered the surface under the cursor, and cut that wall.
testers:
  - Any OS: carve a narrow groove on a sphere (Crease, a few passes), turn on Sculpt > Brush > Contact facing, Trim along one edge of it; the wall across stays put. Contact facing off, or Facing angle 180 over a shallow groove: as before.
---
Reviewers: Contact facing off is untouched (keepConnected's new argument is only passed by Contact facing); the stroke is the same unless the cursor is nearer a vertex facing away from the contact than one facing it.
