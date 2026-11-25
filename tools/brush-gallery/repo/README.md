# SculptForge community brushes

Free sculpting brushes for [SculptForge](https://github.com/islandmed/SculptForge), the free, open-source
a free, open-source sculpting app, shared by the people who made them. Every brush here is public domain
([CC0](LICENSE)): use it in personal and commercial work, change it, bundle it with your own tools.

**Install brushes from inside SculptForge:** *View > Community brushes*, pick one, click **Install**. You can
also download any `.sfbrush` file from [`brushes/`](brushes) and drag it onto the SculptForge window.
Browse the gallery with previews at <https://sculptforge.app/brushes.html>.

## What a brush is

A SculptForge brush is a small JSON *recipe* (`.sfbrush`): how the surface moves, the shape of the tip, a
surface pattern, stroke spacing and masking. It is data only, never code, so installing a brush from a
stranger is as safe as opening a text file. You design brushes in SculptForge's **Brush Lab**, by hand or by
describing them to the AI brush creator ("dry cracked mud", "a palette knife that drags clay").

```json
{
  "name": "Snake Scales",
  "description": "Tight rows of small, smooth scales for snakes, lizards and fish.",
  "direction": "normal", "depth": 0.35, "pattern": "scales", "patternScale": 0.18, "patternAmount": 1,
  "hardness": 0.55, "spacing": 0.2,
  "author": "SculptForge",
  "tags": ["creature", "scales", "texture"],
  "format": "sfbrush", "version": 1
}
```

Parameters you leave out take neutral defaults. The full list, with ranges, is in
[SculptForge's brush guide](https://github.com/islandmed/SculptForge/blob/main/docs/brushes.md).

## Share your brush

1. **Save it.** In Brush Lab, select your brush and click *Save .sfbrush*.
2. **Add the gallery fields.** Open the file in a text editor and add `"author"` (your name or handle) and
   `"tags"` (1-6 lowercase words, e.g. `["skin", "creature", "texture"]`). Give it a one-sentence
   `"description"` that says what it is for.
3. **Name the file after the brush**, lowercase with dashes: *Cracked Earth* → `brushes/cracked-earth.sfbrush`.
4. **Render the preview** with SculptForge (a sphere with three strokes, so every preview matches):

   ```sh
   git clone https://github.com/islandmed/SculptForge   # once, for the scripts
   python3 SculptForge/tools/brush-gallery/render_previews.py brushes/cracked-earth.sfbrush --out previews
   ```

   It needs the SculptForge app on your `PATH` (or `--app /path/to/sculptforge`). This writes
   `previews/cracked-earth.png` (512 x 384).
5. **Open a pull request** adding just those two files. A check runs SculptForge on your brush and tells you
   if anything needs fixing. After it is merged, the gallery index updates itself and the brush shows up in
   everyone's *Community brushes* window.

Can't render the preview? Open the pull request anyway and say so; a maintainer will render it for you.

## Rules

- **Data only.** A brush is one `.sfbrush` file with recipe parameters and the gallery fields `name`,
  `description`, `author`, `tags`, `format`, `version`. No other keys, no scripts, no links, no images
  inside it. Values must be in range (the check tells you if SculptForge would clamp anything).
- **One brush per file, one name per brush.** Names are unique in the gallery (2-4 words an artist would
  recognise). The file name is the name in lowercase with dashes.
- **Your own work, shared as CC0.** By opening a pull request you confirm you made the brush and dedicate it
  to the public domain under [CC0 1.0](LICENSE). Don't submit brushes copied from paid packs.
- **Useful and distinct.** Improvements to an existing brush are welcome as an edit to its file; near-copies
  of a brush that is already here will be declined.
- **Be kind.** Names, descriptions and tags follow SculptForge's
  [code of conduct](https://github.com/islandmed/SculptForge/blob/main/CODE_OF_CONDUCT.md).

Pull requests may only change files in `brushes/` and `previews/`; `index.json` is generated.

## For maintainers

`index.json` is what the app reads. It is rebuilt on every push to `main` by
[`publish.yml`](.github/workflows/publish.yml) using `build_index.py` from the SculptForge repository, which
round-trips every brush through `sculptforge-cli`, checks the preview and records its SHA-256 (the app
refuses a download whose hash does not match). To rebuild by hand:

```sh
python3 SculptForge/tools/brush-gallery/build_index.py .              # validate + write index.json
python3 SculptForge/tools/brush-gallery/render_previews.py brushes --out previews --changed
```

The index format is documented in
[docs/brushes.md](https://github.com/islandmed/SculptForge/blob/main/docs/brushes.md#community-gallery).
