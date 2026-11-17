---
whats_new: **Python scripting in the app**: turn it on in **Edit > Preferences > Scripts**, then the console's **Python** mode runs
  Python against the open scene (loops, conditions, every command), and `.py` files in your Scripts folder are listed
  under **File > Script**. Each line or script is one undo step. Scripts are programs: only run ones you trust.
highlight: Python scripting in the app
added: **Python scripting in the app** (Edit > Preferences > Scripts, off by default): the console's Python mode and `.py` script
  files (the Scripts folder, listed under File > Script, and Run script file) run Python with the same `sculptforge`
  module as pipelines, against the open scene: `sf.document()`, `sf.run("command", ...)`, Parts, the mesh and selection
  data, brushes. Each line or script is one undo step; errors stop the script, never the app. Python is loaded only the
  first time a script runs, so it costs nothing at start-up or per frame. Nothing you open can start a script, AI tools
  and macros can't run Python, and a studio policy can lock it off (`"python": {"scripts": "never"}`). Requested by
  Tpqz_ on Discord and chris_gardner on Discord.
added: The Python module's mesh arrays work without NumPy: they are lists of rows when NumPy isn't installed.
testers:
  - Any OS: Edit > Preferences > Scripts > Enable Python scripts. Open the console (`), click Python, type
    `for i in range(2): sf.run('subdivide')`, Enter, then Enter on the empty line: two levels; Ctrl+Z takes both back.
  - Any OS: Edit > Preferences > Scripts > Open Scripts folder, save a .py file there (`import sculptforge as sf; print(sf.document().parts)`),
    then File > Script: it is listed; click it: the console shows the Parts.
  - Any OS: with Python off, open projects and models as usual: nothing changes, and the console's Python mode says how to turn it on.
  - Windows: the installer is a little bigger (Python ships with the app); the app starts as fast as before.
---
Phase 1 of 3 (docs/design/python-scripting.md): script-made interface and the event-loop hook follow on this branch.
