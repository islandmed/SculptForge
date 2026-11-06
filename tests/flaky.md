# Flaky tests

Written by tools/test/run.py (docs/testing-flow.md): a test that failed, then passed on its one automatic rerun.
A test here twice gets the `flaky` label in CMakeLists.txt (it then never blocks a release) and an owner who fixes or
deletes it; remove its lines once fixed.

| date | test | machine | commit |
|---|---|---|---|
| 2026-11-04 | sculptforge_tests_9of16 | PC (WSL, RTX 3060), ctest -j10 | 76e4be24 |
| 2026-11-06 | ui_idle_redraw (failed its rerun too) | CI Windows (2/4), software OpenGL, runs 42231997469 + 42243212500 | 8c070932, 3959b469 |

## Owned

- **ui_idle_redraw**, CI Windows (fix/ci-site-lane): not a spin. Both failing batches changed only the site (the app
  was byte-identical to green main f4a05960); every failure drew 1-2 frames and woke 16 times in 4 s, as idle should.
  The CPU check counted every thread over the whole window, and one software-OpenGL frame (llvmpipe's threads) costs
  ~0.4 s of processor time on that runner: 2 frames = 10-16% "idle CPU" over the 10% cap. `--measure-idle` now
  checks the CPU between frames and prints the frames' share apart. Remove these lines after a week of green Windows.
