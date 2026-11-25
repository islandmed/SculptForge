# tests

Everything that checks SculptForge. [CONTRIBUTING.md](../CONTRIBUTING.md) says when a change needs a test;
this page says where each kind lives and how to run it. To build first, see [docs/building.md](../docs/building.md).

| What | Where | Run it |
|---|---|---|
| **Unit tests** (C++, no window): `TEST(name) { CHECK(...); }` cases | `test_*.cpp` here, one program `sculptforge_tests` built from all of them. `TestHarness.h` is the harness, `test_main.cpp` the runner | `./build/sculptforge_tests`; words after it run the cases whose name contains them; `--shard K/N` one slice; `--area NAME` one area; `--nightly` the slow `nightly_*` cases |
| **Geometry promises** for brushes and tools | `GeomInvariants.h` (max rise, spikes at the falloff edge, mirror error, volume change) used by the brush tests | part of the unit tests |
| **UI tests**: scripts that drive the real app and check what it shows | `ui/*.sfui` (some 370), `e2e/` (whole modelling workflows), `artist/` (sculpting sessions judged by `tools/qa`), `render_matrix/`, `perf/`, `tsan/` | `SDL_VIDEO_DRIVER=offscreen ./build/sculptforge --ui-test tests/ui/NAME.sfui --ui-out shots/`; the step list is at the top of `src/app/UiTest.cpp` |
| **Python** | `test_python.py`, `python/` (pytest, also run against the CLI) | `ctest --test-dir build -R python` when built with `-DSCULPTFORGE_BUILD_PYTHON=ON` |
| **Integrations** | `blender/`, `maya/` (the add-on and shelf script, against fakes or a real Blender), `upstream/` | `ctest -R blender` and `-R maya` |
| **Release and repo scripts** | `release_*_test.sh`, `release_smoke.py`, `discord_announce_test.py`, `apt_retry_test.sh`, `bench_*.py` | `ctest -R release` and friends |
| **Benchmarks and stress** | `tools/` (programs such as `remesh_bench` and `dyntopo_bench`), `takes/` | `ctest -L nightly` |
| **Data** | `data/`: small meshes, projects, sample plugins, studio profiles, fonts; each with its source or licence where it needs one | read by the tests |

## How the suite is organised

- `CMakeLists.txt` registers everything with CTest. The unit tests are split into shards (`sculptforge_tests_0of16`
  and so on), so `ctest -j 4` runs them at once, each with its own temp folder. A new `tests/test_*.cpp` is picked up
  when you re-run `cmake`.
- `areas.map` assigns every test, unit file and source path to an area (sculpt, mesh, cloth, ui, render, io, ai, unit,
  site). The `test_areas` check fails when a test belongs to none. CI uses the map to run only the tests a change can
  break first; the whole suite runs before a release. How the tiers fit together: [docs/testing-flow.md](../docs/testing-flow.md).
- Labels: `ui`, `nightly`; `ctest -L ui` runs every UI test.
- `quarantine.txt` and `flaky.md` track tests that failed and then passed on a rerun. A quarantined test stops
  blocking only until its date; fix or delete it.

## Writing a test

- **Core behaviour:** add a `TEST` to the nearest `test_*.cpp` (or a new file). Use a real mesh, not a flat plane; a
  plane hides most defects. Assert numbers, and print them so a failure says what happened.
- **A brush or tool:** first write the geometric invariants (nothing rises that should only cut, no spikes at the
  edge, symmetry holds, smoothing keeps volume) on a dense curved mesh, then the feature.
- **A panel or workflow:** a `.sfui` script in `ui/`; it takes screenshots you should look at. Settings are
  isolated, so it never touches your own.
- **A bug fix:** a test that fails before the fix and passes after.
- **New image or video in the repo:** run `tools/check_marks_media.py` and commit `tools/marks_media_cache.json` with it.
- Every test must be deterministic and must not need the network or an account.
