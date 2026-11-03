---
whats_new: **Shapes in the stroke popup**: press **G** and pick Mask Pen, Lasso or Rect, or Cut Line, Lasso, Rect or
  Polyline. A plain drag then masks or cuts with that shape, no Ctrl or Ctrl+Shift held (with a mask shape on, Ctrl
  unmasks). Pick it again to turn it off.
whats_new: **Background jobs keep your Part**: a Quad Remesh, Voxel Remesh, Auto UV or Unwrap that finishes after you
  picked another Part puts its result on its own Part; the Part you picked stays active.
highlight: Mask and Cut shapes in the stroke popup, no keys held
added: **Stroke popup > Shapes** (Discord, Darth): Mask Pen, Mask Lasso, Mask Rect, Cut Line, Cut Lasso, Cut Rect and
  Cut Polyline. Picking one latches Mask or Select with that shape, so a plain drag masks or cuts with it; with Mask
  latched a held Ctrl unmasks. The cursor label names the shape (e.g. "Cut Line").
fixed: The active Part no longer switches by itself (Discord, Oslen): a background job (Quad Remesh, Voxel Remesh with
  Keep Texture, Auto UV, UV Editor Unwrap) that finished after another Part was picked made its own Part active
  again, and cut short a stroke in progress. Its result now goes onto its own Part, waits for a stroke to end, and
  the status bar says where it went.
fixed: A triple-click no longer switches the Part while a pen is in range: a Wacom side button set to Double Click
  (the Intuos 3 default) followed by a tap counted as one (Discord, Oslen).
fixed: With two screens, an unfocused window kept drawing only what a background job asked for: a Sculpt Timeline
  timelapse render and the hardware calibration stopped until the pointer came back (Discord, Tiagoffcruz). Full
  screen no longer minimises when you click on another monitor.
fixed: A Smooth, Mask or Select latch from the stroke popup came back after picking another brush and then the first
  one again.
testers:
  - Windows, two monitors: start Geometry > QRemesh on a dense mesh, click into another app on the other screen;
    the progress keeps moving and the result appears without moving the pointer back.
  - Windows, two monitors, full screen (F11): click on the other monitor; SculptForge stays up.
  - Two Parts: start Quad Remesh on one, pick the other while it runs; when it finishes the second Part stays
    active and the status bar says "(on <Part>; <other> stays the active Part)". Undo on the first Part undoes it.
  - Wacom with the side button set to Double Click: hover the pen over another Part, press the button, tap; the
    Part doesn't change.
  - G > Shapes > Cut Line, then a plain drag across the model: it cuts (Tool > Cut sets Away / In / Add / Clip).
    G > Shapes > Mask Lasso: a plain drag masks, Ctrl+drag unmasks. Pick it again: strokes sculpt.
---
Branch fix/0.10.10-discord-bugs. Darth's "Ctrl+Shift+drag Cut / Ctrl+drag Mask Lasso does nothing" did not reproduce
(mouse, pen, zoomed in, both input styles, Ctrl held over the panel first: all work and are covered by ui_cut_tool,
ui_mask_lasso, ui_stroke_popup); the reply asks him for his OS, input style and what the status bar says.
