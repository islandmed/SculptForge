---
whats_new: **SpaceMouse**: older models that never report letting go of the cap no longer leave the view drifting; a cap that stops reporting stops the view. **Preferences > Input > SpaceMouse** also has **Log device reports** for bug reports.
fixed: **SpaceMouse** (experimental): with some older models (a SpaceExplorer on Windows) the view kept moving at the last speed after the cap was let go, and an opposite push only slowed it. The device never sends its all-zero report, so SculptForge now counts a cap whose six axes have not changed at all for a third of a second as let go (**Stop when the cap stops reporting**, on by default). The pointer filter that drops 3DxWare's mouse and wheel echo no longer stays on while the last push is latched.
added: **Log device reports** (Preferences > Input > SpaceMouse, or the environment variable `SF_SPACEMOUSE_LOG=1`; off unless switched on, local only): writes the SpaceMouse's axis values and its raw HID reports with their report ids and times to `spacemouse-log.txt` in the user data folder (no paths or names in it); **Copy log** puts a short excerpt on the clipboard. To paste into a bug report as text.
testers:
  - SpaceMouse owners, any model, Windows especially (SpaceExplorer, SpaceNavigator, SpacePilot): push the cap and let go; the view stops at once. Push steadily for several seconds: the view keeps moving the whole time.
  - Same with 3DxWare running and with it quit (see the guide's Navigation page).
  - If the view still drifts or stops mid-push: **Edit > Preferences > Input > SpaceMouse > Log device reports** (local, opt-in), reproduce it for ten seconds, switch it off, copy the lines from `spacemouse-log.txt` (the first 50 are enough) and paste them into the report as text.
---
Reporter: Discord #bug-reports, Space Explorer 046d:c627, SDL 3.2.30, speed 0.55x, dead zone 15%. No device here: the stop is guessed from
the symptom and covered by recorded report sequences in tests/test_spacemouse.cpp; the log is for confirming it.
