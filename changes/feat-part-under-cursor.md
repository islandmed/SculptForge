---
whats_new: **Sculpt the Part under the cursor**: point at another Part and press <kbd>Alt</kbd>+<kbd>Q</kbd> (as in
  Blender, in every input style): it becomes the Part you sculpt, and the view stays where it is.
added: **Sculpt the Part under the cursor** (Alt+Q in every input style, the same key as Blender's; Preferences > Hotkeys
  changes it; in the command palette): the visible Part under the pointer becomes the active one, without moving the
  camera; each Part keeps its mask. The status bar says "Sculpting: <Part>", or that no Part is under the cursor.
  Asked for by @ziel9627 on Discord.
testers:
  - Any OS, Blender input style: append a second Part, point at it, press Alt+Q; the status bar says "Sculpting:" and
    its name, the Parts list highlights it, the view doesn't move. Alt+Q over the background: "No Part under the cursor".
  - Windows and macOS (Option+Q), Classic and Maya styles: the same key works and Q alone still picks Draw.
---
Free in every style's keymap (classic, SculptForge, Blender, Maya; shortcut_conflicts == 0 in the UI test). Before,
Alt+Q fell back to Q (Draw) in all styles; Blender itself uses Alt+Q for this (transfer mode).
