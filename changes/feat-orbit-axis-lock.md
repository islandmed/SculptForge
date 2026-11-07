---
whats_new: **Orbit lock**: **View > Orbit lock** turns the view about one axis only: **Free**, **Y (turntable)**,
  **X** or **Z** (<kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd> / <kbd>Y</kbd> / <kbd>X</kbd> / <kbd>Z</kbd>). The axis
  gizmo shows which axis is locked. Thanks foirgairwolf on Discord for the idea.
highlight: Orbit lock: turn the view about X, Y or Z only
added: **Orbit lock** (View > Orbit lock, a request from foirgairwolf on Discord): besides Free and Y (the turntable), rotation
  drags can turn the view about the world X or Z axis only, with no drift over long drags. A key for each
  (Alt+Shift+F / Y / X / Z) and the `orbit_lock axis=free|y|x|z` command; the axis gizmo draws the locked axis
  through its disc with a lock badge. Shift still snaps to side views. Saved with the preferences (also
  Preferences > Interface / Input > Rotation; older versions read Y as Turntable).
testers:
  - Any OS: View > Orbit lock > X; right-drag the background: from the front the model only tips towards you and
    away, sideways drags do nothing; the gizmo shows X through its disc and a lock badge.
  - Any OS: Alt+Shift+Z, then drag around the middle of the view: the model turns like a wheel and keeps facing you.
  - Any OS: restart; the lock is kept. Alt+Shift+F goes back to free rotation.
---
