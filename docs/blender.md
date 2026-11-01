# The Blender bridge

SculptForge and the **SculptForge Bridge** add-on for Blender (`addons/blender/sculptforge_bridge`) exchange
meshes the way GoZ apps do with ZBrush: through files in a shared folder, with each side polling the other's
list. User documentation: [docs/guide/blender.md](guide/blender.md).

Code: `src/io/BlenderBridge.h/.cpp` (folder, manifest, send/receive), `src/io/BlenderCommands.cpp`
(`blender_send`, `blender_receive`), `src/app/PanelBlender.cpp` (File > Blender, polling, heartbeat),
`addons/blender/sculptforge_bridge/exchange.py` (the add-on's bpy-free side: folder, manifest, .GoZ, faces) and
`__init__.py` (Blender UI, mesh <-> arrays). Tests: `tests/test_blender_bridge.cpp`,
`tests/blender/test_exchange.py` (numpy, against `sculptforge-cli`), `tests/blender/test_in_blender.py` (a real
Blender or the `bpy` wheel; CI), `tests/ui/blender_bridge.sfui`.

The add-on borrows ideas and the .GoZ conventions from [GoB](https://github.com/JoseConseco/GoB) (GPL), the
Blender GoZ add-on by Daniel Grauer, JoseConseco and contributors; no GoB code is copied.

## Why files and .GoZ

- **No sockets**: nothing to configure, nothing listening, works in sandboxes that allow files, and a crash on one
  side leaves the other unaffected. Polling is a `stat` of one file a second.
- **.GoZ** (docs/goz.md) already carries everything both sides keep per mesh: positions, triangles and quads,
  per-corner UVs, per-vertex vertex paint and mask, per-face face groups. SculptForge reads and writes it, and it is
  simple to write from Python with numpy. A second mesh format was not needed.

## The folder

`$SCULPTFORGE_BLENDER_EXCHANGE`, else `<app data>/blender`, where `<app data>` is SDL's pref path
(`~/.local/share/SculptForge/SculptForge`, honouring `XDG_DATA_HOME`; `%APPDATA%\SculptForge\SculptForge`;
`~/Library/Application Support/SculptForge/SculptForge`). The add-on computes the same path (and lets the user
override it in its preferences).

| Path | Written by | Contents |
|---|---|---|
| `to_sculptforge/manifest.json` + `r<rev>_<k>.GoZ` | the add-on | what Blender sent |
| `to_blender/manifest.json` + `r<rev>_<k>.GoZ` | SculptForge | what SculptForge sent |
| `app.json` | SculptForge, at start | `{"path": executable, "version": ...}`: the AppImage (`$APPIMAGE`), the `.app` bundle, or the binary |
| `alive.json` | SculptForge, every 2 s once `to_sculptforge/` exists | `{"pid": ..., "time": unix seconds}`; removed on exit |

Writers write the .GoZ files first, then the manifest through a temporary file and a rename, so a reader never
sees a half-written list. File names carry the revision, so a send never overwrites a file the other side may
still be reading; after writing the manifest the writer deletes its earlier files (ignoring files still open on
Windows).

## The manifest

```json
{
  "format": "sculptforge-bridge",
  "version": 1,
  "revision": 12,
  "sender": "blender",
  "sender_version": "4.2.3",
  "time": 1761300000.5,
  "objects": [
    {"name": "Suzanne.001", "file": "r12_000.GoZ",
     "matrix_world": [[1, 0, 0, 0.5], [0, 1, 0, 0], [0, 0, 1, 2], [0, 0, 0, 1]]}
  ]
}
```

- `revision` is the previous manifest's plus one. A reader loads a manifest whose revision differs from the one
  it loaded last (not "greater": a deleted folder restarts at 1). It reads the revision once a
  second rather than trusting file times (Windows copies keep them; FAT and network drives store them coarsely).
- `name` is the exact Blender object name (UTF-8); the .GoZ's own name is GoB's ASCII escape of it. In SculptForge
  it is the Part's `gozName` (shared with GoZ; saved in projects), so the next send updates the same object.
- `file` must be a plain file name in the same folder (`..`, `/`, `\` and `:` are refused).
- `matrix_world` (Blender -> SculptForge only): the object's matrix, rows as Blender prints them, Z-up. The .GoZ
  holds the mesh in object space. SculptForge places it with `A * M * A^-1` where `A` maps Blender
  `(x, y, z)` to SculptForge `(x, z, -y)` (metres both). SculptForge -> Blender sends world space without a
  matrix; the add-on applies the inverse of the object's current `matrix_world`.
- `shapes` (SculptForge -> Blender only, optional): a file beside the .GoZ with the object's sculpt layers, which
  the add-on turns into shape keys (a Basis, then one key per layer with its name and weight; the 52 ARKit facial
  shapes keep their names). The .GoZ then holds the base shape (every layer at zero). Layout (little endian):
  `"SFSHAPES"`, u32 version 1, u32 vertex count, u32 shape count, then per shape u32 name length, the UTF-8 name,
  f32 weight, u32 count N, u32[N] vertex indices, f32[3N] offsets in Blender's axes (world space, metres).
  Older add-ons ignore the field (and then get the base shape without the keys).
- Version: readers refuse a manifest with a higher `version` and say which side to update.

## Faces, winding and in-place updates

The add-on writes each Blender polygon as one GoZ face: triangles and quads as they are, n-gons as the fan
`(0, i, i+1)`. For an object whose matrix has a negative determinant (mirrored), every polygon is written reversed,
so the faces point out once SculptForge applies the matrix; nothing on the SculptForge side knows about it.

SculptForge sends the faces back as it received them: imported quads stay paired (`Mesh::quadPartners`) and are
written as the same quads, possibly starting at another corner; triangles stay triangles (`blender_send` writes
quads only for meshes with kept quads, so an n-gon fan is never re-paired into a quad).

On receive the add-on triangulates the object's current mesh the same way (reversed again for a mirrored object)
and compares the face lists, each face rotated to start at its smallest vertex. Equal: only positions and
attributes are written, mapped back to Blender's loops through the triangulation (n-gons, edges and every other
attribute stay). Different: the mesh is rebuilt (`clear_geometry` keeps materials) from the received triangles and
quads, reversed back for a mirrored object.

## Attributes

| Blender | GoZ / SculptForge |
|---|---|
| active UV map (per loop) | UVs per face corner (V flipped in the file, as GoB does) |
| active colour attribute, point or corner domain (`color_srgb`) | vertex paint (sRGB bytes; corner colours are averaged per vertex) |
| `.sculpt_mask` (float, point) | mask (1 = masked). A send without a mask section removes `.sculpt_mask` |
| `.sculpt_face_set` (int, face) | face group = face set - 1 (16-bit, renumbered if out of range) |

## Starting SculptForge

On send, the add-on treats SculptForge as running when `alive.json` is less than 6 seconds old. SculptForge beats
as soon as it starts (once `to_sculptforge/` exists), every 2 s after that, and right after its own Send to
Blender. On the very first send (when it creates `to_sculptforge/`) the add-on waits up to 2.5 s for a beat, and
within a minute of starting SculptForge itself up to 5 s, so a slow start never gets a second copy. Otherwise it starts SculptForge with `--blender` (macOS: `open -n <app> --args --blender`), found from
the add-on preference, `app.json`, `PATH`, then the usual install folders. `--blender` loads the pending send at
start; without it, a send that was already there when SculptForge started is not loaded by itself.

In Blender, automatic receiving starts after the session's first send (so opening another .blend file never pulls
in an old SculptForge send); **Receive** always loads the current one.

## Packaging

`addons/blender/build_addon.py --out DIR [--version X.Y.Z] [--check]` writes `sculptforge_bridge-X.Y.Z.zip` (an
extension: `blender_manifest.toml` at the root) and `sculptforge_bridge-X.Y.Z-legacy.zip` (a `sculptforge_bridge/`
folder for **Add-ons > Install from Disk**), with fixed timestamps. CMake: `--target blender_addon`; ctest
`blender_addon_package` runs the checks. The release workflow attaches both, plus `sculptforge_bridge.zip`
(version-less, for the website's link).

## ZBrush-style keys in Sculpt Mode

`addons/blender/sculptforge_bridge/zbrush_keys.py`: the add-on preference `zbrush_keys` adds items to the add-on
keyconfig's "Sculpt" keymap (`key_table()`) and removes exactly those again (`disable()`, also on unregister).
Add-on keymap items take priority over Blender's own while they exist. Things that differ between versions,
found by running real Blender 4.2.23 and 5.2.3 headless:

- The radial controls' unified size / strength are `tool_settings.unified_paint_settings` in 4.2 and per mode,
  `tool_settings.sculpt.unified_paint_settings`, in 5 (`_unified_path()`).
- Brushes: 4.3+ activates Essentials brush assets (`brush.asset_activate`); 4.2 sets the `builtin_brush.*` tool,
  which needs a 3D View, so without one (scripts) it assigns the brush with that `sculpt_tool`.
- `hasattr(bpy.ops.x, "y")` is always true; check operators with `get_rna_type()` and versions with
  `bpy.app.version`.
- `paint.hide_show`'s `area` items are `'OUTSIDE'` and `'Inside'` (sic): enum settings are matched ignoring case.
- `object.subdivision_set` doesn't add Multires in 5.2's Sculpt Mode, so Ctrl+D is our own operator
  (`multires_subdivide`).
- Headless Blender has no sculpt PBVH: `paint.mask_flood_fill` (used by Ctrl+W) crashes there, so the test only
  checks that operator's poll.

`tests/blender/test_in_blender.py` checks every item's operator and settings, the data paths in Sculpt Mode, the
brush picks, SDiv/Divide and that turning the preference off or disabling the add-on leaves Blender's keymap as it
was. It also checks vector displacement maps both ways (see core/Vdm.h): SculptForge's exported EXR read by
Blender equals its .sfvdm row for row (Blender's pixels start at the bottom), and Blender's 32-bit ZIP / PIZ and
16-bit EXRs load in SculptForge unchanged.

To run it here (an M1 under Asahi Linux, x86-64 Blender under FEX): the official Linux tarballs run headless as
`muvm -- blender -b --factory-startup -noaudio --python-exit-code 1 --python tests/blender/test_in_blender.py --
--cli build/sculptforge-cli` (redirect the output to a file: muvm doesn't pass it through). The GUI doesn't start
there (no GL on the x86 side).
