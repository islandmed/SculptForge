---
whats_new: **Touch and trackpad navigation**: two fingers pinch to zoom and twist to roll the view; the SpaceMouse holds still while you sculpt.
added: **Touch screens and trackpads**: two fingers over the model pinch to zoom and twist to roll the view, and on a
  touch screen slide to pan; they never sculpt. One finger on a touch screen still sculpts. Thanks to phoenix
  (Discord) for the report.
added: **Hold during strokes** (Preferences > Input > SpaceMouse, on by default): while you sculpt, paint or mask, the
  SpaceMouse leaves the view alone and its buttons do nothing, so a hand on the cap never moves the model under the
  pen or mouse. Pointer events from a SpaceMouse that the system also lists as a mouse are ignored. Thanks to phoenix
  (Discord).
fixed: Pinch and two-finger rotate did nothing on macOS trackpads and touch screens. Thanks to phoenix (Discord).
testers:
  - macOS with a trackpad: pinch over the model (zooms), twist two fingers (the view rolls once you turn past a
    little), two-finger scroll (zooms as before). Nothing gets sculpted.
  - Windows touch screen or touch laptop: one finger sculpts; two fingers pan, pinch-zoom and twist; landing the
    second finger mid-stroke ends the stroke and moves the view.
  - Any OS with a SpaceMouse and a tablet: rest a hand on the cap while you sculpt with the pen; the view must not
    move or jitter during the stroke, and moves again once you lift the pen.
---
phoenix (AL3D), Discord #bug-reports 11-05: mirror freeze, no multi-touch on macOS, SpaceMouse conflicts. Touch:
core/TouchGesture + app/Touch.cpp (finger events; SDL 3.4 pinch events when built against it). SpaceMouse: hold during
strokes, 6-DoF pointer filter. Freeze: no hang found in the symmetry path; the stroke thread wait now logs every 2 s.
