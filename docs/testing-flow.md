# Testing flow

Each assurance is paid for once, by the cheapest machine that can give it. Before this flow, one change was tested
four times: by the agent, by the merger's full local ctest, by CI on main, and by CI again on the release bump.

| tier | when | where | what | target |
|---|---|---|---|---|
| **T0 impact** | every agent commit | the PC (laptop if the PC sleeps), or GitHub Actions while the PC queue is long | build + the tests the change can break | < 1 min |
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

### T0 on GitHub Actions

`SF_T0_CI=1 ~/Work/.sf/t0.sh` (automatic while more than 3 jobs wait for the PC, never with `SF_PC_PRIO=1`;
`SF_T0_CI=0` turns it off) runs the same T0 on GitHub's free runners, so branches test in parallel instead of
queueing for the PC. `~/Work/.sf/t0-ci.sh <worktree>` pushes the worktree's state to `t0/<branch>` (uncommitted work
as a temporary commit; the worktree is not touched), waits for `.github/workflows/t0.yml`, prints the same lines as
`t0.sh` and exits 0 green, 1 red, 3 unavailable (t0.sh then uses the PC); it deletes the `t0/` ref after. Results
stay in `~/Work/.sf/t0-ci/<worktree>/`.

- The run (`tools/ci/t0.sh`): `check_marks`, a Ninja build, impacted.py against the merge base with `origin/main`,
  the picked tests through `run.py` on the CPU (Mesa llvmpipe, `SDL_VIDEO_DRIVER=offscreen`, `SF_TEST_BUDGET=150`;
  `GITHUB_ACTIONS` gives the in-test time limits their CI slack).
- **Always green**: the job is `continue-on-error` and the script never fails, so a red T0 emails nobody. The verdict
  is the run summary and the artifact `t0-result` (`result.json`: green/red, stage, failed tests; `failed.txt`:
  LastTestsFailed + FAILED lines; `output.txt`: `ctest --output-on-failure` on the failed tests).
- **Cache**: every push to main builds once and saves `ccache-t0-linux.<commit>` (main's own CI mostly reuses
  staging's run and builds nothing, and a `t0/` ref can only read its own and main's caches). `t0/` runs only restore.
- **Locked down** (`tools/release/check_workflows.py`): pushes to main and `t0/**` only, `contents: read`, no
  secrets, pinned actions; other branch-push workflows ignore `t0/**`.
- A branch older than t0.yml gets it (and `tools/ci/t0.sh`) from origin/main in one extra commit, "t0-ci overlay";
  the tests are still picked from the branch's own files.
- The PC stays for GPU-specific checks (it runs the D3D12 GPU path), T1 and release staging.

## Areas: test the part you touched, the whole once per batch

`tests/areas.map` is the one map. Eight areas: **sculpt** (brush engine, strokes, masking, paint, input, performance),
**mesh** (remesh, cut, crease, levels, tube, parts, UV), **cloth**, **ui** (shell, panels, menus, gizmos), **render**,
**io** (formats, projects, bridges, packs), **ai** (generation, links, updates), **site** (i18n, docs, release and CI
tooling, repo checks). Its sections map ctest names (`[tests]`), `tests/test_*.cpp` files (`[units]`) and source paths
(`[paths]`); `[smoke]` is the cross-area set (start, open, stroke, save/load, a UI shot, mode matrix and monkey);
`[core]` lists the paths that mean everything.

- **Labels**: cmake/TestAreas.cmake gives every ctest entry exactly one label `area_<name>` (plus `smoke`).
  `ctest -L area_cloth` is the cloth tests, `ctest -L smoke` the quick set; `tools/test/affected.py --area cloth` prints the
  names. The `sculptforge_tests` shards are area `unit`: their TEST cases belong to the area of their file (`[units]`) and
  `SF_TEST_AREA=cloth,ui` (or `--area`) makes every shard run only those cases. `test_areas` fails for a test, a unit
  file or a smoke name the map doesn't cover (add a line; the globs cover most new tests), so every test has one area.
- **T0 = smoke + the changed files' areas + impacted.py's direct picks** (tools/test/affected.py `--merge`; t0.sh, pc.sh and
  tools/ci/t0.sh call it). A branch that touches one area runs that area's tests, its unit cases and the smoke set, and not
  the other areas' tests. A CMakeLists.txt edit that only registers tests counts as those tests' area.
- **Everything runs** (`.`) for a [core] path (CMake beyond test registrations, the harness, the map), a header included by
  30+ files under src/ and tests/ (Document.h, Mesh.h, Json.h ...), or a path no `[paths]` line covers. Nothing is skipped
  or weakened by areas: a flaky test stays in its area and is listed in tests/flaky.md; T1 and the batch stage run every test.
