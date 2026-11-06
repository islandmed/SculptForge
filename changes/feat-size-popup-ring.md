---
whats_new: **The brush ring shows the new size while you set it**: with the **S** pop-up open, dragging or typing a
  size draws the ring at that size on the model, where you pressed S; **O** shows the Falloff ring the same way.
added: While the **S** (Size) pop-up is open, the brush ring stays on the model at the point where it was opened and
  shows the size being set, live while dragging and before Enter while typing a number (#138).
  **O** (Falloff) draws the inner Falloff ring the same way. The ring lies under the pop-up so its number stays readable.
testers:
  - Any OS: point at the model, press S and drag the knob far left and right: the ring on the model grows and shrinks
    live where you pressed S. Press S again and type 150: the ring shows 150 px before Enter.
  - Any OS: press O and type 80 (or -80): the inner ring shows that Falloff on a big brush.
---
Reviewer notes: PickerUi::sliderPreview (Pickers.cpp) gives the pop-up's anchor and the value being set (typed number
parsed as Enter would apply it); App::drawBrushCursor draws at that anchor (drawSurfaceCursor now takes the point and
the Falloff) on the background draw list, without the mm readout (the pop-up shows the size). At a small brush the
Falloff ring sits mostly behind the pop-up bar. tests/ui/size_popup_ring.sfui (size_ring_preview / size_ring_px).
