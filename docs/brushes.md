# Sharing brushes

SculptForge's custom brushes are *recipes*: a few numbers and names that describe how a brush moves the
surface. A recipe is data, never code, so brushes are safe to swap with anyone. This guide covers the ways to
share them: files, brush codes, studio folders and the community gallery, plus the file and index formats.

## Brush files (`.sfbrush`)

In **Brush Lab**, select a brush and click *Save .sfbrush...*; *Load .sfbrush...* (or dragging the file onto the
window) loads one. From scripts and AI agents use the same commands:

```sh
sculptforge-cli -c 'brush_create name="Bark" depth=0.4 pattern=ridges patternAmount=1; brush_save path=bark.sfbrush'
sculptforge-cli -c 'brush_load path=bark.sfbrush; brush_list'
```

A `.sfbrush` file is a JSON object:

```json
{
  "name": "Snake Scales",
  "description": "Tight rows of small, smooth scales for snakes, lizards and fish.",
  "direction": "normal", "depth": 0.35, "plane": "none", "planeAmount": 0.5, "pinch": 0, "drag": 0, "smooth": 0,
  "hardness": 0.55, "tipSharpness": 0, "pattern": "scales", "patternScale": 0.18, "patternAmount": 1,
  "spacing": 0.2, "jitter": 0, "pressureCurve": 1, "frontFacesOnly": false, "cavityMask": 0,
  "format": "sfbrush", "version": 1
}
```

| Key | Range | What it does |
|---|---|---|
| `direction` | `normal` `inflate` `view` `stroke` | Which way the surface moves |
| `depth` | -1 … 1 | Adds (positive) or carves (negative) |
| `plane`, `planeAmount` | `none` `clay` `flatten` `fill` `scrape`; 0 … 1 | Planar behaviour and its strength |
| `pinch` | -1 … 1 | Pulls the surface toward the stroke for crisp creases |
| `drag` | 0 … 1 | Smears the surface along the stroke |
| `smooth` | 0 … 1 | Relaxes the surface after each dab |
| `hardness` | 0 … 1 | Soft airbrush edge to hard stamp |
| `tipSharpness` | 0 … 1 | Round tip to needle point |
| `pattern`, `patternScale`, `patternAmount` | `none` `noise` `scales` `ridges` `pores` `cracks`; 0.03 … 2; 0 … 1 | Surface texture, its size relative to the brush, and strength |
| `spacing` | 0.02 … 2 | Dab spacing relative to brush size |
| `jitter` | 0 … 1 | Random scatter of dabs |
| `pressureCurve` | 0.3 … 3 | Tablet response |
| `frontFacesOnly` | `true` / `false` | Protects the far side of thin parts |
| `cavityMask` | -1 … 1 | Only cavities (positive) or only ridges (negative) |

Missing keys take neutral defaults, out-of-range values are clamped, and unknown keys are ignored when loading
(the gallery is stricter, see below). `sculptforge-cli --list` shows the same parameters under `brush_create`.

## Brush codes

A brush also fits in a single line of text you can paste into a chat, an issue or a forum post:
`sfbrush1:` followed by the compact JSON recipe in base64url (no padding), at most 8 KB. Pasting a code into
SculptForge decodes it with the same validation and clamping as a file. Codes are handled by the brush
library (`brush_install code=...`).

## Your library and studio folders

Installed brushes live in your user library, `<app data>/brushes` (next to `ai.json`: `~/.local/share/SculptForge/SculptForge`
on Linux, `~/Library/Application Support/SculptForge/SculptForge` on macOS, `%APPDATA%\SculptForge\SculptForge` on
Windows). `brush_install path=FILE | code=TEXT | json=TEXT` writes the brush there, loads it and makes it active;
installing a brush with the same name again updates it.

Studios can share brushes read-only from network folders: every directory in `SCULPTFORGE_BRUSH_PATH`
(separated by `:`, or `;` on Windows) is loaded at start-up. Each brush records where it came from (built-in,
user, studio, project or gallery). Projects (`.sfproj`) also embed the brushes they use, so a scene opens with
its brushes on any machine.

## Community gallery

