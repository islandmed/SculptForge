---
testers:
  - Mac: a Micro level (4) on a sculpt with fine detail (Geometry > Add Micro, then surface noise or pores); hold Shift and
    smooth at about 150 and 300 px, several overlapping passes, with and without a mask. Zoom in: nothing sticks out of
    the surface, the detail only softens.
---
Test only: test_micro micro_smooth_never_shoots_points_out keeps the #220 kernel (shipped in 0.13.8) from shooting
points out of a detailed level (main before #220: peaks 2.2x at r 0.4). The member's Mac report is that bug.
