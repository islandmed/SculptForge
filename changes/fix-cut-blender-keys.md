---
whats_new: **Cut** with symmetry cuts both sides alike, also across the middle, and its caps stay closed for the next cut.
whats_new: **Blender keys**: Ctrl+Shift hides and cuts again, and Ctrl+Shift+click on the brush tile (or Ctrl+Shift+B) offers the Cut shapes.
fixed: Blender input style: Ctrl+Shift did nothing on the model (Ctrl is Blender's invert, so Ctrl+Shift became an
  inverted smooth), so the Cut shapes and the Ctrl+Shift hide/isolate gestures were out of reach. Ctrl+Shift now keeps
  its meaning in every style (drag: hide or cut; add Alt: inside / the other side; Ctrl+Shift+click the brush tile or
  Ctrl+Shift+B: the selection and Cut shapes). Ctrl alone still inverts, Shift alone still smooths. Alt+Q (pick the
  Part under the cursor, Blender's own key) is now listed with the Blender keys, since Alt+click masks there (thanks dm4nd on Discord).
fixed: Cut with symmetry on: a shape whose mirror image overlaps it (a rectangle across the middle, a centred one seen
  at an angle) sometimes cut one side only, In and Add went deeper on one side, and a later cut across those caps
  could leave a crack. The mirror image now measures its depth on the Part as it was, cuts stop at once instead of
  leaving one side done, and the zero-area slivers the overlap left in the cap are cleaned up so the next cut closes
  (thanks dm4nd on Discord).
testers:
  - Any OS, X symmetry on: Cut Rectangle across the middle of a sphere in each mode (Away, In, Add, Clip): both sides
    match. Then a second cut across the first, from an angled view: the Part stays closed (Remesh not needed).
  - Any OS, Edit > Preferences > Input > Style: Blender. Hold Ctrl+Shift and click the brush tile: pick Cut Rectangle,
    Ctrl+Shift+drag across the model: a hole, one Ctrl+Z undoes it. Pick Select Rectangle again: Ctrl+Shift+drag hides outside.
---
