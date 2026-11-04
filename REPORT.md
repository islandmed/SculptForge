# feat/link-speed: edit one file -> rebuilt binaries

Branch feat/link-speed, code commit 142f7e82 (on 058dba75). Details and tables: docs/build-speed.md.

## What
- `cmake/BuildSpeed.cmake` (included before the dependencies):
  - **mold**: on when installed (Linux, GCC 12.1+/Clang). MIT licence, deterministic output (3 links gave identical
    bytes). `-DSCULPTFORGE_MOLD=OFF` in every release packaging path (packages.yml Linux x2, PKGBUILD, Flatpak, rez).
    CI Linux test jobs `apt install mold`. Installed in the PC's WSL (apt).
  - **PCH** of std + glm (+ imgui for the app) per target; on for Linux GCC/Clang, opt-in on MSVC (ccache-as-cl can't
    cache /Yu) and macOS (universal builds). `-DSCULPTFORGE_PCH=OFF`. Generated/third-party files skip it.
  - **ccache sloppiness** (pch_defines,time_macros,include_file_*) passed on ccache's command line (4.8+), for C and
    C++, before the deps: SDL's own PCH compiles were 266 uncacheable compiles per build, now cached too.
  - `-gsplit-dwarf` for Debug/RelWithDebInfo on Linux (Release unaffected).
- Static libs `sculptforge_appshared` (10 app/render files the tests use), `sculptforge_actions` (SDL key chords),
  `sculptforge_mcp` (cli + tests): no file is compiled twice any more.
- `tools/dev/edit_timing.sh BUILD [JOBS]`: the measurement (core .cpp, Mesh.h, app .cpp, shared app .cpp, test .cpp).

## Numbers (seconds, wall time to rebuilt sculptforge + sculptforge_tests + sculptforge-cli)
| case | PC before | PC after | laptop before | laptop after |
|---|---|---|---|---|
| cold (no ccache) | 189 | 154-170 | 339 | 290 |
| core .cpp | 5.5 | 4.3 | 6.3 | 5.5 |
| Mesh.h (~280 files) | 157.5 | 114-139 | 269 | 225 |
| app-only .cpp | 0.8 | 0.3 | 3.6 | 2.7 |
| app .cpp also in tests | 3.9 | 2.6 | 7.6 | 4.2 |
| test .cpp | 0.7 | 0.3 | 4.8 | 4.3 |
| clean rebuild, warm ccache | (266 uncacheable) | 7 (868/868 hits) | | |

PC: GCC 13, -j12, both pc.sh locks held. Laptop: GCC 16 ARM64, Make, no ccache/mold, -j3 via heavy.sh, under load.
Links on the PC: ~0.5 s -> ~0.1 s each. The header case is now the only big cost, and it's the optimiser at -O2.
Next lever: fewer TUs depending on core/Mesh.h (forward declarations, split headers), not build flags.

## Tests
Full ctest on the PC (pc.sh): all pass except `ui_micro_levels` (SEGFAULT). Main (058dba75, untouched worktree) fails
it the same way: a WSL D3D12 issue (passes on the native driver; test-flow labels it no_d3d12). Its crash removes the D3D12 device, so 9 UI tests after it
failed in the first run. Rerun alone: all pass. `close_holes_big_hole_is_quick` (timing) missed its limit once under
-j10 and passed on rerun. check_marks and note fragments: OK.

## Notes for the merger
- CMakeLists edits are in the targets (core/ai/phone PCH, new libs, app + tests sources); test-flow edits the tests
  section only.
- Laptop: `sudo pacman -S mold` (needs Tony's password) would give it the link speedup too; ccache + ninja aren't
  installed there either.
