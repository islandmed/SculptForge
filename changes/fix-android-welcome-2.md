---
whats_new: **Help > Copy log to clipboard** is in the Help menu now (it was only in the command search), for reports from a phone where the app's folder can't be opened.
fixed: **Help > Copy log to clipboard** was only in the command search (F3); it is now in the Help menu too, where an Android tester who can't open the app's folder looks for it.
changed: **Android**: a mesh is drawn through the extension's multi-draw only when the driver lists the extensions that define it (otherwise one core draw call per range, and never a missing entry point), and the log records the first GL error a view draw raises and the first frames' triangle counts, for the empty 3D view some Mali tablets show (not confirmed fixed: testers).
changed: **Android**: the welcome's phone sheet needs a touch screen (a short desktop window with a mouse keeps its movable window).
testers:
  - Android Mali tablet (the Mali-G72 that showed an empty 3D view): the sphere shows; if it is still empty, Help > Copy log to clipboard and send the "GL ES: multi-draw", "GL error" and "View frame" lines.
  - Any Android phone or tablet: Help > Copy log to clipboard is listed under Help.
---
Follow-up to fix-android-welcome (0.12.14-beta.1). The multi-draw gate is a hypothesis from ebarranco's log (no shader failure, no GL error line,
81.9k triangles in the bar, nothing drawn): Android's EGL hands out a no-op entry point for an extension function the driver lacks.
