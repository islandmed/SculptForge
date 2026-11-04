---
whats_new: **Pattern brushes on micro levels**: Dragon Scales, Skin Pores, Stone Chisel and the other pattern brushes keep their pattern on a micro level, and carving ones (Wrinkle) carve.
whats_new: **Cmd+, opens Preferences** (Ctrl+, on Windows and Linux).
fixed: **Pattern brushes on micro levels** (#120, reported by RodTDai): a brush recipe on a micro level was a plain
  Standard dab, without its surface pattern (Dragon Scales, Skin Pores, Cracks...) and building up even when the
  recipe carves. It now keeps the pattern and its depth's sign there; planes, pinch and drag still don't apply to
  micro levels.
fixed: **Windows portable zip starts on a fresh PC** (#109, reported by git-tiger): the Visual C++ runtime
  (msvcp140, vcruntime140, vcruntime140_1) now ships next to sculptforge.exe in the zip and the installer; before,
  a PC without the Visual C++ Redistributable couldn't start it. When SculptForge can't start (no OpenGL 3.3, an
  old graphics driver) it now says why in a message box, with the log's path, instead of quietly exiting.
added: **Cmd+, / Ctrl+, opens Preferences** (#111, suggested by fenixpoints).
testers:
  - Windows PC without the Visual C++ Redistributable (or a fresh VM): unzip the portable zip, run bin\sculptforge.exe: it starts.
  - Any OS: Divide to Micro 4 or more, sculpt with Dragon Scales: scales appear; Wrinkle carves a groove.
  - macOS: Cmd+, opens Preferences; Windows/Linux: Ctrl+,.
---
