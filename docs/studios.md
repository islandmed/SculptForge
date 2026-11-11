# SculptForge for studios and pipelines

SculptForge is built to drop into animation, VFX and game studios that run their own pipelines:
Linux-first, scriptable, standards-based, with no licence server and nothing to phone home.

## VFX Reference Platform

SculptForge tracks the [VFX Reference Platform](https://vfxplatform.com). CI builds and tests every commit
in the platform's reference environments:

| | CY2026 (current) | CY2027 | SculptForge |
|---|---|---|---|
| Linux compiler | GCC 14.2 | GCC 14.2 | Built and tested with `gcc-toolset-14` |
| glibc | 2.28 (EL8) | 2.34 (EL9) | Rocky Linux 8 and 9 builds; CI verifies no newer glibc symbols are used |
| C++ | C++20 | C++20 | C++20 |
| Python | 3.13 | 3.13 | `import sculptforge` module built and tested against 3.13 |
| Windows | VS 2022 17.6+ | VS build tools 14.44+ | Visual Studio 2022 |
| macOS minimum | 14.0 | 14.0 | Universal binary, deployment target 11.0 (runs on 14.0+) |
| FBX | 2020.x | 2020.x | Writes FBX 7.4 binary, which FBX SDK 2020 reads |

SculptForge doesn't depend on Qt, OpenEXR, Boost, TBB or other platform libraries, so it can't conflict
with the versions your other tools load. Its few dependencies (SDL3, Dear ImGui, GLM, ufbx) are linked
statically into the binaries.

Each release includes `SculptForge-<version>-linux-<arch>-vfx2026.tar.gz` (Rocky 8, glibc 2.28) and
`…-vfx2027.tar.gz` (Rocky 9, glibc 2.34), with the Python module under `lib/sculptforge/python`.

## Deploying with Rez

`packaging/rez/package.py` builds and installs a Rez package:

```sh
cd packaging/rez
SCULPTFORGE_PYTHON=$(which python3.13) rez-build --install     # or --release for your release repository
rez-env sculptforge -- sculptforge                              # the app
rez-env sculptforge -- sculptforge-cli --list                   # every pipeline command
rez-env sculptforge python-3.13 -- python -c "import sculptforge"
```

The package puts `bin/` on `PATH`, puts the Python module on `PYTHONPATH`, and sets `SCULPTFORGE_ROOT`.
If `SCULPTFORGE_STUDIO_BRUSHES` is set when the package resolves (in a site package, your rezconfig or the
launcher), it is appended to `SCULPTFORGE_BRUSH_PATH` (see below).

## Studio brush libraries

Artists' own brushes live in their user library, one `.sfbrush` file per brush: Brush Lab saves every brush
made or refined there automatically, and brushes installed from a file, a pasted brush code
(`sfbrush1:...`) or the community gallery go there too.

| OS | User library |
|---|---|
| Linux | `$XDG_DATA_HOME/SculptForge/SculptForge/brushes` (`~/.local/share/...` by default) |
| macOS | `~/Library/Application Support/SculptForge/SculptForge/brushes` |
| Windows | `%APPDATA%\SculptForge\SculptForge\brushes` |

To share a set of brushes across a team, put them on a share and list it in `SCULPTFORGE_BRUSH_PATH`
(`:`-separated, `;` on Windows). The app, `sculptforge-cli` and the Python module load these folders at start-up,
read-only: SculptForge never writes into them, so the share can be mounted read-only. Each brush shows where it
comes from (a `studio` tag in the Custom brushes list, `brush_library` from scripts).

```sh
export SCULPTFORGE_BRUSH_PATH=/studio/brushes:/show/troll/brushes
sculptforge-cli -c 'brush_library'        # every brush, its source and file, plus the library folders
```

When brushes share a name, the artist's own wins over a studio one, which wins over a built-in; among studio
folders, the first one listed wins. Refining a studio brush in Brush Lab saves the artist's version (tagged
`yours`) without touching the share; deleting it brings the studio version back. Files that aren't valid
brushes are skipped with a message in the log.

