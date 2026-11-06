---
whats_new: **SpaceMouse with a pen**: turning the cap while the pen hovers or rests on the model only turns the view: the cursor stays on the pen and the model stays put. Thanks @fenixpoints.
fixed: SpaceMouse beside a pen (Discord, @fenixpoints, the first report from a real SpaceMouse): while the cap is pushed, the
  pointer motion and wheel a SpaceMouse driver sends for programs it doesn't know (3DxWare's defaults) no longer move
  the cursor off the pen or zoom toward it; with Hold during strokes off, a Move, Snake Hook or KelvinGrab drag no
  longer pulls the grabbed part across the view when the view turns under a still pen; gizmo, selection and stamp
  drags hold the view whatever the setting. Guide: Navigation > SpaceMouse troubleshooting.
testers:
  - Windows with 3DxWare running and a pen tablet: hover the pen over the model, twist the cap for a few seconds; the
    view turns, the brush ring stays under the pen, the model doesn't zoom or jump. Then rest the pen tip on the
    model with Move and twist again: the view turns, the model's surface doesn't follow.
  - Same with Preferences > Input > SpaceMouse > Hold during strokes on and off.
---
UI test: tests/ui/spacemouse_pen.sfui (driver echo replay: spacemouse_echo; pen_press / pen_lift; resting tip).
