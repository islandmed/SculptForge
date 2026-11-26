---
whats_new: **Monochrome colours**: **Edit > Preferences > Interface > Look > Colours** has a grey-only scheme, brightness and no hue, with a white accent in place of the orange.
added: **Monochrome** colour scheme for Forge Terminal (Preferences > Interface > Look > Colours, next to Neutral): every panel, text and accent colour is a pure grey and the accent is white, so the current tool, the switches that are on, the focus and selected text read by lightness (7:1 or better against idle controls). Deletes and errors keep their red; masks, face groups and paint stay coloured. `ui_look scheme=monochrome`. The default is unchanged.
member_request: true
testers:
  - Any OS: Edit > Preferences > Interface > Look > Colours > Monochrome. No orange or other hue in the panels; the current brush, a switch that is on, a selected row and selected text are all easy to tell from the idle ones.
  - Paint a mask and a face group: they stay coloured on the model.
---
Reviewers: src/app/LookSchemes.cpp (the palette), src/app/Theme.cpp (near-white accent: hover darkens, text selection alpha 0.22, pop-up roles fully grey), tests/test_looks.cpp (no-hue + WCAG checks), tests/ui/look_monochrome.sfui.
