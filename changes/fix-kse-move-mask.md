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
Move snap-back (same report, Keep Strokes Editable on, a mask near): not reproduced. tests/test_kse_move_mask.cpp
holds it at the reporter's size (80K base, Micro 3) with the option off and on: cage Move, Move next to a micro mask,
Move along normals, each kept after the pen lifts, through later strokes, undo and redo, also after the option is
switched off and on mid-session and an undo right before the Move; tests/ui/micro_kse_move_mask.sfui does the same in
the app (press, drag, lift, orbit). Adjust was blind on micro detail with the option off too.
