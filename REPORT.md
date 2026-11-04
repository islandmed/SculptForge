# feat/test-flow: tiered testing, test cache, faster release

Branch `feat/test-flow` (from main 058dba75). Plan and rules: docs/testing-flow.md. Machine scripts in ~/Work/.sf
(not in git; old versions in ~/Work/.sf/archive): pc.sh, t0.sh, t1.sh, merge-batch.sh, sfwatch.sh, nightly.sh.
Delegated to herdr agents (own worktrees, own REPORTs): **warmui** (feat/warm-ui-runner, persistent UI runner, still
working) and **linkspeed** (feat/link-speed, pushed 79f77aa8: mold, PCH, test-binary linking).

## Numbers (PC = RTX 3060 in WSL, ctest -j10; laptop = aarch64 Arch)

| | before | after |
|---|---|---|
| **One-file edit → test result** (PC free) | full suite: build + 240 s of tests (282 s in your run), or ~33 min on the laptop (software GL) | **T0: 31 s** for an app .cpp (build 9 s + 4 tests 12 s); **71 s** for App.h (build 49 s); core .cpp: 24 tests, 49 s of tests cold, 5 s with the cache |
| Waiting for the PC (6+ sessions share it) | 107-1320 s observed tonight | the same queue: the biggest cost left. sfwatch uses only idle time, and T0/the cache make each job short |
| **Full suite (T1)** | 240 s of tests, 175 tests, 4 failing (all GPU-loss related) | cold **248 s**, green (172 tests; 2,235 test-seconds, the PC is busy the whole time). **Warm (nothing changed): 7 s**, 161 of 171 skipped by the cache. A batch reruns only what it changed |
| **Merge batch** | laptop build + full ctest -j3 (~33 min), then main CI (16.6-20 min) | T1 on the PC (≈4 min) **in parallel with** full CI on `staging` (≈17-20 min); main fast-forwards; main's CI reuses the staging run (≈1 min). **≈50 → ≈20 min** |
| **Release, bump → published** | 0.10.11: 24.7 min, 20.3 of it waiting for CI on the bump | CI wait **20.3 min → 0.8 s** (reuse check against real 0.10.11 data). Packages on the bump commit (17-20 min) is now the long pole: **≈20 min** (-4 to -5). Proposal below for ≈5 min |

## What changed

- **T0** `tools/test/impacted.py`: changed files → `ctest -R` regex, from the build graph (Ninja `-t deps` or the
  Makefile `.o.d`/`link.txt`), a test's own files, UI data, a hand map, and for app files name words + co-change
  history + a smoke set. 0.2 s. `~/Work/.sf/t0.sh` (PC, else laptop); `SF_T0_BASE=HEAD` = only uncommitted edits.
