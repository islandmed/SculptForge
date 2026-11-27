---
testers:
  - Any OS: Tool > Visibility, Alt+drag **Delete masked** (the right button of the pair) onto a shelf; the copy is that one button, with no second Remove Hidden next to it.
---
Branch fix/pin-copy-own-item, a follow-up to fix/shelf-draggable-all (not yet in a tag, so no public line). Cause: pinLastItem stretched a
pinned control that starts mid-line and runs to the row's end back to the line's start (a label, then a wide combo), which took the left
button of a two-button row along. It now skips that when another control stands to its left on the line (g_prevItem, Ui.cpp). Test:
tests/ui/shelf_row_button.sfui.
