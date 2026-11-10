// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Startup splash screen: a borderless, centered SDL window shown before the
// main application window opens. The main OpenGL window stays hidden
// (SDL_WINDOW_HIDDEN) while subsystems initialize and while the splash screen
// completes its display duration; only after finish() closes the splash window
// is the main window shown on screen.
#pragma once

#include "core/Image.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct AppOptions {
    std::string openPath;   // model or image to open at start
    bool blender = false;   // started by the Blender add-on: load what it sent (--blender)
    std::string script;     // commands to run after start-up (same language as sculptforge-cli)
    std::string renderPath; // headless: render a PNG and exit
    int width = 1024, height = 1024;
    float yawDegrees = -30.0f, pitchDegrees = -15.0f;
    float zoom = 1.0f; // headless: camera this many times closer than the framed view
    int turntable = 0; // headless: render N frames around the model (<path>_000.png ...)
    bool wireframe = false;
    bool facegroups = false; // headless: colour each face group
    bool hideMesh = false; // headless: show only the splats
    std::string demo;       // scripted demo to play (see Demo.h)
    std::string recordPath; // record the session to this .mp4 (needs ffmpeg) and exit
    std::string uiRecordPath; // --ui-test: also record every frame, paced to real time, to this .mp4 (launch clips)
    std::string demoAsset;  // file the demo opens (image or model)
    std::string demoOutput; // where the demo saves its result (optional)
    int fps = 30;
    int supersample = 1; // recording: render this many times larger, then downsample (crisp text, room to zoom)
    std::string uiTest;   // run this .sfui UI test script headless and exit (see UiTest.cpp)
    std::string uiOutDir; // where UI tests save screenshots
    bool fullscreen = false; // start fullscreen (F11 toggles)
    float measureStroke = 0.0f; // --measure-stroke S: a scripted fast circle stroke, report its path and frame times
    float measureStrokeRadius = 0.0f; // --measure-stroke S,R: the brush radius in screen pixels
    float measureIdle = 0.0f; // --measure-idle S: after a settling pause, count the frames drawn in S idle seconds, exit
    // --timelapse out.mp4: render the opened project's history (its .sfhist) as a turntable timelapse and exit.
    std::string timelapsePath;
    float timelapseSeconds = 20.0f;
    // --benchmark: proof mode (ProofMode.cpp), then exit. Sizes in millions of triangles (empty: 5, 20, 33 and
    // 100 when memory allows); outputs default to the current folder.
    bool benchmark = false;
    std::string benchmarkScene; // --benchmark standard-15m | standard-33m | standard | standard-1b: a standard scene (ProofMode.h)
    std::string benchmarkSizes, benchmarkOut, benchmarkVideo;
    bool benchmarkNoVideo = false, benchmarkVsync = false;
    float benchmarkStrokeSeconds = 6.0f;
    // --benchmark-script take.sfui: the take (a UI test script's input) played in real time with the benchmark HUD,
    // video and a per-frame CSV (BenchTake.cpp); benchmark is set too (isolated settings, --size 1920x1080).
    std::string benchmarkScript;
    bool splash = false;          // --splash: show the startup splash screen even in automated runs
    bool noSplash = false;        // --no-splash: skip the startup splash screen
    float splashSeconds = 2.5f;   // --splash-seconds S: minimum splash screen display time
    bool automated = false;       // set by main when running in an automated agent/CI environment
};

// Returns true when the startup splash screen should be shown for this run.
// Interactive desktop launches show the splash screen by default; headless
// renders, recordings, UI tests, warm servers, benchmarks, and automated runs
// skip it unless explicitly requested via --splash or SF_SPLASH=1.
bool shouldShowSplash(const AppOptions& options, bool headless, bool sharedWindow, bool automated);

// Decodes the compiled-in splash artwork (assets/branding/splash.jpg).
bool decodeSplashImage(Image& out, std::string& error);

// Renders the splash artwork resampled to (width x height) RGBA8 with the
// bottom progress bar overlay for `progress` in [0, 1].
// If `cachedBaseRgba` already holds `width * height * 4` resampled artwork
// bytes, it is reused to avoid resampling the image on every progress tick.
bool renderSplashBuffer(const Image& art, int width, int height, float progress,
                        std::vector<uint8_t>& outRgba,
                        std::vector<uint8_t>* cachedBaseRgba = nullptr);

class SplashScreen {
public:
    SplashScreen() = default;
    ~SplashScreen();

    SplashScreen(const SplashScreen&) = delete;
    SplashScreen& operator=(const SplashScreen&) = delete;

    // Opens the borderless centered splash window and draws the initial frame.
    bool open(float durationSeconds = 2.5f);

    // Updates the progress bar (0..1) and refreshes the window surface.
    void setProgress(float fraction);

    // Smoothly completes the splash screen up to `durationSeconds_`, closes
    // the splash window, and returns true (or false if SDL_EVENT_QUIT arrived).
    bool finish();

    // Destroys the splash window immediately if still open.
    void close();

    bool isOpen() const { return window_ != nullptr; }
    float progress() const { return progress_; }

private:
    void present(float displayProgress);

    SDL_Window* window_ = nullptr;
    Image art_;
    std::vector<uint8_t> baseRgba_;
    std::vector<uint8_t> frameRgba_;
    int cachedW_ = 0;
    int cachedH_ = 0;
    float progress_ = 0.0f;
    float durationSeconds_ = 2.5f;
    std::chrono::steady_clock::time_point openedAt_{};
};

} // namespace sf
