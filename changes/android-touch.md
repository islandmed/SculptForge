---
fixed: **Android**: the welcome window fits a phone screen (its X and Close button were off the screen on a Pixel), no side panel opens on a screen as narrow as a phone held upright (one opened by dragging an edge took the whole screen and could not be closed again), the starting model is framed into the 3D view on a phone held upright (it filled the narrow view edge to edge) and again when the phone is turned, and a touch whose lift was lost (switching apps, rotating, split screen) no longer leaves later touches ignored.
testers:
  - Android phone, first start: the welcome window is inside the screen, its X and Close close it with a finger.
  - Android phone, portrait and landscape: the sphere is whole and centred in the 3D view at start; turn the phone, it stays framed.
  - Android phone, portrait: no empty side panel; the 3D view fills the space between the shelves, also after a restart.
  - Android: start a stroke with two fingers, switch to another app and back; one finger sculpts again.
---
Reported by Hameron on a Pixel phone. Branch fix/android-touch.
