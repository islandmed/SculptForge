---
whats_new: The layout now saves itself. Customize Shelves edits, trays and palettes are written a couple of seconds after you change them and when you quit, so a restart or a crash no longer loses them. **Save Layout** now marks the layout that **Revert to Saved Layout** returns to.
added: Layout autosave (thanks raffaele_3d_design on Discord). `ui.json` keeps the layout in use ("layout") and the Save Layout one ("layoutSaved"); older files read as both. Reset Layout, `--reset-layout` and Reset Layout and Shortcuts still win, and Revert still returns to the Save Layout one afterwards. The console, Library and Customize Shelves being open are not part of it. `ui.prev.json` and `ui.prev2.json` keep the two copies of `ui.json` from before the last sessions that changed the layout.
fixed: Layout edits (Customize Shelves, trays, palettes) were lost on quit, a crash or Revert unless Save Layout was pressed first (reported by raffaele_3d_design on Discord).
testers:
  - Customize Shelves: remove or move a shelf item, wait three seconds, kill the app (no quit), start it again: the edit is there.
  - Edit a layout, Save Layout, edit again, quit and start: the second edit is there; Preferences > Saving & Layouts > Revert to Saved Layout returns to the Save Layout one.
  - Reset Layout, quit, start: the standard layout; Revert to Saved Layout still returns to the saved one.
---
