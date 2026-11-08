---
fixed: **Drape** settles where it used to keep twitching: cloth pressed into a narrow fold of the body (an armpit, the
  inside of an elbow) stays put instead of bouncing between the two sides, and sewn seams no longer pull the cloth
  through the body while they close. It now runs until the cloth has really stopped, which can take a moment longer
  (still under 10 s for a 2M-triangle body).
testers:
  - Any OS: Drape a plane onto a body so it hangs into an armpit or between the arm and the chest: it comes to rest
    (the status says "at rest"), with no flicker in the fold.
  - Garments: sew a T-shirt on the Full Body starter and Drape it: it reaches rest, the back neckline goes round the
    neck (not through it).
---
Solver (src/core/Cloth.*): every particle that moved farther than its contact search reached is swept against the
body (seams pull faster than gravity); a second contact plane in concave corners; the rest test measures how far
particles get over the rest window instead of the last substep's velocity. Tests: seams through a slab, a narrow
wedge, needles stay put after rest, a falling sheet is never at rest.
