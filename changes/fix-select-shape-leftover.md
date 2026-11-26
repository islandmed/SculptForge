---
fixed: **Ctrl+Shift drag kept drawing a Cut Line after you had turned it off** (macOS report): a Cut or Select shape picked in the stroke popup shared the Ctrl+Shift shape with the Visibility and Cut panels, so after Esc (or the shape's button, or another brush) ended it the next Ctrl+Shift drag still drew the grey cut half-plane. Ending the latch now brings back the shape the panels had, Select Rectangle at first, and a shape picked on a panel meanwhile stays.
testers:
  - Stroke popup (G): pick Cut Line, make one cut with a plain drag, press Esc. Hold Ctrl+Shift (Cmd+Shift on a Mac) and drag: a rectangle selection, no grey half-plane.
  - Tool > Visibility: pick Select Circle; latch Cut Line from the popup, Esc: Ctrl+Shift drags a circle again.
---
Tests: tests/ui/select_shape_leftover.sfui (Esc, the shape's tile again, another brush, the panel's own choice kept).
