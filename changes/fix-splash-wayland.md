---
whats_new: **Startup splash on every start**: the logo and version now show at every start for at least half a second, centred, then fade into the main window, and it now shows on Wayland, where it never appeared. **Preferences > Interface > Show startup splash** turns it off for an instant start.
changed: The start-up splash shows on every interactive start for at least 0.5 s (overlapped with start-up work), centred and floating, and fades into the main window. Turn it off in Preferences > Interface > Show startup splash.
fixed: The start-up splash never appeared on Wayland ("Window framebuffer support not available"); it is now drawn through an SDL renderer when the window has no surface, and fully drawn before the window is shown.
testers:
  - Linux Wayland (Hyprland or GNOME): start SculptForge; a centred splash with the version shows for half a second, then the main window appears. No empty flash.
  - Windows at 150% and 200% scaling, macOS Retina: the splash is sharp (not blurry), centred, and fades into the main window.
  - Preferences > Interface > Show startup splash off: restart; the main window shows at once.
---
Debug: SF_SPLASH_PATH=surface|renderer|software picks one drawing path; the log says which one was used.
