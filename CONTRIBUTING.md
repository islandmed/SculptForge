# Contributing to SculptForge

Thanks for your interest! SculptForge is GPL-3.0-or-later; by contributing you
agree your work is licensed under the same terms.

## Ground rules

- **Clean-room only.** Do not contribute code, shaders, brushes, alphas, icons or
  other assets copied or decompiled from any proprietary product. Re-implementing well-known published techniques is fine.
- Keep `src/core` free of UI and GPU dependencies so it stays testable and
  scriptable.
- New editor operations should be registered as commands in
  `src/core/Commands.cpp` so they are available to the UI, scripting and the
  AI assistant alike.
- Add or update tests in `tests/` for core behaviour changes.

## Developer Certificate of Origin

SculptForge uses the [Developer Certificate of Origin 1.1](https://developercertificate.org) (DCO). There is no
CLA: you keep the copyright to your work and license it under GPL-3.0-or-later.

Sign off every commit with `git commit -s`, which adds a line like this to the commit message:

```
Signed-off-by: Your Name <you@example.com>
```

By signing off you certify, as the DCO says, that the contribution is your own original work, or that you have
the right to submit it under the project's license (for example, code under a compatible open-source license,
with its notice kept). You also certify that it is not copied or derived from proprietary software, its SDKs,
decompiled or disassembled code, its documentation or its assets.
Describing how a feature behaves from the outside, and implementing published techniques, is fine.

Forgot the sign-off? `git commit --amend -s` fixes the last commit, and `git rebase --signoff main` the whole
branch.

## Workflow

1. Build and run tests (see README).
2. Keep changes focused; one feature or fix per pull request.
3. Each source file starts with the SPDX license header.
4. CI must pass on Linux, macOS and Windows.

## Code style

C++20, 4-space indentation, `camelCase` functions, `PascalCase` types,
trailing underscore for private members. A `.clang-format` is provided.

## Building, testing and packaging

See [README.md#building](README.md#building). On Linux, `packaging/linux/install-build-deps.sh` installs the
build dependencies. `ctest` runs the unit tests; `cpack` in the build folder makes the platform's packages.
`ctest -j 4` runs them four at a time: the `sculptforge_tests` suite is registered as eight shards
(`sculptforge_tests_0of8` ...), each with its own temporary folder. Run the binary itself for a quick check:
`./build/sculptforge_tests qremesh` runs the tests whose names contain "qremesh", `--shard 1/4` one shard, and
each test prints how long it took.
Releases are built by `.github/workflows/release.yml` when a `v*` tag is pushed; [docs/releasing.md](docs/releasing.md) has the process.

### UI tests

UI changes need a UI test. `tests/ui/*.sfui` scripts drive the real app the way a person does: they click
buttons by their labels, type into fields, draw brush strokes, press shortcuts, and then check the result
(on-screen text, the status bar, mesh values). They also save screenshots, so you can look at what the
test saw.

```sh
./build/sculptforge --ui-test tests/ui/sculpt_basics.sfui --ui-out /tmp/shots   # one script
ctest --test-dir build -L ui                                                  # all of them
```

The step reference is at the top of `src/app/UiTest.cpp`. Tests run headless on simulated time, so most
take under a second. They use a throwaway settings folder and never touch your own. A failing step stops
the script and saves `FAIL-line-N.png`, and clicking a missing item lists similarly named items. Clicks
scroll panels as needed, and clicking an item a person couldn't reach (off screen) fails. CI runs the UI
tests on Linux under Xvfb and on Windows with Mesa's software OpenGL, and uploads the screenshots as an
artifact. CI compiles through ccache (see the comment at the top of `.github/workflows/ci.yml`). Releases: see
[docs/releasing.md](docs/releasing.md).

### Windows, from Linux

`tools/wintest.sh` cross-compiles SculptForge for Windows with llvm-mingw (downloaded once into
`~/.cache/sculptforge`) and runs every test, UI tests included, under Wine. It catches Windows API, path and
file-sharing problems before a push; extra arguments go to `ctest` (`tools/wintest.sh -R ui_subtools`). On an
ARM machine it builds for Windows on ARM, which Wine runs natively.

## Demo videos

The website's videos are real recordings made by the app itself:

```sh
./build/sculptforge --demo sculpt --record website/media/sculpt.mp4 --size 1920x1080 --demo-output creature.glb
./build/sculptforge --demo game-ready --record website/media/game-ready.mp4 --size 1920x1080 --demo-asset creature.glb
```

Demos live in `src/app/Demo.cpp`; they drive the real UI (buttons are found by their labels).

## Community

Questions, tips and work in progress: join the [SculptForge Discord](https://discord.gg/gWrYkEmQhH). Bugs and feature requests still go
to [GitHub issues](https://github.com/islandmed/SculptForge/issues), so they can be tracked and fixed.

Everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report security issues
privately, as described in [SECURITY.md](SECURITY.md).
