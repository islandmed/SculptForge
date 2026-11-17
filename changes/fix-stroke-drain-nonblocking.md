---
fixed: **Heavy meshes:** a shortcut or menu command (Undo, Save...) or a new click right after a stroke no longer freezes
  the window while the stroke's last dabs are placed. It waits for them, and the status bar shows what is queued
  ("Finishing stroke: undo queued"). Esc cancels the queued commands. They run in order, once each, as soon as the stroke is done.
  The new click starts its stroke on the finished surface. Quitting and autosave still wait for the stroke.
---
Commands from scripts, macros, the AI link and panel buttons still finish the stroke first (they need the result at once).
Tests: ui_stroke_drain_undo, ui_stroke_drain_click.
