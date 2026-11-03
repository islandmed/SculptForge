// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"
#include "app/Bench.h"
#include "app/Calibration.h"
#include "app/CrashLog.h"
#include "ai/AiPolicy.h"
#include "core/FocusDetail.h"
#include "core/Pack.h"

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::puts(
        "Usage: sculptforge [model / splats / image file] [options]\n"
        "\n"
        "  --run \"<commands>\"     Run commands at start-up (see sculptforge-cli --list)\n"
        "  --render out.png         Render headless and exit (thumbnails, review sheets)\n"
        "  --turntable N            With --render: N frames around the model (out_000.png ...)\n"
        "  --size WxH               Render size (default 1024x1024)\n"
        "  --measure-idle S         Sit idle, print the frames drawn per second over S seconds, exit\n"
        "                           (SF_CONTINUOUS_REDRAW=1 draws every frame, as before 0.9.5)\n"
        "  --measure-stroke S[,R]   Draw a fast scripted circle stroke for S seconds (brush radius R px),\n"
        "                           e.g. after --run \"new_sphere\"; print how far the dabs left the circle and the frame times, exit\n"
        "                           (SF_STROKE_IMMEDIATE=1 places every dab at once, as before 0.9.6)\n"
        "  --view YAW,PITCH         Camera angles in degrees (default -30,-15)\n"
        "  --zoom F                 Move the camera F times closer than the framed view (default 1)\n"
        "  --wireframe              Render with wireframe overlay\n"
        "  --polygroups             Render with face group colours\n"
        "  --hide-mesh              Render only the Gaussian splats\n"
        "  --demo NAME              Play a scripted demo (sculpt, image-to-3d, brush-lab, game-ready, splats)\n"
        "  --record out.mp4         Record the demo offscreen to a video (needs ffmpeg) and exit\n"
        "  --demo-asset FILE        Image or model the demo opens\n"
        "  --demo-output FILE       Save the demo's result (e.g. creature.glb)\n"
        "  --fps N                  Recording frame rate (default 30)\n"
        "  --supersample N          Render recordings N times larger and downsample (sharper, allows zoom-ins)\n"
        "  --ui-test FILE.sfui      Run a UI test script headless and exit (0 = pass); settings are isolated\n"
        "  --ui-out DIR             Where UI tests save screenshots\n"
        "  --fullscreen             Start filling the screen (F11 toggles)\n"
        "  --no-ai                  Start with every AI feature off and locked (as SF_AI=off)\n"
        "  --no-packs               Safe mode: start without any installed pack (Library > Packs)\n"
        "  --blender                Load what the Blender add-on sent (it starts SculptForge this way)\n"
        "  --bench                  Run the performance benchmark (with GPU upload and orbit timings) and exit\n"
        "  --bench-sizes 4,16,32    Benchmark sizes in millions of triangles\n"
        "  --bench-dabs N           Dabs per benchmark stroke (default 40)\n"
        "  --bench-brushes TEXT     Only the benchmark brushes whose name contains TEXT (e.g. large)\n"
        "  --bench-no-project       Skip the project save / open (it needs twice the memory)\n"
        "  --bench-view             Benchmark only what the viewport costs (uploads, orbit)\n"
        "  --bench-dyntopo          Benchmark dynamic topology strokes (e.g. --bench-sizes 0.25,1,4)\n"
        "  --bench-micro            Benchmark micro-displacement levels (sizes = micro triangles, e.g. 33,100,300)\n"
        "  --bench-micro-level N    Micro level for --bench-micro (default 4: 256 per base triangle)\n"
        "  --bench-out FILE         Write the benchmark report (Markdown) to FILE\n"
        "  --calibrate              Measure this computer (GPU, CPU, memory; about 2-3 s), print the hardware\n"
        "                           profile as JSON and save it (add --calibrate-quick, --calibrate-no-save)\n"
        "  --timelapse out.mp4      Render the opened project's history (saved beside it as .sfhist) as a\n"
        "                           turntable timelapse and exit (--size 3840x2160 for 4K, --fps N)\n"
        "  --timelapse-seconds S    Length of the timelapse (default 20)\n"
        "  --benchmark              Proof mode: a scripted stroke on 5M / 20M / 33M (and 100M when memory\n"
        "                           allows) triangle meshes through the input handlers, with a live HUD; writes a\n"
        "                           JSON result and an MP4 of the run and exits (docs/benchmark.md)\n"
        "  --benchmark standard-15m The standard scene: a 15M triangle head, Clay S/M/L, Move, Smooth and a 240 px\n"
        "                           brush (standard-33m: 33M; standard: both). The numbers on sculptforge.app/benchmarks\n"
        "  --benchmark standard-1b  Experimental Focus Detail: a billion micro triangle head streamed from disk\n"
        "  --focus-detail FILE      Experimental: open a Focus Detail file (.sfdetail)\n"
        "  --benchmark-sizes 5,20   Proof mode mesh sizes in millions of triangles\n"
        "  --benchmark-out FILE     Where the JSON result goes (default: sculptforge-benchmark-<time>.json here)\n"
        "  --benchmark-video FILE   Where the MP4 goes; --benchmark-no-video records none\n"
        "  --benchmark-vsync        Keep vertical sync on (default off: frame times are not capped)\n"
        "  --benchmark-seconds S    Length of each scripted stroke (default 6)\n"
        "  --log-dir DIR            Write sculptforge.log to DIR instead of the user data folder\n"
        "  --crash-test KIND        Crash on purpose (segv, abort or throw) to test the crash note in the log");
}

