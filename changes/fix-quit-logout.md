---
fixed: **Quitting with Alt+F4 on Linux** (#183, X11): the window now stays until Alt and F4 are released (2 seconds at
  most). Closed while they were still down, the held key repeat or the release reached the desktop behind it, where
  Alt+F4 can be Log out. Stopping a helper program (splat trainer, COLMAP, ffmpeg) also can only signal that helper
  now: never our own process group, never everything (a pid of 0 or -1), never one that was already gone.
testers:
  - Linux X11 (.deb, Pop!_OS 22.04 with Nvidia, GNOME): quit with Alt+F4, File > Quit and the window button, also holding Alt+F4 a little long; the desktop session stays logged in. Tell us if it still logs out (with journalctl -b 0 around it).
---
