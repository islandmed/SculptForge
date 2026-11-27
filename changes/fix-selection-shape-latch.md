---
whats_new: **Turning off a latched Mask, Select or Cut shape**: a Line, Lasso or Rectangle picked in the stroke popup stays on for every drag until you turn it off, and now there are three ways: press Esc, pick it again, or press its own button in Tool > Masking, Visibility or Cut. A label in the corner of the canvas shows what is on and that Esc turns it off.
fixed: **A latched shape could not be switched off** (macOS report, all systems): after Cut Line, Lasso or Rectangle (Mask, Select or Cut) was picked in the stroke popup, plain drags kept using it, and the tool panel's shape buttons only changed the shape and never ended it. Esc now ends a latch (Smooth, Mask, Select or a shape), the panel button of the latched shape ends it, and a label stays in the corner of the canvas while one is on.
testers:
  - Stroke popup (G): pick Cut Line; a label "Cut Line on, Esc turns it off" shows at the top of the canvas; press Esc (with the popup open, one Esc closes it and ends the latch; G or a click beside it closes the popup and keeps the latch); drags sculpt again.
  - Same with Rectangle Mask and Select Lasso; then pick Cut Line, press the Cut Line button in Tool > Cut: the label goes and drags sculpt.
  - Pick Cut Rect in the popup, press Rectangle in Tool > Visibility: the shape changes and the label stays; press it again: off.
---
Tests: tests/ui/stroke_latch_off.sfui (Esc for each latch, the badge text, each panel's button, another shape's button keeps it on).
