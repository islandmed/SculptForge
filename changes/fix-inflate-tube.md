---
whats_new: **Inflate and deflate on thin tubes**: a big brush over a coarse tube or limb now thickens (Inflate) or thins (Sub, or Alt) it evenly, each point along its own normal, instead of moving the area as one lump.
fixed: **Inflate and deflate (Sub or Alt) on coarse, long-quad meshes** (a thin Tube): the check for a packed grid took a tube's end cap ring and its long quads for one, so a brush wider than the tube moved the whole area along one normal (bulbs when inflating, barely any thinning when deflating); it now needs a real packed cluster (at least 32 vertices well under the surface's own edge length).
testers:
  - Any OS: Tool > Tube, draw a tube, Bake, pick Inflate, Size wider than the tube, stroke along it: it thickens evenly and round, not into bulbs near the ends. Then the Sub button (or hold Alt before the stroke) and stroke again: it thins evenly along the tube.
---
A member report (#member-bug-reports); tests: tests/test_inflate_tube.cpp.
