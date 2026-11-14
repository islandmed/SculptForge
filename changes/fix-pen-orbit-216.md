---
whats_new: **Pen side buttons**: turning the view with a pen's side button starts where the pen is, with no jump of the model, and a button or window change mid-drag no longer leaves the pen stuck.
fixed: **Pen side-button orbit** (#216, thanks @Calavera00): pressing a stylus side button to rotate (or pan) the view started the drag at the position SDL puts on the button event, the pen's last one it knows, which can be far from where the pen is; the first pen movement then threw the model across the view, or the press did nothing when that position was off the canvas. The drag now starts at the cursor the pen has moved, and the first movement after the pen comes into range never turns the view. A side button whose release never came (the pen left range or the window lost focus with it down) is let go as soon as the next pen event shows SDL no longer holds it, so the stylus no longer keeps turning the view or stops sculpting.
testers:
  - Linux Wayland (KDE, Wacom Intuos Pro) and X11: hold the lower side button above the model and move the pen; the view turns from where it is, with no jump. Repeat right after bringing the pen into range, with the upper button, and with the button mapped to Rotate view in Preferences > Tablet.
  - Hold a side button, lift the pen out of range, bring it back and sculpt: the stroke works. Same after switching to another window and back.
---
Reporter's Troubleshooting details (Preferences > Tablet) not received yet when written. Tests: tests/ui/pen_orbit.sfui.
