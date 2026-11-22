---
fixed: **Forge Spheres**: a click that only selects a sphere in Move or Scale mode no longer adds an empty undo step, so Ctrl+Z after a few clicks takes back the last real edit instead of nothing.
testers:
  - Any OS: Tool > Forge Spheres, Move: drag a sphere, click two other spheres, press Ctrl+Z once: the dragged sphere goes back.
---
Repro: tests/ui/forge_spheres_click_undo.sfui (fails on origin/main at the first Ctrl+Z after the clicks).
