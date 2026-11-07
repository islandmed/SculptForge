---
fixed: Holding a rebound Smaller brush / Larger brush key (and the 1 px Shift steps) repeats again, like [ and ]
  always did; after a rebind it changed the size once and stopped. Those keys also step through an insert set.
  Not while typing in a field (thanks Ziel on Discord).
testers:
  - Preferences > Input: bind Smaller brush to Q and Larger brush to E; hold each over the canvas: the size keeps
    changing until let go. Shift with the 1 px steps' keys does the same.
  - Default keys: hold [ and ] as before; type in a text field and hold Q: no brush size change.
---