// UI tests must never read or change the user's real settings, recent files or
// autosaves: point the per-user data directories at a fresh temporary folder.
std::filesystem::path g_isolatedDir; // isolateSettings' folder, removed when the run ends well

void isolateSettings() {
    if (!g_isolatedDir.empty()) return; // (once: --run with --measure-stroke asks twice)
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) /
                     ("sculptforge-ui-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir, ec);
    g_isolatedDir = dir;
    const std::string d = dir.string();
#if defined(_WIN32)
    _putenv_s("APPDATA", d.c_str());
#else
    setenv("XDG_DATA_HOME", d.c_str(), 1);
    setenv("XDG_CONFIG_HOME", d.c_str(), 1);
    setenv("HOST_XDG_DATA_HOME", d.c_str(), 1); // in a Flatpak, the Blender bridge folder (io/BlenderBridge.h)
#if defined(__APPLE__)
    setenv("HOME", d.c_str(), 1);
#endif
#endif
    for (const char* key : {"ANTHROPIC_API_KEY", "OPENAI_API_KEY", "HF_TOKEN", "HUGGING_FACE_HUB_TOKEN", "FAL_KEY", "MESHY_API_KEY"}) {
#if defined(_WIN32)
        _putenv_s(key, "");
#else
        unsetenv(key);
#endif
    }
}

// Runs by an agent or a script (SDL_APP_ID=sculptforge-agent, as ~/Work/CLAUDE.md asks for agent windows, or a
// Claude Code shell) never touch the user's settings, log or hardware profile: a benchmark or render started there
// wrote into the user's sculptforge.log and pushed the session's log to sculptforge-previous.log.
// SF_ISOLATE=1 isolates any run, SF_ISOLATE=0 none of the automatic ones.
bool automatedRun() {
    const char* force = std::getenv("SF_ISOLATE");
    if (force && *force) return *force != '0';
    const char* appId = std::getenv("SDL_APP_ID");
    const char* claude = std::getenv("CLAUDECODE");
    return (appId && std::string(appId) == "sculptforge-agent") || (claude && *claude);
}

int finishIsolated(int code) {
    // The isolated folder (settings, the log, autosaves: a benchmark's 33M mesh is over a gigabyte) goes with a run
    // that passed; a failed one keeps it, log included, to look at.
    if (code == 0 && !g_isolatedDir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(g_isolatedDir, ec);
    }
    return code;
}

} // namespace

// --crash-test: crash the way real bugs do, after the log has started.
[[noreturn]] static void crashOnPurpose(const std::string& kind) {
    sf::crashlog::quietSystemCrashDialog();
    sf::crashlog::action("crash_test " + kind);
    if (kind == "abort") std::abort();
    if (kind == "throw") throw std::runtime_error("crash test");
    volatile int* nowhere = nullptr;
    *nowhere = 1;
    std::abort();
}

