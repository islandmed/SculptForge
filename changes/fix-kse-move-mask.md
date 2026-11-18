---
fixed: **Tool > Masking > Adjust on micro detail**: Invert, Blur, Sharpen, Grow, Shrink, Clear and All now change the
  micro level's own mask, the one its brushes and the viewport use (before, they changed only the base mesh's mask, so
  nothing happened on a micro level, with or without Keep Strokes Editable). Ctrl+click Blur and Ctrl+click Invert on
  the background work there too; each is one undo step. Reported by noved_.
testers:
  - Any OS: add a micro level (Tool > Geometry > Add Micro), paint a mask on it, then Tool > Masking > Adjust > Invert:
    the masked and free areas swap at once; Blur softens the edge; Ctrl+Z steps back each time.
  - Same, with Preferences > Performance > Keep Strokes Editable on: the same results, and a Move next to the mask keeps
    its shape after the pen lifts.
---
Move snap-back (same report): tests/test_kse_move_mask.cpp and tests/ui/micro_kse_move_mask.sfui hold it; see the
branch's report for what they found.
