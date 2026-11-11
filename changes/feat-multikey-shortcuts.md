---
whats_new: **Two-key shortcuts**: bind an action to two different keys in a row, like A then S (**Edit > Preferences > Hotkeys**, the small button beside a key).
highlight: Shortcuts with two keys in a row
added: **Two-key shortcuts** (Preferences > Hotkeys, the >> button beside an action's key): press one key, then a different one within half a second (A then S). Added to the action's other keys; the same key twice is refused; a sequence another action uses asks first (Replace / Cancel); Backspace in that box removes only the two-key shortcuts. A first key that is bound on its own still works: it waits half a second and runs by itself when no second key follows. Requested by Artizmo on Discord.
testers:
  - Any OS: Edit > Preferences > Hotkeys, find Wireframe, click the >> button, press F7 then F8. Press F7, then F8 within a moment - Wireframe toggles; F7 alone, or F8 a second later, does nothing.
  - Any OS: give Zoom in the single key F7 too; F7 alone zooms after half a second, F7 then F8 toggles Wireframe without zooming.
---
Sequences are one more alternative in an action's shortcut text ("A then S / F5"), saved in the same overrides, so old preference files keep working. Single-chord readers (pan and zoom hold, brush sets, Library keys) never see them. The timer is stepped by the frame loops' dt (App::tickKeySequence, in both the app's loop and the UI test runner's), not a wall clock. Conflict: holders compare whole sequences; "A then S" does not conflict with a plain A or S. App.cpp: one line in each frame loop, away from updateKeyNav.
