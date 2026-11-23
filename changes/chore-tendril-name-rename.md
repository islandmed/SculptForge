---
changed: "**Tendril**: the brush is called Tendril everywhere inside the program too (code, tests, scripts). Brush settings, shelves, radial menus, scripts and projects saved by older versions under its old name still load. The Forge Deck guide pictures show the Tendril tile."
testers:
  - "Any OS: open a project, brush file or ui.json saved with an older version that used the old brush name (or a script that calls it): the Tendril brush is found, its saved Size and Intensity are kept, and a shelf or radial-menu slot holding it still shows Tendril."
  - "Any OS: Tendril strokes (long pulls, with Dynamic on and off) behave as before; Edit > Preferences keeps the brush's own settings."
---
Internal rename of the brush type to Tendril, plus tools/check_marks.py's borrowed-name rule (Blender's own brush id stays in the add-on key table, the keymap reader and its test export). The old name is a hashed old word (W14) read by core/LegacyNames.h; test legacy_tendril_old_name_still_loads. docs/guide images forge-deck-* re-taken from the deck page.
