---
fixed: **UV Editor**: the "No UVs yet: pick a method and Unwrap" hint was cut off on both sides in a narrow view or at
  a big UI scale (240 %); it now wraps inside the view, and it is translated.
---
Reviewer notes: PanelUvEditor.cpp, the empty-state hint: AddText with a wrap width (the view less 48 px scaled), the
box sized to the wrapped text; tr() on both messages (es + zh-TW added). tests/ui/uv_editor_hint.sfui (shots at 100 / 240).
