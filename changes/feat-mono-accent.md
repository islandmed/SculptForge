---
whats_new: **Monochrome colours**: **Edit > Preferences > Interface > Look > Colours** has a grey-only scheme, brightness and no hue, with a white accent in place of the orange. The brush ring is white too, and its line shows the mode: solid Add, dashed Subtract, dotted Smooth.
added: **Monochrome** colour scheme for Forge Terminal (Preferences > Interface > Look > Colours, next to Neutral): every panel, text and accent colour is a pure grey and the accent is white, so the current tool, the switches that are on, the focus and selected text read by lightness (7:1 or better against idle controls). Deletes and errors keep their red; masks, face groups and paint stay coloured. The 3D view's brush ring is white with a dark edge and its line shows the mode (solid Add, dashed Subtract with Alt, dotted Smooth with Shift; a grey ring for Mask; the Paint brush keeps the paint colour), and the tags by the cursor and the Lazy Stroke icon's dot are white. `ui_look scheme=monochrome`. The default is unchanged, and every other colour scheme keeps its coloured ring.
member_request: true
testers:
  - Any OS: Edit > Preferences > Interface > Look > Colours > Monochrome. No orange or other hue in the panels; the current brush, a switch that is on, a selected row and selected text are all easy to tell from the idle ones.
  - Hover the model: a solid white ring; hold Alt for dashed, Shift for dotted. Still readable on a light model.
  - Paint a mask and a face group: they stay coloured on the model.
---
Reviewers: src/app/LookSchemes.cpp (the palette), src/app/Theme.cpp (near-white accent: hover darkens, text selection alpha 0.22, pop-up roles fully grey), tests/test_looks.cpp (no-hue + WCAG checks), tests/ui/look_monochrome.sfui, tests/ui/mono_ring.sfui; ring: src/app/App.cpp strokeRing / drawBrushCursor.
