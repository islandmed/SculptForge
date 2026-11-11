---
whats_new: **Orbit around the pointer**: Preferences > Input. A rotation that starts over the model turns the view
  about the point under the pointer, and zooms after it work at that depth. On for new installs; turn it on there if
  you used SculptForge before (thanks dm4nd on Discord).
highlight: Orbit around the point under the pointer
added: **Orbit around the pointer** (Edit > Preferences > Input, every key style; on for new installs, off for
  existing settings so their orbit doesn't change): a rotation drag that starts over the model turns the view about the surface point under the pointer instead of the scene centre, without
  the view jumping; the target moves to that point's depth, so zooming afterwards works there too (auto depth).
  Started over the background or on the axis gizmo's disc, it turns about the target as before. Turntable, orbit
  locks and Shift snapping are kept. The point is picked once per drag (thanks dm4nd on Discord).
testers:
  - Any OS, any key style: load a long model (or zoom in on a hand), right-drag (Blender: middle-drag; Maya: Alt+left)
    starting over a finger tip: the tip stays under the pointer while the view turns; no jump when the drag starts.
  - Start the same drag over the background: the view turns about the middle as before.
  - Edit > Preferences > Input > Orbit around the pointer off: every rotation turns about the middle again; restart, kept.
  - Upgrade from 0.11.15 with your settings: the box is off and orbiting is as before; a fresh install has it on.
---
Discord #ideas thread 1568719332224278623 (dm4nd). Pick once at drag start (BVH ray, no depth readback).
