# src/core

The sculpting engine. Everything here is plain C++ with no window, no GPU and no UI toolkit (no SDL, OpenGL or Dear
ImGui), which is why the same code runs in the app, in `sculptforge-cli`, in the MCP server, in the Python module and
in the unit tests. It builds as the static library `sculptforge_core`; `src/io` is compiled into it too.

## Boundaries

- Do not include anything from `render`, `app`, `ai` or `link`. Anything that needs the network lives in `src/ai`
  (the registry is shared, so `ai` registers its own commands on top); anything that draws lives in `src/render`.
- Long work takes a progress/cancel hook (`OpControl.h`) rather than touching UI.
- Threads: per-dab brush work uses the persistent pool in `ThreadPool.h` (deterministic: results do not depend on
  the thread count); bulk loops use `Parallel.h`.

## Key files

| Area | Files |
|---|---|
| Data | `Mesh` (positions, triangles and optional attributes: mask, vertex colours, face groups, UVs; see [docs/architecture-attributes.md](../../docs/architecture-attributes.md)), `Document` (the open scene), `BVH`, `Parts`, `LayerStack`, `FaceGroups` |
| Commands | `Commands` (the registry every front end calls), `CommandParse`, `Script` (the command language of the CLI and console), `Macro`, `AgentTools` |
| Brushes | `Sculpt`, `SculptKernel`, `BrushSettings`, `BrushRecipe`, `BrushLibrary`, `BrushPacks`, `Stroke`, `Symmetry`, `Masking`, `Ink`, `Kelvinlet`, `Vdm` |
| Undo and layers | `Undo`, `UndoBatch`, `LayerStack`, `PaintLayer`, `Timeline` |
| Resolution | `Multires` (subdivision levels), `Micro*` (micro-displacement levels), `Dyntopo`, `Voxel`, `QRemesh*` (quad remesher), `QuickRemesh`, `Decimate` |
| Geometry ops | `Cut`, `MeshBoolean`, `PartBoolean`, `CloseHoles`, `Tube`, `Deform`, `Cloth*`, `Garment*`, `Drape`, `Procedural`, `Primitives`, `StarterMeshes` |
| Texture and UV | `Uv*`, `AutoUv`, `Texture*`, `Bake*`, `MapExport`, `Projection` |
| Splats | `Splats`, `SplatCapture`, `ViewCapture` |
| Projects | `Project` (the `.sfproj` ZIP), `Pack` (packs: brushes, macros, plugins), `Plugin` (sandboxed WebAssembly plugins) |
| Safety | `ManagedPolicy` and `StudioProfile` (studio lock-down), `NetworkPolicy`, `AuditLog`, `Keychain`, `SelfUpdate` (signed one-click update), `Ed25519`, `Sha256` |
| Performance | `Benchmark`, `BenchPack`, `HardwareTier`, `FrameGovernor`, `MemoryGuard`, `PerfTrace` |

## Adding to it

- A new operation is a command first: register it in `Commands.cpp` (or a `*Commands.cpp` beside the feature) with
  typed, described parameters. The CLI, the MCP tool list, Python's `sf.commands()` and the command palette are
  generated from that registry. Make it undoable through `Undo`.
- Brush and mesh changes need a test in `tests/` that checks the geometry on a dense curved mesh, not only that the
  code ran (`tests/GeomInvariants.h`).
- Keep results deterministic: the tests compare numbers.

## Test it

The unit cases are the `sculptforge_tests` program (`tests/test_*.cpp`). After a build:

```sh
./build/sculptforge_tests            # every unit case
ctest --test-dir build -R sculptforge_tests -j 4
```

See [tests/README.md](../../tests/README.md). Questions: [GitHub issues](https://github.com/islandmed/SculptForge/issues)
or the [Discord](https://discord.gg/gWrYkEmQhH).
