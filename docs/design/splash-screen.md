# Startup Splash Screen (`SplashScreen`)

*   **Author:** Lowen & Antigravity
*   **Status:** Ready for Implementation
*   **Date:** 2026-11-09

--------------------------------------------------------------------------------

## Overview

When a user starts SculptForge interactively on the desktop, the application initializes SDL video, creates an OpenGL 3.3 context, compiles viewport shaders, rasterizes UI fonts, loads the brush library, and builds the initial sphere mesh inside `App::init` (`src/app/App.cpp`). Today, the main application window is created visible at the start of `App::init`, showing an unrendered or blank frame while startup tasks run.

This design introduces a dedicated borderless startup splash screen (`sf::SplashScreen`) that displays the SculptForge splash artwork (`assets/branding/splash.jpg`) with an animated progress bar immediately after `SDL_Init(SDL_INIT_VIDEO)`. While the splash screen is active, the main SculptForge window is created with `SDL_WINDOW_HIDDEN` and remains completely hidden. Only after all initialization steps finish and the splash screen completes its display duration and closes does SculptForge show and focus the main application window.

## The Problem

Creating the main application window visible before subsystem initialization completes exposes an unpopulated window during startup and provides no visual feedback while shaders, fonts, and brushes load. Furthermore, the user requires that the main application window must not open on screen until the splash screen has finished and closed.

## Goals

*   **G1 — Strict Main Window Gating:** Keep the main application window hidden (`SDL_WINDOW_HIDDEN`) throughout `App::init` and defer window maximization or fullscreen transitions until after the splash window finishes and is destroyed.
*   **G2 — Embedded Self-Contained Artwork:** Embed `assets/branding/splash.jpg` (`1024x571` JPEG) into the `sculptforge` binary via `cmake/EmbedFile.cmake` (`sf::assets::splashImage`) so portable builds, AppImages, and macOS bundles require no external image files.
*   **G3 — OpenGL Context Isolation:** Present the splash screen using `SDL_GetWindowSurface`, `SDL_ConvertPixels`, and `SDL_UpdateWindowSurface` so the splash window never alters or unbinds the main window's OpenGL 3.3 context during `App::init`.
*   **G4 — Zero Disruption to Headless and Automated Runs:** Automatically bypass the splash screen in headless renders (`--render`, `--record`, `--timelapse`), UI tests (`--ui-test`, `--ui-serve`), benchmarks (`--benchmark`, `--measure-idle`, `--measure-stroke`), demos (`--demo`), and agent/CI runs (`automatedRun()`), while providing `--splash`, `--no-splash`, and `--splash-seconds` CLI flags for explicit control and testing.

## Non-Goals

*   **NG1:** Replacing or altering the in-app first-run Welcome dialog (`App::drawSupportUi` in `src/app/PanelSupport.cpp`) or the Help > About dialog.
*   **NG2:** Adding a splash screen to `sculptforge-cli` or the headless Python module (`sculptforge_python`).

## Open Questions

*   None. All requirements and constraints are resolved.

## Critical User Journeys (CUJs)

### CUJ 1: Interactive Desktop Launch

*   **Given**: A user launches `sculptforge` interactively on macOS, Windows, or Linux without `--no-splash` or headless flags.
*   **When**: `App::init` starts `SDL_Init(SDL_INIT_VIDEO)`.
*   **Then**:
    1. A centered, borderless `768x428` splash window opens immediately showing the embedded SculptForge artwork and a progress bar at the bottom.
    2. The main `SculptForge` OpenGL window is created with `SDL_WINDOW_HIDDEN` and does not appear on screen while OpenGL, shaders, fonts, brushes, and starter meshes load.
    3. As each subsystem initializes in `App::init`, the splash screen progress bar advances smoothly from `0%` to `100%`.
    4. Once initialization completes and the splash screen finishes its duration (default `2.2` seconds), the splash window closes (`SDL_DestroyWindow`) and only then does the main `SculptForge` window appear (`SDL_ShowWindow`, deferred maximize/fullscreen, `SDL_RaiseWindow`).

### CUJ 2: Headless Render, Benchmark, or Automated UI Test

*   **Given**: `sculptforge` is invoked with `--render`, `--ui-test`, `--ui-serve`, `--record`, `--benchmark`, `--timelapse`, `--measure-idle`, `--measure-stroke`, `--demo`, `--no-splash`, or under `automatedRun()`.
*   **When**: `App::init` executes.
*   **Then**: `shouldShowSplash` returns `false`, no splash window is created, and startup timing and headless behavior remain unchanged.

