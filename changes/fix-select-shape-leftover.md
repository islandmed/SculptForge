---
fixed: **A shape or modifier left latched from the stroke popup broke the Gizmo and the next Ctrl+Shift drag** (macOS report, artist-qa): after Cut Line (or Select, Mask, Smooth) was latched from the stroke popup, switching to the Gizmo left it on, so a drag on a handle moved nothing and on release cut the Part. Choosing the Gizmo (any mode) or another tool that takes the drags now ends the latch, and a handle drag never cuts, selects or masks. One Esc ends a latch even while it closes the popup (G or a click beside it closes the popup and keeps the latch). A Cut Line used through the popup no longer stays on the Ctrl+Shift drag: ending the latch brings back the shape the Visibility and Cut panels had, Select Rectangle at first, and a shape picked on a panel meanwhile stays.
testers:
  - Stroke popup (G): pick Cut Line, press G to close the popup (the label shows Cut Line is on), press Shift+W (Gizmo, All): the label goes; drag an arrow: the Part moves, nothing is cut.
  - Pick Cut Line, press Esc once with the popup still open: the popup closes and the label is gone; drags sculpt.
  - Pick Cut Line, make a cut, Esc, hold Ctrl+Shift (Cmd+Shift on a Mac) and drag: a rectangle selection, no grey half-plane.
  - Tool > Visibility: pick Select Circle; latch Cut Line from the popup, then end it: Ctrl+Shift drags a circle again.
---
Tests: tests/ui/select_shape_leftover.sfui (Esc, the shape's tile again, another brush, every Gizmo mode with Select, Mask, Smooth and shape latches, one Esc with the popup open, the panel's own choice kept).
