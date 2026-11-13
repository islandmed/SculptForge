---
fixed: A bottom-shelf slot for one of your brushes still showed "missing" after a restart when the brush's name held one of the old words SculptForge renamed in 2026 (rand_gogh on Discord; the 0.12.13 fix covered renames and moves, not this). The shelf, the brush's key and its saved id now keep a brush's name exactly as you typed it, and a slot that an earlier version already changed finds its brush again by itself. If a slot still finds nothing, the log now says which name and id it looked for and the closest brush names.
testers:
  - Windows: a brush on the shelf that has a key (Ctrl+Alt+click the tile) keeps its tile and its key after a restart (the retired words themselves are only exercised by the unit tests).
  - Any OS: an existing slot that shows the missing mark for a brush that is in the Library comes back after the update, without touching the brush.
---
Core: legacy::modernizeId keeps recipe/file item names; sameBrushName + findBrushForSlot re-link a slot saved in today's spelling.
