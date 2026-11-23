---
fixed: **Trackpad**: one pinch event can no longer fling the view. Two contacts that land on the same spot (the resting finger of a click) and then jump apart read as a span going from nothing to a hand's width, which zoomed the camera out a thousandfold in one step (the model left the view, Size read kilometres). A pinch step is now limited to 2x either way (reported by @phoenix).
testers:
  - macOS trackpad: with a brush on, click in empty canvas beside the model, several times, with a second finger resting on the pad; the view stays put.
---
Hardening for the phoenix clip (not reproduced with mouse clicks: tests/ui/empty_click_camera.sfui holds the camera invariants). Confirmed only if the reporter's log shows "touch: two-finger trackpad gesture" at the jump.
