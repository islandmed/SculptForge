# assets

Files that ship inside SculptForge or its packages. Most are compiled into the program (the list is in the top-level
`CMakeLists.txt`, through `cmake/EmbedFile.cmake`), so what is here is what every user gets, and its size is a cost
to everyone.

| Folder | What | Made by |
|---|---|---|
| `starter/` | The starter meshes (File > New from Starter Mesh): small base cages as zlib-compressed `.sfproj.z` projects, with the subdivision levels added when they open. Provenance of each: [docs/starter-meshes.md](../docs/starter-meshes.md) | `tools/starter/build.sh` |
| `insert-sets/` | The built-in "Creature bones" insert set, one multi-object OBJ, compressed. Every piece is designed from simple distance fields, with no outside meshes or scans | `tools/creature-bones/build.sh` |
| `materials/` | The built-in Material Recipes (`.sfmaterial`: a stack of Fill layers saved as small JSON): worn metal, painted edges, dirt and dust, rusted iron | by hand, in the app |
| `hdri/` | Three small CC0 environment maps for the PBR viewport (512 x 256) | credits in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) |
| `face/` | The template head for Generate Facial Shapes. [Its README](face/README.md) has the source and licence | `tools/face/build_ict_template.py` |
| `fonts/` | The interface fonts and the icon font, with their licences. CJK fonts are cut down to the characters the translations use | `tools/i18n/subset_cjk_font.py` |
| `phone/` | `capture.html`, the page the desktop serves to a phone (served only while the Capture from phone window is open; [src/link](../src/link/README.md)) | by hand |
| `branding/` | The app icon in every size (`.png`, `.ico`, `.icns`), the splash images, the bust and the social icons. Which mark goes where: [docs/brand.md](../docs/brand.md) | `tools/brand/make_icons.py`: edit the generator, not the outputs |
| `demo/` | Models and sketches the scripted demos and Generate 3D samples use. [CREDITS.md](demo/CREDITS.md) has the source and licence of each | |

## Rules

- **Provenance first.** Every file must be our own work, or from a source whose licence allows shipping it, recorded
  with its URL, licence and date (here, in a `CREDITS` or `README`, and in
  [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)). Never anything from another sculpting application: no
  brushes, alphas, icons, art, documents or models. Nothing scraped.
- **Own names only.** A file name, a brush or a material name must not use another company's marks
  ([docs/legal/trademarks.md](../docs/legal/trademarks.md); `tools/check_marks.py` checks the repository).
- **Mind the size.** Everything embedded adds to the download and to start-up memory. Compress, downsample, and cut to
  what is used; a set that grows past its budget fails a test (`starter_meshes_set_and_budget`).
- **A new image or video:** run `tools/check_marks_media.py` and commit `tools/marks_media_cache.json` with it.
- A generated file is changed by changing its generator and running it, so the next person can reproduce it.
