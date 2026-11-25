# src

All of SculptForge's C++ source. It is split into subsystems that depend on each other in one direction only, so most
of the code can be built and tested without a window or a GPU.

| Folder | What it is | Built as |
|---|---|---|
| [core](core/README.md) | The engine: mesh, brushes, undo, remeshing, projects, the command registry. No UI, no GPU | `sculptforge_core` |
| [io](io/README.md) | File formats (OBJ, FBX, glTF, PLY, STL, USD) and the Blender and Maya exchange | compiled into `sculptforge_core` |
| [ai](ai/README.md) | HTTPS client, AI services, ComfyUI, and helpers that start other programs | `sculptforge_ai` |
| [link](link/README.md) | Local links: the AI live-mode pipe, phone link, Unreal and Maya | `sculptforge_phone` (phone link), `sculptforge_mcp` (live mode, Maya), `sculptforge_ai` (sockets, Unreal) |
| [cli](cli/README.md) | `sculptforge-cli` and the MCP server | `sculptforge_mcp`, `sculptforge-cli` |
| [render](render/README.md) | OpenGL renderer, camera, micro-level and splat drawing | the app (`Camera`, `Clusters` and `Ghost` also in `sculptforge_appshared`, which the tests link) |
| [app](app/README.md) | The desktop application: SDL3 and Dear ImGui, panels, input, UI test harness | `sculptforge` |
| [python](python/README.md) | The `sculptforge` Python module and the app's embedded Python host | `sculptforge_python`, `sculptforge_pyhost` |

The `CMakeLists.txt` at the top of the repository lists every file of every library; a new `.cpp` must be added there.

## The one rule

The arrows only go down: `app` -> (`render`, `ai`, `link`, `cli`, `python`) -> `core`. `core` includes no SDL, no
OpenGL and no Dear ImGui, and nothing from `render`, `app`, `ai` or `link` (`io` is compiled into it); `render` and `python` do not include
`app`. That is what lets the tests, `sculptforge-cli` and the Python wheel run the same engine headless. If you want
to call upward (say, the engine needs to ask the user something), pass a callback or return a result and let `app`
decide.

## Every operation is a command

Editor operations are registered once in `core/Commands.cpp` (and its `*Commands.cpp` neighbours) as named commands
with typed parameters. The UI, the shortcuts, `sculptforge-cli`, the Python module, the MCP server and the AI
assistant all call that registry, so each is the same undoable action. When you add an operation, register a command
first, then give it a panel.

## Conventions

C++20, 4-space indent, `camelCase` functions, `PascalCase` types, trailing underscore on private members; every file
starts with the SPDX header. See [CONTRIBUTING.md](../CONTRIBUTING.md) for the full rules and the sign-off, and
[docs/building.md](../docs/building.md) to build. Tests live in [tests](../tests/README.md).
