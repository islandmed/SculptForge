---
whats_new: **Smooth pan and zoom keys**: holding Pan left/right/up/down or Zoom in/out now glides: it eases in over a tenth of a second, moves at the same speed at any frame rate and eases out when you let go, instead of a jump and a pause. Requested by Artizmo.
changed: **Pan and zoom keys** (new in this version; Ctrl+Numpad 4, 6, 8, 2, Zoom in/out) glide: a held key eases in and out (about 0.12 s each) and moves by speed x time every frame, so a 1-second hold covers the same distance at 30 and 144 fps, instead of a step, a quarter-second pause and then the hold. No OS key repeat is involved (requested by Artizmo).
testers:
  - Any OS: hold Ctrl+Numpad 6 for a second: the view starts gently, glides evenly, and stops softly on release; a quick tap still nudges the view a little.
  - Zoom in/out (Numpad + / -) the same; Preferences > Input > Pan and zoom key speed still scales it.
---
