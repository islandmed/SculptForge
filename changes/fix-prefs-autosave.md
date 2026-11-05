---
whats_new: **Preferences save themselves**: key style, shortcuts, tablet, interface, performance and radial menus
  are kept a moment after every change, with nothing to press. The layout (shelves, trays) is kept with
  **Edit > Preferences > Config > Save Layout**. Thanks fenixpoints (#117).
whats_new: **I is Falloff** with SculptForge keys (U stays Intensity); the Classic sculpting keys are unchanged.
highlight: Preferences save themselves
changed: **Preferences save themselves** (#117, thanks fenixpoints): the key style, shortcuts, tablet, interface,
  performance and other preferences, and the radial menus, are written to ui.json half a second after they change
  (and at quit), through a temporary file. The layout is kept only by the new **Save Layout**
  (Edit > Preferences > Config). The old save command and its Ctrl+Shift+I key are gone; keymaps and macros that
  name its id (`config.store`) still load, as **Save Preferences Now** in the command palette.
changed: **SculptForge keys: I is Falloff** (as O); U stays Intensity. The Classic sculpting keys keep S size,
  U intensity, I colour intensity, O falloff (#117).
changed: **Plain names** for controls that carried another application's labels: Size, Intensity, Colour intensity,
  Falloff, Ctrl+hover help, Help box opacity, Customize Shelves, Reset Layout and Shortcuts, Revert to Saved Layout,
  Export / Import Interface, Lower / Higher Level, Delete Lower / Higher, Project Detail, Lasso Mask, Rectangle Mask,
  Radial Copies and Group from Mask. Script commands, their parameters and saved files are unchanged.
testers:
  - Any OS: Edit > Preferences > Input, pick another style; change a hotkey and UI scale; quit and start again:
    all three are kept, without pressing anything.
  - Move a palette into a tray, quit, restart: it is back where it was; do it again and press
    Edit > Preferences > Config > Save Layout first: the tray is kept.
  - SculptForge keys: I opens the Falloff slider at the cursor, U Intensity. Classic keys: I is Colour intensity.
  - Ctrl+Shift+I does nothing; F3 "save preferences now" works.
---
Reviewers: autosave is App::autosavePrefs (Config.cpp), the UI test tests/ui/prefs_autosave.sfui (`restart` step).
