---
whats_new: **SpaceMouse detection** (#104): more 3Dconnexion devices are recognised (by name, also over Bluetooth),
  and **Edit > Preferences > Input > SpaceMouse** lists every controller SculptForge sees, says when another program
  (3DxWare, spacenavd) holds the device, and has **Copy device report** for bug reports.
whats_new: **Also new since 0.10.9** (missing from its notes): the brush ring is a true circle lying on the surface,
  symmetry copies show as dots, the crosshair stays while you turn the view, and **Preferences > Cursor > Hide ring
  while stroking** leaves only the dot while you sculpt (#88); drag empty panel space, or middle-drag anywhere in a
  panel, to scroll it with a pen (#100); Neutral is the default look for new installs; **Look > Outlines** (#98);
  the Strokes ring on <kbd>J</kbd>.
added: SpaceMouse (#104, reported by fenixpoints): Preferences > Input > SpaceMouse lists each joystick SDL reports
  (name, USB ids, and why it was or wasn't taken: not a SpaceMouse, cannot open, fewer than six axes), warns when the
  SpaceMouse is open but no axis has moved (another program such as 3DxWare holds it), and **Copy device report**
  copies that list with the version, platform and SDL version. Skipped devices are also logged.
changed: SpaceMouse recognition (#104): a device whose name says 3Dconnexion counts whatever its USB ids, and one
  with no vendor id (as some systems report Bluetooth devices) counts when its name says SpaceMouse / SpaceNavigator;
  the CadMouse and the keyboards never do.
changed: 0.10.9 notes, added late (they were lost in that release's merges; the code shipped in 0.10.9): brush cursor
  (#88, TheRedWaxPolice, with rodrigomoretto): the ring is a true circle in the surface's tangent plane, symmetry and
  radial copies are dots, the crosshair stays while the view turns, pans or zooms, and Preferences > Cursor has Hide
  ring while stroking (off by default). Panels scroll with a left-drag on empty space or a middle-drag (#100,
  ebarranco). Neutral is the default look for new installs; Look > Outlines and Outline opacity (#98); the Strokes
  radial menu on J; viewport hints with Don't show again (Preferences > Hints).
testers:
  - macOS with a SpaceMouse (3DxWare installed): Preferences > Input > SpaceMouse; note what it lists, push the cap,
    then quit 3DxWare, replug, and check the live bars move. Click Copy device report and paste it in #testers.
  - Windows with a SpaceMouse, with and without 3DxWare running: the same; good = the view pans/turns with the cap.
  - Any OS without a SpaceMouse but with a gamepad: it is listed as "not a SpaceMouse"; nothing moves the view.
---
#82 (Alt along the normal for Move / Snakehook) shipped before 0.10.9 (Preferences > Input > Along the normal); no
code change, reply only.
