---
name: qa-e2e
description: Run or add an end-to-end modelling scenario (tests/e2e/*.sfui). Use when fixing a bug (every fix adds or extends one), or to check a workflow end to end before a release.
---

# qa-e2e: end-to-end modelling scenarios

`tests/e2e/*.sfui` are real artist workflows run headless in the real app (SDL offscreen), with numeric asserts: mesh
stats, colour, undo, timing caps. Zero Claude tokens per run. They are ctest entries `ui_e2e_<name>` with labels `ui;e2e`,
so the suites that already run the UI tests (T0 picks by area, T1, the Actions full suite) run them. No extra release step.
Total budget for all of them: 5 minutes wall; each one is parallel-safe (own isolated settings folder).

## The RULE

**Every bug fix adds or extends a scenario that reproduces the reporter's steps**, with the issue or Discord link in the
script's first comment. A bug whose fix isn't merged yet: write the scenario now and add `# xfail: BUG-ID what` in its first
40 lines. It runs as XFAIL (exit 0) and flips to XPASS when the fix lands; then delete the line.

## Run one

```
~/Work/.sf/heavy.sh build/sculptforge --ui-test tests/e2e/sculpt_basics.sfui --ui-out /tmp/e2e-out     # laptop, one script
~/Work/.sf/pc.sh . -R ui_e2e_sculpt_basics                                                            # the PC's build
~/Work/.sf/pc.sh . -L e2e                                                                             # all of them
```

A failure prints `FAIL file:line: why` and saves `FAIL-line-N.png` beside the shots. Read the shot first.

## Add one

1. `tests/e2e/<name>.sfui`. Start from a neighbour. Vocabulary: the header of `src/app/UiTest.cpp` (steps and `expect_value`
   names). `run <command>` takes any `sculptforge-cli -l` command. Use `snapshot` / `moved*`, `remember X name` / `$name`,
   `expect_mesh_valid`, `open_edges`, `solid_volume`, `longest_frame_ms` (reset on read; scale caps by `$scale` from
   `remember time_limit_scale scale`: 4 on CI).
2. Write a number that fails when the bug is back: run it against the unfixed build and see it FAIL (or XFAIL).
3. CMake picks it up (glob). `tests/areas.map` gives it an area by name (`ui_e2e_*` lines); add a line for a new family.
4. A new test hook belongs in `UiTest.cpp` (value, one line) and its header comment. No other app code for a test.
5. A scenario that needs its own process (thread pool, quit paths) is a script plus a small driver, like `quit_clean.py`.

Keep it short: one workflow per file, no loops (the language has none: generate repeated blocks and commit the result).
