---
whats_new: **Input style keys after a restart**: the Blender, Classic and Maya styles keep every key when SculptForge
  starts again (F for the Size slider in the Blender style, 9 for a Snapshot with the Classic keys).
fixed: The input style's keys for the Size slider, the Material picker and Save Snapshot came back as the Classic
  defaults after a restart (Blender style: F no longer showed the Size slider, S and M opened the Size slider and the
  Material picker) until the style was picked again in Preferences > Input; your own rebinds were not affected
  (#147, thanks @Velisariy).
testers:
  - Any OS: Preferences > Input > Blender; quit and reopen; hover the model and press F: the Size slider opens.
  - Any OS: Classic, rebind Size at cursor in Controls (F1); quit and reopen: your key is kept, M opens the Material picker.
---
Cause: ui.json is read (and the style applied) before the actions are registered; the Library's actions
(LibraryBrowser.cpp) are registered last and brought back their own defaults. ActionRegistry now remembers the
style's defaults and applies them when an action is added. The UI test's `restart` re-registers those actions as a
start does (tests/ui/input_style_restart.sfui), so it fails without the fix.
