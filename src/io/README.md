# src/io

File formats and the exchange with other 3D programs. It is compiled into `sculptforge_core`, so nothing here may
need a window or a GPU. Every loader treats its input as untrusted: check sizes and indices, return an error string
instead of crashing.

| Format | Files | Read | Write |
|---|---|---|---|
| OBJ | `ObjIO` | yes (several objects become several Parts) | yes |
| FBX | `FbxIO` | yes, with the vendored [ufbx](../../third_party/README.md) | binary FBX 7.4, written from the public layout (quads, normals, UVs, LOD chains) |
| glTF / GLB | `GltfIO` | yes (also `KHR_gaussian_splatting`) | GLB |
| PLY | `PlyIO` | meshes and Gaussian splats, ASCII and binary | meshes and splats |
| STL | `StlIO` | binary and ASCII | binary (converts to millimetres and Z up) |
| USD | `UsdIO` | `.usda` and `.usdz` | `.usda` and `.usdz`, without the OpenUSD SDK |
| ZIP | `Zip` | | stored (uncompressed) archives: what USDZ is |

Shared pieces: `MeshObject.h` (one named mesh per object for files that hold several), `TextureExport.h` (a Part's
texture as a PNG beside the model or embedded), `ArExport` (the phone-friendly GLB and USDZ for "View in AR").

## Exchange with other programs

| Files | What |
|---|---|
| `BridgeIO`, `BridgeProtocol.h`, `BridgeCommands` | Bridge files and the shared Bridge folder; layout in [docs/bridge-format.md](../../docs/bridge-format.md) |
| `BlenderBridge`, `BlenderCommands` | The Blender add-on's exchange, through a per-user folder and manifests; see [docs/blender.md](../../docs/blender.md) and [addons](../../addons/README.md) |
| `MayaBridge`, `MayaCommands` | The Maya shelf script's exchange through one folder you pick; see [integrations/maya](../../integrations/maya/README.md) |

Neither bridge opens a port: each side writes files and polls the other's manifest. Live updates for Maya are in
[src/link](../link/README.md).

## Adding a format

1. Add `XxxIO.h/.cpp` here and list the `.cpp` in the top-level `CMakeLists.txt` (`sculptforge_core`).
2. Return `bool` plus an error string; fill `Mesh` (with `quadPartners`, UVs, colours when the format has them).
3. Register `open_xxx` / `export_xxx` commands next to the existing ones (`src/core/Commands.cpp`, `export_stl` in
   `BridgeCommands.cpp`) so the UI, CLI, Python and MCP get them.
4. Add a round-trip test in `tests/` (`test_part_io.cpp`, `test_texture_io.cpp`) and a damaged-file case in
   `test_malformed_files.cpp`; for exports other tools read, a check in
   [tools/export_check](../../tools/export_check/README.md).
5. Clean-room only: write from published specifications, never from another program's SDK or code.

## Test it

The unit-test program runs every case, or only those whose name contains the words you pass it; see
[tests/README.md](../../tests/README.md).
