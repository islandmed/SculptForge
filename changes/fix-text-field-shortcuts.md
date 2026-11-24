---
fixed: **Typing in a field**: while a text field has the keyboard (a slider's typed number, the command palette's search, any text box), no shortcut runs. Ctrl+A selects the field's text instead of Mask All (one undo step), Ctrl+Z, Ctrl+Y and plain letters stay in the field. It could happen when the key came right after the click that opened the field.
testers:
  - Any OS: double-click a slider (or Ctrl+click Size), press Ctrl+A, type 2, Enter. The value is 2, no "Mask All" in the status line or Undo list. Type B, S in the field: the brush stays. In the Classic and the Streamlined layout.
---
