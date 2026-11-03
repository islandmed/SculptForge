# SculptForge for studios

SculptForge is built to drop into animation, VFX and game studios that run their own pipelines: native on
Linux, scriptable, standards-based, with no licence server and nothing that phones home. This chapter is for
pipeline TDs and IT: the VFX Reference Platform builds, Rez deployment, shared brush libraries, environment
variables, headless use and licensing.

## VFX Reference Platform builds

SculptForge tracks the [VFX Reference Platform](https://vfxplatform.com). Every commit is built and tested in
the platform's reference environments:

| | CY2026 | CY2027 | SculptForge |
|---|---|---|---|
| Linux compiler | GCC 14.2 | GCC 14.2 | Built and tested with `gcc-toolset-14` |
| glibc | 2.28 (EL8) | 2.34 (EL9) | Rocky Linux 8 and 9 builds; CI checks that no newer glibc symbols are used |
| C++ | C++20 | C++20 | C++20 |
| Python | 3.13 | 3.13 | The `sculptforge` module is built and tested against 3.13 |
| Windows | VS 2022 17.6+ | VS build tools 14.44+ | Visual Studio 2022 |
| macOS minimum | 14.0 | 14.0 | Universal binary with deployment target 11.0 |
| FBX | 2020.x | 2020.x | Writes FBX 7.4 binary, which FBX SDK 2020 reads |

SculptForge doesn't depend on Qt, OpenEXR, Boost, TBB or other platform libraries, so it can't conflict with
the versions your other tools load. Its few dependencies (SDL3, Dear ImGui, GLM, ufbx) are linked into the
binaries.

Each release includes `SculptForge-<version>-linux-<arch>-vfx2026.tar.gz` (Rocky 8, glibc 2.28) and
`...-vfx2027.tar.gz` (Rocky 9, glibc 2.34), for x86_64 and ARM64, with the Python module under
`lib/sculptforge/python`.

## Deploy with Rez

`packaging/rez/package.py` in the source tree builds and installs a Rez package:

```sh
cd packaging/rez
SCULPTFORGE_PYTHON=$(which python3.13) rez-build --install     # or --release for your release repository
rez-env sculptforge -- sculptforge                              # the app
rez-env sculptforge -- sculptforge-cli --list                   # every pipeline command
rez-env sculptforge python-3.13 -- python -c "import sculptforge"
```

The package puts `bin/` on `PATH` and the Python module on `PYTHONPATH`, and sets `SCULPTFORGE_ROOT`. The
Python module is built for the first `python3` found unless `SCULPTFORGE_PYTHON` names an interpreter.

## Studio brush libraries

Each artist's own brushes live in their user brush library, one `.sfbrush` file per brush (see
[Brush Lab and sharing brushes](brush-lab.md)). To give a whole team the same brushes, put `.sfbrush` files on
a share and list it in `SCULPTFORGE_BRUSH_PATH`:

```sh
export SCULPTFORGE_BRUSH_PATH=/studio/brushes:/show/troll/brushes    # ';' between folders on Windows
sculptforge-cli -c 'brush_library'      # every brush, where it comes from and its file, plus the folders
```

- The app, `sculptforge-cli` and the Python module load these folders at start-up, **read-only**: SculptForge
  never writes into them, so the share can be mounted read-only.
- Studio brushes show a `studio` tag in the Custom brushes list.
- When brushes share a name, the artist's own wins over a studio one, which wins over a built-in. Among studio
  folders, the first one listed wins.
- Refining a studio brush in Brush Lab saves the artist's own version (tagged `yours`) without touching the
  share. Deleting that version brings the studio brush back.
- To publish a brush, copy its `.sfbrush` file into the share: right-click it in the Custom brushes list >
  **Save .sfbrush...**, or take it from an artist's user library.

With Rez, set the share once for everyone: when `SCULPTFORGE_STUDIO_BRUSHES` is set as the package resolves
(in a site package, your rezconfig or the launcher), it is appended to `SCULPTFORGE_BRUSH_PATH`.