- **The full suite runs once per release batch**: `merge-batch.sh` stages the batch (CI on `staging` and `t1.sh` on the PC,
  both the whole suite). `t1.sh` logs a green run on a clean tree in `~/Work/.sf/t1/green.tsv`, and `release.sh X.Y.Z --stage`
  skips its own T1 when the bump commit only adds release paperwork to a commit logged there
  (`tools/release/paperwork_only.py`, the rule CI's reuse uses; `SF_T1_FORCE=1` runs it anyway). It does the same for the
  full CI on `staging` (`ci_reused`: the batch commit's CI passed, so `SF_STAGE_CI_REUSED` makes merge-queue.sh skip the
  second dispatch). The push to `staging` still happens, for its Packages run. The bump costs paperwork checks, not a second
  suite; the bump's own tests (What's New, guide, notes) run in step `check`. The bump pins `tests/ui/whats_new.sfui`, which
  paperwork_only.py now allows line by line (version, comment, expected texts); before, every real bump failed that check.
  Floor benches stay with `bench.sh`.

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
- **Shards by time**: `tests/shard_times.inc` lists the TEST cases of 0.5 s or more with their PC seconds; the
  shards take them longest first, each to the shard with the least work so far (tests/test_main.cpp). When a test
  gets much faster or slower, re-run `tools/test/shard_times.py /tmp/sf-pc-*-times.tsv` (`--override` your branch's
  times file with `--match` for the tests you changed).
- **Where the time goes**: `SF_UI_PROFILE=1` makes a UI test print its slowest script lines with real seconds
  (`SF_PC_ENV=SF_UI_PROFILE=1 ~/Work/.sf/pc.sh . -R ui_bake`).
- **Parallel runs** (`ctest -jN`, N > 1): timing checks get CI's slack (`SF_TIME_LIMIT_SCALE=4`, `timeLimit()` in
  tests/TestHarness.h). With `--threads-per-test auto` (pc.sh), each test process gets its share of the cores
  (`SF_THREADS`, core/ThreadPool.cpp), instead of ten pools each the size of the machine.
- **GPU lost**: a failure whose output says the graphics device went away is the machine, not the code (exit 4).
  pc.sh says so, and such a failure is never logged as a flake. Tests that the PC's WSL GPU path (Mesa on D3D12) can't
  run carry the label `no_d3d12`, which pc.sh leaves out. ui_micro_levels makes D3D12 remove the device, yet passes on
  CI and on the same PC's native driver.

## Modes and the monkey

A mode that won't let go (Draw Tube or Append > Insert taking every drag after another brush was picked) breaks the
app for the person using it, and a hand-written test only covers the modes someone thought of. Two generated UI tests
cover the class (src/app/UiTestModes.cpp); both run in T0 whenever the app changes (tools/test/impacted.py SMOKE).

- **`ui_mode_matrix`** (tests/ui/mode_matrix.sfui, about 30 s): every action in the registry is run on a small sphere.
  One that then latches something (`App::latchedForTest()`: a mode a drag goes to, input still captured, an overlay or
  modal) or stops a plain stroke from sculpting is a **mode**, and each way out is tried from it: Esc, a brush on the
  shelf, Draw, Move, Scale, Rotate, a palette action, undo. Afterwards the tool is the one picked, a plain stroke
  sculpts, nothing is latched and the status bar doesn't still name the mode. A palette action or undo needn't leave
  the mode, but a brush after it must. A new mode is covered without a line anywhere; a way in that isn't an action
  (Append > Insert's menu) is a `mode_entry NAME | LINE | ...` line in the script. A window an earlier case left open
  over the model fails the run (close it in `App::unlatchForTest()`), so a leak can't hide later modes.
  Not modes: an action that asks for a file (no dialog ever opens in a test: src/app/NativeDialog.h), a floating
  window over the model (the model sculpts beside it), an undoable edit (Mask All), a brush that moves no vertices
  (Paint). A modal dialog is only expected to close on Esc. Menus and overlays may take a first click to close.
  Actions that leave the app or reach the network are never run (`kNeverRun`).
- **`ui_monkey`** (tests/ui/monkey.sfui, a few seconds): 10 seeded sequences of 40 user actions (brushes, strokes,
  undo / redo, panels, mode toggles, view changes), then Esc and a brush, and the same invariants plus: the mesh is
  valid (finite, indices in range, watertight if it was), and undo back to the start restores it bit for bit.
  `ui_monkey_many` (tests/ui/nightly/, T3) runs 200 more seeds.
- **A failure** names the case (`[mode tube.draw (Draw Tube), exit Esc]`, `[monkey 53]`) and writes its script beside
  the shots (`ui-shots/<test>/replay-*.sfui`), which replays on its own. `tools/test/shrink_sfui.py APP REPLAY` cuts a
  monkey replay down to the fewest actions that still fail the same way (50 actions to 2 in 15 s). Failures inside a
  generated case don't stop the script: every failing mode is listed in one run.
- **Known failures** (tests/ui/mode_matrix_known.txt, `ID | EXIT | fix task`) are logged as KNOWN, not failed. When a
  fix lands, its cases pass and the matrix fails until their lines are deleted, so the list only shrinks.
- **Values for any script:** `latched`, `latched_modes`, `input_captured`, `overlay_open`; steps `expect_latched none`,
  `expect_sculpts`, `expect_status_not TEXT`, `snapshot_start` / `undo_all` / `expect_restored`, `expect_mesh_valid`,
  `action ID`. A new mode's state belongs in `App::latchedForTest()` (one line) so failures can name it.

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
