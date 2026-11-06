---
whats_new: **Drag Stamp with a pen**: a stamp (a vector displacement or any brush) dragged to size keeps its strength when
  you lift the pen, instead of going weak or vanishing.
fixed: **Drag Stamp / Slide Stamp with a pen** (#146, Huion RTP-700 on Windows): the stamp went weak or vanished when
  the pen lifted. A tablet's last samples before the lift fall towards 0, and the stamp is pressed again at every
  move; it now keeps the firmest pressure of the drag (Windows Ink, Wintab and Linux alike).
testers:
  - Windows with a pen (Windows Ink on, then off for Wintab): Brush > Vector Displacement > Horn, press and drag it to
    size, lift the pen. Good: the horn stays as it looked while dragging.
  - Same with Stroke > Slide Stamp: the stamp stays where and as strong as it was at the lift.
---
Before the fix, tests/ui/vdm_penup.sfui's lift with falling pressure left no stamp at all (moved 0).
