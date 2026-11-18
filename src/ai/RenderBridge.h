// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The render bridge (File > Render with...): a still of the scene from a Hydra render delegate the artist or studio
// already has. Out of process: the app writes a job folder (scene.usda, the normal USD export of the visible Parts,
// and render.usda, which sublayers it and adds the camera, a light rig and a RenderSettings prim) and runs the user's
// own render helper on it, OpenUSD's `usdrecord` (or any program with the same command line). The helper loads Hydra
// and the delegate in its own process, writes the image and exits: delegate code never runs inside SculptForge.
// No sockets, pipes or IPC: spawn, files, exit code. docs/design/render-bridge.md has the design and the security
// review; the helper is only ever one the user chose (and confirmed: its SHA-256 is remembered, a changed file asks
// again), started with a cleaned environment, and a studio policy ("render": {"bridge": "never"}) turns it all off.
#pragma once

#include "ai/Process.h"
#include "core/Commands.h"
#include "core/Image.h"
#include "io/MeshObject.h"

#include <glm/glm.hpp>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace sf::renderbridge {

// The camera to render from, as the viewport has it (app: from render/Camera.h; headless: framedView).
struct View {
    glm::mat4 cameraToWorld{1.0f}; // inverse of the view matrix: the camera looks down its -Z, Y up
    float fovY = 0.61f;            // vertical field of view, radians (perspective)
    bool ortho = false;
    float orthoHalfHeight = 1.0f;  // half the view's height in scene units (orthographic)
    glm::vec2 shift{0.0f};         // lens shift in normalised device units (where the target lands off centre)
    float nearClip = 0.01f, farClip = 1000.0f;
};

// A three-quarter view from the front, slightly above, that fits `min`..`max` into a frame of `aspect`.
View framedView(const glm::vec3& min, const glm::vec3& max, float aspect);

struct Request {
    std::string renderer;  // as the helper lists it; "" = the helper's default (Storm for usdrecord)
    int width = 1920, height = 1080;
    std::string format = "png"; // "png" or "exr"
    bool studioLight = true;    // our key / fill / rim + dome; off = the helper's own camera light
    View view;
};

// What `helper --help` says it can do.
struct HelperInfo {
    bool ok = false;
    std::string error;
    std::vector<std::string> renderers; // argparse's --renderer {A,B} choices
    bool renderSettings = false;        // --renderSettingsPrimPath (OpenUSD 23.11+)
    bool disableCameraLight = false;    // --disableCameraLight
};

// --- Pieces (pure; tests/test_render_bridge.cpp) ---

// render.usda: sublayers `sceneFile` (relative to it) and adds /SculptForgeRender/{Camera, Key, Fill, Rim, Sky, Settings}.
std::string stageText(const Request& r, const std::string& sceneFile);
constexpr const char* kCameraPath = "/SculptForgeRender/Camera";
constexpr const char* kSettingsPath = "/SculptForgeRender/Settings";
HelperInfo parseHelp(const std::string& helpText);
// A renderer name we put on a command line: letters, digits, space, '_', '.', '-' (1-64 of them).
bool validRendererName(const std::string& name);
// The helper's arguments (after the program) for one render.
std::vector<std::string> helperArgs(const Request& r, const HelperInfo& info, const std::string& stage,
                                    const std::string& out);
