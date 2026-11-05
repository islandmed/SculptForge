# Flaky tests

Written by tools/test/run.py (docs/testing-flow.md): a test that failed, then passed on its one automatic rerun.
A test here twice gets the `flaky` label in CMakeLists.txt (it then never blocks a release) and an owner who fixes or
deletes it; remove its lines once fixed.

| date | test | machine | commit |
|---|---|---|---|
| 2026-11-04 | sculptforge_tests_9of16 | PC (WSL, RTX 3060), ctest -j10 | 76e4be24 |
