---
whats_new: **Drape**: **Tool > Drape** drops the active Part as cloth onto the other Parts (a sheet over a chair, a
  shirt over a body), live, with Silk, Cotton, Denim or Leather; the mask pins it; at rest it is one undo step.
added: **Drape** (Tool > Drape, `cloth_drape`): the active Part simulated as cloth (our XPBD solver) until it rests on
  the other visible Parts; fabric presets, real-size scale (units per metre), pins from the mask, Simulate / Pause /
  Keep / Reset with a live view, one undo step. The cloth doesn't pass through thin, sharp shapes (needle tips,
  fingers, curls) or through the body when it falls fast. Tip: drop the cloth from in front of overhanging shapes
  (beards, chins), or it settles behind them.
testers:
  - Any OS: a sphere Part, then Append a plane above it, Tool > Drape > Simulate: the plane falls, wraps the ball and
    stops; Ctrl+Z puts it back.
  - Mask one edge of the plane first: it hangs from that edge like a curtain.
  - Any OS: a scan with fingers or curls, Append a plane above it, Tool > Drape > Simulate: no fingertip or curl pokes
    through the cloth at rest.
  - A plane dropped from high above a sphere (fast fall): it lands on the ball, never inside it.
  - Windows: a 2M-triangle body and a 20K-vertex cloth Part: at rest within 10 s.
---
Phase 2 of docs/design/cloth.md, with continuous collision (fix/drape-collision 1463c0ad: needles, curls under a fast
sheet, shelf, moved Part); the cloth brush keeps the plain contacts (per-dab cost).
