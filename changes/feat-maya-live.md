---
whats_new: **Maya Live**: turn on **File > Maya > Live** (and **Live** on the Maya shelf) and each app sends what you change by itself, 300 ms after a stroke or edit, only the Parts that changed. Thanks @tristratos (#42).
whats_new: **Maya Live link**: **File > Maya > Live link...** connects SculptForge and Maya on this computer so Maya follows your stroke while you draw it. It asks every time, is off at every start, and the ember **Live link on** chip in the top bar turns it off.
highlight: Maya follows your strokes live (thanks @tristratos, #42)
added: **Maya Live** (#42, thanks @tristratos): **File > Maya > Live** sends the Parts a stroke, undo or edit changed
  to the exchange folder by itself once the scene is still for 300 ms (never mid-stroke), written in the background;
  the Maya shelf's new **Live** button does the same for Maya edits (node-dirty callbacks, debounced). Echo guard:
  manifests carry the writer's session id, a Get is never sent back, and an object that comes back unchanged is
  skipped (no undo step). Both sides pick up sends every 250 ms (was once a second); OBJs are written to a
  temporary name and renamed.
added: **Maya Live link** (#42): opt-in, off at every start and never remembered; a warning every time it is turned
  on; an ember **Live link on** chip in the top bar (and the Forge status bar) turns it off. A per-user Unix socket
  or named pipe (never a TCP or UDP port), a 128-bit pairing code (**File > Maya > Copy pairing code**, pasted into
  the shelf's new **Link** button), one client at a time, mesh data only in strictly parsed frames (1 GiB cap,
  timeouts, fuzz-tested). SculptForge streams the vertices a stroke moves about ten times a second and sends whole
  meshes when faces change; Maya edits come back as whole meshes. Studios can forbid it with
  `"maya": {"link": "never"}`. Guide: Working with Maya > Live, Live link.
security: New local listener, the Maya Live link: per-user socket / pipe only, opt-in with a warning each time,
  pairing code, mesh data only (SECURITY.md).
testers:
  - NOT YET VERIFIED in a real Maya (tests use a mocked maya.cmds) or on Windows (the named-pipe client): these
    are the first runs; report anything odd on #42.
  - Any OS with Maya 2022+: install the shelf again (six buttons). File > Maya > Live on in SculptForge, Live on the
    shelf; sculpt a stroke: Maya updates within about half a second; edit the mesh in Maya: SculptForge updates.
    No ping-pong (each change arrives once; no extra undo steps).
  - Live link: File > Maya > Live link... shows the warning (Cancel leaves it off); Turn on, Copy pairing code,
    Link on the shelf, paste: Maya follows a stroke while you draw it. Click Live link on in the top bar: off.
  - Windows: the same through the named pipe; a wrong code is refused.
---
Security read by input: OK after the pre-pairing allocation fix (readFrame caps a Hello at 256 bytes and grows
payloads a MiB at a time as bytes arrive). Tony approved the link 11-06 in the mayalive session. Not verified in real
Maya or on Windows yet.
