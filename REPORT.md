# REPORT: feat/stroke-popup (Discord 11-02 stroke-type popup), 0.10.9

## What
- **Stroke popup**: action `stroke.popup`. Key G in the ZBrush, SculptForge and Maya styles, J in Blender (its G is the Move brush). Also a new left-shelf button under the Stroke tile; layout version 4 adds it to stored layouts.
- A plain 5x2 grid at the cursor (not a pie ring). It opens with the current stroke under the pointer. Top row: the 5 stroke types. Bottom row: Add, Sub, Smooth, Mask, Select, with key hints (Alt / Shift / Ctrl / Ctrl+Shift) and a help line on hover.
- Hold the key, point, let go: picked (a hovering pen works the same). Tap the key: the grid stays open; clicking a stroke picks it and closes, clicking a modifier toggles it and stays open. Esc or a click beside the grid closes it. The click is swallowed and never reaches the model.
- Smooth, Mask and Select latch: `inputMods()` adds Shift / Ctrl / Ctrl+Shift until the same item is picked again or another brush is picked. A "Mask" etc. label shows next to the cursor. Navigation uses the new `inputMods(false)`, so a background drag still orbits: the onMouseDown `nav` branches, orbit snap / roll / alt-pan, Gizmo snap and hover, Kelvinlet capture.
- Core logic, unit-tested: src/core/StrokePopup.{h,cpp} (geometry, hit-test, latch rules). App side: src/app/StrokePopup.cpp. The shelf tile's stroke drawing was refactored into `drawStrokeIcon`.
- "Remember last choice": the grid opens on the current stroke. Persistence is left to fix/inbox-0107's per-brush settings (#78), which already store the stroke per brush, so I dropped my project-file field to avoid two stores fighting.
- Docs: CHANGELOG [Unreleased], whats-new.md "## Version 0.10.9" bullet (rename as needed when merging), sculpting.md "The stroke popup" section, shortcuts.md regenerated (ui_guide_shortcuts passed locally).

## Tests
- New: tests/test_stroke_popup.cpp, tests/test_shell_layout.cpp `shell_left_shelf_stroke_popup`, tests/ui/stroke_popup.sfui, plus `ui_stroke_popup_scaled` (2400x1350, 1.5x UI). New UI-test verb `stroke_popup_item ITEM [click] [pen]` and probes stroke_popup_*, stroke_latch, invert_toggle.
- Ran locally: build green; ui_guide_shortcuts passed. ui_stroke_popup and ui_stroke_popup_scaled passed through line 78: hold/let go, beside/cancel, tap+click, modifiers, the latched Mask stroke masking with moved == 0, and a background drag not inverting the mask.
- The rest of the UI script has NOT run locally. The failure at 79/84 was the test's `drag` verb (a UI drag, so ImGui kept the mouse and swallowed the next key); it's now `view_drag`. The full ctest and the new unit tests have not run locally (queue stopped by the new plan). CI and the merger's combined ctest are the check.
- Screenshots checked at 1x and 1.5x (build/ui-shots/stroke_popup*/). Fixed after looking: the brush ring drew over the popup; it's now hidden while the popup is open.

## Merge notes
- fix/windows-pen-ui conflicts in App.h / Tablet.cpp: it splits `keyMods()` out of `inputMods()`. Resolve as `inputMods(bool latch)` = keyMods() + pen buttons + my latch block at the end; keep both declarations.
- fix/inbox-0107 and feat/view-gizmo-size: no conflicts with this branch (their whats_new.sfui conflict is with main). No branch binds G or J.
- UiLayout version 4: if another branch also bumps the layout version, take the higher number and keep both migrations.

## Files for Tony
REPLY.md: Discord reply draft, no signature. TESTERS.md: #testers checklist (Windows tablets first).
