---
fixed: **Drape** no longer lets the cloth pass through thin, sharp shapes (needle tips, fingers, curls) or
  through the body when it falls fast: the cloth now keeps track of which side of each sharp point it is on and checks
  the path each particle takes in a frame. Tip: drop the cloth from in front of overhanging shapes (beards, chins),
  or it settles behind them.
testers:
  - Any OS: a scan with fingers or curls, Append a plane above it, Tool > Drape > Simulate: no fingertip or curl pokes
    through the cloth at rest.
  - A plane dropped from high above a sphere (fast fall): it lands on the ball, never inside it.
  - Windows: a 2M-triangle body and a 20K-vertex cloth Part: still at rest within 10 s.
---
Continuous collision for Drape (docs/design/cloth.md phase 2 follow-up). The cloth brush keeps the plain contacts
(per-dab cost). Unit tests: needles, curls under a fast sheet, shelf, moved Part.
