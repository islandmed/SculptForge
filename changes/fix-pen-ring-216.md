---
fixed: **Brush ring frozen during a pen side-button drag** (#216, thanks @Calavera00): on a compositor that turns the pen's side buttons into right / middle clicks (KDE on Wayland), the ring stayed where the button was pressed, and the view did not turn, while the cursor went on, until the button was let go. A drag that a mouse button began now goes by the pointer only while the pointer keeps moving; if it goes quiet the pen carries the drag on, so the ring and the view follow the pen.
testers:
  - Linux, KDE Plasma (Wayland) with a Wacom tablet: hold the lower side button over the 3D view and move the pen: the view turns and the brush ring stays under the pen all along, and after letting go it is at the pen. Then the upper side button (pans).
---
Reproduced in tests/ui/pen_kde_buttons.sfui (new pen_stream kde_stalled: the compositor's pointer sends one move after the click, then none; new values pen_x / pen_y). Mouse and the other pen streams unchanged.
