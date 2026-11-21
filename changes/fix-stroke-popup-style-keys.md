---
whats_new: The **stroke popup** shows the keys of your input style: with the Blender style Sub reads Ctrl and Mask reads Alt, with Maya Mask reads Ctrl+Alt.
fixed: The stroke popup's Sub / Smooth / Mask / Select tiles showed the default style's keys (Alt, Shift, Ctrl,
  Ctrl+Shift) in every input style; they now show the keys of the style set in Preferences > Input (Fixes #233).
  Reported by @Velisariy.
testers:
  - Preferences > Input > Style > Blender, then press Z over the model: Sub reads Ctrl, Mask reads Alt. Hold those keys and drag; they do what the tile says.
  - Same with Maya (Sub Ctrl, Mask Ctrl+Alt), SculptForge and Classic (Alt, Shift, Ctrl, Ctrl+Shift).
---
Reviewers: core/StrokePopup.cpp strokeModifierKeys (one row per style, mirrors keyMods / blenderMods); test: ui_stroke_popup_style_keys, test_stroke_popup.
