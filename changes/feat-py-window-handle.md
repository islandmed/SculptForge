---
whats_new: **Script windows stay with SculptForge**: a script's own window (PySide, Qt) can be parented to SculptForge's main window with `sf.ui.main_window_handle()`, so it groups with it in the taskbar and stays on top.
added: **`sf.ui.main_window_handle()`** (Python scripts): the main window's native handle as an int (window handle on
  Windows, `NSWindow` pointer on macOS, X11 window id on X11; `None` on Wayland and without a window), so a script's
  own PySide dialog can be made a transient child of SculptForge (taskbar grouping, stays on top, modality). Read-only;
  it adds no new capability. Requested by a community member.
testers:
  - Windows with PySide6 in the Scripts folder's site-packages: run the PySide recipe in the guide (Python > A PySide window); the Parts window stays above SculptForge and shares its taskbar button.
  - Any OS: in the console, `sf.ui.main_window_handle()` prints a number (None on Wayland).
---
Host ABI 4 -> 5 (AppSide::windowHandle); the host and app ship together, the build check already refuses a mismatch.
