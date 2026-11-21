---
fixed: **View skipped while rotating or panning with the pen's side buttons** (#216): on a compositor that turns the pen's side buttons into right / middle clicks (KDE on Wayland), the view moved only when the compositor's pointer reported, so it waited and then jumped. A side-button drag begun while the pen is sending samples now follows the pen alone for its whole length.
testers:
  - Linux, KDE Plasma (Wayland) with a Wacom tablet: hold a side button over the 3D view and rotate, then pan, with a steady stroke: the view turns smoothly every frame, with no waiting and catching up, and the brush ring stays under the pen.
---
Reproduced in tests/ui/pen_skip_216.sfui (new pen_stream kde_sparse: the pointer reports every third pen sample; new view_trace on / off / check). With the old code that shape moves the view only on the pointer's samples. The pen's drags are unchanged, a mouse's drag stays the mouse's.