int main(int argc, char** argv) {
    sf::AppOptions options;
    std::string logDir, crashTest;
    bool sizeGiven = false;
    sf::BenchCommandLine bench;
    bool calibrate = false, calibrateQuick = false, calibrateSave = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (sf::parseBenchArg(argc, argv, i, bench)) continue;
        if (a == "--calibrate") calibrate = true;
        else if (a == "--calibrate-quick") calibrate = calibrateQuick = true;
        else if (a == "--calibrate-no-save") calibrateSave = false;
        else if (a == "--run") options.script += next() + "\n";
        else if (a == "--render") options.renderPath = next();
        else if (a == "--turntable") options.turntable = std::atoi(next().c_str());
        else if (a == "--size") sizeGiven = std::sscanf(next().c_str(), "%dx%d", &options.width, &options.height) == 2;
        else if (a == "--view") std::sscanf(next().c_str(), "%f,%f", &options.yawDegrees, &options.pitchDegrees);
        else if (a == "--zoom") options.zoom = std::max(0.05f, float(std::atof(next().c_str())));
        else if (a == "--wireframe") options.wireframe = true;
        else if (a == "--polygroups") options.polygroups = true;
        else if (a == "--hide-mesh") options.hideMesh = true;
        else if (a == "--demo") options.demo = next();
        else if (a == "--record") options.recordPath = next();
        else if (a == "--demo-asset") options.demoAsset = next();
        else if (a == "--demo-output") options.demoOutput = next();
        else if (a == "--fps") options.fps = std::atoi(next().c_str());
        else if (a == "--supersample") options.supersample = std::clamp(std::atoi(next().c_str()), 1, 4);
        else if (a == "--ui-test") options.uiTest = next();
        else if (a == "--fullscreen") options.fullscreen = true;
        else if (a == "--no-ai") sf::ai::setAiForcedOff(true); // as SF_AI=off (ai/AiPolicy.h)
        else if (a == "--no-packs") sf::setPacksSafeMode(true); // safe mode: no installed pack is loaded
        else if (a == "--blender") options.blender = true;
        else if (a == "--ui-out") options.uiOutDir = next();
        else if (a == "--log-dir") logDir = next();
        else if (a == "--crash-test") crashTest = next();
        else if (a == "--measure-idle") options.measureIdle = std::max(0.5f, float(std::atof(next().c_str())));
        else if (a == "--measure-stroke") {
            const std::string v = next(); // S or S,RADIUS
            options.measureStroke = std::max(0.5f, float(std::atof(v.c_str())));
            if (const size_t comma = v.find(','); comma != std::string::npos)
                options.measureStrokeRadius = float(std::atof(v.c_str() + comma + 1));
        }
        else if (a == "--timelapse") options.timelapsePath = next();
        else if (a == "--timelapse-seconds") options.timelapseSeconds = std::max(2.0f, float(std::atof(next().c_str())));
        else if (a == "--benchmark") {
            options.benchmark = true;
            // An optional scene name: sculptforge --benchmark standard-15m
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("standard", 0) == 0) {
                options.benchmarkScene = argv[++i];
                if (options.benchmarkScene != "standard" && options.benchmarkScene != "standard-15m" &&
                    options.benchmarkScene != "standard-33m" && options.benchmarkScene != "standard-1b") {
                    std::fprintf(stderr, "Unknown benchmark scene %s (standard-15m, standard-33m, standard or standard-1b)\n", argv[i]);
                    return 1;
                }
            }
        }
        else if (a == "--focus-detail") options.openPath = next(); // a .sfdetail file (prototype)
        else if (a == "--benchmark-sizes") options.benchmarkSizes = next();
        else if (a == "--benchmark-out") options.benchmarkOut = next();
        else if (a == "--benchmark-video") options.benchmarkVideo = next();
        else if (a == "--benchmark-no-video") options.benchmarkNoVideo = true;
        else if (a == "--benchmark-vsync") options.benchmarkVsync = true;
        else if (a == "--benchmark-seconds") options.benchmarkStrokeSeconds = std::max(1.0f, float(std::atof(next().c_str())));
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (!a.empty() && a[0] != '-') options.openPath = a;
        else {
            std::fprintf(stderr, "Unknown option %s\n", a.c_str());
            usage();
            return 1;
        }
    }
    if (automatedRun()) isolateSettings();
    if (bench.run) return finishIsolated(sf::runAppBenchmark(bench));
    if (calibrate) return finishIsolated(sf::runCalibrateCommandLine(calibrateQuick, calibrateSave));
    if (!options.uiTest.empty()) {
        if (!sizeGiven) options.width = 1600, options.height = 960;
        isolateSettings();
    }
    if (options.benchmarkScene == "standard-1b") { // the asset first (written once, a few seconds), before any window
#if defined(_WIN32)
        std::fprintf(stderr, "standard-1b (experimental Focus Detail) needs Linux or macOS for now\n");
        return 1;
#endif
        std::string error;
        if (sf::focusStandard1bPath(error, [](const std::string& s) { std::fprintf(stderr, "%s\n", s.c_str()); }).empty()) {
            std::fprintf(stderr, "standard-1b: %s\n", error.c_str());
            return 1;
        }
    }
    if (options.benchmark && !sizeGiven) options.width = 1920, options.height = 1080;
    if (options.benchmark || !options.timelapsePath.empty()) isolateSettings(); // the user's settings and files untouched
    if (!options.timelapsePath.empty() && !sizeGiven) options.width = 1920, options.height = 1080;
    if (options.measureIdle > 0.0f || options.measureStroke > 0.0f) isolateSettings(); // a fresh start, no restore prompt, the user's files untouched
    if (!options.recordPath.empty() && options.supersample > 1) { // the app renders large; the video keeps --size
        options.width *= options.supersample;
        options.height *= options.supersample;
    }
    sf::crashlog::start(logDir); // after isolateSettings: a UI test logs into its own folder
    if (!crashTest.empty()) crashOnPurpose(crashTest);
    sf::App app;
    if (!app.init(options)) {
        app.shutdown();
        sf::crashlog::stop();
        return 1;
    }
    int code = app.run();
    app.shutdown();
    sf::crashlog::stop();
    return finishIsolated(code);
}
