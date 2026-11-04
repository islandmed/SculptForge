# Build speed: the edit-build loop

How long it takes from saving one file to rebuilt `sculptforge`, `sculptforge_tests` and `sculptforge-cli`, and what
keeps it short. Measure with `tools/dev/edit_timing.sh BUILD_DIR [JOBS]` (a configured, built tree; Ninja gives the
compile/link split, Make only the wall time).

## What the build does (cmake/BuildSpeed.cmake)

| | default | off switch | why |
|---|---|---|---|
| mold linker | ON when `mold` is installed (Linux, GCC 12.1+ or Clang) | `-DSCULPTFORGE_MOLD=OFF` | links in a fraction of ld.bfd's time. MIT licence (mold 2.0+). Output is deterministic (same inputs, same bytes: checked by linking three times). Release packaging (packages.yml, PKGBUILD, Flatpak, rez) passes OFF: shipped binaries keep the system linker. CI's Linux test jobs install it from apt. |
| precompiled headers | ON on Linux with GCC/Clang | `-DSCULPTFORGE_PCH=OFF` | the standard library, glm and (app) imgui parsed once per target. Only headers that never change with our code, so editing `src/` never rebuilds a PCH. Windows (ccache as cl.exe can't cache `/Yu`) and macOS (universal builds) build without; those CI jobs keep the includes honest. With ccache 4.8+ the build passes ccache the `sloppiness` PCH needs; older ccache turns PCH off. |
| shared app libraries | always | | `sculptforge_appshared` (layout, looks, translations, Library, brush previews, camera, clusters), `sculptforge_actions` (key chords; SDL) and `sculptforge_mcp` (MCP server): the app, the CLI and the tests link them instead of each compiling the files again. |
| split DWARF | ON for Debug/RelWithDebInfo on Linux | `-DSCULPTFORGE_SPLIT_DWARF=OFF` | debug info stays in `.dwo` files; the linker never reads it. Release builds have no debug info, so nothing changes there. |

Not done: unity builds. Our TUs are optimiser-bound at `-O2`, and a unity batch turns a one-file edit into an
eight-file compile, which is the case this work is about.

## Numbers (2026-11-04, `tools/dev/edit_timing.sh`; seconds of wall time to rebuilt sculptforge + sculptforge_tests + sculptforge-cli)

PC (WSL2 Ubuntu 24.04, GCC 13, 12 threads, Ninja + ccache; the whole PC to itself; mold + PCH; "after" is two runs):

| case | before | after | |
|---|---|---|---|
| cold build (no ccache) | 189 | 154-170 | -10 to -19 % |
| one `src/core` .cpp (Sculpt.cpp) | 5.5 | 4.3 | links 1.7 s -> 0.7 s |
| widely included header (core/Mesh.h, ~280 files) | 157.5 | 114-139 | -12 to -28 %; CPU 1839 s -> 1345-1648 s |
| one app-only .cpp (PanelBrushes.cpp) | 0.8 | 0.3 | link 0.5 s -> 0.1 s |
| one app .cpp the tests also use (AssetLibrary.cpp) | 3.9 | 2.6 | compiled once instead of twice |
| one test .cpp (test_brushes.cpp) | 0.7 | 0.3 | |
| clean rebuild from a warm ccache | | 7 | 868/868 hits, SDL's own PCH compiles included (before: 266 uncacheable) |

Laptop (Arch, GCC 16, ARM64, Make, no ccache, -j3 through heavy.sh, other sessions running; no mold installed, so PCH +
shared libraries only):

| case | before | after |
|---|---|---|
| cold build | 339 | 290 |
| one `src/core` .cpp | 6.3 | 5.5 |
| widely included header | 269 | 225 |
| one app-only .cpp | 3.6 | 2.7 |
| one app .cpp the tests also use | 7.6 | 4.2 |
| one test .cpp | 4.8 | 4.3 |

mold's output is deterministic: three links of sculptforge and sculptforge-cli gave identical bytes.

What is left is the header case: an edit to `core/Mesh.h` recompiles ~280 files at `-O2`, and that time is the
optimiser, not parsing or linking. The next lever is fewer files depending on the core headers (forward
declarations, splitting Mesh.h's rarely changing data from its helpers), not the build system.
