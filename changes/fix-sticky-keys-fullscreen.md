---
whats_new: **No more Sticky Keys prompt** (Windows): pressing <kbd>Shift</kbd> five times while sculpting no longer pops
  the Sticky Keys prompt. **Edit > Preferences > Input > Stop the Sticky Keys prompt while SculptForge has focus** (on by default).
fixed: Windows: pressing Shift five times (smoothing, the camera keys) popped the Sticky Keys prompt over the canvas,
  full screen or windowed. While SculptForge has focus, only the Sticky, Filter and Toggle Keys shortcuts are turned
  off (Microsoft's way for games), never a feature that is on and never saved to the profile; they come back on focus
  loss, minimise, quit, a crash or a closed console, and a killed session's are put back at the next start.
  **Edit > Preferences > Input > Stop the Sticky Keys prompt while SculptForge has focus** (on by default; script:
  `sticky_keys on=false`). Reported by Oslen on Discord.
testers:
  - Windows, full screen (F11): press Shift five times on the canvas; no Sticky Keys prompt.
  - Windows, windowed: the same; then Alt+Tab to Notepad and press Shift five times: the Sticky Keys prompt shows there.
  - Windows: turn Sticky Keys on in Settings > Accessibility > Keyboard, start SculptForge: Sticky Keys still works,
    and Settings shows it unchanged after quitting.
  - Windows: Preferences > Input > untick Stop the Sticky Keys prompt; Shift five times shows the prompt again.
---
Clean-room from Microsoft's documented SystemParametersInfo pattern; src/app/StickyKeys.{h,cpp}.
