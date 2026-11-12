---
fixed: **Brush Size and Intensity** keep their value when the pointer goes past the canvas edge while you adjust (S / F, U / Shift+F), instead of snapping back to the old one; only a click on another control cancels.
testers:
  - Press S (F in the Blender style) with the pointer in the right half of the canvas, move right past the canvas edge and beyond: the Size stays at the value it had at the edge. Enter or a click on empty panel keeps it, Esc restores.
  - Same pop-up, then click a brush on the shelf: the pop-up cancels (the old size returns) and that brush is picked.
---
Follow-up to the 0.12.9 follow-the-mouse pop-ups. A click on a panel button still cancels and presses it (artist-qa feel note; a later change could make that first click only cancel).
