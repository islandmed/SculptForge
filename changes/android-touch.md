---
fixed: **Android**: the welcome window fits a phone screen (its X and Close button were off the screen on a Pixel), and a touch whose lift was lost (switching apps, rotating, split screen) no longer leaves later touches ignored.
testers:
  - Android phone, first start: the welcome window is inside the screen, its X and Close close it with a finger.
  - Android: start a stroke with two fingers, switch to another app and back; one finger sculpts again.
---
Reported by Hameron on a Pixel phone. Branch fix/android-touch.
