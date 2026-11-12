---
testers:
  - Android (Insiders beta): Settings > Apps > Special access > Install unknown apps: allow your browser or file manager, and uninstall an older SculptForge APK first (each beta is signed with a throwaway key; arm64 phones and tablets take the arm64-v8a file, Chromebooks and the emulator the x86_64 one).
  - Pen: strokes with pressure; rest your palm on the screen while drawing (the palm does nothing) and lift the pen (a finger works again).
  - Two-finger pan, pinch and twist; Save then Open a project; close the app and reopen it (autosave).
---
Tooling: scripts/release.sh release_apk puts the Android APKs of the beta's commit on the private Insiders release (never the public one); a warning, never a failure, if there is none.
