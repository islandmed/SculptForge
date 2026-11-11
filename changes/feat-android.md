---
added: **Android (early, members first)**: SculptForge builds for Android tablets and phones (arm64, Android 9 or
  later, OpenGL ES 3.2) as a sideloaded APK. Sculpt with a finger or a stylus with pressure, navigate with two fingers
  (pan, pinch to zoom, twist to roll), undo, save to the app's Projects folder and open any file through the system's
  document picker. The desktop layout for now; a tablet layout of its own comes next (android/README.md).
testers:
  - Android tablet with a stylus (Android 9+): install the APK (allow installs from your browser or file manager),
    open it; the starter mesh shows within 10 s and nothing asks for a permission.
  - Draw with the stylus, light then hard: the stroke gets stronger with pressure. Lifting the stylus ends the stroke.
  - One finger on the mesh sculpts; one finger on the background turns the view; two fingers move it, pinch zooms,
    twist rolls. A two-finger touch never leaves a dab.
  - Undo and redo (Edit menu or the top bar) step back and forward through the strokes.
  - File > Save: the log line says where (Projects folder); close the app from Recents, reopen, File > Open Recent
    finds it and the sculpt is as it was.
  - File > Open: the system picker opens; pick a .sfproj or .obj from Downloads; it opens.
  - Rotate the tablet and switch apps and back: no crash, the sculpt is still there.
  - Report: device model, Android version, and what felt slow (strokes per second, orbit smoothness).
---
Reviewers: CI builds the APK (.github/workflows/android.yml); the GLES path also builds on Linux with
-DSCULPTFORGE_GLES=ON.
