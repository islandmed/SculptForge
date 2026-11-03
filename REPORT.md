# REPORT: feat/stroke-popup (Discord 11-02 stroke-type popup), 0.10.9

## What
- **Stroke popup**: action `stroke.popup`. Key G in the ZBrush, SculptForge and Maya styles, Z in Blender (its G is the Move brush; J is cursor-look's Strokes radial menu, N is bound to a user menu in ui_radial_menu). Also a new left-shelf button under the Stroke tile; layout version 4 adds it to stored layouts.
- A plain 5x2 grid at the cursor (not a pie ring). It opens with the current stroke under the pointer. Top row: the 5 stroke types. Bottom row: Add, Sub, Smooth, Mask, Select, with key hints (Alt / Shift / Ctrl / Ctrl+Shift) and a help line on hover.
- Hold the key, point, let go: picked (a hovering pen works the same). Tap the key: the grid stays open; clicking a stroke picks it and closes, clicking a modifier toggles it and stays open. Esc or a click beside the grid closes it. The click is swallowed and never reaches the model.
- Smooth, Mask and Select latch: `inputMods()` adds Shift / Ctrl / Ctrl+Shift until the same item is picked again or another brush is picked. A "Mask" etc. label shows next to the cursor. Navigation uses the new `inputMods(false)`, so a background drag still orbits: the onMouseDown `nav` branches, orbit snap / roll / alt-pan, Gizmo snap and hover, Kelvinlet capture.
- Core logic, unit-tested: src/core/StrokePopup.{h,cpp} (geometry, hit-test, latch rules). App side: src/app/StrokePopup.cpp. The shelf tile's stroke drawing was refactored into `drawStrokeIcon`.
- "Remember last choice": the grid opens on the current stroke. Persistence is left to fix/inbox-0107's per-brush settings (#78), which already store the stroke per brush, so I dropped my project-file field to avoid two stores fighting.
- Docs: CHANGELOG [Unreleased], whats-new.md "## Version 0.10.9" bullet (rename as needed when merging), sculpting.md "The stroke popup" section, shortcuts.md regenerated (ui_guide_shortcuts passed locally).

## Tests
- New: tests/test_stroke_popup.cpp, test_shell_layout `shell_left_shelf_stroke_popup`, tests/ui/stroke_popup.sfui and `ui_stroke_popup_scaled` (2400x1350, 1.5x). New UI-test verb `stroke_popup_item ITEM [click] [pen]`, probes stroke_popup_*, stroke_latch, invert_toggle.
- After merging origin/merge/0.10.9 (ea476e3), locally via heavy.sh: build green; all 8 sculptforge_tests shards passed; ui_stroke_popup, ui_stroke_popup_scaled, ui_radial_menu, ui_shell_layout, ui_shell_layout_1080, ui_whats_new, ui_guide_shortcuts passed. The full ctest has not run (the merger runs it on the combined tree).
- Bug fixed from the tests: near the canvas edge the grid shifts to stay on screen, and a quick tap used to pick the item that landed under the pointer. A tap is now judged against the item under the pointer when the grid opened.
- Screenshots checked at 1x and 1.5x. The brush ring no longer draws over the grid.

## Merge notes
- Merged origin/merge/0.10.9 (ea476e3). The latch now sits on top of inbox-0107's keyMods(): inputMods(bool latch) = keyMods() + pen buttons + latch. It coexists with cursor-look's Strokes ring (J) and stroke.type.* actions, and with precision's Steady Stroke (L). Guide text uses Edit > Preferences paths.
- UiLayout version 4 (the stroke popup's left-shelf button).

## Files for Tony
REPLY.md: Discord reply draft, no signature. TESTERS.md: #testers checklist (Windows tablets first).