### CUJ 3: Error Recovery During Startup

*   **Given**: `sculptforge` is launched interactively and the splash window is open, but OpenGL 3.3 context creation or shader compilation fails in `App::init`.
*   **When**: `App::init` returns `false`.
*   **Then**: `SplashScreen` closes and destroys its window immediately via RAII before `runMain` (`src/app/main.cpp`) displays the error dialog (`sf::dialogs::message`), ensuring the always-on-top splash window never obscures the error message box.

## Technical Plan (Architecture)

The splash screen is implemented in a dedicated module (`src/app/SplashScreen.h` and `src/app/SplashScreen.cpp`) and integrated into `App::init` (`src/app/App.cpp`) and `runMain` (`src/app/main.cpp`).

```text
+-----------------------------------------------------------------------+
|                           runMain (main.cpp)                          |
|  Parses --splash, --no-splash, --splash-seconds into sf::AppOptions   |
|  Sets options.automated = automatedRun()                              |
+-----------------------------------+-----------------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
|                          App::init (App.cpp)                          |
|  1. SDL_Init(SDL_INIT_VIDEO)                                          |
|  2. showSplash = shouldShowSplash(options, headless,                  |
|                                   sharedWindow_ != nullptr,           |
|                                   options.automated)                  |
|  3. If showSplash: splash.open(options.splashSeconds)                 |
|     -> Decodes sf::assets::splashImage (1024x571 JPEG)                |
|     -> Opens borderless SDL_Window (768x428, centered)                |
|     -> Renders frame via renderSplashBuffer + SDL_UpdateWindowSurface |
|  4. Creates main window_ with (flags | SDL_WINDOW_HIDDEN)             |
|     (defers SDL_SetWindowFullscreen / SDL_MaximizeWindow)             |
|  5. Subsystem init updates splash.setProgress(0.15 .. 0.98)           |
|  6. splash.finish() animates progress to 1.0, waits remaining time    |
|     while pumping SDL_PollEvent, and destroys splash SDL_Window       |
|  7. SDL_ShowWindow(window_), applies fullscreen/maximize,             |
|     SDL_RaiseWindow(window_), SDL_SyncWindow(window_)                 |
+-----------------------------------------------------------------------+
```

### Key Architectural Details

1.  **Pure CPU Frame Compositor (`renderSplashBuffer`)**:
    *   `renderSplashBuffer(const Image& art, int width, int height, float progress, std::vector<uint8_t>& outRgba, std::vector<uint8_t>* cachedBaseRgba = nullptr)` resamples `art` (`1024x571`) to `width x height` RGBA8 using bilinear filtering. When `cachedBaseRgba` is provided, the resampled background image is cached once for `(width, height)` and copied on subsequent calls before painting the bottom progress bar.
    *   Draws a clean bottom bar overlay (`26 px` high at `768x428`, scaled proportionally with `height`) with a dark `#0e0e11` backdrop (`alpha = 0.92`) covering the bottom edge, a `1 px` outer border (`#2c2c34`), a recessed progress track (`#1c1c22`), and a forged-orange progress fill (`#f28c3c` with a top highlight `#ffb877`) proportional to `std::clamp(progress, 0.0f, 1.0f)`.
2.  **Pixel Format Conversion for Window Surface**:
    *   Window surfaces returned by `SDL_GetWindowSurface(window)` often use `SDL_PIXELFORMAT_BGRX8888` or `SDL_PIXELFORMAT_BGRA8888` rather than `SDL_PIXELFORMAT_RGBA32`.
    *   `SplashScreen::present()` uses `SDL_ConvertPixels(surf->w, surf->h, SDL_PIXELFORMAT_RGBA32, rgba_.data(), surf->w * 4, surf->format, surf->pixels, surf->pitch)` inside `SDL_LockSurface` / `SDL_UnlockSurface`, followed by `SDL_UpdateWindowSurface(window_)`, guaranteeing accurate colors on macOS, Windows, and Linux.
3.  **HiDPI Window Surface Sizing**:
    *   When `SDL_WINDOW_HIGH_PIXEL_DENSITY` is set on the splash window (`768x428` logical points), `SDL_GetWindowSurface(window_)` may have pixel dimensions `surf->w x surf->h` (e.g., `1536x856` on a 2x Retina display). `renderSplashBuffer` renders directly at `(surf->w, surf->h)` so the splash image and progress bar are sharp on HiDPI displays.