Publishing to the share is copying `.sfbrush` files into it: save one from the app (right-click a brush >
Save .sfbrush...), take it from the user library, or turn a brush code into a file with
`sculptforge-cli -c 'brush_install code=sfbrush1:...'` on a machine whose user library you then copy from.

With Rez, set the share once for everyone:

```sh
export SCULPTFORGE_STUDIO_BRUSHES=/studio/brushes     # e.g. in the studio's launcher environment
rez-env sculptforge -- sculptforge                    # SCULPTFORGE_BRUSH_PATH now includes /studio/brushes
```

## Python API

```python
import sculptforge as sf

doc = sf.Document()
sf.run(doc, "open", path="/show/assets/troll/troll_base.fbx")
sf.run(doc, "remesh", resolution=320)
sf.run(doc, "export_usd", path="/show/assets/troll/troll_sculpt.usda")

verts, tris = doc.vertices, doc.triangles     # NumPy (N, 3) float32 and (M, 3) uint32
doc.set_mesh(my_vertices, my_triangles)      # bring meshes in from your own tools
for name, weight in doc.layers:              # sculpt layers / blend shapes
    print(name, weight, doc.layer_delta(0).shape)

sf.run_script(doc, "decimate ratio=0.25; export_lods path=/out/troll.glb levels=4")
print([c["name"] for c in sf.commands()])     # everything the app can do
```

Failures raise `sculptforge.CommandError`. Long operations release the GIL.

## Headless and farm use

- `sculptforge-cli` runs every editor operation without a display: imports, remeshing, decimation, LODs,
  FBX/USD/glTF export, splat conversion, AI generation.
- `sculptforge --render out.png --turntable 8` renders review images and turntables offscreen.
- `sculptforge-cli --mcp` exposes the same operations to AI agents through the Model Context Protocol.

## Formats in your pipeline

| Format | Read | Write | Notes |
|---|---|---|---|
| OpenUSD (`.usda`, `.usdz`) | ✓ | ✓ | Meshes, xform ops, Z-up stages, UsdSkel blend shapes, ParticleField Gaussian splats; checked with OpenUSD 26.08 `usdchecker` |
| FBX | ✓ (any version, via ufbx) | ✓ (7.4 binary) | LOD groups, blend shapes, cm units |
| glTF / GLB | ✓ | ✓ | Morph targets, `KHR_gaussian_splatting`; checked with the Khronos validator |
| OBJ, PLY | ✓ | ✓ (OBJ) | PLY meshes, point clouds and splats |

Binary `.usdc` and USD references/payloads aren't read yet; convert with `usdcat in.usd -o out.usda`.

## Licensing for studios

SculptForge is GPL-3.0-or-later. In practice:

- **Using it is unrestricted**, commercially or otherwise, on any number of machines. There are no seats,
  licence servers or accounts.
- **Your artwork belongs to you.** The GPL covers the program, not the models you make with it.
- **You can modify it in-house.** Fork it and wire it into your asset system. The GPL only requires you to
  share source if you *distribute* the modified program outside your organisation. Internal use, including
  across your own facilities and farm, isn't distribution. (This is the common reading of the GPL, not
  legal advice; check with your counsel.)
- Contributions back upstream are welcome, but never required for internal use.

## Studio builds and offline deployment