- **tools/test/run.py**, the one ctest runner for every tier:
  - **content-hash test cache** (Tony's item 1): local on the PC, never on CI.
  - **one rerun**, with flakes logged to tests/flaky.md (one real flake so far: sculptforge_tests_9of16).
  - **20 s budget** with the tests/slow_tests.txt ratchet (34 entries, each with its highest measured seconds and a plan; near 20 s a test varies by ±5 s with the PC's load).
  - **nightly exclusion**, and **GPU-lost** classification (exit 4).
  - `--threads-per-test`: each test process gets its share of the cores.
- **T1/T2** `scripts/merge-queue.sh staging --with ~/Work/.sf/t1.sh HEAD`: the batch goes to `staging`, the full CI runs
  there and T1 runs on the PC; main fast-forwards when both pass. I tested it in a sandbox (green lands, red CI holds,
  red T1 holds), but **not live yet**: the first real batch will be merge0109's. merge-batch.sh now uses it (old path
  kept for older main). ci.yml: main reuses a full staging run of the same commit; main's runs are never cancelled.
- **Release**: `tools/release/paperwork_only.py` (exact files and lines, fails safe) lets release.sh step `ci` and
  release.yml `locate` reuse the parent's green CI. Step `check` runs the bump's own tests. ci.yml's run on the bump
  also reuses it, which frees runners for Packages.
- **T3**: `TEST(nightly_*)` → `sculptforge_tests_nightly`; tests/perf/*.sfui → `perf_*`; .github/workflows/nightly.yml
  (daily, the non-GPU part); ~/Work/.sf/nightly.sh (by hand, no cron: PC GPU, no-cache full run, floor tier on the
  laptop at load < 2, Windows desktop idle and calibrate). 6 stress and perf-gate cases moved to nightly (list in
  slow_tests.txt).
- **Speculative CI** (Tony's item 3): ~/Work/.sf/sfwatch.sh, **running in herdr tab `sfwatch`**. It runs T0 on every
  new worktree commit and T1 on a new main, only while the PC is idle. Results go to ~/Work/.sf/watch/<wt>.md; a red
  result is sent to that worktree's herdr agent. t0.sh reuses its result for the same clean commit.
- **ccache** (Tony's item 5): the PC now shares ccache across all worktrees (`CCACHE_BASEDIR=/root/wt`; before, every
  worktree missed: 10 % hits); linkspeed measured a warm clean rebuild at 7 s with 868/868 hits. Laptop ↔ PC can't
  share: aarch64 vs x86_64. **CI ↔ PC: not done.** It needs your OK, because objects built off GitHub would flow
  into release packages (a cache-poisoning surface).
- pc.sh: per-worktree dependencies (the shared /root/fc build was rebuilt or clobbered by other worktrees), one PC job
  at a time, a per-worktree lock, a fixed `-R '^x$'` quoting bug (my earlier edit), times/JUnit copied back,
  `SF_PC_ENV`, wait time printed.
- `SF_UI_PROFILE=1`: a UI test prints its slowest script lines. ui_fill_layers now uses 1024 textures (was 2048).
- IP: the UI tests whose names carried marks are renamed: classic_keys, forge_spheres, library, parts, parts_files,
  custom_lit_spheres (comments cleaned too).
- ~/Work/CLAUDE.md Gate and Lean lines updated (59 lines); docs/releasing.md and docs/merge-queue.md updated.

## Your questions

- **ui_micro_levels SEGFAULT: D3D12/WSL-specific.** Its first micro-displacement frame makes D3D12 remove the device.
  It passes on the same PC's native NVIDIA driver (v0.10.11, `PASS (128 frames)`) and on CI's llvmpipe. It now has
  the label `no_d3d12`, which pc.sh leaves out, with the reason in CMakeLists.txt.
- **ui_update_check: not a bug.** It fails only when the GPU is gone and passes on the rerun (run.py logs
  "gpu: … not a flake"). ui_texture_start, ui_timeline and ui_retopo failed the same way: they ran beside the device
  loss.
- **Real bug found, fixed here:** after a failed init (no window or no OpenGL), the app crashed in shutdown instead of
  exiting with its error (seen on the PC during a GPU outage). Fixed in App.cpp; reasoned from the code, not
  reproduced (SDL falls back to software GL).

## Watch

- **The PC is unstable under heavy load.** Three compiler crashes tonight ("cc1: Bus error", "internal compiler
  error: Segmentation fault" on trivial lines) and several GPU losses, all while builds overlapped tests or another
  agent's build (load 56-65). Now serialized; if crashes come back at normal load, check its RAM, heat and
  overclock.
- slow_tests.txt is a to-do list with a plan per line. ui_uv_editor (116 s) and insider_drops (100 s) are the top
  two.

## Next (proposal)

- **Release ≈5 min after the release batch lands.** Run Packages on `staging` too, and let release.yml promote
  that run (same commit). Include the bump commit in the release batch. Then Packages runs in parallel with CI
  instead of after the bump. This changes the coordinator's bump step, so it's their call.

Tests: T1 on the PC green at 74d8e424 (143 run, 28 cached, 0 failed; the 2 tests over budget there were then listed
in a222de07). Full CI on the branch with full=true is green: run 41807683356, 20.9 min, every platform plus the VFX
Reference Platform and Blender jobs. run.py ran on every runner: no flakes, nothing over the 150 s CI budget.
Self-tests test_impact, test_run and release_paperwork pass; release_script_beta passes.
SHA: a222de07 (pushed to origin/feat/test-flow). Merge note: the renamed UI tests (git mv) merge cleanly with edits
on other branches; ~/Work/.sf scripts are already live.
