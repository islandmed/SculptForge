# src/app

The desktop application (target `sculptforge`): the window, input, the interface and the main loop. It uses SDL3
for windows and input and Dear ImGui for every control, and it sits on top of everything else in `src`: `core` does
the work, `render` draws it, `ai`, `link`, `cli` and `python` add their parts. Nothing outside `app` includes `app`.
Android builds the same code as a shared library (`SCULPTFORGE_GLES`); the macOS menu bar bits are `MacAppMenu.mm`.

## Where things are

| Files | What |
|---|---|
| `main.cpp` | Command-line options (`--help` lists them), isolated settings for tests, entry points for `--ui-test`, `--ui-serve`, `--bench`, `--demo` |
| `App.cpp/.h` | The application object and its frame loop; sculpt strokes on heavy meshes run on a solver thread (`AsyncStroke`) |
| `Shell*`, `UiLayout`, `Palettes`, `Theme`, `UiScale`, `LookSchemes` | The layout (shelves, trays, Library strip, console), the palette bar and the interface looks; the design is in [docs/ui-design.md](../../docs/ui-design.md) |
| `Panel*.cpp` | One file per panel or window (Parts, Cloth, UV Editor, Generate 3D, Plugins, Phone...). Each is the UI for commands that live in `core` |
| `Actions`, `CommandPalette`, `CommandBar` | One registry of user-facing actions drives shelves, palettes, shortcuts, help on hover and customisation; every registry command without required parameters becomes an action |
| `Input`, `Tablet`, `Wintab`, `Touch`, `SpaceMouse`, `RadialMenu` | Mouse, pen (pressure, Wintab on Windows), touch, 3D mouse and pie menus. Input changes need a usability check with each device |
| `Config`, `UserData` | Preferences (`ui.json` and friends), saved layouts, shortcut overrides |
| `I18n*.cpp` | Interface translations: the English text is the key; Spanish, Traditional and Simplified Chinese and Japanese are tables compiled in. A missing line stays English |
| `LibraryBrowser`, `AssetLibrary`, `UserGuide`, `QuickTour`, `WhatsNewPopup` | The Library strip (comma key), the guide embedded in the app (built from [docs/guide](../../docs/guide)), the tour and the What's New window |
| `UiTest*.cpp`, `UiServe` | The UI test harness (below) |
| `Demo`, `Bench*`, `ProofMode`, `CrashLog` | The scripted demo recorder, benchmarks, and the log file |

## Rules

- The app asks `core` to do things by command and shows the result; it does not hold the only copy of a rule. If a
  feature cannot be driven from `sculptforge-cli` it is probably in the wrong place.
- Every user-visible string goes through the translator; add the Spanish, Chinese and Japanese lines or let
  `tools/i18n/coverage.py` tell you what is missing.
- Never block the frame: long work goes to a worker thread with progress and Cancel.
- A new control gets a UI test script, and screenshots of it at the normal and 2x scale.
- Icons come from the bundled Lucide font; `tools/dev/icons_used.py` regenerates `IconsUsed.h`.

## UI tests

A `.sfui` script drives the real interface (click a control by its label, type, draw a stroke, press a key) and
checks what it shows. They run headless on SDL's offscreen driver:

```sh
SDL_VIDEO_DRIVER=offscreen ./build/sculptforge --ui-test tests/ui/sculpt_basics.sfui --ui-out shots/
```

The statements are listed at the top of `UiTest.cpp`; `tests/ui` has some 370 examples. `--ui-serve` keeps one window
and context alive and runs scripts one after another, which `tools/test/warm_ui.py` uses to make the suite fast.
More: [tests/README.md](../../tests/README.md) and [CONTRIBUTING.md](../../CONTRIBUTING.md).

Build instructions: [docs/building.md](../../docs/building.md).
