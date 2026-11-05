# Testing flow

Each assurance is paid for once, by the cheapest machine that can give it. Before this flow, one change was tested
four times: by the agent, by the merger's full local ctest, by CI on main, and by CI again on the release bump.

| tier | when | where | what | target |
|---|---|---|---|---|
| **T0 impact** | every agent commit | the PC (laptop if the PC sleeps) | build + the tests the change can break | < 1 min |
| **T1 batch** | once per merge batch | the PC, while T2 runs | the whole suite, -j10 on the GPU, test cache on | < 5 min |
| **T2 platforms** | once per merge batch | CI on the branch `staging` | every platform, VFX Reference Platform, Blender | the gate for main |
| **T3 nightly** | once a day | CI (scheduled) + the PC | label `nightly`: benchmarks, stress, heavy meshes, floor-tier perf | |

## T0: what can this change break?

`tools/test/impacted.py` turns the changed files (branch vs `origin/main`, plus uncommitted ones) into a
`ctest -R` regex:

1. **build graph**: `build.ninja` plus `ninja -t deps`, so headers count. A file maps to the objects that include it,
   then to the libraries and programs that link them, then to the tests that run those programs. A core header
   reaches every `sculptforge_tests` shard, `sculptforge-cli` and the Python API.
2. **a test's own files**: the script it runs, any file its command names, and that file's folder.
3. **UI data**: scripts that name a changed `tests/ui/data` file.
4. **hand map** (top of impacted.py): docs, site, release scripts, note fragments, packaging.
5. **app changes** (every UI test runs the app, so the graph alone would pick all of them): UI tests whose name
   shares a word with the changed file (`PanelMicro.cpp` picks `ui_micro_levels`), UI tests changed in the same
   commits as it in the last 600 commits, and a smoke set.

`marks` (the IP check) always runs. CMake files mean everything runs (`.`). `-v` explains each pick.
Run it with `~/Work/.sf/t0.sh`: `pc.sh` builds on the PC, runs impacted.py there against the PC's build graph, then
runs the tests through `tools/test/run.py`.

## T1 and T2: one batch, two machines at once

The merger (`~/Work/.sf/merge-batch.sh`) merges the green branches, then calls
`scripts/merge-queue.sh staging --with ~/Work/.sf/t1.sh HEAD`:

- the batch commit is pushed to `staging` and the full CI runs there (`workflow_dispatch`, `full=true`);
- at the same time, `t1.sh` runs the whole suite on the PC;
- when both are green, main fast-forwards to exactly that commit. When either is red, main doesn't move.

The PC runs one job at a time, build and tests together. A build overlapping another job's tests drove the
load to 65 on 12 threads and the compiler crashed. Each worktree fetches and builds its own dependencies
(`/root/wt/<name>/fc`); ccache (`CCACHE_BASEDIR=/root/wt`) is shared, so another worktree's objects are cache hits.

CI on main then doesn't build it all again. The `Changes` job sees that this exact commit passed a full run on
staging, with both VFX jobs green, and passes as reused. The merger no longer runs a local full ctest. The release
packages (packages.yml) are built on staging beside CI, and main's Packages run reuses them (job `reuse`). A release
batch carries the bump commit (`RELEASE=X.Y.Z ~/Work/.sf/merge-batch.sh ...`, docs/releasing.md), so its packages
are the ones the release publishes.

## Release: the bump reuses CI (or rides in the batch)

