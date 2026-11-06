---
whats_new: **Brush sets**: save the bottom shelf and its brushes' keys as a named set (Wood, Metal, Stone...) and swap
  both in one step from **Brush > Brush set**, the shelf's right-click menu or <kbd>F3</kbd>. Asked for by @NicTanghe.
highlight: Brush sets: swap the whole bottom shelf and its keys in one step
added: **Brush sets** (#143, asked for by @NicTanghe): **Brush > Brush set > Save** keeps the bottom shelf's brushes, in
  order, and their keys as a named set in your brush library; picking a set (the Brush palette, the bottom shelf's
  right-click menu **Brush set**, or **Brush Set: Wood** in the command palette) swaps the shelf and the keys in one
  step, and the set you switched to comes back at the next start. Custom and pack brushes on the bottom shelf can now
  hold a key too (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click the tile, then press the key). Commands: `brush_set`,
  `brush_set_save`, `brush_set_delete`.
testers:
  - Any OS: add a pack brush to the bottom shelf with +, Ctrl+Alt+click it and press J; Brush > Brush set > Save as
    "Wood". Remove a few brushes, give another brush J, save as "Metal". Switch between them from the Brush set menu:
    the shelf and J follow each time.
  - Any OS: switch to a set, restart SculptForge; the shelf and its keys are that set's.
  - Windows at 150 % or 200 % scaling: the Brush set row and its menu fit, nothing clipped.
---
Sets are `.sfset` JSON files in `<brush library>/sets/` (core/BrushSets.h). Tested by `ui_brush_sets` and `brush_sets_*`.
