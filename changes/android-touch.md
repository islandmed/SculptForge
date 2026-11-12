---
fixed: **Android**: the welcome window fits a phone screen (its X and Close button were off the screen on a Pixel), no side panel opens on a screen as narrow as a phone held upright (one opened by dragging an edge took the whole screen and could not be closed again), the starting model is framed into the 3D view on a phone held upright (it filled the narrow view edge to edge) and again when the phone is turned, the launcher icon is the same cube on its dark tile, shaped by the launcher (it sat inside a white circle), and a touch whose lift was lost (switching apps, rotating, split screen) no longer leaves later touches ignored.
added: **Help > Copy log to clipboard**: copies the last 60 lines of the log as text, to paste into a problem report where the file can't be found or attached (Android hides its app folder from the Files app).
testers:
  - Android phone, first start: the welcome window is inside the screen, its X and Close close it with a finger.
  - Android phone, portrait and landscape: the sphere is whole and centred in the 3D view at start; turn the phone, it stays framed.
  - Android phone, portrait: no empty side panel; the 3D view fills the space between the shelves, also after a restart.
  - Android home screen: the SculptForge icon is the orange cube on dark, no white circle (also in Android 13+ themed icons).
  - Android: start a stroke with two fingers, switch to another app and back; one finger sculpts again.
---
Reported by Hameron on a Pixel phone. Branch fix/android-touch.
