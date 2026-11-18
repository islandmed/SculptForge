---
fixed: **View jumps with the pen's side buttons** (#216, thanks @Calavera00): letting go of a side button that turned the view left that orbit's start state behind, so the next turn began from the view as the last one began (or about its old pivot) and the view shifted and zoomed while the brush ring stayed where it was. Both buttons, any action. A turn a mouse button began (KDE turns the pen's buttons into a right and a middle click) also follows the pointer once it moves it, instead of the pen's own samples driving it a second time.
changed: **Preferences > Tablet > Troubleshooting details** also lists the touch devices (a tablet's touch surface included), whether palm rejection is on, and how many wheel, finger-touch and pinch events reached the view.
testers:
  - Linux, KDE Plasma (Wayland) with a Wacom tablet: hold the lower side button over the 3D view and move the pen to turn the view, let go, then do it again from a different view (zoom with the wheel first): each turn starts from the view as it is, the sphere stays where the pen puts it, and nothing moves by itself.
  - Same with the upper side button, and with the buttons set to Rotate view / Pan view in Preferences > Tablet.
---
Reproduced in tests/ui/pen_orbit.sfui (case 5: a second side-button orbit began from the first one's camera) and tests/ui/pen_kde_buttons.sfui (pen_stream kde / kde_pointer, new pen_move step: the compositor's click without the tablet protocol's button left the drag dead, then run twice). The reporter confirms on a build.
