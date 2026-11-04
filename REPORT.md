# feat/content-pass: 60 brushes in six packs, A-Z lists with a Sort menu, one Masking panel

From the reviewer video of 11-03 ("missing a ton of stuff", "have to build a lot of these brushes myself", "not much
masking", "alphas not alphabetical"). Clean-room: own engine, own procedural alphas, own names (check_marks passes),
no other app used or looked at. Target for 0.11.0-beta.1.

## What
1. **Sorting**: brushes (grid + Custom list), alphas, materials and the Library are A-Z by default (natural,
   case-insensitive: "Brush 2" before "Brush 10"). A **Sort: ...** button beside each list switches to Recent or
   (brushes, alphas) Pack; saved in the preferences (`brushSort`, `alphaSort`, `materialSort`, `librarySort`).
   Filters stay; the Alpha palette gains one. Round stays first among alphas (it is "no alpha"). QuickSave stays
   newest-first (its names are times). Core: `src/core/ListSort.h`.
2. **Masking panel** (Tool > Masking): Shapes (Mask Pen / Lasso / Rectangle), **Mask by** (cavity, peaks, face group,
   colour, facing, border) and **Adjust** (grow, shrink, sharpen, blur, invert, clear, all), each a button with a
   drawn picture of its effect and a Ctrl-hover hint; options in "Mask by options". New commands
   `mask_by_peaks`, `mask_by_color` (main colour, tolerance), `mask_by_facing` (camera / up / down, angle).
   All old labels/ids kept (existing UI tests and the Spanish UI unchanged).
3. **Brush packs**: 60 recipes in Clay & Form, Hard Surface, Organic & Skin, Cloth & Folds, Damage & Wear, Ornament
   (`src/core/BrushPacks.cpp`), each with a one-line description; built-in (read-only, override = user copy).
   Six new procedural alphas: Cobblestone, Leather Grain, Hex Plates, Knit, Bolt, Chips (21 in all). Recipes may
   name an `alpha` (+ `alphaFollow`); a pack brush brings it on first pick and on Reset Brush to Default; older
   .sfbrush files/brush codes are byte-identical. `brush_use name=...` picks one from scripts/macros.
   Thumbnails by our renderer (textured brushes stroke wider so texture reads; thumbnail cache version 4 -> 5).
4. **Docs**: `docs/guide/brush-packs.md` (one sheet per pack + table), Masking chapter rewritten for the panel
   (new image), `docs/brushes.md` keys, `changes/feat-content-pass.md` (whats_new, 2 highlights, testers list).

Contact sheet of all 60 thumbnails: `REPORT-contact-sheet.jpg`. Masking panel: `docs/guide/images/masking-panel.jpg`.

## The bar, in our own numbers (absolute targets, no side-by-side)
| Target | Result |
|---|---|
| >= 60 ready-to-use brushes in named packs | 60 in 6 packs (built-in recipes 7 -> 67), 10 per pack, unique names |
| Every pack brush leaves a visible mark on the preview ball (lift >= 0.5% of radius, >= 200 vertices moved) | 60/60 |
| No pack brush folds triangles at preview strength 0.9 | 60/60 with 0 folded (the classic Wrinkle folds 30, Palette Knife 54 under the same check) |
| No towers (lift <= ~28% of radius) | 60/60 (first pass had Raised Plate at 147%: fixed) |
| Stroke cost on the ~82k-triangle ball | 60 strokes 1.6 s in all, slowest 62 ms (budget 400 ms) |
| Alphas: real pattern, in range, no square edge | 21/21 (border < 0.05, mean 0.02-0.8) |
| Masking: whole-mesh mask builders | 4 -> 7 (cavity, face group, border + peaks, colour, facing), 7 adjust ops |
All enforced by tests (`test_brush_packs.cpp`, `test_brushes.cpp`, `test_masking.cpp`), so they can't regress.

## Tests (local, Linux, heavy.sh -j3)
- Unit: all 16 `sculptforge_tests` shards pass (new: brush_packs_*, brush_recipe_alpha_keys_round_trip,
  brush_alphas_builtin_set, list_sort_orders, mask_by_peaks_finds_ridges, mask_by_color_and_facing).
- UI: new `brush_packs.sfui`, `masking_panel.sfui` pass; the 46 existing UI tests touching masks, brushes, alphas,
  materials and the Library pass (mask_lasso and packs needed a hint word / a value-name fix, done).
- marks, release_notes(_fragments), guide_site, guide_menu_paths, brush_gallery_index pass.
- Not run: full ctest (the merger runs it). Screenshots reviewed: brush palette by pack, alpha palette, masking
  panel (two iterations: bigger pictures, outlines on dark), contact sheet (two iterations: clay end flares, Hard
  Cut tear and faint textures fixed).

## Notes for the merger
- Merges cleanly onto origin/main (17 commits ahead at push time).
- Library projects are now A-Z by default (were newest first); **Sort: Recent** gives the old order.
- Projects save all brushes incl. the 60 pack recipes in the manifest (~25 KB more JSON); unchanged ones load back as
  library brushes, as before.

SHA: 1d0062f3 (code, docs, tests); this REPORT and the contact sheet on top of it. Branch feat/content-pass.