4.  **Event-Loop Responsiveness During `SplashScreen::finish()`**:
    *   `SplashScreen::finish()` smoothly animates `progress_` from its current value to `1.0f` over the remaining time up to `durationSeconds_`.
    *   During each `16 ms` tick (`SDL_Delay(16)`), `finish()` pumps the OS event loop via `SDL_PumpEvents()` and checks `SDL_HasEvent(SDL_EVENT_QUIT)` without draining startup events (`SDL_EVENT_DROP_FILE` or `SDL_EVENT_JOYSTICK_ADDED`) meant for `App::run`. If `SDL_EVENT_QUIT` is queued, `finish()` closes the splash window immediately and returns `false` so `App::init` sets `running_ = false` and aborts opening the main window.

## Risks & Mitigations

*   **Risk 1 — Consuming Startup File-Drop or Device Events During Splash**:
    *   *Mitigation*: `SplashScreen` uses `SDL_PumpEvents()` and `SDL_HasEvent(SDL_EVENT_QUIT)` rather than `SDL_PollEvent()`, preserving any `SDL_EVENT_DROP_FILE` (Finder open-with) or `SDL_EVENT_JOYSTICK_ADDED` (SpaceMouse) events in the SDL queue for `App::run()`.
*   **Risk 2 — Premature Window Unhiding or Blank Frame Flash on Transition**:
    *   *Mitigation*: `SDL_WINDOW_FULLSCREEN` is excluded while `showSplash` is true, `App::fitWindowToDisplay` skips `SDL_MaximizeWindow` while `window_` has `SDL_WINDOW_HIDDEN`, and `App::init` clears and swaps the OpenGL backbuffer (`gl::ClearColor(0.07f, 0.07f, 0.085f, 1.0f)`) before calling `SDL_ShowWindow(window_)`.
*   **Risk 3 — Covering OS Keychain Prompts or Blocking Bridge Launches**:
    *   *Mitigation*: The splash window is created with `SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY` (without `SDL_WINDOW_ALWAYS_ON_TOP`) so OS Keychain prompts from `ai::secrets::preload()` are never obscured, and `shouldShowSplash` automatically skips the splash screen when `--blender` or `--run` is used (unless `--splash` is explicitly passed).

## Alternatives Considered

### Alternative A: Render Splash Screen as an ImGui Modal Inside the Main Window

*   **Pros**: Reuses the main window and ImGui draw list.
*   **Cons**: Opens the main application window immediately with its title bar and OS frame; cannot display until after the OpenGL context, renderer, and ImGui fonts are already initialized.
*   **Reason for Rejection**: Violates Goal G1 ("do not open the app until the splash screen is finished").

### Alternative B: Use `SDL_CreateRenderer` on the Splash Window

*   **Pros**: Hardware-accelerated 2D texture blitting.
*   **Cons**: On macOS and Linux, `SDL_CreateRenderer` can create an internal Metal or OpenGL context that interferes with `SDL_GL_MakeCurrent(window_, glContext_)` while `App::init` compiles shaders and uploads textures on the main window.
*   **Reason for Rejection**: `SDL_GetWindowSurface` is completely decoupled from OpenGL state and takes less than 1 ms per frame for a `768x428` window.

--------------------------------------------------------------------------------

## Non-Functional Requirements

*   **Security & Privacy**: Zero network calls or external file reads; decodes only the compiled-in `sf::assets::splashImage` buffer via `Image::decode`.
*   **Performance & Latency**: `renderSplashBuffer` caches the resampled background image once for `(surf->w, surf->h)` and only redraws the bottom progress strip on subsequent `setProgress` calls, keeping per-step CPU cost under `0.2 ms`. Headless and UI test runs bypass the splash screen completely (`0 ms` overhead).
*   **Rollout & Rollback**: Controlled via `AppOptions::noSplash` (`--no-splash`) and `SF_NO_SPLASH=1` environment variable for instant bypass if needed.

--------------------------------------------------------------------------------

## Detailed Implementation Plan

### Target Files to Create/Modify

#### 1. `assets/branding/splash.jpg` (Create)

*   **Type / Scope**: Binary Image Asset (`1024x571` JPEG)
*   **Proposed Changes**: Copy the user-supplied splash artwork into `assets/branding/splash.jpg` so CMake can embed it into the desktop executable.

#### 2. `tools/marks_media_cache.json` (Modify)

