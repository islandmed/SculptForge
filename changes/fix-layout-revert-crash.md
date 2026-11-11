---
whats_new: **Revert to Saved Layout** no longer crashes, a colour picker or tile taken off a shelf can be put back (**Preferences > Saving & Layouts**), and there is always a way out of a broken layout: **Reset Layout** and `--reset-layout`.
added: **Reset Layout** (Preferences > Saving & Layouts, command palette): the built-in shelves and trays with every panel back in the main window, shortcuts and preferences kept. `sculptforge --reset-layout` does the same at start. A shelf widget (colour picker, brush tiles, sliders) taken off a shelf is listed there with a Put back button.
fixed: Revert to Saved Layout, Reset Layout and Import Interface run between frames instead of inside the palette that asked, so they can no longer swap the layout under it (reported by @phoenix, raffaele_3d_design, macOS). Unsaved work is autosaved first; the first autosave of a session keeps the previous session's as `autosave.prev.sfproj` (Library > QuickSave), and the Restore prompt offers it.
testers:
  - macOS, Classic layout: Ctrl+Alt+drag the colour picker off the left shelf onto the canvas; Preferences > Saving & Layouts lists "Put back on the shelf: color"; click it.
  - Preferences > Saving & Layouts > Revert to Saved Layout with a palette in a window of its own; no crash, unsaved work still there.
---
Cause notes and files: BRIEF/REPORT only (not public).
