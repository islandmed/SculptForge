# Bridge format notes (developers)

SculptForge reads and writes **Bridge files**, a binary mesh format, and exchanges them with another sculpting
application through a shared folder. The format notes below come from reading
[GoB](https://github.com/JoseConseco/GoB), the open-source (GPL) Blender add-on by Daniel Grauer, JoseConseco and
contributors. Its `gob_import.py`, `gob_export.py` and `paths.py` are the reference for this implementation. Many
thanks to them. The exact bytes the format and the folder need (extension, header, folder and file names) are in
`src/io/BridgeProtocol.h`; this page calls the extension `<ext>`.

The Blender bridge ([blender.md](blender.md)) sends meshes to and from Blender as the same Bridge files.

Code: `src/io/BridgeIO.h/.cpp` (format + exchange), `src/io/BridgeProtocol.h` (bytes), `src/io/StlIO.h/.cpp`,
`src/io/BridgeCommands.cpp` (commands), `src/app/PanelBridge.cpp` (File > Bridge). Tests: `tests/test_bridge.cpp`.

## Commands

| Command | What it does |
|---------|--------------|
| `bridge_import path=` | Open a Bridge file (`open` does the same for Bridge files) |
| `bridge_export path= [name=] [quads=true]` | Write a Bridge file; with several visible Parts, one `<stem>_<Part>.<ext>` each beside `path` |
| `bridge_send [name=] [folder=] [launch=true] [app=]` | Send every visible Part through the shared folder (`name=` only for a single one) |
| `bridge_receive [folder=]` | Load what the other application last sent: each object updates its Part (undoable) or becomes a new one |
| `export_stl path=` | Binary STL of the visible Parts merged (`open` reads binary and ASCII `.stl`) |

All of them are available in `sculptforge-cli`, the MCP server and Python, like every other command.

## The Bridge file

Everything is little-endian. After the header comes a list of sections. Each section is a `uint32` tag
and a `uint32` length. The length counts the tag and length fields too, so `length - 8` bytes follow.
Element sections (vertices, faces...) then hold a `uint64` count and `length - 16` payload bytes.

| Offset / tag | Contents |
|--------------|----------|
| 0 | `kHeader` (26 bytes) + six `'.'` (0x2E) |
| 32 | `uint32 1`, the tag of the object-name record |
| 36 | `uint32 length` (= name length + 24), `uint64 1`, `kNamePrefix` (8 bytes) + ASCII name |
| `0x1389` | mesh name record: length 20, `uint64 1`, `uint32 0` (skipped) |
| `0x138A` | subdivision level records, 4 × `uint32` each (read for information only) |
| `0x2711` | vertices: count, then `float32 x, y, z` each |
| `0x4E21` | faces: count, then 4 × `uint32` indices; the 4th is `0xFFFFFFFF` for triangles |
| `0x61A9` | UVs: face count, then 4 corners × `float32 u, v` per face (triangles pad with (0, 1)) |
| `0x88B9` | vertex paint: vertex count (GoB writes it as `uint32` + `float32 0`), then `B, G, R, 0` bytes per vertex, sRGB |
| `0x7532` | mask: count, then `uint16` per vertex, **65535 = unmasked, 0 = fully masked** |
| `0x9C41` | face groups: face count, then a `uint16` id per face (GoB uses 65504 for "no group") |
| `0xAFC9` / `0xD6D9` / `0xC351` | diffuse / displacement / normal map path (`uint64 1` + bytes); kept in `BridgeInfo`, not loaded |
| `0x0000` | end of file: GoB writes 16 zero bytes |

Unknown tags are skipped using their length. Section lengths are checked against the file size, and
faces with out-of-range indices are dropped, so a broken file cannot read past its end.

### Conventions and mapping to `sf::Mesh`

- **Axes.** Bridge files are Y-down. GoB maps `(x, y, z)` to Blender `(x, z, -y)` (Z-up). In SculptForge's
  Y-up space that is `(x, -y, -z)`, a 180° turn about X, which keeps the triangle winding. It is applied
  on import and undone on export. GoB's default scale factor is 1, so no unit scaling is applied.
- **Faces.** A quad `(a, b, c, d)` becomes two consecutive triangles, `(a, b, c)` and `(a, c, d)`, kept
  paired as one quad (`Mesh::quadPartners`, so Wire shows the quad and sculpting, undo, subdivision and
  projects keep it). On export every pair is written back as that quad, `(a, b, c, d)`, provided the two
  halves share a face group and their UVs agree on the shared corners (`quads=false` turns this off). A mesh
  received through the Bridge therefore goes back with its original quads. Meshes without kept quads (older
  projects) fall back to pairing consecutive triangles of exactly that pattern. Degenerate triangles (e.g.
  a quad with a repeated index) are dropped, and their quad partner becomes a plain triangle.
- **UVs.** Bridge stores V downwards, and GoB flips V both ways by default. `Mesh::uvs` uses V up
  (OpenGL/OBJ), so V is flipped on the way in and out. There are 3 corners per triangle, in triangle order.
- **Vertex Paint.** The bytes are sRGB. `Mesh::colors` is linear, so they are converted with
  `srgbToLinear`/`linearToSrgb`.
- **Mask.** `Mesh::mask = 1 - value / 65535`. A section that masks nothing is dropped, so no memory is used.
- **Face Groups.** The 16-bit ids are stored as they are in `Mesh::facegroups`. On export, ids above 65535
  keep only their low 16 bits.
- **Hidden faces.** Bridge has no hidden flag, so hidden triangles are exported like any others.
- The object name (`BridgeInfo::name`) is remembered per Part (`Document::bridgeName` for the active one,
  `Part::bridgeName` for the others, saved in projects), so `bridge_send` updates the object the
  mesh came from and `bridge_receive` updates the SculptForge Part it was sent from.

## The shared folder (Windows, macOS)

Applications exchange Bridge files through a shared folder: `%PUBLIC%\<kVendorFolder>` on Windows and `kMacRoot`
on macOS (`src/io/BridgeProtocol.h`). The other application doesn't run on Linux, so there is no default there: the
commands explain this, Bridge files can still be imported and exported, and a custom folder (the `folder=`
argument, or the field in File > Bridge), such as the same folder inside a Wine prefix, makes the exchange work.

**Send** (`bridge_send`), following GoB's `GoB_OT_export.execute`. Every visible Part is one object, named after
the Bridge name it came with, else its Part name (`bridgeObjectName`; duplicates get `_2`, `_3`...):

1. Write `<kProjectsFolder>/Default/<Name>.<ext>` for each object, plus `<Name>.ztn`, which holds the object path
   without its extension.
2. Write those paths (forward slashes, no extension), one line per object in Part order, to the object list
   (`<kInboxFolder>/<kObjectList>`).
3. Write `kApplicationFile` = `SculptForge` and `kProjectPathFile` beside it.
4. Register SculptForge: `<kAppsFolder>/SculptForge/<kInfoFile>` (`kInfoText`, the same keys as GoB writes) and
   `kConfigFile` (`PATH = "<sculptforge executable>"`, written by the app).
5. Tell the other application. If its notifier (`kNotifierWin` / `kNotifierMac` in the inbox folder) exists, it is
   started, which makes a running application import the object list. Otherwise the newest install in the default
   folders, or `app=`, is started, as GoB does; there, import from its own exchange button.

**Receive** (`bridge_receive`), following GoB's `GoB_OT_import.execute`: read the object list, append `<ext>` to
each line and load every object. Each object then:

- replaces the mesh of the Part whose Bridge name matches (an undoable step in that Part's history),
- or, for the first object when the scene is a single Part that didn't come through the Bridge (e.g. a new
  scene), replaces that Part,
- or else is added as a new Part named after the object.

The active Part stays selected. `bridgeReceive(root, Mesh&)` still offers the objects merged into one mesh for
callers that want that.

In the app, **Receive automatically** checks the object list's modification time once a second and receives when
it is rewritten, which is what GoB's auto-import timer does. Sending turns it on and remembers the file time of
SculptForge's own write, so a send is never received back.

## STL

`open` reads binary STL and ASCII STL. A file is binary when its size is exactly `84 + 50 × count`,
otherwise it must start with `solid`. Coincident corners are welded with `weldImported`. `export_stl`
writes binary STL with facet normals. Positions are kept as stored, with no axis or unit conversion.
