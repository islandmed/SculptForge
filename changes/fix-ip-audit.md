---
whats_new: **Our own names, everywhere**: the keymap is now **Classic sculpting keys** with the **Classic layout**,
  the exchange menu is **File > Bridge** (Send, Receive, Import / Export Bridge file), and the brushes are stored as
  Sharp Crease, Clay Block and Tendril. Older projects, brush files, hotkeys, shelves and radial menus load as before.
highlight: Classic sculpting keys and the Bridge: SculptForge's own names throughout
changed: SculptForge now uses only its own names in the app, guide, website and listings: **Classic sculpting
  keys** and the **Classic layout** (Preferences, first-start welcome), **File > Bridge** (Send, Receive,
  Import / Export Bridge file), Lit Spheres, Parts, Face groups, Vertex Paint, Library. Brushes are stored as
  SharpCrease, ClayBlock and Tendril (old names in projects, brush files, scripts and configs still load); the
  library's lit-sphere folder is now `Documents/SculptForge/Lit Spheres` (an existing folder moves over once).
  The export plug-in is `SculptForgeBridgeExport.txt`, with its setup on the Bridge setup page. A new check
  (`tools/check_marks.py`, ctest `marks`) keeps third-party product names out of what we publish.
testers:
  - Any OS: open a project from 0.10.10 that uses Sharp Crease / Clay Block and has custom hotkeys or shelf
    buttons for them; the brushes, hotkeys and shelf buttons still work.
  - Any OS with custom lit spheres in Documents/SculptForge: start once; the images are in the Lit Spheres folder
    and still in the Material list.
  - Windows or macOS: File > Bridge > Send and Receive round trip as before.
  - First start (fresh config): the welcome offers Classic sculpting keys; Preferences > Input shows the same name.
---
fix/ip-audit phase 1 (user-visible). Phase 2 (code identifiers, comments, command and on-disk key names with read
aliases) follows on its own branch; see REPORT.md.
