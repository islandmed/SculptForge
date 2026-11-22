---
whats_new: **Chinese, Japanese and Korean names**: a Part or layer named in those scripts is readable in the title strip's info line (it is drawn in the regular size, not the small one).
fixed: The title strip's readout ("Verts ... Part ... Layer ...") drew a Part or layer name in Chinese, Japanese or Korean as a grey smudge at 12 px (the small face); with such text the readout now uses the regular face. Latin names are unchanged, pixel for pixel.
testers:
  - Rename a Part to 頭 (Parts list, F2); the title strip's "Part 頭 (1/1)" reads clearly. Rename it back to a Latin name: the small readout as before.
---
Reviewers: src/app/Shell.cpp (hasWideText, infoFont), App.h infoFontPx_, UiTest info_font_px, tests/ui/info_line_cjk.sfui. The whole readout line changes size with CJK text (labels included), as in the Chinese and Japanese UIs.
