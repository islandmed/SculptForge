---
fixed: **SpaceMouse**: turning the cap slowly no longer ends in a zoom that flies the view away (3DxWare's scrolling got
  through while the cap was inside the dead zone, and just after the cap was let go). Thanks @fenixpoints.
testers:
  - macOS with 3DxWare installed: twist and tilt the cap slowly, then let go; the view only turns, never zooms.
  - Any OS: right after letting go of the cap the mouse cursor moves at once; the mouse wheel zooms again
    after 0.3 s (a wheel turned in that moment is ignored: tell us if that gets in the way).
---
fenix (Discord 11-06, SpaceMouse Compact, macOS): a slow rotation of the cap first turned the view, then zoomed it
away. tests/ui/spacemouse_tilt.sfui fails without the fix (distance 6.34 -> 2.98).
