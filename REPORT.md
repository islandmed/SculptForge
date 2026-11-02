# feat/look-mockups: the 0.10.7 default look

- Merged origin/main (0.10.6); one conflict (src/app/Config.h, both sides kept). The looks' CHANGELOG lines had landed
  under [0.10.6] in the merge: moved to [Unreleased] and rewritten for the new default.
- Defaults: new installs (no project.json and no ui.json) start in Forge Terminal + Tokyo Night + SculptForge layout.
  Existing installs keep theirs: a ui.json without look/layout keys (pre-0.10.7) reads as Classic + ZBrush-style, and an
  install with project.json but no remembered look gets Classic + ZBrush-style. They see a one-time card "SculptForge
  has a new look" [Try it] [Keep my layout] (close box = keep). The look is remembered in project.json ("ui_look",
  "new_look_card_done") without Store Config; a ui.json that names a look wins, as with the input style.
- Welcome: "Coming from ZBrush? Use the ZBrush-style layout" (one click, remembered).
- Fix (a): App::applyLook scaled the ImGui style again on every call (WindowMinSize grew 1.5x each time at 2400x1350,
  so the Timeline strip covered the axis gizmo; Classic too). Now it starts from a fresh ImGuiStyle. Test value
  style_window_min, checked in tests/ui/looks.sfui.
- Fix (b): fromJson(UiLayout) takes the shell layout; readUiConfig reads prefs first, so a SculptForge-layout ui.json
  without shelves gets the SculptForge shelves (tests/test_looks.cpp looks_stored_layout_without_shelves).
- Wording (Tony): "Match my Omarchy theme" is now "Match my desktop theme", tooltip "Uses your Omarchy theme's colours
  when Omarchy is installed", hidden when no Omarchy theme is found (unless it is on). UI, Spanish, guide, What's New,
  CHANGELOG, ui-design.md, THIRD_PARTY_NOTICES.md; identifiers unchanged.
- Tests: 49 UI scripts written for the Classic/ZBrush-style shell now set it explicitly on their first line
  (`run ui_look look=classic layout=zbrush`); no checks weakened. looks.sfui checks the new default, the card and the
  welcome button; whats_new.sfui checks the 0.10.7 section. Full ctest -j3 (Unix Makefiles): 117/117 pass
  (ui_stroke_resize disabled as before).
- Docs: CHANGELOG [Unreleased], What's New "## Version 0.10.7", guide interface chapter ("Looks and layouts"),
  coming-from-zbrush note, README feature line; website/guide rebuilt.
- Screenshots regenerated in ~/Work/look-mockups/{1600x960,2400x1350} (plus new-look-card.png).

## Website images that show the old default UI (not replaced: they are video recordings and guide shots)
- website/media: poster-showcase.jpg, poster-showcase-mobile.jpg, poster-brush-lab.jpg, poster-image-to-3d.jpg,
  poster-game-ready.jpg, poster-splats.jpg, screenshot-showcase.jpg, screenshot-game-ready.jpg, screenshot-splats.jpg,
  and the videos showcase*.mp4, brush-lab.mp4, image-to-3d.mp4, game-ready*.mp4, splats*.mp4, sculpt.mp4
- website/press: sculptforge-lion.jpg, sculptforge-lion-close.jpg; website/img/social-preview.png
- Guide images (docs/guide/images, e.g. interface-overview.jpg, palette-dropdown.jpg): ZBrush-style layout; the
  interface chapter now says the pictures show that layout. Regenerate with tools/guide/shots.sfui (now runs in the
  new default; add `run ui_look look=classic layout=zbrush` at its top to keep the old look).