`scripts/release.sh` step `ci` used to wait about 20 minutes for CI on the bump commit. That commit only adds
paperwork to a commit CI had already passed. `tools/release/paperwork_only.py BASE HEAD` checks this file by file
and line by line: the version lines, CHANGELOG, Markdown docs, the generated site, deleted fragments. Anything else
fails safe. When the check passes, step `ci` and release.yml (job `locate`) take the earlier commit's CI result.
On main, a newer push never cancels a run that is still going (ci.yml `concurrency`), so that earlier result exists. The
bump's own Packages run still has to pass, because the version is built into the app. Step `check` runs the bump's
own tests (What's New, the guide, notes) on the bumped tree.

## tools/test/run.py: cache, flakes, budget

Every tier runs ctest through `tools/test/run.py --build build [-- ctest args]`:

- **Nightly** tests (label `nightly`) run only when the ctest arguments select them (`-L nightly`).
- **Test cache** (`--cache DIR`, `$SF_TEST_CACHE`; the PC keeps `/root/.cache/sf-test`): every test gets a
  fingerprint, a hash of everything it can read. That covers its program, every file or folder its command names, the app's assets and
  test data for app tests, `tests/data` for the unit tests, its ENVIRONMENT, and the `SF_`, `SCULPTFORGE_`,
  `SDL_` and GL driver variables. Script tests that nobody listed in `READS` hash every tracked file. A fingerprint
  that passed on the first try is skipped next time, so a rerun of the full suite runs only what changed. The cache
  is never used on CI, and the nightly run on the PC runs everything without it once.
- **Flakes**: failed tests run once more. A test that passes on the rerun is logged as `flaky: name`, added to
  `tests/flaky.md` (by pc.sh, t0.sh and t1.sh), and doesn't fail the run. A test listed there twice gets the label
  `flaky`, and a failure then never blocks a release. Its owner fixes it or deletes it.
- **Budget**: a test slower than `--budget` seconds fails the run. For the `sculptforge_tests` shards, each TEST
  case counts on its own. The default is 20 s on the PC's GPU; CI sets `SF_TEST_BUDGET=150` for software OpenGL on
  4-core runners. `tests/slow_tests.txt` lists the tests over budget today, with their PC seconds and a plan for
  each. A listed test may grow to 1.5× its seconds (scaled by budget ÷ 20 on CI), so the list can only shrink. A
  slow test should wait on events rather than fixed frames, use a smaller mesh or texture, be split, or be labelled
  nightly.
- **Where the time goes**: `SF_UI_PROFILE=1` makes a UI test print its slowest script lines with real seconds
  (`SF_PC_ENV=SF_UI_PROFILE=1 ~/Work/.sf/pc.sh . -R ui_bake`).
- **Parallel runs** (`ctest -jN`, N > 1): timing checks get CI's slack (`SF_TIME_LIMIT_SCALE=4`, `timeLimit()` in
  tests/TestHarness.h). With `--threads-per-test auto` (pc.sh), each test process gets its share of the cores
  (`SF_THREADS`, core/ThreadPool.cpp), instead of ten pools each the size of the machine.
- **GPU lost**: a failure whose output says the graphics device went away is the machine, not the code (exit 4).
  pc.sh says so, and such a failure is never logged as a flake. Tests that the PC's WSL GPU path (Mesa on D3D12) can't
  run carry the label `no_d3d12`, which pc.sh leaves out. ui_micro_levels makes D3D12 remove the device, yet passes on
  CI and on the same PC's native driver.

## T3 nightly

- `TEST(nightly_*)` cases (tests/test_main.cpp) are left out of every shard. They run in
  `sculptforge_tests_nightly`. The `tests/perf/*.sfui` scripts are `perf_*` (labels ui and nightly).
- `.github/workflows/nightly.yml` runs the nightly tests that don't need a GPU (`-L nightly -LE ui`) once a day.
- `~/Work/.sf/nightly.sh` is run by hand at the end of the day, not from cron. It runs `ctest -L nightly` on the PC's GPU, the whole suite with no
  cache, floor-tier stroke benchmarks on the laptop (`SF_HW_SIMULATE=floor`, only at load < 2), and, only on request
  (`SF_NIGHTLY_WIN=1`: it opens a window on that desktop), an idle and calibration check on the Windows desktop
  (`win.sh`). Every other app run is offscreen (`SDL_VIDEO_DRIVER=offscreen`). The summary goes to `~/Work/.sf/nightly/<date>.md`.

## Speculative CI

`~/Work/.sf/sfwatch.sh` runs in the background and tests every new commit as it lands. A worktree's HEAD gets T0
and a new `origin/main` gets T1. Results go to `~/Work/.sf/watch/<worktree>.md`. A red result is sent to the
worktree's herdr agent. `t0.sh` prints that result instead of running again when the commit matches and the tree is
clean. By the time an agent asks, the answer is usually there.
