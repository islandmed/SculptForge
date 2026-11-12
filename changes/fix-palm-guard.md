---
added: **Palm rejection** (Android): fingers that land while the pen is in range, or in the 0.3 s after it leaves, are a resting
  palm and do nothing; a trackpad's fingers are never ignored. A pen that never reports leaving stops counting after 3 s. Off on desktop touch screens;
  **Preferences > Tablet > Ignore fingers while the pen is near** turns it on or off.
testers:
  - Android tablet with a pen: rest the hand on the screen while sculpting; the palm draws and navigates nothing.
  - Lift the pen: a finger sculpts or pinches again right away.
---
Palm rejection by pen proximity (PalmGuard in core/TouchGesture.h); desktop-testable through the palm_guard UI-test verb.
