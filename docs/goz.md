# ZBrush interoperability (Send to ZBrush, GoZ format) and STL

SculptForge reads and writes ZBrush's **GoZ** bridge format and can send meshes to and receive them
from a running ZBrush the way GoZ-enabled apps do. The format notes below come from reading
[GoB](https://github.com/JoseConseco/GoB), the open-source (GPL) Blender GoZ add-on by Daniel Grauer,
JoseConseco and contributors. Its `gob_import.py`, `gob_export.py` and `paths.py` are the
reference for this implementation. Many thanks to them.

ZBrush's own **.ZTL** (tool) and **.ZPR** (project) files are proprietary and undocumented, so SculptForge
cannot open them. Opening one gives an error that suggests sending the model with Send to ZBrush or exporting OBJ/FBX
from ZBrush.

The Blender bridge ([blender.md](blender.md)) sends meshes to and from Blender as the same `.GoZ` files.

Code: `src/io/GozIO.h/.cpp` (format + bridge), `src/io/StlIO.h/.cpp`, `src/io/GozCommands.cpp`
(commands), `src/app/PanelGoz.cpp` (File > Send to ZBrush menu). Tests: `tests/test_goz.cpp`.

## Commands

| Command | What it does |
|---------|--------------|
| `goz_import path=` | Open a `.GoZ` file (`open` does the same for `.GoZ`) |
| `goz_export path= [name=] [quads=true]` | Write a `.GoZ` file; with several visible Parts, one `<stem>_<SubTool>.GoZ` each beside `path` |
| `goz_send [name=] [folder=] [launch=true] [zbrush=]` | Send every visible Part to ZBrush through the bridge (`name=` only for a single one) |
| `goz_receive [folder=]` | Load what ZBrush last sent: each object updates its Part (undoable) or becomes a new one |
| `export_stl path=` | Binary STL of the visible Parts merged (`open` reads binary and ASCII `.stl`) |

All of them are available in `sculptforge-cli`, the MCP server and Python, like every other command.

## The .GoZ file

Everything is little-endian. After the header comes a list of sections. Each section is a `uint32` tag
and a `uint32` length. The length counts the tag and length fields too, so `length - 8` bytes follow.
Element sections (vertices, faces...) then hold a `uint64` count and `length - 16` payload bytes.

| Offset / tag | Contents |
|--------------|----------|
| 0 | `"GoZb 1.0 ZBrush GoZ Binary"` (26 bytes) + six `'.'` (0x2E) |
| 32 | `uint32 1`, the tag of the object-name record |
| 36 | `uint32 length` (= name length + 24), `uint64 1`, `"GoZMesh_"` + ASCII name |
| `0x1389` | mesh name record: length 20, `uint64 1`, `uint32 0` (skipped) |
| `0x138A` | subdivision level records, 4 × `uint32` each (read for information only) |
| `0x2711` | vertices: count, then `float32 x, y, z` each |
| `0x4E21` | faces: count, then 4 × `uint32` indices; the 4th is `0xFFFFFFFF` for triangles |
| `0x61A9` | UVs: face count, then 4 corners × `float32 u, v` per face (triangles pad with (0, 1)) |
| `0x88B9` | vertex paint: vertex count (GoB writes it as `uint32` + `float32 0`), then `B, G, R, 0` bytes per vertex, sRGB |
| `0x7532` | mask: count, then `uint16` per vertex, **65535 = unmasked, 0 = fully masked** |
| `0x9C41` | face groups: face count, then a `uint16` id per face (GoB uses 65504 for "no group") |
| `0xAFC9` / `0xD6D9` / `0xC351` | diffuse / displacement / normal map path (`uint64 1` + bytes); kept in `GozInfo`, not loaded |
| `0x0000` | end of file: GoB writes 16 zero bytes |

Unknown tags are skipped using their length. Section lengths are checked against the file size, and
faces with out-of-range indices are dropped, so a broken file cannot read past its end.

### Conventions and mapping to `sf::Mesh`

- **Axes.** ZBrush is Y-down. GoB maps GoZ `(x, y, z)` to Blender `(x, z, -y)` (Z-up). In SculptForge's
  Y-up space that is `(x, -y, -z)`, a 180° turn about X, which keeps the triangle winding. It is applied
  on import and undone on export. GoB's default scale factor is 1, so no unit scaling is applied.
- **Faces.** A quad `(a, b, c, d)` becomes two consecutive triangles, `(a, b, c)` and `(a, c, d)`, kept
  paired as one quad (`Mesh::quadPartners`, so Wire shows the quad and sculpting, undo, subdivision and
  projects keep it). On export every pair is written back as that quad, `(a, b, c, d)`, provided the two
  halves share a face group and their UVs agree on the shared corners (`quads=false` turns this off). A mesh
  imported from ZBrush therefore goes back with its original quads. Meshes without kept quads (older
  projects) fall back to pairing consecutive triangles of exactly that pattern. Degenerate triangles (e.g.
  a quad with a repeated index) are dropped, and their quad partner becomes a plain triangle.
- **UVs.** GoZ stores V downwards, and GoB flips V both ways by default. `Mesh::uvs` uses V up
  (OpenGL/OBJ), so V is flipped on the way in and out. There are 3 corners per triangle, in triangle order.
- **Vertex Paint.** The bytes are sRGB. `Mesh::colors` is linear, so they are converted with
  `srgbToLinear`/`linearToSrgb`.
- **Mask.** `Mesh::mask = 1 - value / 65535`. A section that masks nothing is dropped, so no memory is used.
- **Face Groups.** The 16-bit ids are stored as they are in `Mesh::polygroups`. On export, ids above 65535
  keep only their low 16 bits.
- **Hidden faces.** GoZ has no hidden flag, so hidden triangles are exported like any others.
- The object name (`GozInfo::name`) is remembered per Part (`Document::gozName` for the active one,
  `SubTool::gozName` for the others, saved in projects), so `goz_send` updates the ZBrush SubTool the
  mesh came from and `goz_receive` updates the SculptForge Part it was sent from.

## The bridge (Windows, macOS)

GoZ apps exchange files through a shared folder:

- Windows: `%PUBLIC%\Pixologic` (usually `C:\Users\Public\Pixologic`)
- macOS: `/Users/Shared/Pixologic`
- Linux: ZBrush doesn't run there, so there is no default. The bridge commands explain this, and GoZ
  files can still be imported and exported. Setting a custom folder (the `folder=` argument, or the
  field in File > Send to ZBrush), such as a Wine prefix's `drive_c/users/Public/Pixologic`, makes the
  bridge work anyway.

**Send** (`goz_send`), following GoB's `GoB_OT_export.execute`. Every visible Part is one GoZ object,
named after the GoZ name it came with, else its Part name (`gozObjectName`; duplicates get `_2`,
`_3`...), so ZBrush gets them as separate SubTools:

1. Write `GoZProjects/Default/<Name>.GoZ` for each object, plus `<Name>.ztn`, which holds the object path
   without its extension.
2. Write those paths (forward slashes, no extension), one line per object in Part order, to
   `GoZBrush/GoZ_ObjectList.txt`.
3. Write `GoZBrush/GoZ_Application.txt` = `SculptForge` and `GoZBrush/GoZ_ProjectPath.txt`.
4. Register SculptForge as a GoZ application: `GoZApps/SculptForge/GoZ_Info.txt` (same keys as GoB's
   `Blender/GoZ_Info.txt`) and `GoZ_Config.txt` (`PATH = "<sculptforge executable>"`, written by the app).
5. Tell ZBrush. If `GoZBrush/GoZBrushFromApp(.exe|.app)` (installed by ZBrush's GoZ setup) exists, it is
   started. That is how official GoZ plug-ins make a running ZBrush import the object list. Otherwise
   the newest ZBrush in the default install folders, or `zbrush=`, is started, like GoB's
   `find_zbrush`. GoB also starts its own compiled ZScript at that point. SculptForge does not ship a
   ZScript, so after a fresh start you press GoZ in ZBrush.

**Receive** (`goz_receive`), following GoB's `GoB_OT_import.execute`: read `GoZ_ObjectList.txt`, append
`.GoZ` to each line and load every object. Each object then:

- replaces the mesh of the Part whose GoZ name matches (an undoable step in that Part's history),
- or, for the first object when the scene is a single Part that didn't come from ZBrush (e.g. a new
  scene), replaces that Part, as receiving did before Parts,
- or else is added as a new Part named after the object.

The active Part stays selected. `gozReceive(root, Mesh&)` still offers the objects merged into one
mesh for callers that want that.

In the app, **Receive automatically** checks the object list's modification time once a second and
receives when ZBrush rewrites it, which is what GoB's auto-import timer does. Sending turns it on and
remembers the file time of SculptForge's own write, so a send is never received back.

## STL

`open` reads binary STL and ASCII STL. A file is binary when its size is exactly `84 + 50 × count`,
otherwise it must start with `solid`. Coincident corners are welded with `weldImported`. `export_stl`
writes binary STL with facet normals. Positions are kept as stored, with no axis or unit conversion.
