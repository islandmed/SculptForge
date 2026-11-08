---
whats_new: **Quad Remesh on pointed parts**: **Tool > Geometry > Quad Remesh** on a thin part with a sharp tip (a
  horn, a claw, a blade) no longer tangles along its length, and the quads now end on the point. Thanks Oslen on
  Discord for the report.
fixed: **Quad Remesh on thin, pointed parts** (reported by Oslen on Discord): a long part that tapers to a sharp tip,
  like a horn, a claw or a blade, could take a very long time at high **Target Quads** and come out with folded,
  tangled quads along its whole length, and the quads stopped short of the point. The tip now gets its own handling:
  the quads converge on it like a pencil's and end on the point. Measured on a 2.5 m horn (41k triangles) on our
  floor-tier laptop: at 5,000 quads 3.5 s to 2.4 s, the distance to the sculpt 7.9 % to 0.5 % of its size, poles 1,533 to 846;
  at 20,000 quads 16.1 s to 7.1 s, 6.7 % to 0.4 %, 6,036 to 4,580 poles; at 50,000 quads 45.6 s to 13.3 s, 0.5 % to
  0.3 %, 18,714 to 13,430 poles. Models without sharp tips give the same mesh as before; the full-body starter's
  fingertips fit closer (1.5 % to 1.1 %).
testers:
  - Any OS: Quad Remesh a thin pointed Part (a horn, a claw, a spike) at 5,000 and at 20,000 **Target Quads**. Good:
    an even quad layout along it with no tangles, and the quads reach the point (at 20,000 the last few quads before
    the point can still be uneven: say how it looks there); note the seconds.
  - Any OS: Quad Remesh a model without sharp tips (a head, a hand): the same result as the previous release.
---
