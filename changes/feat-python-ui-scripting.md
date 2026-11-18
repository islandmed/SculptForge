---
whats_new: **Scripts can add buttons and panels**: a Python script can add actions (command palette, shelves, My Panel, keys)
  and panels of labels, buttons, sliders and check boxes under **File > Script**. Scripts ticked under **Edit > Preferences >
  Scripts > Run at start-up** bring theirs back at every start; a script that changed since you trusted it does not run
  until you trust it again.
highlight: Python scripts can add buttons and panels
added: **Python scripts add to the interface** (`sf.ui`, Edit > Preferences > Scripts): actions (command palette, shelves,
  My Panel, keys) and panels of labels, buttons, sliders and check boxes under File > Script, drawn by SculptForge's own
  controls. **Run at start-up** (off, per script, only for files in the Scripts folder): the script's bytes are read once,
  hashed and run from memory; if they differ from the version you trusted, nothing runs, the status bar says why, and
  **Trust this version** brings it back. A studio policy of `"python": {"scripts": "never"}` also stops start-up runs. A timer
  on the main thread (`sf.ui.timer`) keeps other toolkits' windows, such as PySide, alive beside the app. Requested by
  Tpqz_ on Discord and chris_gardner on Discord.
testers:
  - Any OS: save the "A panel of your own" recipe from the guide's Python scripting page as a script and run it: a Clean up
    section appears in My Panel; its button remeshes at the slider's resolution; Ctrl+Z takes it back in one step.
  - Any OS: tick that script under Edit > Preferences > Scripts > Run at start-up, restart: the panel is back. Edit the
    file, restart: it doesn't run, the status bar says why, and Trust this version (Preferences > Scripts) brings it back.
  - Any OS with PySide6 installed for Python 3.13 (see the guide): run the "A PySide window" recipe: a Parts window opens
    beside SculptForge; sculpting stays smooth; its button subdivides the picked Part; closing it stops its timer.
---
P2 and P3 of docs/design/python-scripting.md, with the P3 security review there; start-up runs the bytes that were hashed.