The community gallery, [islandmed/sculptforge-brushes](https://github.com/islandmed/sculptforge-brushes), is a
GitHub repository of CC0 brushes with matching preview images. Browse it:

- **In the app:** *View > Community brushes*. Search, filter by tag, click **Install**. The index and previews
  are cached, so the window still opens offline (with a notice); brushes you installed always work.
- **On the web:** <https://sculptforge.app/brushes.html>.
- **From scripts and AI agents:**

  ```sh
  sculptforge-cli -c 'brush_gallery_search tag=skin'
  sculptforge-cli -c 'brush_gallery_install name="Snake Scales"; stroke points="-0.5,0,0 0.5,0.2,0"'
  ```

Every download is checked against the SHA-256 the index lists and refused if it differs; it is then installed
with `brush_install`.

### Using another gallery

The gallery is just an `index.json` next to `brushes/` and `previews/` folders, so a studio can mirror it or run
its own on any web server or shared drive. Point SculptForge at it with, in order of precedence:

1. the environment variable `SCULPTFORGE_BRUSH_GALLERY`, or
2. the *Gallery* field at the bottom of the Community brushes window (saved in `<app data>/brush-gallery.json`),
3. otherwise the official gallery,
   `https://raw.githubusercontent.com/islandmed/sculptforge-brushes/main/index.json`.

The value can be an `https://` or `http://` URL, a `file://` URL or a local folder, pointing at `index.json` or at the
folder that holds it (`/mnt/studio/brushes` means `/mnt/studio/brushes/index.json`). Commands take the same value
as `gallery=`.

### Index format

```json
{
  "format": "sculptforge-brush-gallery",
  "version": 1,
  "brushes": [
    {
      "name": "Snake Scales",
      "description": "Tight rows of small, smooth scales for snakes, lizards and fish.",
      "author": "SculptForge",
      "tags": ["creature", "scales", "texture"],
      "file": "brushes/snake-scales.sfbrush",
      "preview": "previews/snake-scales.png",
      "sha256": "9f0c…",
      "added": "2026-10-21"
    }
  ]
}
```

| Field | Rules |
|---|---|
| `format`, `version` | Must be `sculptforge-brush-gallery` and `1`. A newer version asks the user to update SculptForge. |
| `name` | Unique (case-insensitive), at most 64 characters. |
| `description`, `author` | Shown in the window; at most 500 and 64 characters. |
| `tags` | Lowercase words for the tag filter. |
| `file`, `preview` | Paths relative to the index: letters, digits, `.`, `_`, `-` and `/` only, no `..`. `file` ends in `.sfbrush`, `preview` in `.png` (512 x 384). |
| `sha256` | Lowercase hex SHA-256 of the `.sfbrush` file. |
| `added` | Date the brush joined the gallery, `YYYY-MM-DD`. |

SculptForge skips (and logs) entries that break these rules instead of rejecting the whole index.

### Gallery tools

The gallery's tooling lives in this repository under `tools/brush-gallery/` (Python, standard library only):

| Tool | What it does |
|---|---|
| `render_previews.py` | Renders the preview for each `.sfbrush`: `sculptforge --render` sculpts three S-curves of the brush on a sphere with the `stroke` command and shoots it with the Clay Lit Sphere at a fixed camera. Brushes masked to cavities or ridges get raised forms or grooves to work on first. |
| `build_index.py` | Validates every brush (only recipe keys plus `name`, `description`, `author`, `tags`, `format`, `version`; values in range, checked by round-tripping through `sculptforge-cli`; file named after the brush; a 512 x 384 preview) and writes `index.json`. `--validate-only` for pull requests, `--check` to verify the index is current (a ctest test runs it on `tools/brush-gallery/repo`). |
| `sync_website.py` | Copies the gallery into `website/gallery/` and writes the cards into `website/brushes.html`. |
| `repo/` | The content of the gallery repository: README with the submission rules, CC0 licence, seed brushes, previews, index and its GitHub Actions workflows (`validate.yml` checks pull requests; `publish.yml` renders missing previews and rebuilds `index.json` on `main`). |

### The `stroke` command

Previews use `stroke`, which is useful on its own for scripted detailing and AI agents: it applies a brush along
a path on the active Part as one undoable stroke.

```sh
sculptforge-cli -c 'new_sphere subdivisions=7; brush_load path=bark.sfbrush;
  stroke points="-0.8,0.3,0 -0.2,0.5,0 0.4,0.2,0 0.8,0.4,0" radius=0.1; export_glb path=bark.glb'
```

Each point is projected onto the surface along `view` (default `0,0,-1`, the front view), so a path drawn in the
camera's plane behaves like a screen-space stroke; `curve` (default on) passes a smooth curve through the
points. `brush` picks a library brush or a built-in (`Clay`, `Crease`, `Move`...); `radius`, `strength`,
`pressure`, `invert`, `spacing`, `symmetry` and `seed` work as in the viewport.
