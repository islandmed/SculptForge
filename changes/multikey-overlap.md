---
fixed: two-key shortcuts also fire when both keys are pressed together (thanks Artizmo on Discord)
added: **Two-key window** (Preferences > Input): how long the second key of a two-key shortcut has, 0.35 to 1 s (0.4 s by default). The keys of a two-key shortcut can be pressed together, either one first, or one after the other. A key that is part of one runs its own action once the window is up; every other key runs at once.
testers:
  - Windows 11, SculptForge keys: bind Fill to "C then V" (Preferences > Hotkeys, the two-keys button). Press C and V together (V first too), then one after the other, quickly: Fill every time, never Pick colour or Switch Color. Raise the Two-key window in Preferences > Input to 1 s and try slower presses.
  - Same binding: tap C alone (Pick colour after a short wait), tap V alone (Switch Color after the same wait; any other key is at once).
---