```sh
export SCULPTFORGE_STUDIO_BRUSHES=/studio/brushes
rez-env sculptforge -- sculptforge
```

## Environment variables

| Variable | Effect |
|---|---|
| `SF_AI` | `off`: every AI feature off and locked (app, `sculptforge-cli`, MCP, Python); `on`: on whatever was saved. `--no-ai` does the same as `off` for one start |
| `SCULPTFORGE_BRUSH_PATH` | Read-only studio brush folders (`:`-separated, `;` on Windows) |
| `SCULPTFORGE_STUDIO_BRUSHES` | With the Rez package: appended to `SCULPTFORGE_BRUSH_PATH` |
| `SCULPTFORGE_BRUSH_GALLERY` | Use another community brush gallery index (a URL or a folder), for example an in-house mirror |
| `HF_TOKEN` (or `HUGGING_FACE_HUB_TOKEN`) | Hugging Face token for the free image-to-3D models |
| `FAL_KEY`, `MESHY_API_KEY` | Keys for the paid generation services |
| `ANTHROPIC_API_KEY`, `OPENAI_API_KEY` | Keys for Brush Lab providers |
| `XDG_DATA_HOME` (Linux), `APPDATA` (Windows) | Move the settings folder (see [where settings live](troubleshooting.md#where-settings-live)) |

Keys from the environment are used when none is saved in the app, so a launcher can supply them without
writing them to each artist's settings.

## Headless and farm use

- `sculptforge-cli` runs every editing operation without a display: imports, remeshing, decimation, LOD
  chains, FBX, USD and glTF export, splat conversion and AI generation. See
  [Scripting and automation](scripting.md#sculptforge-cli).
- `sculptforge --render out.png --turntable 8` renders review images and turntables offscreen. It needs an
  OpenGL 3.3 context; on a headless Linux node, run it under Xvfb.
- `sculptforge-cli --mcp` exposes the same operations to AI agents through the Model Context Protocol.
- `import sculptforge` gives Python the same commands plus NumPy arrays of the mesh, for your own tools.

A typical farm job turns incoming scans into game-ready assets:

```sh
sculptforge-cli --set in=/ingest/scan_042.ply --set usd=/assets/scan_042.usda --set glb=/assets/scan_042.glb -c '
  open path=$in
  splat_transform rotate_x=180 fit=true
  splat_clean
  splat_to_mesh detail=320 preset=game color=texture
  export_usd path=$usd
  export_lods path=$glb levels=4 ratio=0.5
'
```

A `--set` variable replaces a whole value, so pass complete paths (with their extensions) as variables.

## Formats in a pipeline

| Format | Read | Write | Notes |
|---|---|---|---|
| OpenUSD (`.usda`, `.usdz`) | Yes | Yes | Meshes, xform ops, Z-up stages, UsdSkel blend shapes, ParticleField Gaussian splats, textures; checked with OpenUSD's validators |
| FBX | Yes (any version) | Yes (7.4 binary) | LOD groups, blend shapes, centimetre units, textures; checked with ufbx's strict mode |
| glTF / GLB | Yes | Yes | Morph targets, vertex colours, `KHR_gaussian_splatting`, embedded textures; checked with the Khronos validator |
| OBJ | Yes | Yes | Objects as Parts, groups as face groups, vertex colours, textures through `.mtl` |
| PLY | Yes | Yes | Meshes with vertex colours, Gaussian splats |
| STL | Yes (binary and ASCII) | Yes (binary) | |
| Send to ZBrush (GoZ-compatible exchange) | Yes | Yes | Face Groups, mask, vertex paint, UVs |
| SculptForge project (`.sfproj`) | Yes | Yes | Everything; a documented ZIP of JSON and binary arrays |

Binary `.usdc` files and USD references or payloads aren't read yet; convert with `usdcat in.usd -o out.usda`.
The project format is described in `docs/project-format.md` in the source tree, so pipeline tools can read it
without SculptForge.

## Licensing

SculptForge is free software under the GNU General Public License, version 3 or later. In practice:

- **Using it is unrestricted**, commercially or otherwise, on any number of machines. There are no seats,
  licence servers or accounts.
- **Your artwork belongs to you.** The GPL covers the program, not the models you make with it.
- **You can modify it in-house.** The GPL only requires you to share source if you *distribute* the modified
  program outside your organisation. Internal use, including across your own facilities and farm, is generally
  not considered distribution. (This is the common reading of the GPL, not legal advice; check with your
  counsel.)
- SculptForge has no telemetry and no servers of its own. AI features are optional and talk only to the
  service each artist chooses, or to your own ComfyUI server.

## Studio membership

SculptForge is free for any number of artists, and nothing in the app is ever paywalled. A Studio membership
($300 a month; **Founding Studio:** the first 10 studios lock in $150 a month for as long as they stay
subscribed) covers your whole company, with no seat count:

- **Insider for every artist:** the monthly pack, daily drops (a brush, alpha or VDM almost every day), the
  feature vote and the monthly dev Q&A. Insiders will get each release about two weeks early, starting with
  SculptForge 1.0.
- **A custom studio build** with your presets, keymaps, packs, shelves and pipeline defaults baked in.
- **Custom content:** brushes, alphas, VDMs, macros and pipeline presets made for your team.
- **Guided development:** your requests go to the front of the roadmap, with a monthly call or async review
  with the developer.
- **Priority triage:** reviewed within 2 business days (no fix-time guarantee).
- A **private team channel** on Discord, and your **logo** on the website and in the README.

Every release stays free for everyone; bug-fix and security releases go out to everyone the same day. Larger
custom features are quoted separately. Join at [sculptforge.app/studios.html](https://sculptforge.app/studios.html#membership).

## Studio onboarding

After checkout, email hello@sculptforge.app from a studio address with the GitHub usernames that should get
the private repository (as many artists as you like) and who should join the team channel.

### Custom content

Studios can ask for their own content, made to order:

- **Brushes** tuned to your style.
- **Alphas and VDMs** made from your own references or scans (only material you own).
- **Macros** and one-click workflows.
- **Pipeline presets:** export settings, naming, scale, axis, texture packing and ZBrush Bridge setup.
- **Keymaps and layouts.**

All of it is baked into your custom studio build. Up to 5 custom items a month (brushes, alphas, macros or
presets); larger features are quoted separately. Your assets stay private to your studio and are never used
for anything else.

### Requesting a custom build

Send, in one email or a message in your team channel:

- **What to bake in:** your exported keymap and layout (**Edit > Preferences > Interface**), brush presets and packs
  (`.sfbrush`, `.sfpack`), shelf setup, and the pipeline defaults you want (export format and preset, units
  and scale, up axis, naming pattern, texture packing, ZBrush Bridge folder).
- **References for custom content:** images or scans you own, with a line on what each item should do.
- **Where it runs:** platforms and, for Linux, the distribution or VFX Reference Platform year.

Files are treated as data only: scripts and executables you send are never run. Your build is published as a
release in the private repository, built by the same pipeline and tests as public builds, with its source
(it is GPL like every SculptForge build). Each new release brings an updated studio build.

### Guided development

Tell us what your team needs most, in the team channel or on the monthly call (or an async review, if a call
doesn't suit your time zone). Studio requests go to the front of the roadmap: each month we agree what comes
next, and you see it in preview builds about a week before the release. Requests that are bigger than a
normal release's work are quoted separately before anything starts.

## Not yet

Planned pipeline features, roughly in order: OpenColorIO viewport display, Alembic export, fuller USD
authoring (layers, variants, references, `.usdc`), UVs and texture baking, multiresolution sculpting. Pipeline
requests are welcome as issues on the project's GitHub page.

## See also

- [Scripting and automation](scripting.md)
- [Exporting, Send to Unreal and ZBrush](exporting.md)
- [Brush Lab and sharing brushes](brush-lab.md)
- [Image and text to 3D](generate-3d.md): running models on your own ComfyUI server
- [Troubleshooting](troubleshooting.md)