// What to start for `helper` (absolute path): itself; on Windows a Python script (a "#!...python" first line, no
// .exe) as `python <script>`; .cmd / .bat refused (cmd.exe re-parses arguments). False + `error` when it can't run.
bool launchFor(const std::string& helper, std::string& program, std::vector<std::string>& prefixArgs, std::string& error);
// ENV without what is ours or secret: SF_* / SCULPTFORGE_* (our test and policy switches), names with TOKEN, SECRET,
// PASSWORD, PASSWD, CREDENTIAL, or ending in _KEY / API_KEY (AI keys a user exported). Licence and USD variables stay.
std::vector<std::string> cleanEnvironment(const std::vector<std::string>& env);
// ENV with a self-contained OpenUSD install's own folders first when `helper` is <root>/bin/usdrecord of one (a
// <root>/lib/python/pxr folder: the prebuilt downloads, which set these only in their own .bat/.sh wrappers): PATH gets
// <root>/python, python/bin, pip-packages/bin, lib, plugin/usd, bin; PYTHONPATH <root>/lib/python, pip-packages;
// LD_LIBRARY_PATH (Linux) <root>/lib; PXR_MTLX_STDLIB_SEARCH_PATHS <root>/libraries when unset. Only folders that exist,
// all inside the confirmed helper's install. Otherwise ENV unchanged (an install the user set up on PATH already).
std::vector<std::string> installEnvironment(const std::vector<std::string>& env, const std::string& helper);
// Lowercase SHA-256 of a file ("" when it can't be read). Files over 512 MB hash their size and first 64 MB.
std::string fileFingerprint(const std::string& path);
// Each render's job folder lives under this (created 0700): %LOCALAPPDATA%\SculptForge\render-bridge,
// ~/Library/Caches/SculptForge/render-bridge, $XDG_CACHE_HOME (~/.cache)/SculptForge/render-bridge.
// SF_RENDER_BRIDGE_DIR overrides it (tests).
std::string jobRoot();
// `usdrecord` on PATH (".exe" or the script on Windows), "" when none. Only a suggestion: nothing runs until the
// user confirms it.
std::string foundHelper();
// The still for showing: PNG (stb_image) or EXR (tinyexr, linear -> sRGB, clamped). Refuses over 16384 px a side.
bool loadImage(const std::string& path, Image& out, std::string& error);
// The last `lines` lines of a log file.
std::string logTail(const std::string& path, int lines = 6);

// --- Settings (render_bridge.json in the app's data folder, shared by the app and sculptforge-cli) ---

struct Settings {
    std::string helper;        // absolute path; "" = not set up (the feature is off until it is)
    std::string helperSha256;  // the fingerprint the user confirmed
    std::string editor;        // image editor program; "" = the desktop's default
    std::string rendersDir;    // where finished stills are kept; "" = defaultRendersDir()
    std::string renderer;
    int width = 1920, height = 1080;
    std::string format = "png";
    bool studioLight = true;
    bool openInEditor = false;
    bool matchView = true;     // the size follows the viewport's shape (height from width)

    static Settings load(const std::string& path);
    bool save(const std::string& path) const;
    // The helper is set and its file is the one the user confirmed.
    bool helperTrusted() const;
};
std::string defaultSettingsPath();
std::string defaultRendersDir(); // ~/Pictures/SculptForge Renders (the platform's pictures folder), else <data>/Renders

// Probes `helper --help` (a few seconds at most; killed after 20 s). Runs only the given, trusted helper.
HelperInfo probe(const std::string& helper, const std::vector<std::string>& env);

// --- A render ---

enum class State { Idle, Running, Done, Failed, Cancelled };

class Job {
public:
    ~Job(); // stops a render still running
    // Writes the job folder from copies of `meshes` (scene.usda through io/UsdIO.h, its textures beside it) and starts
    // the helper, both on a worker: it returns at once. False + `error` when it can't (nothing is left running).
    bool start(const std::string& helper, const HelperInfo& info, const Request& r, const std::vector<MeshObjectRef>& meshes,
               const std::string& name, const std::vector<std::string>& env, std::string& error);
    // Call once a frame while running: notices the end (or the 30-minute limit) and checks the result.
    State poll();
    void cancel();
    State state() const { return state_; }
    double elapsedSeconds() const;
    const std::string& image() const { return image_; }   // the helper's output (in the job folder) once Done
    const std::string& error() const { return error_; }   // why it Failed, with the log's last lines
    const std::string& folder() const { return folder_; }
    double timeLimitSeconds = 30.0 * 60.0;

private:
    std::unique_ptr<ai::ChildProcess> child_;
    std::future<std::string> prepare_; // writing the scene and starting the helper: "" or why it failed
    State state_ = State::Idle;
    std::chrono::steady_clock::time_point started_{}, ended_{};
    std::string folder_, image_, log_, error_;
};

// Copies a finished still into `dir` as "<name>_0001.<ext>" (the first free number). Returns the new path, "" + error.
std::string keepRender(const std::string& image, const std::string& dir, const std::string& name, std::string& error);

// render_bridge and render_bridge_renderers for sculptforge-cli: renders in the foreground with the saved settings,
// from framedView of the visible Parts. `overrideHelper` is --render-helper PATH (the user's choice for that run, no
// confirmation needed); "" = the helper confirmed in the app.
void registerCommands(CommandRegistry& registry, std::function<Settings()> settings, std::string overrideHelper = {});

} // namespace sf::renderbridge