*   **Type / Scope**: Test Verification Cache (JSON)
*   **Proposed Changes**: Add the SHA-256 hash entry for `assets/branding/splash.jpg` mapped to `[]` (zero OCR trademark hits, verified via `tesseract` and `tools/check_marks_media.py`) so `ctest -R marks_media` (`--cached-only`) passes.

#### 3. `CMakeLists.txt` (Modify)

*   **Type / Scope**: Build Configuration
*   **Proposed Changes**:
    *   Call `embed_files(${CMAKE_CURRENT_BINARY_DIR}/EmbeddedSplash.cpp sf::assets splashImage ${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/splash.jpg)` inside the `if(SCULPTFORGE_BUILD_APP)` block to generate `EmbeddedSplash.cpp` containing `sf::assets::splashImage` and `sf::assets::splashImage_size`.
    *   Add `src/app/SplashScreen.cpp` and `${CMAKE_CURRENT_BINARY_DIR}/EmbeddedSplash.cpp` to `add_library(sculptforge_actions STATIC ...)`, which already links `SDL3::SDL3` and `sculptforge_appshared` and is linked by both the `sculptforge` desktop executable and `sculptforge_tests`.
    *   Add `tests/test_splash.cpp` to `SCULPTFORGE_TEST_SOURCES` when `SCULPTFORGE_BUILD_APP` is enabled.

#### 4. `src/app/SplashScreen.h` (Create)

*   **Type / Scope**: C++ Header (`sf` namespace)
*   **Proposed Changes**:
    *   Define `struct AppOptions` directly in `src/app/SplashScreen.h` (which only depends on `<string>` and `"core/Image.h"`, avoiding `<imgui.h>` so `sculptforge_actions` and `sculptforge_tests` compile without ImGui include paths), including `bool splash = false;`, `bool noSplash = false;`, `float splashSeconds = 2.5f;`, and `bool automated = false;`.
    *   Declare `bool shouldShowSplash(const AppOptions& options, bool headless, bool sharedWindow, bool automated);` (where `sharedWindow` is true when `App` reuses a pre-created `SDL_Window*` in `--ui-serve` warm server mode).
    *   Declare `bool decodeSplashImage(Image& out, std::string& error);`
    *   Declare `bool renderSplashBuffer(const Image& art, int width, int height, float progress, std::vector<uint8_t>& outRgba, std::vector<uint8_t>* cachedBaseRgba = nullptr);`
    *   Declare `class SplashScreen` with:
        *   `SplashScreen() = default; ~SplashScreen();`
        *   `bool open(float durationSeconds = 2.5f);`
        *   `void setProgress(float fraction);`
        *   `bool finish();`
        *   `void close();`
        *   `bool isOpen() const;`
        *   `float progress() const;`

#### 5. `src/app/SplashScreen.cpp` (Create)

*   **Type / Scope**: C++ Implementation (`sf` namespace)
*   **Proposed Changes**:
    *   Implement `shouldShowSplash(const AppOptions& options, bool headless, bool sharedWindow, bool automated)` checking `options.noSplash`, `SF_NO_SPLASH`, `options.splash`, `SF_SPLASH`, `headless`, `sharedWindow`, `automated`, `options.blender`, `!options.script.empty()`, `!options.demo.empty()`, `!options.uiTest.empty()`, `!options.recordPath.empty()`, `!options.renderPath.empty()`, `!options.timelapsePath.empty()`, `options.benchmark`, `!options.benchmarkScript.empty()`, `options.measureIdle`, and `options.measureStroke`.
    *   Implement `decodeSplashImage` decoding `sf::assets::splashImage` (`sf::assets::splashImage_size`).
    *   Implement `renderSplashBuffer` resampling `art` to `width x height` (caching the resampled base image when `cachedBaseRgba` is provided) and painting the bottom progress bar overlay (`#0e0e11` footer band, `#1a191f` track, `#f28c3c` progress fill with `#ffb877` highlight, `#363440` border).
    *   Implement `SplashScreen::open`, `SplashScreen::setProgress`, `SplashScreen::finish`, and `SplashScreen::close` using `SDL_CreateWindow("SculptForge", 768, 428, SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY)`, `SDL_GetWindowSurface`, `SDL_ConvertPixels`, `SDL_UpdateWindowSurface`, `SDL_PumpEvents()`, and `SDL_HasEvent(SDL_EVENT_QUIT)`.

#### 6. `src/app/App.h` (Modify)

*   **Type / Scope**: C++ Header (`sf::App`)
*   **Proposed Changes**:
    *   Include `"app/SplashScreen.h"` to provide `sf::AppOptions` and `sf::SplashScreen` to all callers of `App.h`.

