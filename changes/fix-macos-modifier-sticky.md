---
whats_new: **Stuck Shift, Ctrl or Alt**: if the system took a modifier key's release (a macOS menu, another app, the Windows key), SculptForge now notices and lets go of it. Strokes no longer smooth, mask or invert until you restart.
fixed: **A modifier key stuck down** (macOS first, also Windows): Shift, Ctrl/Cmd or Alt that the system let go of without telling the app (a native menu or another window took the key-up) kept strokes smoothing, masking, selecting or inverting. The held keys are now compared with the system's own state every frame while one is down, when the window gains or loses focus, and when the stroke popup closes; a key the system says is up is released. Linux has no system query: the window system already tells SDL.
testers:
  - macOS: hold Shift, open a menu in the menu bar (or Cmd+Tab away and back), let go of Shift, return to the window; a stroke sculpts normally (no smoothing), and Ctrl/Cmd mask and Alt invert only while held.
  - macOS, Windows: pick Select or Mask in the stroke popup (G), then pick it again, then another brush; strokes act as plain sculpting.
---
The cause on the reporter's Mac is not confirmed: the stroke popup, latch and key code are unchanged since 0.14.0. This closes the one path that can leave modifier state stuck (a key-up the app never received). tests/ui/modifier_stuck_resync.sfui covers each path (frame, focus gained / lost, popup close, a key the system agrees is held stays held).
