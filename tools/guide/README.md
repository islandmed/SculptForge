# User guide tools

The user guide lives in `docs/guide` (Markdown, one file per chapter, `index.md` lists them in order). The
same files feed three places:

- **The app**: `cmake/EmbedGuide.cmake` embeds `docs/guide/*.md` and `docs/guide/images/*` in the binary;
  `src/app/UserGuide.cpp` shows them (Help > User Guide, Shift+F1). While writing, set
  `SCULPTFORGE_GUIDE_DIR=docs/guide` to read the files from disk without rebuilding.
- **The website**: `python3 tools/guide/build_site.py` writes `website/guide/*.html` (and copies the images).
- **Tests**: `ctest` runs `guide_site` (every link, anchor and image resolves, only characters the app's font
  has) and `ui_guide_shortcuts` (the keyboard table matches the app); `tests/test_guide.cpp` covers the
  parser and `tests/ui/user_guide.sfui` the reader.

Write only the Markdown subset described at the top of `src/core/Guide.h` (the reader renders nothing else),
in ASCII or Latin-1 characters.

## Regenerating

```sh
# Keyboard table in shortcuts.md, from the app's action registry
python3 tools/guide/update_shortcuts.py --app build/sculptforge

# Screenshots: drive the real UI, then crop, label and compress them into docs/guide/images (needs Pillow)
build/sculptforge --ui-test tools/guide/shots.sfui --ui-out build/guide-shots
python3 tools/guide/make_images.py build/guide-shots --preview build/guide-shots/sheet.png

# Website pages
python3 tools/guide/build_site.py --check
```

Crop rectangles for each image are in `make_images.py`; if the UI layout changes, look at the preview sheet and
adjust them. Re-run cmake after adding or removing guide files so the app embeds them.

## Menu paths

The guide names the default **SculptForge layout**'s menus: **Edit > Preferences > Input**, **Sculpt > Stroke**,
**Render > Materials**. **Tool > ...** paths stay, since the Tool palette is in the right tray in both layouts.
`python3 tools/guide/menu_paths.py` rewrites ZBrush-style palette paths (**Preferences > Input**) in every
chapter; ctest `guide_menu_paths` fails while any are left.

## Feature panels

Bake, fill layers, PBR, the UV Editor, Performance and other feature images come from the feature UI tests, run in
the default look (tests written for the Classic look run from a converted copy):

```sh
python3 tools/guide/feature_shots.py --app build/sculptforge --out build/guide-features
python3 tools/guide/make_images.py build/guide-shots --features build/guide-features
```