#### 7. `src/app/App.cpp` (Modify)

*   **Type / Scope**: C++ Application Initialization (`App::init` and `App::fitWindowToDisplay`)
*   **Proposed Changes**:
    *   Include `"app/SplashScreen.h"`.
    *   After `SDL_Init(SDL_INIT_VIDEO)`, determine `const bool showSplash = shouldShowSplash(options, headless, sharedWindow_ != nullptr, options.automated);`.
    *   If `showSplash`, instantiate `SplashScreen splash; splash.open(options.splashSeconds);`.
    *   Add `SDL_WINDOW_HIDDEN` to `flags` when `headless || showSplash` is true, and omit `SDL_WINDOW_FULLSCREEN` from `flags` while `showSplash` is true so `window_` does not appear on screen while `splash` is open.
    *   In `App::fitWindowToDisplay`, guard `SDL_MaximizeWindow(window_)` with `!(SDL_GetWindowFlags(window_) & SDL_WINDOW_HIDDEN)`.
    *   Call `splash.setProgress(...)` across `App::init` milestones (`0.18f`, `0.38f`, `0.58f`, `0.75f`, `0.88f`, `0.95f`).
    *   In the `options.script` execution loop (`--run`), handle `st.command == "quit" || st.command == "exit"` by setting `running_ = false`.
    *   Before `initialised_ = true; return true;`, if `showSplash`: call `if (!splash.finish()) running_ = false;`, then if `running_` clear and swap the GL backbuffer, call `SDL_ShowWindow(window_)`, apply `options.fullscreen` or `fitWindowToDisplay(saved)` + maximize/fullscreen restoration, call `SDL_RaiseWindow(window_)`, `SDL_SyncWindow(window_)`, and refresh `windowFocused_`.

#### 8. `src/app/main.cpp` (Modify)

*   **Type / Scope**: CLI Argument Parser (`usage` and `runMain`)
*   **Proposed Changes**:
    *   Document `--no-splash`, `--splash`, and `--splash-seconds S` in `usage()`.
    *   Parse `--no-splash` (`options.noSplash = true`), `--splash` (`options.splash = true`), and `--splash-seconds` (`options.splashSeconds = std::clamp(float(std::atof(next().c_str())), 0.1f, 30.0f)`).
    *   Set `options.automated = automated;` before calling `app.init(options)` so agent/CI runs bypass the splash screen by default unless `--splash` or `SF_SPLASH=1` is explicitly passed.

#### 9. `tests/test_splash.cpp` (Create)

*   **Type / Scope**: C++ Unit Test (`sculptforge_tests`)
*   **Proposed Changes**:
    *   Guard tests with `#if defined(SF_TEST_ACTIONS)` so builds with `-DSCULPTFORGE_BUILD_APP=OFF` remain unaffected.
    *   `TEST(splash_decode_and_render)`: Verifies `decodeSplashImage` succeeds, decodes the `1024x571` image, and `renderSplashBuffer` produces non-empty RGBA pixels with the progress bar fill advancing monotonically as `progress` goes from `0.0f` to `0.5f` to `1.0f`.
    *   `TEST(splash_startup_gating_rules)`: Verifies `shouldShowSplash` returns `true` for normal interactive startup, `false` when `headless`, `sharedWindow`, `automated`, `noSplash`, `blender`, `script`, `demo`, `benchmark`, `uiTest`, `measureIdle`, or `measureStroke` is active, and `true` when `options.splash` overrides automated mode.

### Verification Plan

*   **Compilation/Build**: `cmake -B build && cmake --build build -j`
*   **Unit Tests**: `./build/sculptforge_tests splash_decode_and_render splash_startup_gating_rules`
*   **Static & Repository Checks**:
    *   `python3 tools/check_marks.py`
    *   `python3 tools/check_marks_media.py --cached-only`
    *   `python3 tools/i18n/coverage.py --check`
    *   `python3 tools/i18n/coverage.py --raw-check`
    *   `python3 tests/check_native_dialogs.py`
    *   `python3 tools/dev/icons_used.py --check`
    *   `python3 tools/dev/check_literals.py`
*   **End-to-End Splash Execution**: Run `./build/SculptForge.app/Contents/MacOS/SculptForge --splash --splash-seconds 0.5 --run "quit"` (or headless/windowed test) to verify the splash screen opens, advances to 100%, closes before the main window opens, and exits cleanly with code `0`.

--------------------------------------------------------------------------------

*Drafted with EGM*
