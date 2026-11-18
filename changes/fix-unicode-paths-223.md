---
whats_new: **Files with non-English names** open on Windows again: a model with a Japanese name, a folder in Cyrillic or Korean, an emoji in the name no longer closes SculptForge.
fixed: Windows: SculptForge closed with no message on opening or importing a file whose name has characters outside the system code page (gh#223, reported by @maxyui77). The window title, the status line, Open, Import and drag and drop, project open and save, the Sculpt Timeline sidecar and pack create/install now read and write names as UTF-8 (core/Utf8Path.h). A command that fails with an error now says why in the status line instead of closing the app.
testers:
  - Windows with an English or other non-Japanese system language: copy any .glb to a Japanese name such as the word test in katakana (also .obj, .fbx, .ply, .stl, .usda, and a saved .sfproj); File > Open, drag it onto the window, Import. It opens, the title bar shows the name.
  - Same with the file inside a folder with a Chinese, Cyrillic or Korean name, and with an emoji in the name.
  - Export Glb/Obj/Fbx/Stl/Ply/Usd to such a name, then open it again.
---
Regression test: tests/test_unicode_paths.cpp (Japanese, Chinese, Korean, Cyrillic, accents, emoji; every format; project; pack).
