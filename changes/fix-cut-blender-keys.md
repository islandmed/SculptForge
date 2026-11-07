---
whats_new: **Blender keys**: Ctrl+Shift hides and cuts again, and Ctrl+Shift+click on the brush tile (or Ctrl+Shift+B) offers the Cut shapes.
fixed: Blender input style: Ctrl+Shift did nothing on the model (Ctrl is Blender's invert, so Ctrl+Shift became an
  inverted smooth), so the Cut shapes and the Ctrl+Shift hide/isolate gestures were out of reach. Ctrl+Shift now keeps
  its meaning in every style (drag: hide or cut; add Alt: inside / the other side; Ctrl+Shift+click the brush tile or
  Ctrl+Shift+B: the selection and Cut shapes). Ctrl alone still inverts, Shift alone still smooths (thanks dm4nd on Discord).
testers:
  - Any OS, Edit > Preferences > Input > Style: Blender. Hold Ctrl+Shift and click the brush tile: pick Cut Rectangle,
    Ctrl+Shift+drag across the model: a hole, one Ctrl+Z undoes it. Pick Select Rectangle again: Ctrl+Shift+drag hides outside.
---
