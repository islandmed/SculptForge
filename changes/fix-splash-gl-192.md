---
whats_new: **Start-up fix**: SculptForge should start again on graphics drivers where 0.12.5 and 0.12.6 stopped with "Shader compile error".
fixed: **App would not start on some Linux NVIDIA drivers** (0.12.5 regression): most likely the start-up splash left its own OpenGL context current, so the main shaders compiled in the wrong context ("Shader compile error" with garbage text, thanks @dCo3lh0 (#192)). The main context is made current again after every splash step, the splash draws a plain software surface, and a start-up shader error now says "no current OpenGL context" or "(empty driver log)" instead of random bytes.
testers:
  - Linux NVIDIA (X11 and Wayland): start the app with SF_SPLASH=1; the splash shows, then the main window opens with no shader error.
---
Workaround for 0.12.5/0.12.6: SF_STARTUP_SPLASH=0.
