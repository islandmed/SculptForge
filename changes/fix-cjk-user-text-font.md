---
highlight: Your languages: names you type in Chinese, Japanese or Korean (Parts, layers, folders, brush sets, files) draw with your system's fonts instead of "?"
whats_new: Names you type in Chinese, Japanese or Korean (Parts, folders, layers, brush sets, files, Library items) draw with your system's fonts instead of **?**, in any interface language.
fixed: Text people type (Part, folder, layer and brush-set names, file names, Library items, pasted text) with a
  character outside the bundled fonts drew as "?": the Chinese fonts carry only the interface's own characters.
  Such a character is now taken from the system's fonts the first time it is drawn (Windows: Microsoft YaHei,
  JhengHei, Yu Gothic, Malgun Gothic; macOS: PingFang, Hiragino, Apple SD Gothic Neo; Linux: fontconfig, e.g. Noto
  Sans CJK), including characters past U+FFFF such as 𠀋. Nothing is loaded while no such text is shown, and the
  interface uses about 15 MB less memory (the fonts are no longer copied per size). Shortened names (the title bar)
  no longer end in a broken character when cut in Chinese.
testers:
  - Windows, English interface: rename a Part to 鬱龘𠀋 (or any Japanese / Korean name): it draws, no "?"; same at 150% / 200% UI scale.
  - Windows, Preferences > Interface > 简体中文: the same name still draws; the menus look as before.
  - Linux with noto-fonts-cjk: the same.
---
Mechanism: cmake/Dependencies.cmake builds imgui_draw.cpp from a copy whose ImFont::FindGlyph calls a hook on a
missing glyph and AddFont no longer copies caller-owned font data per size (fails the configure if a new ImGui
changes either); IMGUI_USE_WCHAR32 for characters past the BMP;
src/app/UserGlyphs.cpp looks them up between frames and the atlas is rebuilt with them merged.
Test: tests/ui/user_text_glyphs.sfui (glyphs_missing / glyphs_merged; test font tests/data/fonts/cjk-fallback-test.otf).
Measured (laptop, load ~4, offscreen UI test, 5 runs): no CJK text 0.31 s / 170 MB vs main 0.32 s / 185 MB; with a
CJK name and the system's Noto Sans CJK (19 MB) +19 MB and ~0.2 s once, at the first such frame.
