---
changed: **Startup splash**: the start-up is also checked just before the first frame (a slow first frame or panel restore
  showed no splash), and once the splash is up it stays at least 0.7 s so it is seen, not flashed. A start that is over
  within 0.3 s still shows no splash and gets no added wait. `SF_SPLASH=1` (environment variable) shows it on that run
  whatever the start-up time or the preference, for 0.7 s at least, to see it on a fast computer.
testers:
  - Linux (Wayland and X11): start SculptForge with `SF_SPLASH=1` set; the splash shows, stays about 0.7 s, then the main window replaces it.
  - Any OS, normal start on a fast computer: no splash and no extra wait (the main window just appears).
---
The 0.12.5 splash (#176) is new in 0.12.5, so this is a "changed" follow-up, not a fix of something users had.
