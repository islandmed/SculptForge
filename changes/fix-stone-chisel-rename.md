---
changed: The built-in **Stone Carve** brush (Sculpt > Brush, and the community gallery) has a name of our own. Shelves,
  key bindings, pie menus and scripts saved with its old name still find it.
testers:
  - Any OS: Sculpt > Brush: Stone Carve is listed and stamps hard planes with cracks; a shelf or key saved with the
    brush's old name still picks it.
---
Old name read on load only: N1/N2 in docs/legal/trademarks.md (src/core/LegacyNames.h kNames, whole names only, so a
user's own brush with the word in its name keeps its name). Text renamed with ~/Work/.sf/ip_rename.py; gallery
index rebuilt (tools/brush-gallery/build_index.py), website gallery synced, guide rebuilt.
