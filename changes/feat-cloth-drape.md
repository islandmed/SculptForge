---
whats_new: **Drape**: **Tool > Drape** drops the active Part as cloth onto the other Parts (a sheet over a chair, a
  shirt over a body), live, with Silk, Cotton, Denim or Leather; the mask pins it; at rest it is one undo step.
added: **Drape** (Tool > Drape, `cloth_drape`): the active Part simulated as cloth (our XPBD solver) until it rests on
  the other visible Parts; fabric presets, real-size scale (units per metre), pins from the mask, Simulate / Pause /
  Keep / Reset with a live view, one undo step.
testers:
  - Any OS: a sphere Part, then Append a plane above it, Tool > Drape > Simulate: the plane falls, wraps the ball and
    stops; Ctrl+Z puts it back.
  - Mask one edge of the plane first: it hangs from that edge like a curtain.
  - Windows: a 2M-triangle body and a 20K-vertex cloth Part: at rest within 10 s.
---
Phase 2 of docs/design/cloth.md.