A Studio membership (a flat $300 a month for the whole studio, any number of artists; Founding Studio a flat
$150 for the first 5 studios) includes a custom build with your own assets and defaults baked in, a private release channel,
studio mode (no outside connections), a pinned version with security backports to the current release and one LTS
line, and an offline signed installer. The build is GPL-3.0 like every SculptForge build; your assets, settings
and network stay private. See the user guide's
[Your studio build](guide/studios.md#your-studio-build) and [Offline deployment](guide/studios.md#offline-deployment).

## The studio kit: your build, on your machines

`packaging/studio-kit/` makes that build on the studio's side, so we never need your files. Copy the folder into a
repository of your own and fill an overlay: `studio.json` (id, name, update channel, allow list), `brushes/`,
`alphas/`, `litspheres/`, `materials/`, `vdms/`, `keymaps/`, `piemenus/`, `macros/` (your scripts),
`plugins/` (sandboxed), `preferences/ui.json` and `branding/logo.png`. Then, on your CI or an air-gapped box:

```sh
python3 build.py check overlay                                   # mistakes first, nothing written
python3 build.py pin --release releases/0.12.6                   # signature + checksums checked, pin.json written
python3 build.py build --overlay overlay --release releases/0.12.6 --pin pin.json --out dist
python3 build.py feed --version 0.12.6 --download https://sw.acme.internal/sculptforge/   # latest.json for your channel
```

- **No calls to us.** The kit is one Python file (standard library only) and `openssl`. You download the release
  files once (`SHA256SUMS.txt`, its `.sig`, the SBOM and the archives) to any folder or mirror; `pin` checks the
  Ed25519 signature with the key shipped in the kit (the one the app checks its updates with) and every checksum.
- **Our binaries, unchanged.** The kit adds your folder at `share/sculptforge/studio/` inside the Linux `.tar.gz`
  (including the VFX Reference Platform builds) and the Windows `.zip`; the program finds it beside itself and starts
  in studio mode. Nothing is recompiled, so the release signatures stay valid. On macOS the folder goes beside
  SculptForge.app. `build.py source` compiles from a tag instead, with the profile built in.
- **Reproducible, with an SBOM.** Same overlay + release + pin = byte-identical outputs (same Python version):
  fixed entry order and times, no owners. Each output has a CycloneDX SBOM (our release's components, the release
  archive's hash, every one of your files with its hash) and `SHA256SUMS.txt` covers everything.
- **Pinned, with an update flow.** `pin.json` names the release and its hashes; a build refuses any other file. A
  patch release is: download, `pin` (review the diff), `build`, deploy, `feed`.
- **Your files stay data.** Brushes, keymaps, macros and plugins become one pack, checked at start-up as any pack
  (docs/pack-format.md). Macros run only when an artist runs them; plugins only see the mesh they are given.

CI runs the whole kit against a throwaway signing key (`ctest -R studio_kit`, tests/studio_kit_test.py): tampered
archives, signatures and pins are refused, two builds are byte-identical, and the real app unpacked from a rebuilt
archive starts with the overlay's brush, keymap, macro and branding.

## Bug report packs

**Help > Create bug report pack...** writes one `.zip` on the artist's computer for a bug report, without the model.
Before saving, a preview lists every file and shows its text; any of them can be left out. The text is redacted by
rules, not understood: **read `log.txt` and `actions.txt` before posting a pack publicly**, as a name typed into free
text that SculptForge doesn't know can remain.

| File | What's in it |
|---|---|
| `system.txt` | App version, studio build or not, OS, CPU, memory, GPU, driver, UI scale |
| `settings.json` | The preferences that differ from a fresh install; text values become `<text>` |
| `log.txt` | The last 200 log lines, with paths, file names, URLs, e-mail addresses, quoted text and known names replaced |
| `actions.txt` | The last 200 commands and strokes: command names with only their number and on/off values, brush types |
| `mesh.json` | Per Part: vertex and triangle counts, pieces, genus, holes, open and non-manifold edges, subdivision levels, layers, which attributes it has; the scene's overall size |
| `proxy.ply` | A generated stand-in: spheres, tori and sheets with those counts (up to 400,000 vertices), inside the scene's size |

### What never leaves the building

- **No model:** no vertex position, normal, UV, colour or mask value goes in. The stand-in is built from the counts
  alone; Part names, positions and transforms are left out.
- **No textures or images**, no project files, no attachments.
- **Names and places replaced:** file names and paths, URLs and host names, e-mail addresses and quoted text, and
  the names of the Parts, Part folders, layers, selection sets, custom brushes, project, packs, macros and studio,
  and the user and computer names, wherever they appear. Other words in log lines stay as they are.
- **No upload.** The pack is written where the artist chooses; there is no send button and no telemetry.

tests/test_repro_pack.cpp checks that no source vertex (as bytes or as text), no texture byte and none of those
names ends up in a pack, including a fuzz test over random multi-Part scenes with textures, colours, layers, packs
and moved Parts.

## Roadmap items for pipelines

Planned, roughly in this order: OpenColorIO viewport display, Alembic export, fuller USD authoring (layers,
variants, references, `.usdc`), multiresolution sculpting and masking. Pipeline requests are welcome as issues.
