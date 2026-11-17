---
changed: The Scoop manifest keeps the portable .zip's `bin\data` folder (settings, brushes, library, layouts) across `scoop update`, now that the portable .zip carries its data beside the program (#170).
testers:
  - Windows with Scoop on 0.13.6: `scoop update sculptforge` to 0.13.7; change a setting first (Edit > Preferences), after the update it is still set and `bin\data` is in the app folder under `scoop\apps\sculptforge\current`.
---
Packaging only; the manifest's `persist` entry, and release.sh `check` refuses a manifest without it.
