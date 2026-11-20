---
whats_new: **Inflate on a thin tube**: a big brush over a coarse tube or limb now thickens it evenly, each point along its own normal, instead of swelling the area into one lump.
fixed: **Inflate on coarse, long-quad meshes** (a thin Tube): the check for a packed grid took a tube's end cap ring and its long quads for one, so a brush wider than the tube moved the whole area along one normal and swelled bulbs; it now needs a real packed cluster (at least 32 vertices well under the surface's own edge length).
testers:
  - Any OS: Tool > Tube, draw a tube, Bake, pick Inflate, Size wider than the tube, stroke along it; it thickens evenly, round, not into bulbs near the ends.
---
A member report (#member-bug-reports); test: tests/test_inflate_tube.cpp.
