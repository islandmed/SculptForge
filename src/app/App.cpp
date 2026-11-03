// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/App.h"
#include "core/LayerStack.h"
#include "app/ShellQuote.h"
#include "app/SpaceCamera.h"
#include "app/UiScale.h"
#include "app/AirStroke.h"
#include "app/AsyncStroke.h"
#include "app/Calibration.h"
#include "app/CrashLog.h"

#include "ai/AiCommands.h"
#include "app/Demo.h"
#include "app/Shell.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <imgui_internal.h>
#include "ai/Generator3D.h"
#include "core/Micro.h"
#include "core/Polypaint.h"
#include "core/EnginePresets.h"
#include "link/UnrealLink.h"
#include "core/BrushLibrary.h"
#include "core/Benchmark.h"
#include "core/Decimate.h"
#include "core/ImageTo3D.h"
#include "core/Project.h"
#include "core/Script.h"
#include "io/BlenderBridge.h"
#include "render/GL.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <functional>
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <memory>
#include <thread>

namespace sf {

namespace assets {
extern const unsigned char fontRegular[];
extern const unsigned long long fontRegular_size;
extern const unsigned char fontLucide[];
extern const unsigned long long fontLucide_size;
extern const unsigned char fontSemiBold[];
extern const unsigned long long fontSemiBold_size;
extern const unsigned char fontMono[];
extern const unsigned long long fontMono_size;
extern const unsigned char fontMonoBold[];
extern const unsigned long long fontMonoBold_size;
extern const unsigned char appIcon[];
extern const unsigned long long appIcon_size;
} // namespace assets

namespace {

const SDL_DialogFileFilter kObjFilters[] = {{"Wavefront OBJ", "obj"}, {"All files", "*"}};
const SDL_DialogFileFilter kGlbFilters[] = {{"glTF binary", "glb"}, {"All files", "*"}};
const SDL_DialogFileFilter kBrushFilters[] = {{"SculptForge brush", "sfbrush"}, {"All files", "*"}};
const SDL_DialogFileFilter kModelFilters[] = {{"Projects, models and splats", "sfproj;obj;fbx;glb;gltf;ply;stl;GoZ;goz;ztl;ZTL;splat;usda;usd;usdz"}, {"All files", "*"}};
const SDL_DialogFileFilter kFbxFilters[] = {{"FBX", "fbx"}, {"All files", "*"}};
const SDL_DialogFileFilter kUsdFilters[] = {{"USDZ package", "usdz"}, {"USD text", "usda"}};
const SDL_DialogFileFilter kSplatFilters[] = {{"Gaussian splats (PLY)", "ply"}, {"glTF splats", "glb"}, {"OpenUSD", "usda;usdz"}};
const SDL_DialogFileFilter kImageFilters[] = {{"Images", "png;jpg;jpeg;bmp;tga;psd;gif"}, {"All files", "*"}};
const SDL_DialogFileFilter kWorkflowFilters[] = {{"ComfyUI API workflow", "json"}, {"All files", "*"}};

const char* kExamplePrompts[] = {
    "Sharp wrinkles that fade out at the ends",
    "Overlapping dragon scales",
    "Weathered, chipped stone edges",
    "Dry cracked mud",
    "Soft clay buildup like a thumb smear",
};

bool ctrlDown(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }

} // namespace

App::App() = default;
App::~App() = default;

bool App::init(const AppOptions& options) {
    options_ = options;
    const bool recording = !options.recordPath.empty() || !options.uiTest.empty();
    const bool headless = !options.renderPath.empty() || recording || !options.timelapsePath.empty();
    // UI tests run in a hidden window that never has focus; SDL drops joystick input without focus otherwise, and
    // the scripted SpaceMouse is a virtual joystick (UiTest.cpp).
    if (!options.uiTest.empty()) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // The pen tip's mouse events come from the app, not SDL (App::penMouseEvents, #95).
    SDL_SetHint(SDL_HINT_PEN_MOUSE_EVENTS, "0");
    if (!headless && options.demo.empty()) applyCachedThreadCount(); // the hardware profile's threads, before any brush work
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    // Joysticks for a 3Dconnexion SpaceMouse (SpaceMouse.cpp). Not fatal: the app works without them.
    if (!headless || !options.uiTest.empty()) {
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK)) spaceMouseScan();
        else SDL_Log("SDL joystick init failed (no SpaceMouse): %s", SDL_GetError());
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    // No multisampling in the window: the 3D view antialiases in its own target (Renderer::drawView), so it can
    // draw with fewer samples while a huge scene moves (Preferences > Performance).
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

    const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                  (headless ? SDL_WINDOW_HIDDEN : 0) |
                                  (options.fullscreen && !headless ? SDL_WINDOW_FULLSCREEN : 0);
    const bool sized = recording || options.benchmark; // proof mode: --size (1920x1080 by default)
    rememberWindow_ = !sized && !headless && options.demo.empty() && !options.fullscreen;
    const WindowState saved = rememberWindow_ ? loadWindowState() : WindowState{};
    const int winW = sized ? options.width : saved.w, winH = sized ? options.height : saved.h;
    window_ = SDL_CreateWindow("SculptForge", winW, winH, flags);
    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    wintab_.open(window_); // Windows: pressure from a tablet driver without Windows Ink (Wintab.h)
    if (rememberWindow_) { // #35: where and how big it was last time
        if (saved.placed) SDL_SetWindowPosition(window_, saved.x, saved.y);
        if (restoresFullscreen(saved.fullscreen)) SDL_SetWindowFullscreen(window_, true);
        // #41, #85: on macOS full screen is a Space of its own; entered at start-up (before the first frame, or right
        // after it) the menu bar and traffic lights couldn't be revealed. A window that was full screen comes back
        // filling the screen as a maximised window instead (the green button, or F11, takes it full screen again).
        else if (saved.maximized || saved.fullscreen) SDL_MaximizeWindow(window_);
    }
    {
        Image icon;
        std::string iconErr;
        if (Image::decode(assets::appIcon, size_t(assets::appIcon_size), icon, iconErr)) {
            if (SDL_Surface* surface = SDL_CreateSurfaceFrom(icon.width, icon.height, SDL_PIXELFORMAT_RGBA32, icon.rgba.data(),
                                                             icon.width * 4)) {
                SDL_SetWindowIcon(window_, surface);
                SDL_DestroySurface(surface);
            }
        }
    }
    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        SDL_Log("OpenGL 3.3 context creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(1);

    if (const char* missing = gl::load([](const char* n) { return reinterpret_cast<gl::GenericProc>(SDL_GL_GetProcAddress(n)); })) {
        SDL_Log("Missing OpenGL function: %s", missing);
        return false;
    }
    SDL_Log("OpenGL %s on %s", reinterpret_cast<const char*>(gl::GetString(GL_VERSION)),
            reinterpret_cast<const char*>(gl::GetString(GL_RENDERER)));

    std::string err;
    if (!renderer_.init(err)) {
        SDL_Log("%s", err.c_str());
        return false;
    }
    renderer_.setEnvironmentWake([] { wakeMainLoop(); }); // (PBR view: a new environment's lighting is ready)
    // Fill layers ask for Mesh Maps: the app bakes them on a worker thread (PanelFill.cpp, updateMeshMaps).
    setMeshMapRequester([this](Document&) { meshMapsWanted_ = true; });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
        iniPath_ = std::string(pref) + "imgui.ini";
        io.IniFilename = iniPath_.c_str();
        aiSettingsPath_ = std::string(pref) + "ai.json";
        aiSettings_ = ai::AiSettings::load(aiSettingsPath_);
        SDL_free(pref);
    } else {
        io.IniFilename = nullptr;
    }
    // Preferences > AI: tests, renders and videos start with AI on whatever this computer chose (SF_AI still wins).
    ai::setAiEnabled(ai::aiFeaturesAllowed(headless || !options.demo.empty() || aiSettings_.enabled));
    ai::registerAiCommands(commands_, [this] { return aiSettings_; });
    registerPaintUiCommands();
    std::snprintf(falKey_, sizeof falKey_, "%s", aiSettings_.falKey.c_str());
    std::snprintf(falModel_, sizeof falModel_, "%s", aiSettings_.falModel.c_str());
    std::snprintf(meshyKey_, sizeof meshyKey_, "%s", aiSettings_.meshyKey.c_str());
    std::snprintf(hfToken_, sizeof hfToken_, "%s", aiSettings_.hfToken.c_str());
    std::snprintf(comfyUrl_, sizeof comfyUrl_, "%s", aiSettings_.comfyUrl.c_str());
    initUiConfig(headless || !options.demo.empty()); // ui.json (layout, shortcuts, UI scale); never for tests and videos
    uiScale_ = targetUiScale(); // the style follows in applyLook below
    buildUiFonts(uiScale_);
    if (recording) {
        ImGui::GetCurrentContext()->TestEngineHookItems = true; // lets the demo find buttons by label
        io.IniFilename = nullptr;                               // fixed, reproducible layout
    }
    applyLook(); // Preferences > Interface > Look (after the fonts it may switch to)
    ImGui_ImplSDL3_InitForOpenGL(window_, glContext_);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    for (int i = 0; i < int(FileAction::Count); ++i) dialogRequests_[i] = {this, FileAction(i)};
    initBrushLibrary(); // before a project can bring its own brushes
    initProject(); // before opening anything, so it lands in the recent files
    initActions(); // the default shortcuts and every command, for keys, help and the command palette
    initBrushSettings(); // each brush's own size, intensity, stroke and alpha (#78)
    loadBrushCurves(); // each brush's own pressure curve (Precision.cpp)
    initPhoneActions(); // File > Capture from phone (PanelPhone.cpp)
    initLightBox();
    runCommand("new_sphere");
    if (!options.openPath.empty()) openPath(options.openPath);
    if (!options.script.empty()) {
        std::vector<ScriptStatement> statements;
        std::string err;
        if (!parseScript(options.script, statements, err)) {
            SDL_Log("Script error: %s", err.c_str());
            return false;
        }
        for (const auto& st : statements) {
            CommandResult r = commands_.execute(doc_, st.command, st.args);
            SDL_Log("%s: %s", st.command.c_str(), r.message.c_str());
            if (!r.ok && headless) return false;
        }
        camera_.frame(sceneBounds());
        updateTitle();
    }
    if (!aiSettings_.generatorCommand.empty())
        std::snprintf(generatorCommand_, sizeof generatorCommand_, "%s", aiSettings_.generatorCommand.c_str());
    else if (const char* base = SDL_GetBasePath()) {
#if defined(_WIN32)
        const char* cliName = "sculptforge-cli.exe";
#else
        const char* cliName = "sculptforge-cli";
#endif
        std::snprintf(generatorCommand_, sizeof generatorCommand_, "%s",
                      ai::defaultGeneratorCommand(std::string(base) + cliName).c_str());
    }
    initHardwareProfile(); // the cached hardware profile, or calibration over the first frames (PanelPerformanceProfile.cpp)
    return true;
}

void App::shutdown() {
    saveWindowState();
    if (doc_.sculptor.strokeActive()) endStroke();
    solver_.reset(); // (its thread uses the renderer)
    flushBrushEdits();
    if (blenderInitialised_) writeBridgeHeartbeat(blenderExchangeRoot(), true); // the add-on starts a new one

    spaceMouseClose();
    shutdownHardwareProfile(); // its timer queries need the GL context
    if (refTexture_) gl::DeleteTextures(1, &refTexture_);
    gallery_.reset(); // its preview textures need the GL context
    thumbs_.clear();
    clearSubtoolThumbnails();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    for (SDL_Cursor*& c : cursors_) {
        if (c) SDL_DestroyCursor(c);
        c = nullptr;
    }
    renderer_.shutdown();
    if (glContext_) SDL_GL_DestroyContext(glContext_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

int App::renderHeadless() {
    const int frames = std::max(1, options_.turntable);
    camera_.frame(options_.hideMesh && !doc_.splats.empty() ? doc_.splats.bounds(0.9f) : sceneBounds());
    camera_.aspect = float(options_.width) / float(std::max(options_.height, 1));
    camera_.pitch = glm::radians(options_.pitchDegrees);
    camera_.distance /= options_.zoom;
    renderSettings_.wireframe = renderSettings_.wireframe || options_.wireframe; // syncScene builds its edges
    RenderSettings settings = renderSettings_;
    settings.wireframe = options_.wireframe;
    settings.showPolygroups = settings.showPolygroups || options_.polygroups;
    settings.showMesh = !options_.hideMesh;
    settings.dimInactive = false; // a beauty shot: every SubTool at full brightness
    syncScene();
    const std::filesystem::path out(std::u8string(options_.renderPath.begin(), options_.renderPath.end()));
    for (int f = 0; f < frames; ++f) {
        camera_.yaw = glm::radians(options_.yawDegrees + 360.0f * float(f) / float(frames));
        Image img;
        img.width = options_.width;
        img.height = options_.height;
        std::string err;
        if (!renderer_.renderToImage(camera_, img.width, img.height, settings, img.rgba, err)) {
            SDL_Log("Render failed: %s", err.c_str());
            return 1;
        }
        std::string path = options_.renderPath;
        if (options_.turntable > 0) {
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "_%03d.png", f);
            auto stem = (out.parent_path() / out.stem()).u8string();
            path = std::string(stem.begin(), stem.end()) + suffix;
        }
        if (!img.savePng(path, err)) {
            SDL_Log("%s", err.c_str());
            return 1;
        }
        SDL_Log("Rendered %s", path.c_str());
    }
    return 0;
}

int App::runUiTest(int w, int h, int fps) {
    // Same frame loop as a recording, but frames are only read back for screenshots.
    std::vector<uint8_t> rgba;
    std::string err;
    const float dt = 1.0f / float(fps);
    int frames = 0;
    bool finalFrame = false;
    while (frames < fps * 60 * 30) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) // the script provides all input (its SpaceMouse is a virtual joystick)
            if (e.type >= SDL_EVENT_JOYSTICK_AXIS_MOTION && e.type < SDL_EVENT_GAMEPAD_AXIS_MOTION) spaceMouseEvent(e);
        if (demo_->finished()) {
            if (finalFrame) break;
            finalFrame = true; // one more frame, for a failure screenshot
        }
        demo_->update(dt);
        processPendingFiles();
        animateLayers(dt);
        pollBrushRequest();
        pollUnreal();
        pollGenerator();
        pollBlender(); // (demo recordings leave the user's Blender bridge alone)
        pollMaya(); // (tests and recordings never read the user's Maya folder: maya.json isn't loaded)
        pollPhone();
        pollAutoUv();
        pollUvEditor();
        pollBake();
        pollQRemesh();
        pollSplatTrain();
        pollSculptable();
        updateCameraAspect();
        updateSpaceMouse(dt);

        hardwareProfileFrameBegin(); // (only with SF_HW_PROFILE_IN_TESTS=1: tests use fixed settings)
        Demo::beginFrame();
        if (uiScaleDirty_) applyUiScale(); // (Preferences, a display-scale change: #87)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        keepImGuiOnPen();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(float(w), float(h));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = dt;
        ImGui::NewFrame();
        ImGui::LogToBuffer(0); // capture every drawn string (0: don't auto-open tree nodes)
        drawUi();
        drawHardwareProfileOverlay();
        if (ImGuiContext* g = ImGui::GetCurrentContext()) demo_->setFrameText(g->LogBuffer.c_str());
        ImGui::LogFinish();
        ImGui::Render();
        updateMouseCursor();

        prewarmTexturePaint(); // dense textured meshes: ready the area under the hovering Paint brush (PanelPbr.cpp)
        const auto syncStart = std::chrono::steady_clock::now();
        updateFocusDetail(); // (a streamed micro level: promote what the camera and brush need, before the upload)
        syncScene();
        notePerformanceFrame(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - syncStart).count());
        viewSamples(); // keeps Fast Navigation's view of the camera current (UI tests read view_samples)
        hardwareProfileFrameEnd();
        ++frames;
        const std::string shot = demo_->takeShotRequest();
        if (shot.empty()) continue;
        if (!renderer_.beginCapture(w, h, err)) {
            SDL_Log("%s", err.c_str());
            return 1;
        }
        renderer_.draw(camera_, w, h, renderSettings_, canvasViewport(w, h));
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        renderer_.endCapture(rgba);
        Image img;
        img.width = w;
        img.height = h;
        img.rgba = rgba;
        for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
        demo_->setLastShot(img.rgba, w, h);
        if (!img.savePng(shot, err)) SDL_Log("%s", err.c_str());
        else SDL_Log("Screenshot %s", shot.c_str());
    }
    for (const auto& f : demo_->failures()) std::fprintf(stderr, "FAIL %s\n", f.c_str());
    if (demo_->failures().empty())
        std::printf("PASS %s (%d frames)\n", options_.uiTest.c_str(), frames);
    return demo_->failures().empty() ? 0 : 1;
}

namespace {

// Area-weighted resample of the rectangle (x0, y0, cw, ch) of `src` (w x h RGBA) to dw x dh: a box filter when
// shrinking (supersampled frames come out anti-aliased), linear interpolation when enlarging.
struct Taps {
    std::vector<int> first, count;
    std::vector<float> weights; // count[i] weights per output pixel, packed
};
Taps makeTaps(float origin, float extent, int out) {
    Taps t;
    const float step = extent / float(out), width = std::max(step, 1.0f);
    for (int i = 0; i < out; ++i) {
        const float c = origin + (float(i) + 0.5f) * step, a = c - width * 0.5f, b = c + width * 0.5f;
        const int lo = int(std::floor(a)), hi = int(std::ceil(b));
        t.first.push_back(lo);
        t.count.push_back(hi - lo);
        float sum = 0.0f;
        const size_t at = t.weights.size();
        for (int j = lo; j < hi; ++j) {
            const float wgt = std::max(0.0f, std::min(b, float(j + 1)) - std::max(a, float(j)));
            t.weights.push_back(wgt);
            sum += wgt;
        }
        for (size_t j = at; j < t.weights.size(); ++j) t.weights[j] /= sum > 0.0f ? sum : 1.0f;
    }
    return t;
}

void cropResample(const std::vector<uint8_t>& src, int w, int h, float x0, float y0, float cw, float ch, int dw, int dh,
                  std::vector<uint8_t>& dst) {
    const Taps tx = makeTaps(x0, cw, dw), ty = makeTaps(y0, ch, dh);
    const int rowLo = std::clamp(ty.first.front(), 0, h - 1);
    const int rowHi = std::clamp(ty.first.back() + ty.count.back(), 1, h);
    std::vector<float> rows(size_t(rowHi - rowLo) * size_t(dw) * 3);
    for (int y = rowLo; y < rowHi; ++y) { // horizontal pass over the rows the output needs
        const uint8_t* in = src.data() + size_t(y) * size_t(w) * 4;
        float* out = rows.data() + size_t(y - rowLo) * size_t(dw) * 3;
        size_t k = 0;
        for (int x = 0; x < dw; ++x) {
            float r = 0, g = 0, b = 0;
            for (int j = 0; j < tx.count[x]; ++j, ++k) {
                const int sx = std::clamp(tx.first[x] + j, 0, w - 1);
                const float wgt = tx.weights[k];
                r += wgt * in[sx * 4 + 0];
                g += wgt * in[sx * 4 + 1];
                b += wgt * in[sx * 4 + 2];
            }
            out[x * 3 + 0] = r, out[x * 3 + 1] = g, out[x * 3 + 2] = b;
        }
    }
    dst.assign(size_t(dw) * size_t(dh) * 4, 255);
    std::vector<float> acc(size_t(dw) * 3);
    size_t k = 0;
    for (int y = 0; y < dh; ++y) { // vertical pass
        uint8_t* out = dst.data() + size_t(y) * size_t(dw) * 4;
        std::fill(acc.begin(), acc.end(), 0.0f);
        for (int j = 0; j < ty.count[y]; ++j, ++k) {
            const int sy = std::clamp(ty.first[y] + j, rowLo, rowHi - 1);
            const float wgt = ty.weights[k];
            const float* in = rows.data() + size_t(sy - rowLo) * size_t(dw) * 3;
            for (int i = 0; i < dw * 3; ++i) acc[i] += wgt * in[i];
        }
        for (int x = 0; x < dw; ++x)
            for (int c = 0; c < 3; ++c) out[x * 4 + c] = uint8_t(std::clamp(acc[x * 3 + c] + 0.5f, 0.0f, 255.0f));
    }
}

} // namespace

int App::runRecording() {
    demo_ = std::make_unique<Demo>(*this);
    std::string err;
    const bool testing = !options_.uiTest.empty();
    forceTurntable_ = !testing; // demos' scripted camera moves are turntable orbits
    if (testing ? !demo_->loadScript(options_.uiTest, err)
                : !demo_->load(options_.demo.empty() ? "sculpt" : options_.demo, err)) {
        SDL_Log("%s", err.c_str());
        return 1;
    }
    const int w = options_.width, h = options_.height, fps = std::max(options_.fps, 1);
    if (testing) return runUiTest(w, h, fps);
    // The video is --size; with --supersample the window renders larger and each frame is cropped to the demo's
    // virtual camera and downsampled.
    const int ss = std::max(options_.supersample, 1), ow = w / ss, oh = h / ss;
    // Frames go straight to ffmpeg as raw RGBA.
    if (shellQuote(options_.recordPath).empty()) {
        SDL_Log("--record: choose a file name without \" %% ! ^ & | < > in it");
        return 1;
    }
    std::string cmd = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s " + std::to_string(ow) + "x" + std::to_string(oh) +
                      " -r " + std::to_string(fps) +
                      " -i - -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -movflags +faststart " +
                      shellQuote(options_.recordPath);
#if defined(_WIN32)
    FILE* ff = _popen(cmd.c_str(), "wb");
#else
    FILE* ff = popen(cmd.c_str(), "w");
#endif
    if (!ff) {
        SDL_Log("Cannot start ffmpeg");
        return 1;
    }
    std::vector<uint8_t> rgba, frame;
    const float dt = 1.0f / float(fps);
    int frames = 0, written = 0;
    while (!demo_->finished() && frames < fps * 60 * 30) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
        } // the demo provides all input
        demo_->update(dt);
        processPendingFiles();
        animateLayers(dt);
        pollBrushRequest();
        pollUnreal();
        pollAutoUv();
        pollUvEditor();
        pollBake();
        pollQRemesh();
        pollSplatTrain();
        pollSculptable();
        updateCameraAspect();

        Demo::beginFrame();
        if (uiScaleDirty_) applyUiScale(); // (Preferences, a display-scale change: #87)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        keepImGuiOnPen();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(float(w), float(h));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = dt;
        ImGui::NewFrame();
        drawUi();
        demo_->drawOverlay();
        ImGui::Render();
        updateMouseCursor();

        syncScene();
        ++frames;
        // While waiting on a model, keep only every Nth frame (marked on screen as sped up).
        if ((frames % demo_->frameStride()) != 0) continue;
        if (!renderer_.beginCapture(w, h, err)) {
            SDL_Log("%s", err.c_str());
            break;
        }
        renderer_.draw(camera_, w, h, renderSettings_, canvasViewport(w, h));
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        renderer_.endCapture(rgba);
        ImVec2 vc;
        float vz;
        demo_->view(vc, vz);
        if (ss == 1 && vz <= 1.0f) {
            fwrite(rgba.data(), 1, rgba.size(), ff);
        } else {
            const float cw = float(w) / vz, ch = float(h) / vz;
            cropResample(rgba, w, h, vc.x - cw * 0.5f, vc.y - ch * 0.5f, cw, ch, ow, oh, frame);
            fwrite(frame.data(), 1, frame.size(), ff);
        }
        ++written;
    }
#if defined(_WIN32)
    const int status = _pclose(ff);
#else
    const int status = pclose(ff);
#endif
    if (!status_.empty()) SDL_Log("Last status: %s", status_.c_str());
    SDL_Log("Recorded %d frames (%.1f s) to %s", written, float(written) / float(fps), options_.recordPath.c_str());
    return status == 0 ? 0 : 1;
}

namespace {

Uint32 g_wakeEvent = 0; // registered in App::run(); 0 before

constexpr int kIdleTickMs = 250;       // asleep: wake this often to run the pollers (the Blender bridge, autosave)
constexpr int kCaretTickMs = 530;      // a text field has the keyboard: redraw for the caret's blink
constexpr double kAfterChangeSeconds = 0.25; // a poller changed something: draw for this long
constexpr double kStrokeBudgetSeconds = 0.010; // a frame's share of placing a stroke's dabs; the rest waits for the next
constexpr float kRightClickSlop = 4.0f;        // pixels a right press may move and still be a click (#22)
constexpr double kStrokeBacklogSeconds = 0.05; // queued dabs beyond this much work are thinned (strokeTo)
constexpr size_t kStrokeBacklogMinDabs = 4;    // ... but never fewer dabs than this waiting

} // namespace

void wakeMainLoop() {
    if (g_wakeEvent == 0) return;
    SDL_Event e{};
    e.type = g_wakeEvent;
    SDL_PushEvent(&e);
}

bool App::backgroundBusy() const {
    auto running = [](const auto& f) { return f.valid() && f.wait_for(std::chrono::seconds(0)) != std::future_status::ready; };
    return running(pendingBrush_) || running(pendingGenerator_) || running(pendingModels_) || running(pendingUnreal_) ||
           running(pendingComfyCheck_) || galleryBusy() || thumbs_.busy() || libraryScanner_.busy() || autoUv_ ||
           bakeBusy() || qremesh_ || voxelRemesh_ || updateJob_ || splatTrainBusy() || sculptableBusy() || phoneBusy() || uvEditorBusy() ||
           supportCardLoading() || renderer_.environmentPending() || meshMapsBusy();
}

void App::redrawIn(double seconds) {
    const double at = double(SDL_GetTicksNS()) * 1e-9 + seconds;
    redrawAt_ = redrawAt_ > 0.0 ? std::min(redrawAt_, at) : at;
}

// With two screens the pointer often rests on the other one while the keyboard works here: no pointer events
// then, so nothing kept the frames coming and the app looked as if in the background until the pointer came back
// (Discord, Windows 11). Whether the app is in use follows the window's focus, never the pointer: while focused,
// it keeps drawing for a while after any input (keys included) and while a key is held.
bool App::focusKeepsDrawing(double now) const {
    if (!windowFocused_) return false;
    if (now - lastInputAt_ < kFocusedActiveSeconds) return true;
    int n = 0;
    const bool* keys = SDL_GetKeyboardState(&n);
    for (int i = 0; keys && i < n; ++i)
        if (keys[i]) return true;
    return false;
}

bool App::wantsFrames(double now) const {
    return now < activeUntil_ || focusKeepsDrawing(now) || drag_ != Drag::None || ui::HoverAnimating() || // a hover ring easing in or out
           doc_.sculptor.strokeActive() ||
           !dabQueue_.empty() || // a stroke's dabs still waiting (drainStrokeDabs) are placed a few each frame
           strokeFinishing_ || solverBusy() || // the solver thread is placing a stroke's dabs (AsyncStroke.cpp)
           backgroundBusy() || playingLayer_ >= 0 || faceTakePlaying_ ||
           spaceMouseMoving() || // a held SpaceMouse cap sends no events, but the view keeps moving (SpaceMouse.cpp)
           perf_.framesStill < 10; // the view just moved: Fast Navigation's full-quality frame comes after it stops
}

uint64_t App::frameKey() const {
    uint64_t h = std::hash<std::string>{}(status_) ^ (std::hash<std::string>{}(updateStatus_) << 1);
    for (const uint64_t v : {uint64_t(doc_.undo.revision()), uint64_t(doc_.splatsVersion), uint64_t(doc_.modified),
                             uint64_t(doc_.subtoolCount()), uint64_t(int64_t(doc_.activeSubtool))})
        h = h * 1099511628211ull ^ v;
    return h;
}

int App::run() {
    if (!options_.renderPath.empty()) return renderHeadless();
    if (!options_.recordPath.empty() || !options_.uiTest.empty()) return runRecording();
    if (!options_.timelapsePath.empty()) return runTimelapseCli(); // (PanelTimeline.cpp)
    if (options_.benchmark) startProofMode();                     // runs in this loop (ProofMode.cpp)
    if (g_wakeEvent == 0) g_wakeEvent = SDL_RegisterEvents(1);
    // Dabs are queued and placed within a time budget each frame (strokeTo). SF_STROKE_IMMEDIATE=1 places them all
    // as each pointer sample arrives, as before 0.9.6 (for comparison).
    const char* immediateEnv = std::getenv("SF_STROKE_IMMEDIATE");
    interactiveStrokes_ = !(immediateEnv && *immediateEnv && *immediateEnv != '0');
    // SF_CONTINUOUS_REDRAW=1 draws every frame as before 0.9.5 (for comparison, or a driver that misbehaves).
    const char* continuousEnv = std::getenv("SF_CONTINUOUS_REDRAW");
    const bool continuous = continuousEnv && *continuousEnv && *continuousEnv != '0';
    const bool logRedraw = std::getenv("SF_REDRAW_LOG") != nullptr; // why each frame is drawn (bug reports)
    // SF_FRAME_LOG=ms: every frame slower than that, with where its time went.
    const double frameLogMs = std::getenv("SF_FRAME_LOG") ? std::atof(std::getenv("SF_FRAME_LOG")) : -1.0;
    double phase[10] = {};
    int phases = 0;
    const auto mark = [&] {
        if (frameLogMs >= 0.0 && phases < 10) phase[phases++] = double(SDL_GetTicksNS()) * 1e-6;
    };
    auto seconds = [] { return double(SDL_GetTicksNS()) * 1e-9; };
    // --measure-idle: let start-up settle (first frames, the LightBox's thumbnails), then count.
    constexpr double kMeasureSettle = 3.0;
    const double started = seconds();
    uint64_t framesDrawn = 0, wakeUps = 0, framesAtMeasure = 0, wakesAtMeasure = 0;
    std::clock_t cpuAtMeasure = 0; // processor time of every thread (the driver's included)
    bool measuring = false;
    // Input while measuring (a pointer crossing the window on a shared desktop) restarts the count once it has
    // passed: idle means no input. After kMeasureGiveUp seconds it counts whatever comes (the test's timeout).
    constexpr double kMeasureGiveUp = 40.0;
    double measureFrom = started + kMeasureSettle;
    int measureRestarts = 0;
    float lastMotionX = -1e9f, lastMotionY = -1e9f; // the mouse's last position, from its motion events
    Uint64 lastTicks = SDL_GetTicksNS();
    uint64_t lastKey = frameKey();
    bool draw = true;
    activeUntil_ = started + 1.0;
    // --measure-stroke: two turns of a circle over the model in S seconds. The pointer is read once per pass of the loop, as a busy program gets it (the window system hands
    // over only the latest position), so a pass that places many dabs makes the next gap longer.
    // The brush behind the pointer is measured twice: the surface (the last dab whose result is on the GPU) and
    // the preview (the predictive stroke's head, render/Ghost.h; the surface when there is none). Release is how
    // long letting go holds up the UI, then how long until the last dab is on screen.
    struct MeasureView {
        float distance, aspect, w, h;
        bool operator==(const MeasureView&) const = default;
    };
    struct StrokeMeasure {
        bool started = false, moving = false; // moving: the press's first frame is on screen, the pointer moves
        bool releasing = false;               // let go: waiting for the last dab to reach the screen
        double pressMs = 0.0, start = 0.0, lastFrame = 0.0, maxFrame = 0.0, sumFrames = 0.0, maxLag = 0.0, sumLag = 0.0;
        double pressAt = 0.0, firstSurfaceMs = 0.0, maxPreviewLag = 0.0, sumPreviewLag = 0.0;
        double releaseMs = 0.0, releaseAt = 0.0;
        uint64_t frames = 0, samples = 0, lagSamples = 0;
        float cx = 0.0f, cy = 0.0f, r = 0.0f;
        double angle = 0.0; // the pointer's angle round the circle (0 at the press, 4 pi at the end)
        std::vector<glm::vec2> dabs;
        MeasureView view{};     // the view before the stroke (it starts once this has held still for a second)
        double viewSince = 0.0;
        std::vector<double> frameTimes; // seconds, for the percentiles
        std::vector<float> radii;       // each dab's brush radius (px)
        // SF_MEASURE_VERIFY=1: the mesh before the stroke, to check undo and replay the dabs (verifyStroke).
        uint64_t before = 0;
        // SF_MEASURE_SNAPSHOT=prefix: the canvas at a few points of the stroke, as prefix-N.png.
        int snapshots = 0;
    } sm;
    // SF_MEASURE_POP=1: how much the picture jumps when the solver's result replaces the preview (commitSolverBatch).
    const char* popEnv = std::getenv("SF_MEASURE_POP");
    popProbe_ = popEnv && *popEnv && *popEnv != '0';
    const char* verifyEnv = std::getenv("SF_MEASURE_VERIFY");
    const bool verify = verifyEnv && *verifyEnv && *verifyEnv != '0';
    const char* snapshotEnv = std::getenv("SF_MEASURE_SNAPSHOT");
    while (running_) {
        const double t0 = seconds();
        if (options_.measureIdle > 0.0f) {
            if (!measuring && t0 >= measureFrom) {
                measuring = true;
                framesAtMeasure = framesDrawn;
                wakesAtMeasure = wakeUps;
                cpuAtMeasure = std::clock();
            } else if (measuring && t0 >= measureFrom + options_.measureIdle) {
                const double fps = double(framesDrawn - framesAtMeasure) / double(options_.measureIdle);
                const double cpu = 100.0 * double(std::clock() - cpuAtMeasure) / CLOCKS_PER_SEC / double(options_.measureIdle);
                std::printf("Idle: %llu frames, %llu wake-ups in %.1f s: %.2f fps, CPU %.1f%% of one core (%s redraw)\n",
                            (unsigned long long)(framesDrawn - framesAtMeasure), (unsigned long long)(wakeUps - wakesAtMeasure),
                            double(options_.measureIdle), fps, cpu, continuous ? "continuous" : "on-demand");
                if (measureRestarts > 0) std::printf("  (restarted %d times after input)\n", measureRestarts);
                std::fflush(stdout);
                // Idle is 0 fps; a pointer crossing the window draws about a second's worth. Every frame is 60+.
                return continuous || fps < 5.0 ? 0 : 1;
            }
        }
        // Nothing to draw: sleep until an event, or the next poller tick.
        const ImGuiIO& io = ImGui::GetIO();
        // Input keeps the frames coming a little while (hover highlights, tooltips after their delay); other
        // events (the window shown, moved, focused) need a frame or two.
        const auto onEvent = [&](const SDL_Event& ev) {
            if (strokeProbe_ && ((ev.type >= SDL_EVENT_MOUSE_MOTION && ev.type <= SDL_EVENT_MOUSE_WHEEL) ||
                                 (ev.type >= SDL_EVENT_PEN_PROXIMITY_IN && ev.type <= SDL_EVENT_PEN_AXIS)))
                return; // --measure-stroke: the real pointer stays out of the scripted stroke
            handleEvent(ev);
            if (ev.type == SDL_EVENT_KEYMAP_CHANGED || ev.type == SDL_EVENT_KEYBOARD_ADDED ||
                ev.type == SDL_EVENT_KEYBOARD_REMOVED || ev.type == SDL_EVENT_MOUSE_ADDED ||
                ev.type == SDL_EVENT_MOUSE_REMOVED || ev.type == SDL_EVENT_CLIPBOARD_UPDATE ||
                (ev.type == SDL_EVENT_JOYSTICK_AXIS_MOTION && !spaceMouseDeflected()))
                return; // nothing on screen changes (a hand resting on a SpaceMouse: inside its dead zone)
            // A mouse motion event that doesn't move the mouse: what a window system sends when the cursor's shape
            // changes or the window is refocused under a still pointer (the crosshair over the view, #73). Not input,
            // and nothing to draw: counting it would make setting the cursor wake the idle loop.
            if (ev.type == SDL_EVENT_MOUSE_MOTION && ev.motion.which != SDL_PEN_MOUSEID) {
                const bool still = ev.motion.x == lastMotionX && ev.motion.y == lastMotionY;
                lastMotionX = ev.motion.x;
                lastMotionY = ev.motion.y;
                if (still) return;
            }
            draw = true;
            if (logRedraw) SDL_Log("redraw: event 0x%x", unsigned(ev.type));
            const bool input = (ev.type >= SDL_EVENT_KEY_DOWN && ev.type < SDL_EVENT_JOYSTICK_AXIS_MOTION) ||
                               (ev.type >= SDL_EVENT_FINGER_DOWN && ev.type <= SDL_EVENT_FINGER_CANCELED) ||
                               (ev.type >= SDL_EVENT_PEN_PROXIMITY_IN && ev.type <= SDL_EVENT_PEN_AXIS) ||
                               ev.type == SDL_EVENT_DROP_FILE;
            const double t = seconds();
            activeUntil_ = std::max(activeUntil_, t + (input ? std::max(0.75, double(uiPrefs_.tooltipDelay) + 0.5) : 0.1));
            if (input && options_.measureIdle > 0.0f && t - started < kMeasureGiveUp) {
                if (measuring) ++measureRestarts;
                measuring = false;
                measureFrom = std::max(measureFrom, activeUntil_ + 0.5);
            }
        };
        SDL_Event e;
        if (!continuous && !draw) {
            int timeout = io.WantTextInput ? kCaretTickMs : kIdleTickMs;
            if (redrawAt_ > 0.0) timeout = std::clamp(int((redrawAt_ - t0) * 1000.0) + 1, 0, timeout);
            if (SDL_WaitEventTimeout(&e, timeout)) {
                onEvent(e);
            } else if (io.WantTextInput) {
                draw = true; // the caret blinks
            }
            ++wakeUps;
        }
        phases = 0;
        mark();
        while (SDL_PollEvent(&e)) onEvent(e);
        mark();
        // (The window must have settled too: a window manager that maximises it late, on a busy machine, changes
        // the canvas and with it the brush's size in the world mid-stroke.)
        if (options_.measureStroke > 0.0f && !sm.started) {
            const CanvasRect c = canvasRect();
            const MeasureView v{camera_.distance, camera_.aspect, c.w, c.h};
            if (!(v == sm.view)) {
                sm.view = v;
                sm.viewSince = t0;
            }
        }
        if (options_.measureStroke > 0.0f && !sm.started && t0 - started >= kMeasureSettle && t0 - sm.viewSince >= 1.0 &&
            !backgroundBusy()) {
            const CanvasRect c = canvasRect();
            sm.cx = c.x + c.w * 0.5f;
            sm.cy = c.y + c.h * 0.5f;
            sm.r = 0.25f * std::min(c.w, c.h);
            sm.started = true;
            sm.start = sm.lastFrame = sm.pressAt = seconds();
            strokeProbe_ = &sm.dabs;
            strokeRadiusProbe_ = &sm.radii;
            if (options_.measureStrokeRadius > 0.0f) radiusPx_ = std::clamp(options_.measureStrokeRadius, 2.0f, 800.0f);
            if (const char* b = std::getenv("SF_MEASURE_BRUSH")) // e.g. KelvinGrab: measure another built-in brush
                if (BrushType t; brushFromName(b, t)) brush_ = t, doc_.activeBrush = -1;
            if (verify) sm.before = meshStateHash(doc_.mesh);
            onMouseDown(sm.cx + sm.r, sm.cy, SDL_BUTTON_LEFT);
            if (drag_ != Drag::Stroke) {
                std::printf("Stroke: no stroke began (nothing to sculpt under the circle's start)\n");
                return 1;
            }
        } else if (sm.moving) {
            // The pointer follows the clock, but moves at most kMaxPointerStep per pass: the stroke follows the chords
            // between samples, and on a slow frame rate (software GL on a busy machine: 100 ms frames and more) the
            // chords would cut well inside the circle, or one long frame would skip the whole stroke. Above 30 fps
            // the clock alone sets it; below, the stroke takes longer and is checked just as strictly.
            constexpr double kMaxPointerStep = 0.25; // radians (a chord's middle 0.8% of the radius inside the circle)
            const double clock = std::min(1.0, (seconds() - sm.start) / double(options_.measureStroke)) * 4.0 * 3.14159265358979;
            const double a = std::min(clock, sm.angle + kMaxPointerStep);
            const double u = a / (4.0 * 3.14159265358979);
            sm.angle = a;
            // SF_MEASURE_RESIZE=1: the brush size changes every frame mid-stroke, as [ and ] would (the solver thread
            // must see each batch's own size: ui_stroke_resize, with verify).
            static const bool resize = std::getenv("SF_MEASURE_RESIZE") != nullptr;
            if (resize) radiusPx_ = std::clamp(options_.measureStrokeRadius > 0.0f ? options_.measureStrokeRadius : 60.0f, 2.0f, 800.0f) *
                                    (1.0f + 0.4f * float(std::sin(u * 40.0)));
            onMouseMove(sm.cx + sm.r * float(std::cos(a)), sm.cy + sm.r * float(std::sin(a)), 0.0f, 0.0f, false);
            ++sm.samples;
            if (u >= 1.0) {
                sm.releaseAt = seconds();
                onMouseUp(); // places the dabs still waiting (or, with predictive strokes, leaves them to the solver)
                sm.releaseMs = (seconds() - sm.releaseAt) * 1000.0;
                sm.moving = false;
                sm.releasing = true;
            }
        } else if (sm.releasing && !doc_.sculptor.strokeActive()) {
            {
                const double releaseDoneMs = (seconds() - sm.releaseAt) * 1000.0;
                const double releaseMs = sm.releaseMs;
                strokeProbe_ = nullptr;
                strokeRadiusProbe_ = nullptr;
                std::vector<float> off;
                for (const glm::vec2& d : sm.dabs) off.push_back(std::abs(std::hypot(d.x - sm.cx, d.y - sm.cy) - sm.r));
                std::sort(off.begin(), off.end());
                const auto pct = [&](double q) { return off.empty() ? 0.0f : off[std::min(off.size() - 1, size_t(q * off.size()))]; };
                const double frameMean = sm.frames ? sm.sumFrames / double(sm.frames) * 1000.0 : 0.0;
                std::sort(sm.frameTimes.begin(), sm.frameTimes.end());
                const auto framePct = [&](double q) {
                    return sm.frameTimes.empty() ? 0.0
                                                 : sm.frameTimes[std::min(sm.frameTimes.size() - 1, size_t(q * double(sm.frameTimes.size())))] * 1000.0;
                };
                std::printf("  frame percentiles: p50 %.1f ms, p95 %.1f ms, p99 %.1f ms\n", framePct(0.5), framePct(0.95), framePct(0.99));
                const double lagMean = sm.lagSamples ? sm.sumLag / double(sm.lagSamples) : 0.0;
                const double previewMean = sm.lagSamples ? sm.sumPreviewLag / double(sm.lagSamples) : 0.0;
                std::printf("Stroke (%s%s): %zu dabs from %llu pointer samples in %.1f s, circle radius %.0f px, brush radius %.0f px\n"
                            "  off the circle: median %.1f px, 95%% %.1f px, worst %.1f px\n"
                            "  press %.0f ms (first dab's surface on screen %.0f ms); then frames: %llu, mean %.1f ms, worst %.1f ms\n"
                            "  brush behind the pointer: surface mean %.0f px, worst %.0f px; preview mean %.0f px, worst %.0f px\n"
                            "  release %.0f ms (last dab on screen after %.0f ms)\n",
                            interactiveStrokes_ ? "queued" : "immediate", predictiveStrokes() ? ", predictive" : "", sm.dabs.size(),
                            (unsigned long long)sm.samples, sm.releaseAt - sm.start, double(sm.r), double(radiusPx_),
                            double(pct(0.5)), double(pct(0.95)), double(off.empty() ? 0.0f : off.back()), sm.pressMs,
                            sm.firstSurfaceMs, (unsigned long long)sm.frames, frameMean, sm.maxFrame * 1000.0, lagMean, sm.maxLag,
                            previewMean, sm.maxPreviewLag, releaseMs, releaseDoneMs);
                // tools/bench_strokes.sh reads this line.
                std::printf("bench: %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f\n", frameMean, sm.maxFrame * 1000.0, lagMean,
                            previewMean, sm.pressMs, sm.firstSurfaceMs, releaseMs, releaseDoneMs, framePct(0.95), framePct(0.99));
                if (popProbe_ && popCommits_ > 0)
                    std::printf("pop: when the solver's dabs replace their preview the view changes by %.3f on average "
                                "(worst %.3f); without the preview those dabs would change it by %.3f (%llu commits)\n",
                                popWith_ / double(popCommits_), popWorst_, popWithout_ / double(popCommits_),
                                (unsigned long long)popCommits_);
                std::fflush(stdout);
                const MeasureView now{camera_.distance, camera_.aspect, canvasRect().w, canvasRect().h};
                if (verify && !(now == sm.view))
                    std::printf("verify: skipped (the view changed during the stroke: distance %.3f -> %.3f, canvas %.0fx%.0f -> %.0fx%.0f)\n",
                                double(sm.view.distance), double(now.distance), double(sm.view.w), double(sm.view.h), double(now.w), double(now.h));
                else if (verify && !verifyStroke(sm.before, sm.dabs, sm.radii)) return 1;
                // The test (ui_stroke_path, a light sphere): every dab placed, all of them on the circle.
                const float spacing = dabSpacingPx(radiusPx_, 0.15f);
                const double expected = 4.0 * 3.14159265358979 * double(sm.r) / double(spacing);
                if (std::getenv("SF_MEASURE_RESIZE")) return 0; // (the spacing changed with the size: verify checked it)
                const bool enough = double(sm.dabs.size()) >= 0.9 * expected, onCircle = !off.empty() && off.back() < spacing;
                if (!enough || !onCircle)
                    std::printf("Stroke check failed: %zu dabs (want %.0f or more), worst %.1f px off the circle (want under %.1f)\n",
                                sm.dabs.size(), 0.9 * expected, double(off.empty() ? 0.0f : off.back()), double(spacing));
                return enough && onCircle ? 0 : 1;
            }
        }
        // The stroke's queued dabs: handed to the solver thread (predictive strokes, AsyncStroke.cpp), or placed here
        // for a limited time, then look at the pointer again.
        // Continuous (#31): a pointer held still keeps dabbing where it is, 30 times a second.
        if (strokeContinuous_ && drag_ == Drag::Stroke && !firstDabPending_ && dabQueue_.empty() &&
            strokeBrush_ != BrushType::Move && strokeBrush_ != BrushType::SnakeHook && strokeType_ != StrokeType::DragRect &&
            strokeType_ != StrokeType::DragDot) {
            const double nowS = double(SDL_GetTicksNS()) * 1e-9;
            if (nowS - strokeMovedAt_ > 0.05 && nowS - continuousDabAt_ > 1.0 / 30.0) {
                dabQueue_.push({lastX_, lastY_, lastStrokePressure_});
                continuousDabAt_ = nowS;
            }
        }
        flushGrab(); // Move: the pointer's moves since the last frame, as one
        if (asyncStroke_) pumpStroke();
        else drainStrokeDabs(kStrokeBudgetSeconds);
        mark();
        kelvinCatchUp(); // a long Kelvinlet move, a bounded part per frame (PanelKelvinlet.cpp)
        Uint64 now = SDL_GetTicksNS();
        float dt = float(double(now - lastTicks) * 1e-9);
        lastTicks = now;
        if (!solverBusy()) { // (the solver thread owns the document's mesh until the stroke's end)
            processPendingFiles();
            animateLayers(dt);
            pollBrushRequest();
            pollGenerator();
            pollUnreal();
            pollGoz();
            pollBlender();
            pollMaya();
            pollPhone();
            pollAutoUv();
            pollUvEditor();
            pollBake();
            pollQRemesh();
            pollSplatTrain();
            pollSculptable();
        }
        tickProject(dt); // the title and autosave, asleep or not
        const double t = seconds();
        if (const uint64_t key = frameKey(); key != lastKey) { // a job finished, the Blender bridge sent a model...
            lastKey = key;
            draw = true;
            if (logRedraw) SDL_Log("redraw: changed (status \"%s\")", status_.c_str());
            activeUntil_ = std::max(activeUntil_, t + kAfterChangeSeconds);
        }
        if (redrawAt_ > 0.0 && t >= redrawAt_) {
            redrawAt_ = 0.0;
            draw = true;
        }
        if (!draw && !continuous && !wantsFrames(t)) continue;

        int fbW = 0, fbH = 0, winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window_, &fbW, &fbH);
        SDL_GetWindowSize(window_, &winW, &winH);
        if (fbW <= 0 || fbH <= 0) {
            draw = false; // minimised: the next event (restore) draws again
            continue;
        }
        updateCameraAspect();
        // The SpaceMouse moves the view by its held axes every frame (frame-rate independent); while it does,
        // spaceMouseMoving() is true and a loop that waits for events when idle must keep drawing.
        updateSpaceMouse(dt);

        hardwareProfileFrameBegin(); // the frame governor's CPU and GPU timing
        if (uiScaleDirty_) applyUiScale(); // (Preferences, a display-scale change: #87)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        keepImGuiOnPen();
        ImGui::NewFrame();
        drawUi();
        drawHardwareProfileOverlay();
        ImGui::Render();
        updateMouseCursor();
        mark();

        buildGhost(); // the dabs not on the GPU yet, drawn as a preview (predictive strokes)
        // dense textured meshes: ready the area under the hovering Paint brush (PanelPbr.cpp); not while the solver
        // thread owns the mesh
        if (!solverBusy()) prewarmTexturePaint();
        const auto syncStart = std::chrono::steady_clock::now();
        syncScene();
        mark();
        notePerformanceFrame(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - syncStart).count());
        const int samples = viewSamples();
        renderSettings_.motionProxy = perf_.proxyNow; // big meshes draw their lighter proxy while the view moves
        renderSettings_.materialTint = materialTintFor(doc_.paintColor); // (Color > Colour tints the material)
        perf_.samplesUsed = renderer_.drawView(camera_, fbW, fbH, renderSettings_, canvasViewport(fbW, fbH), samples);
        if (snapshotEnv && *snapshotEnv && (sm.moving || sm.releasing)) {
            // Three points of the stroke, and the end (the frame the last dab reached the screen).
            const double u = sm.moving ? (seconds() - sm.start) / double(options_.measureStroke) : 2.0;
            const bool last = sm.releasing && !doc_.sculptor.strokeActive();
            if ((sm.snapshots < 3 && u >= 0.3 * (sm.snapshots + 1)) || (last && sm.snapshots < 4)) {
                sm.snapshots = last ? 4 : sm.snapshots + 1;
                const Renderer::Viewport v = canvasViewport(fbW, fbH);
                Image img;
                img.width = v.w > 0 ? v.w : fbW;
                img.height = v.h > 0 ? v.h : fbH;
                img.rgba.resize(size_t(img.width) * size_t(img.height) * 4);
                gl::ReadPixels(v.w > 0 ? v.x : 0, v.w > 0 ? v.y : 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
                for (int y = 0; y < img.height / 2; ++y) // (GL rows are bottom-up)
                    std::swap_ranges(img.rgba.begin() + std::ptrdiff_t(size_t(y) * img.width * 4),
                                     img.rgba.begin() + std::ptrdiff_t(size_t(y + 1) * img.width * 4),
                                     img.rgba.begin() + std::ptrdiff_t(size_t(img.height - 1 - y) * img.width * 4));
                std::string error;
                img.savePng(std::string(snapshotEnv) + "-" + std::to_string(sm.snapshots) + ".png", error);
            }
        }
        renderSettings_.motionProxy = false; // (other renders, e.g. project thumbnails, draw in full)
        mark();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        hardwareProfileFrameEnd();
        if (proof_) proofBeforeSwap(fbW, fbH); // Help > Benchmark: timing, video capture, the scripted pointer
        SDL_GL_SwapWindow(window_);
        mark();
        if (phases == 7 && phase[6] - phase[0] > frameLogMs)
            SDL_Log("frame %.1f ms: events %.1f, dabs %.1f, UI %.1f, ghost + sync %.1f, draw %.1f, UI draw + swap %.1f",
                    phase[6] - phase[0], phase[1] - phase[0], phase[2] - phase[1], phase[3] - phase[2], phase[4] - phase[3],
                    phase[5] - phase[4], phase[6] - phase[5]);
        ++framesDrawn;
        {
            const double frameNow = seconds();
            static double lastSwap = 0.0;
            if (lastSwap > 0.0) frameSeconds_ = frameSeconds_ * 0.9 + std::min(0.1, frameNow - lastSwap) * 0.1;
            lastSwap = frameNow;
        }
        if (sm.started && sm.firstSurfaceMs == 0.0 && !sm.dabs.empty()) sm.firstSurfaceMs = (seconds() - sm.pressAt) * 1000.0;
        if (sm.started && !sm.moving && !sm.releasing && sm.pressMs == 0.0) { // the press is on screen: start moving round the circle
            sm.moving = true;
            sm.pressMs = (seconds() - sm.start) * 1000.0;
            sm.start = sm.lastFrame = seconds();
        } else if (sm.moving) {
            const double now = seconds();
            sm.maxFrame = std::max(sm.maxFrame, now - sm.lastFrame);
            sm.sumFrames += now - sm.lastFrame;
            sm.frameTimes.push_back(now - sm.lastFrame);
            sm.lastFrame = now;
            ++sm.frames;
            if (!sm.dabs.empty()) { // how far round the circle the brush is behind the pointer, on screen now
                const double pointer = std::fmod(sm.angle, 2.0 * 3.14159265358979); // where the pointer was sent
                const glm::vec2 d = sm.dabs.back();
                double behind = pointer - std::atan2(double(d.y - sm.cy), double(d.x - sm.cx));
                behind = std::remainder(behind, 2.0 * 3.14159265358979);
                const double px = std::abs(behind) * double(sm.r);
                sm.maxLag = std::max(sm.maxLag, px);
                sm.sumLag += px;
                ++sm.lagSamples;
                const glm::vec2 h = ghostHeadValid_ ? ghostHead_ : d; // the preview's head (or the surface's)
                double ahead = pointer - std::atan2(double(h.y - sm.cy), double(h.x - sm.cx));
                const double hpx = std::abs(std::remainder(ahead, 2.0 * 3.14159265358979)) * double(sm.r);
                sm.maxPreviewLag = std::max(sm.maxPreviewLag, hpx);
                sm.sumPreviewLag += hpx;
            }
        }
        lastKey = frameKey(); // what this frame changed is on screen already
        draw = continuous || wantsFrames(seconds());
        if (logRedraw && draw && !continuous)
            SDL_Log("redraw: keep drawing (input %d, drag %d, busy %d, view moved %d)", int(seconds() < activeUntil_),
                    int(drag_ != Drag::None), int(backgroundBusy()), int(perf_.framesStill < 10));
    }
    finishProject();
    return options_.benchmark && proofSkippedScenes() ? 2 : 0; // (--benchmark: a scene skipped for memory)
}

// The UI scale (Preferences > Interface > UI scale, #87). Automatic follows the window's display: on Windows
// (per-monitor DPI v2) and with X11's Xft.dpi that is the content scale, and moving the window to a monitor at another
// scale sends SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED. Tests and videos size the UI for their frame instead.
float App::targetUiScale() const {
    float automatic = 1.0f;
    if (displayScaleOverride_ > 0.0f) automatic = displayScaleOverride_; // UI tests: display_scale
    else if (!options_.recordPath.empty() || !options_.uiTest.empty()) automatic = float(options_.height) / 900.0f;
    else if (window_) automatic = automaticUiScale(SDL_GetWindowDisplayScale(window_), SDL_GetWindowPixelDensity(window_));
    return resolveUiScale(uiPrefs_.uiScale, automatic);
}

// Live, between frames (the atlas can't change inside one): the fonts rasterised again at the new size, the style
// rebuilt from scratch (applyLook), and the shell lays itself out at uiScale_ on the next frame.
void App::applyUiScale() {
    uiScaleDirty_ = false;
    const float scale = targetUiScale();
    if (!uiScaleChanged(uiScale_, scale)) return;
    SDL_Log("UI scale %.0f%%", double(scale * 100.0f));
    uiScale_ = scale;
    ImGui::GetIO().Fonts->Clear();
    buildUiFonts(scale);
    if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_DestroyFontsTexture(); // the next NewFrame uploads it
    applyLook();
}

void App::requestUiScale() {
    uiScaleDirty_ = true;
    redrawIn(0.0);
}

// Every font the UI uses, rasterised at `scale` for crisp text (Dear ImGui 1.91 has no dynamic font sizes).
void App::buildUiFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    const bool recording = !options_.recordPath.empty() || !options_.uiTest.empty();
    // Inter (SIL OFL), embedded in the binary. Rasterised at the final size for crisp text.
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    fontConfig.OversampleH = 2;
    fontRegular_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontRegular), int(assets::fontRegular_size),
                                                  15.0f * scale, &fontConfig);
    // Lucide icons (ISC/MIT) merged into the UI font, so ICON_LC_* strings work in any label.
    {
        static const ImWchar kIconRanges[] = {ICON_MIN_LC, ICON_MAX_LC, 0};
        ImFontConfig icons;
        icons.MergeMode = true;
        icons.PixelSnapH = true;
        icons.FontDataOwnedByAtlas = false;
        icons.GlyphMinAdvanceX = 15.0f * scale;
        icons.GlyphOffset = ImVec2(0, 2.0f * scale);
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontLucide), int(assets::fontLucide_size),
                                       15.0f * scale, &icons, kIconRanges);
    }
    // Shell fonts: 12 px captions and 10 px right-shelf labels (with icons), large shelf icons, SemiBold titles.
    {
        auto addIcons = [&](float size) {
            static const ImWchar kIconRanges[] = {ICON_MIN_LC, ICON_MAX_LC, 0};
            ImFontConfig icons;
            icons.MergeMode = true;
            icons.PixelSnapH = true;
            icons.FontDataOwnedByAtlas = false;
            icons.GlyphMinAdvanceX = size * scale;
            icons.GlyphOffset = ImVec2(0, size * 0.13f * scale);
            io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontLucide), int(assets::fontLucide_size),
                                           size * scale, &icons, kIconRanges);
        };
        fontSmall_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontRegular),
                                                    int(assets::fontRegular_size), 12.0f * scale, &fontConfig);
        addIcons(12.0f);
        fontTiny_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontRegular),
                                                   int(assets::fontRegular_size), 10.5f * scale, &fontConfig);
        fontBold_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontSemiBold),
                                                   int(assets::fontSemiBold_size), 15.0f * scale, &fontConfig);
        addIcons(15.0f);
        static const ImWchar kIconRanges[] = {ICON_MIN_LC, ICON_MAX_LC, 0};
        ImFontConfig big;
        big.PixelSnapH = true;
        big.FontDataOwnedByAtlas = false;
        fontIcons_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontLucide), int(assets::fontLucide_size),
                                                    20.0f * scale, &big, kIconRanges);
    }
    { // JetBrains Mono (SIL OFL) for the looks (Preferences > Interface > Look), with the icons and box drawing.
        static const ImWchar kMonoRanges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x2500, 0x259F, 0x25A0, 0x25FF, 0};
        static const ImWchar kIconRanges[] = {ICON_MIN_LC, ICON_MAX_LC, 0};
        auto addMono = [&](const unsigned char* data, unsigned long long size, float px) {
            ImFont* f = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), int(size), px * scale, &fontConfig, kMonoRanges);
            ImFontConfig icons;
            icons.MergeMode = true;
            icons.PixelSnapH = true;
            icons.FontDataOwnedByAtlas = false;
            icons.GlyphMinAdvanceX = px * scale;
            icons.GlyphOffset = ImVec2(0, px * 0.13f * scale);
            io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontLucide), int(assets::fontLucide_size), px * scale,
                                           &icons, kIconRanges);
            return f;
        };
        fontMono_ = addMono(assets::fontMono, assets::fontMono_size, 15.0f);
        fontMonoSmall_ = addMono(assets::fontMono, assets::fontMono_size, 12.0f);
        fontMonoTiny_ = addMono(assets::fontMono, assets::fontMono_size, 10.5f);
        fontMonoBold_ = addMono(assets::fontMonoBold, assets::fontMonoBold_size, 15.0f);
        fontInterSmall_ = fontSmall_;
        fontInterTiny_ = fontTiny_;
        fontInterBold_ = fontBold_;
        theme::fonts = {fontMono_, fontMonoSmall_, fontMonoBold_};
    }
    { // The support card's display sizes (PanelSupportCard.cpp): Latin only, so the atlas grows by a few glyphs.
        static const ImWchar kCardRanges[] = {0x0020, 0x00FF, 0x2014, 0x2014, 0x2019, 0x2019, 0x2026, 0x2026, 0};
        fontCardTitle_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontSemiBold),
                                                        int(assets::fontSemiBold_size), 40.0f * scale, &fontConfig, kCardRanges);
        fontCardHead_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontSemiBold),
                                                       int(assets::fontSemiBold_size), 21.0f * scale, &fontConfig, kCardRanges);
        fontCardBody_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontRegular),
                                                       int(assets::fontRegular_size), 16.5f * scale, &fontConfig, kCardRanges);
    }
    addHelpFonts(scale); // Ctrl-help titles (Inter SemiBold) and the console (ProggyClean)
    guide_.addFonts(scale); // User Guide headings
    if (recording) { // the demo captions, sized for the video
        fontCaption_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontSemiBold),
                                                      int(assets::fontSemiBold_size), float(options_.height) * 0.042f,
                                                      &fontConfig);
        fontCaptionSub_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::fontRegular),
                                                         int(assets::fontRegular_size), float(options_.height) * 0.024f,
                                                         &fontConfig);
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void App::handleEvent(const SDL_Event& event) {
    if (dropPenMouseEvent(event)) return; // the pen's mouse events are made here, not by SDL (#95, Tablet.cpp)
    penMouseEvents(event, true);          // a pen leaving lets go of the tip's button first
    dispatchEvent(event);
    penMouseEvents(event, false);         // the tip's mouse events, after the pen's own
}

void App::dispatchEvent(const SDL_Event& event) {
    if (strayMouseMotion(event)) return; // neither the canvas nor the UI follows a driver's stray mouse events
    if ((event.type >= SDL_EVENT_KEY_DOWN && event.type < SDL_EVENT_JOYSTICK_AXIS_MOTION &&
         event.type != SDL_EVENT_KEYMAP_CHANGED && event.type != SDL_EVENT_KEYBOARD_ADDED &&
         event.type != SDL_EVENT_KEYBOARD_REMOVED && event.type != SDL_EVENT_MOUSE_ADDED &&
         event.type != SDL_EVENT_MOUSE_REMOVED) ||
        (event.type >= SDL_EVENT_PEN_PROXIMITY_IN && event.type <= SDL_EVENT_PEN_AXIS))
        lastInputAt_ = double(SDL_GetTicksNS()) * 1e-9; // (focusKeepsDrawing)
    wintabEvent(event); // Windows Ink off: the tablet driver's pressure for its mouse events (Tablet.cpp)
    SDL_Event e = event;
    mapPenPosition(e); // a tablet in mouse mode moves the cursor relatively (#54), for the canvas and the UI
    if (supportCardEvent(e)) return;  // the support card at launch takes clicks and keys while open
    if (whatsNewEvent(e)) return;     // so does What's New (WhatsNewPopup.cpp)
    if (radialMouseEvent(e)) return;  // clicks on an open radial menu, and the buttons that open one (RadialMenu.cpp)
    if (strokePopupMouseEvent(e)) return; // clicks on the open stroke popup (StrokePopup.cpp)
    ImGui_ImplSDL3_ProcessEvent(&e);
    const ImGuiIO& io = ImGui::GetIO();

    switch (e.type) {
    case SDL_EVENT_QUIT:
        requestQuit();
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        spaceMouseEvent(e); // a SpaceMouse (SpaceMouse.cpp)
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        windowFocused_ = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        windowFocused_ = false;
        if (radial_.hold != RadialHold::None) closeRadialMenu(); // its key or button's release won't come here
        if (strokePopup_.held) closeStrokePopup();
        spaceHeld_ = false; // Space let go in another window never reaches us
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: // dragged to a monitor at another scale, or the system setting changed
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        if (e.window.windowID == SDL_GetWindowID(window_)) requestUiScale();
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (e.window.windowID == SDL_GetWindowID(window_)) requestQuit();
        break;
    case SDL_EVENT_PEN_PROXIMITY_IN:
    case SDL_EVENT_PEN_PROXIMITY_OUT:
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
    case SDL_EVENT_PEN_MOTION:
    case SDL_EVENT_PEN_AXIS:
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP:
        handlePenEvent(e);
        break;
    case SDL_EVENT_MOUSE_ADDED:
    case SDL_EVENT_MOUSE_REMOVED:
        tabletDeviceEvent(e); // a tablet plugged in or out (Tablet.cpp)
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        lastInputPen_ = e.button.which == SDL_PEN_MOUSEID;
        // Strokes and navigation start only on the canvas, never on the shell around it.
        if (!io.WantCaptureMouse && canvasRect().contains(e.button.x, e.button.y)) {
            mouseClicks_ = e.button.clicks;
            onMouseDown(e.button.x, e.button.y, e.button.button);
            mouseClicks_ = 1;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        // A right click that didn't drag: the QuickMenu (#22). (SDL's click for a pen button doesn't count: a pen
        // button opens it only when set to Right click, in penButton.)
        if (e.button.button == SDL_BUTTON_RIGHT && e.button.which != SDL_PEN_MOUSEID) rightClickRelease();
        onMouseUp();
        break;
    case SDL_EVENT_MOUSE_MOTION:
        onMouseMove(e.motion.x, e.motion.y, e.motion.xrel, e.motion.yrel, e.motion.which == SDL_PEN_MOUSEID);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (!io.WantCaptureMouse && canvasRect().contains(e.wheel.mouse_x, e.wheel.mouse_y)) camera_.zoom(e.wheel.y);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (e.key.key == SDLK_SPACE && !e.key.repeat && // hold Space and drag: the camera (#43; not while typing)
            spaceCameraKeyDown(uiPrefs_.spaceCamera, io.WantTextInput || io.WantCaptureKeyboard)) {
            spaceHeld_ = true;
            break;
        }
        if (!io.WantCaptureKeyboard && !e.key.repeat) onKey(e.key.key, e.key.mod);
        else if (!io.WantCaptureKeyboard && (e.key.key == SDLK_LEFTBRACKET || e.key.key == SDLK_RIGHTBRACKET))
            onKey(e.key.key, e.key.mod);
        break;
    case SDL_EVENT_KEY_UP:
        if (e.key.key == SDLK_SPACE) spaceHeld_ = false;
        if (e.key.key == SDLK_SPACE) quickMenuKeyUp(); // a held Space closes the QuickMenu when let go
        radialKeyUp(e.key.key);                        // a radial menu's held key: run the slot pointed at
        strokePopupKeyUp(e.key.key);                   // the stroke popup's held key: pick the item pointed at
        break;
    case SDL_EVENT_DROP_FILE:
        if (e.drop.data) openPathConfirmed(e.drop.data);
        break;
    default:
        break;
    }
}

bool App::pick(float x, float y, RayHit& hit) const {
    if (!doc_.activeSubtoolInfo().visible) return false; // you can't sculpt what you can't see
    glm::vec3 origin, dir;
    canvasRay(x, y, origin, dir);
    return frontRaycast(origin, dir, hit);
}

// The surface under a ray as drawn: with Backface culling on (#68) a back face isn't drawn, so the ray goes on
// through it to the next face it meets (a brush through the opening of an open mesh works on the far side).
bool App::frontRaycast(const glm::vec3& origin, const glm::vec3& dir, RayHit& hit) const {
    if (!surfaceRaycast(doc_.mesh, doc_.bvh, origin, dir, hit)) return false; // (the micro surface on a micro level)
    if (!renderSettings_.backfaceCulling) return true;
    glm::vec3 o = origin;
    for (int i = 0; i < 16 && glm::dot(hit.normal, dir) > 0.0f; ++i) {
        const glm::vec3 d = glm::normalize(dir);
        o = hit.position + d * std::max(1e-6f, 1e-5f * glm::distance(origin, hit.position)); // just past it
        if (!surfaceRaycast(doc_.mesh, doc_.bvh, o, dir, hit)) return false;
    }
    return glm::dot(hit.normal, dir) <= 0.0f;
}

// The crosshair (#73): over the 3D view, where a click sculpts, paints or masks, while a stroke runs (it may
// drag over a panel) and while the view turns, pans or zooms (#88: the arrow flashing in for every orbit was
// distracting). Not over the panels or windows floating above the view (ImGui has the pointer), a gizmo handle,
// or where ImGui asks for a cursor of its own (a resize handle on the edge).
bool App::viewportCursorWanted() const {
    if (uiPrefs_.viewportCursor != 0) return false;
    if (drag_ == Drag::Stroke || drag_ == Drag::MaskCanvas || drag_ == Drag::AirStroke) return true;
    if (drag_ == Drag::Orbit || drag_ == Drag::Pan || drag_ == Drag::Zoom || drag_ == Drag::Roll) return true;
    if (drag_ != Drag::None) return false;
    if (ImGui::GetIO().WantCaptureMouse || !canvasRect().contains(mouseX_, mouseY_)) return false;
    if (ImGui::GetMouseCursor() != ImGuiMouseCursor_Arrow) return false;
    if (gizmoActive() && gizmo_.hover != GizmoHandle::None) return false;
    return true;
}

// The system cursor, set here rather than by ImGui's backend (which knows no crosshair, and would not put its own
// back after another was set): ImGui's choice, or the crosshair over the view. Decided after the frame's UI, from
// that frame's hover state, so a window floating over the view never makes it flicker. SDL_SetCursor runs only
// when the kind changes.
void App::updateMouseCursor() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    const ImGuiMouseCursor ui = ImGui::GetMouseCursor();
    // (Mouse mode with the pen: the system's would sit where the pen is on the tablet, not at the cursor.)
    cursorKind_ = penSoftCursor()           ? kCursorHidden
                  : viewportCursorWanted()  ? kCursorCrosshair
                  : ui == ImGuiMouseCursor_None || io.MouseDrawCursor ? kCursorHidden
                                                                      : int(ui);
    if (cursorKind_ == cursorShown_) return; // (the shape on screen already: nothing for the window system to do)
    const bool wasHidden = cursorShown_ == kCursorHidden || cursorShown_ == -2;
    cursorShown_ = cursorKind_;
    if (cursorKind_ == kCursorHidden) {
        SDL_HideCursor();
        return;
    }
    SDL_SystemCursor id = SDL_SYSTEM_CURSOR_DEFAULT;
    switch (cursorKind_) {
    case kCursorCrosshair: id = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
    case ImGuiMouseCursor_TextInput: id = SDL_SYSTEM_CURSOR_TEXT; break;
    case ImGuiMouseCursor_ResizeAll: id = SDL_SYSTEM_CURSOR_MOVE; break;
    case ImGuiMouseCursor_ResizeNS: id = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
    case ImGuiMouseCursor_ResizeEW: id = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
    case ImGuiMouseCursor_ResizeNESW: id = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
    case ImGuiMouseCursor_ResizeNWSE: id = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
    case ImGuiMouseCursor_Hand: id = SDL_SYSTEM_CURSOR_POINTER; break;
    case ImGuiMouseCursor_Wait: id = SDL_SYSTEM_CURSOR_WAIT; break;
    case ImGuiMouseCursor_Progress: id = SDL_SYSTEM_CURSOR_PROGRESS; break;
    case ImGuiMouseCursor_NotAllowed: id = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
    default: break;
    }
    const size_t slot = size_t(id) < std::size(cursors_) ? size_t(id) : 0;
    if (!cursors_[slot]) cursors_[slot] = SDL_CreateSystemCursor(id);
    if (!cursors_[slot] && id != SDL_SYSTEM_CURSOR_DEFAULT) { // (a system without this one: the arrow)
        if (!cursors_[0]) cursors_[0] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        if (cursors_[0]) SDL_SetCursor(cursors_[0]);
    } else if (cursors_[slot]) {
        SDL_SetCursor(cursors_[slot]);
    }
    if (wasHidden) SDL_ShowCursor();
}

BrushType App::activeBrush() const {
    // ZBrush convention: holding Shift temporarily switches to Smooth.
    // The pen's eraser end can be set to smooth too (Preferences > Tablet).
    const bool smooth = (inputMods() & SDL_KMOD_SHIFT) || (erasing() && eraserAction() == PenAction::Smooth);
    return smooth ? BrushType::Smooth : brush_;
}

void App::onMouseDown(float x, float y, Uint8 button) {
    if (strokeFinishing_) endStroke(); // the last stroke's dabs still being placed: finish them first
    mouseX_ = x;
    mouseY_ = y;
    const SDL_Keymod mod = inputMods();
    const SDL_Keymod nav = inputMods(false); // (navigation ignores the stroke popup's latched Smooth / Mask / Select)
    // A pen button is already driving the view: touching down with the tip keeps navigating.
    if (penNavButton_ && (drag_ == Drag::Orbit || drag_ == Drag::Pan)) return;
    dragFromPen_ = lastInputPen_;
    doc_.syncBvh();
    if (button == SDL_BUTTON_LEFT && erasing() && eraserAction() != PenAction::Invert && eraserAction() != PenAction::Smooth) {
        // The eraser end set to Erase mask or Nothing (Preferences > Tablet).
        RayHit hit;
        if (eraserAction() == PenAction::EraseMask && previewLod_ < 0 && pick(x, y, hit)) beginMaskStroke(x, y);
        return;
    }
    // A right press with no keys held may be a click that opens the QuickMenu (rightClickRelease); it navigates
    // only once it moves past the slop.
    rightClick_.pending = button == SDL_BUTTON_RIGHT && uiPrefs_.rightClickQuickMenu &&
                          !(SDL_GetModState() & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI));
    rightClick_.x = x;
    rightClick_.y = y;
    rightClick_.dx = rightClick_.dy = 0.0f;
    zsMenu_.armed = false;
    // A right click on a Forge Sphere opens its menu (#8): Delete, Insert Between, Set as Root.
    if (button == SDL_BUTTON_RIGHT && zsphereActive() && !doc_.mesh.zspherePreview &&
        !(SDL_GetModState() & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)))
        zsphereRightDown(x, y);
    if (spaceHeld_) { // Preferences > Input > Hold Space and drag to move the camera (#43)
        switch (spaceCameraMove(button, ctrlDown(SDL_GetModState()))) {
        case SpaceCameraMove::Orbit: drag_ = Drag::Orbit; break;
        case SpaceCameraMove::Pan: drag_ = Drag::Pan; break;
        case SpaceCameraMove::Zoom: drag_ = Drag::Zoom; break;
        case SpaceCameraMove::None: break;
        }
        if (drag_ != Drag::None) {
            rightClick_.pending = false;
            return;
        }
    }
    if (button == SDL_BUTTON_RIGHT && ctrlDown(SDL_GetModState()) && (SDL_GetModState() & SDL_KMOD_SHIFT) &&
        !(SDL_GetModState() & SDL_KMOD_ALT)) { // in every input style (Ctrl+right-drag still zooms)
        drag_ = Drag::SizeScrub; // Ctrl+Shift+right-drag: Draw Size, finely (Precision.cpp)
        sizeScrubStart_ = radiusPx_;
        sizeScrubDx_ = 0.0f;
        rightClick_.pending = false;
        return;
    }
    if (blenderStyle() && blenderNavigationDown(button)) return; // middle / Alt+left / right drags navigate
    if (mayaStyle() && mayaNavigationDown(button)) return;       // Alt+left / middle / right: orbit / pan / zoom
    if (button == SDL_BUTTON_LEFT) {
        if (axisGizmoMouseDown(x, y)) return;   // the axis gizmo's ends snap to a side view (#43)
        if (zsphereMouseDown(x, y, mod)) return; // a ZSphere SubTool takes clicks on it (PanelZSpheres.cpp)
        if (polygroupMouseDown(x, y, mod)) return;
        if (maskSelectMouseDown(x, y, mod)) return;
        if (gizmoMouseDown(x, y, mod)) return; // Gizmo 3D handles, pivot clicks (Gizmo.cpp)
        if (uvToolMouseDown(x, y, mod)) return; // the UV Editor's Seam / Select in 3D tool (PanelUvEditor.cpp)
        if (guideMouseDown(x, y, mod)) return; // the QRemesh Guides brush (PanelRetopo.cpp)
        if (faceMouseDown(x, y, mod)) return;  // placing facial landmarks (PanelFace.cpp)
        if (sculptableMouseDown(x, y)) return; // Make sculptable: landmarks, crop box (PanelSculptable.cpp)
        if (captureMouseDown(x, y)) return;    // Capture from view: the drag draws the square (PanelKelvinlet.cpp)
        // A triple-click on another SubTool makes it the active one and frames it (#43). (Its first two clicks
        // don't sculpt it: brushes work on the active SubTool only.)
        // Not from a pen: quick taps of a stippling pen counted as a triple-click and switched the Part.
        if (mouseClicks_ >= 3 && !lastInputPen_ && !wintabPen_ && !(mod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) &&
            insertType_.empty() && doc_.subtoolCount() > 1) {
            const int target = pickSubtool(x, y);
            if (target >= 0 && target != doc_.activeSubtool) {
                runCommand("subtool_select", {{"index", double(target)}});
                partSelected("triple-click");
                frameActiveSubtool();
                return;
            }
        }
        // Alt+click on another SubTool selects it (ZBrush); Alt+drag stays an inverted stroke or a pan.
        // Shift+Alt+click selects and frames it (ZBrush), the active one too.
        // Only the keyboard's Alt: a pen button set to Invert (Alt) is for subtracting, and a tap with it held
        // switched the Part without the user knowing why.
        const SDL_Keymod keys = keyMods();
        const bool frameClick = (keys & SDL_KMOD_ALT) && (mod & SDL_KMOD_SHIFT) && !ctrlDown(mod);
        if (!blenderStyle() && (keys & SDL_KMOD_ALT) && !ctrlDown(mod) && (frameClick || !(mod & SDL_KMOD_SHIFT)) &&
            (doc_.subtoolCount() > 1 || frameClick)) {
            const int target = pickSubtool(x, y);
            subtoolClickFrame_ = frameClick;
            if (target >= 0 && (target != doc_.activeSubtool || frameClick)) {
                drag_ = Drag::SubToolClick;
                subtoolClickTarget_ = target;
                subtoolClickX_ = x;
                subtoolClickY_ = y;
                subtoolClickNs_ = clickClockNs();
                return;
            }
        }
        RayHit hit;
        const bool maskMod = ctrlDown(mod) && !(mod & SDL_KMOD_SHIFT);
        if (!insertType_.empty() && !maskMod && previewLod_ < 0 && pick(x, y, hit)) { // Append > Insert (#34)
            drag_ = Drag::Insert;
            insertAt_ = hit.position;
            insertNormal_ = hit.normal;
            insertX_ = x;
            insertY_ = y;
            insertPx_ = 0.0f;
            return;
        }
        if (previewLod_ < 0 && pick(x, y, hit)) {
            if (maskMod) beginMaskStroke(x, y);
            else beginStroke(x, y, hit);
        } else if (airStrokeMouseDown(x, y, mod)) { // Start strokes off the model (#62, Input.cpp)
        } else if (blenderStyle()) {
            // Blender: a drag beside the model navigates as the middle button would (Shift pans, Ctrl zooms).
            const SDL_Keymod keys = SDL_GetModState();
            drag_ = (keys & SDL_KMOD_SHIFT) ? Drag::Pan : ctrlDown(keys) ? Drag::Zoom : Drag::Orbit;
        } else if (ctrlDown(nav) && !(nav & SDL_KMOD_SHIFT) && !(nav & SDL_KMOD_ALT) && previewLod_ < 0) {
            drag_ = Drag::MaskCanvas; // click inverts, drag draws a mask rectangle (PanelMasking.cpp)
            maskCanvasX_ = x;
            maskCanvasY_ = y;
            maskCanvasMoved_ = false;
        } else {
            // Dragging on empty space navigates (ZBrush style).
            drag_ = (nav & SDL_KMOD_ALT) ? Drag::Pan : ctrlDown(nav) ? Drag::Zoom : Drag::Orbit;
            panFromAlt_ = drag_ == Drag::Pan;
            // ZBrush: Shift+click, let go of Shift, then drag turns the view about its own axis (Z).
            rollPending_ = drag_ == Drag::Orbit && (nav & SDL_KMOD_SHIFT);
            rollStartX_ = x;
            rollStartY_ = y;
        }
    } else if (button == SDL_BUTTON_RIGHT) {
        // ZBrush's right-click navigation: rotate (Shift snaps), Alt pans (release Alt to zoom), Ctrl zooms.
        drag_ = (nav & SDL_KMOD_ALT) ? Drag::Pan : ctrlDown(nav) ? Drag::Zoom : Drag::Orbit;
        panFromAlt_ = (nav & SDL_KMOD_ALT) != 0;
    } else if (button == SDL_BUTTON_MIDDLE) {
        drag_ = (nav & SDL_KMOD_CTRL) ? Drag::Zoom : Drag::Pan;
    }
}

// Blender: middle-drag orbits, Shift+middle pans, Ctrl+middle zooms; Alt+left-drag beside the model (anywhere with
// Emulate 3 Button Mouse) is the emulated middle button (pens, trackpads); the right button drags the same way. True
// when the press started navigating.
bool App::mayaNavigationDown(Uint8 button) {
    const SDL_Keymod keys = SDL_GetModState(); // the keyboard itself, before inputMods' Maya mapping
    if (!(keys & SDL_KMOD_ALT) || ctrlDown(keys) || (keys & SDL_KMOD_SHIFT)) return false; // Alt alone navigates
    if (button == SDL_BUTTON_LEFT) drag_ = Drag::Orbit;
    else if (button == SDL_BUTTON_MIDDLE) drag_ = Drag::Pan;
    else if (button == SDL_BUTTON_RIGHT) drag_ = Drag::Zoom;
    else return false;
    rightClick_.pending = false;
    return true;
}

bool App::blenderNavigationDown(Uint8 button) {
    const SDL_Keymod keys = SDL_GetModState(); // the keyboard itself: blenderMods turns Ctrl into the invert
    // Alt+left: Blender 5.1+ masks with it on the model (blenderMods, #77); beside the model, or always with
    // Preferences > Input > Emulate 3 Button Mouse, it is the emulated middle button.
    RayHit hit;
    const bool emulated = button == SDL_BUTTON_LEFT && (keys & SDL_KMOD_ALT) &&
                          (uiPrefs_.emulate3Button || (keys & SDL_KMOD_SHIFT) || previewLod_ >= 0 ||
                           !pick(mouseX_, mouseY_, hit)); // (Alt+Shift: unbound in Blender, pans here anywhere)
    if (button != SDL_BUTTON_MIDDLE && button != SDL_BUTTON_RIGHT && !emulated) return false;
    drag_ = (keys & SDL_KMOD_SHIFT) ? Drag::Pan : ctrlDown(keys) ? Drag::Zoom : Drag::Orbit;
    return true;
}

void App::onMouseMove(float x, float y, float dx, float dy, bool fromPen) {
    // (A driver's stray mouse events beside the pen's never get here: strayMouseMotion.) A pen that went quiet
    // without saying it left (some drivers never do) hands the cursor back to the mouse.
    if (!fromPen && penInProximity_) penLeft();
    if (fromPen) {
        // The pen moves by the distance from its last position taken here, not by SDL's xrel: SDL keeps one cursor
        // position for the pen and every mouse, so with a driver's stray mouse events in between (a Ugee pen on
        // X11, #7) each pen event's xrel jumped back from the stray position and the view shook instead of turning.
        dx = x - mouseX_;
        dy = y - mouseY_;
        // The pen's first position after coming into range, or after the mouse moved the cursor, is a jump from
        // wherever the cursor was, not a motion: it must not throw a drag (the view) across the screen.
        if (penJump_ || !lastInputPen_) dx = dy = 0.0f;
        penJump_ = false;
    }
    mouseX_ = x;
    mouseY_ = y;
    lastInputPen_ = fromPen;
    if (rightClick_.pending) { // a right click so far: the view waits until it is a drag
        // (By the motion's own steps, not the position: some input moves only relatively.) Past the slop, the
        // drag goes on by everything since the press, so it loses nothing.
        rightClick_.dx += dx;
        rightClick_.dy += dy;
        if (std::hypot(rightClick_.dx, rightClick_.dy) <= kRightClickSlop) return;
        rightClick_.pending = false;
        dx = rightClick_.dx;
        dy = rightClick_.dy;
    }
    // ZBrush: start a pan with Alt, then let go of Alt while still dragging to zoom.
    if (drag_ == Drag::Pan && panFromAlt_ && !(inputMods(false) & SDL_KMOD_ALT)) drag_ = Drag::Zoom;
    // Shift+press on the background: letting go of Shift before the drag gets going rolls the view instead.
    if (rollPending_ && drag_ == Drag::Orbit) {
        if (!(inputMods(false) & SDL_KMOD_SHIFT)) {
            drag_ = Drag::Roll;
            rollPending_ = false;
        } else if (std::hypot(x - rollStartX_, y - rollStartY_) > 6.0f) {
            rollPending_ = false; // a Shift-snapped rotation after all
        } else {
            return;
        }
    }
    switch (drag_) {
    case Drag::Stroke: strokeTo(x, y); break;
    case Drag::Orbit: orbitBy(dx, dy); break;
    case Drag::Roll: rollBy(x, y, dx, dy); break;
    case Drag::Pan: camera_.pan(dx, dy, canvasHeight()); break;
    case Drag::Zoom: camera_.zoom(zoomDragAmount(dx, dy)); break;
    case Drag::MaskCanvas: maskCanvasMove(x, y); break;
    case Drag::SubToolClick: subtoolClickMove(x, y); break;
    case Drag::Select: selectionMove(x, y); break;
    case Drag::Gizmo: gizmoDragTo(x, y); break;
    case Drag::Guide: guideDragTo(x, y); break;
    case Drag::UvTool: uvToolDragTo(x, y); break;
    case Drag::Sculptable: sculptableMouseMove(x, y); break;
    case Drag::Capture: captureMouseMove(x, y); break;
    case Drag::ZSphere: zsphereDragTo(x, y); break;
    case Drag::AirStroke: airStrokeTo(x, y); break;
    case Drag::SizeScrub:
        sizeScrubDx_ += dx;
        radiusPx_ = drawSizeScrub(sizeScrubStart_, sizeScrubDx_);
        break;
    case Drag::None: break;
    }
}

// How far a zoom drag zooms in (negative: out). ZBrush: dragging up (forward) zooms out; the SculptForge and
// Blender styles zoom in. Maya (#32): a drag to the right zooms in, as Maya's own dolly (Preferences > Input >
// Zoom drag can make it vertical). Invert zoom turns either around.
float App::zoomDragAmount(float dx, float dy) const {
    float a = mayaStyle() && uiPrefs_.mayaZoomHorizontal ? dx : zbrushStyle() ? dy : -dy;
    if (uiPrefs_.invertZoom) a = -a;
    return a * 0.05f;
}

void App::orbitBy(float dx, float dy) {
    if (autoOrtho_) { // an axis view turned: back to perspective (Blender's Auto Perspective)
        camera_.ortho = false;
        autoOrtho_ = false;
    }
    if (!orbitStarted_) {
        orbitRaw_ = camera_;
        orbitStarted_ = true;
    }
    if (uiPrefs_.turntable || forceTurntable_) orbitRaw_.orbit(dx, dy);
    else orbitRaw_.orbitFree(dx, dy);
    // Holding Shift snaps to the nearest view along an axis; letting go continues from where the cursor took it.
    if (inputMods(false) & SDL_KMOD_SHIFT) camera_.setOrientation(Camera::snapped(orbitRaw_.orientation()));
    else camera_.setOrientation(orbitRaw_.orientation());
}

void App::rollBy(float x, float y, float dx, float dy) {
    // Turn about the view axis by the angle the cursor sweeps around the canvas centre (a sideways drag far
    // from the centre still turns, by its length).
    const CanvasRect c = canvasRect();
    const glm::vec2 centre(c.x + c.w * 0.5f, c.y + c.h * 0.5f);
    const glm::vec2 now(x, y), before(x - dx, y - dy);
    float angle = 0.0f;
    if (glm::length(now - centre) > 20.0f && glm::length(before - centre) > 20.0f) {
        const glm::vec2 a = before - centre, b = now - centre;
        angle = std::atan2(a.x * b.y - a.y * b.x, glm::dot(a, b));
    } else {
        angle = dx * 0.008f;
    }
    camera_.setOrientation(camera_.orientation() * glm::angleAxis(-angle, glm::vec3(0, 0, 1)));
}

void App::onMouseUp() {
    rollPending_ = false;
    rightClick_.pending = false;
    zsMenu_.armed = false;
    orbitStarted_ = false;
    panFromAlt_ = false;
    if (drag_ == Drag::Select) selectionMouseUp();
    if (drag_ == Drag::Insert) {
        // The drag's length on screen, as a size at the surface point's depth (at least a small default).
        const float px = std::max(insertPx_, 12.0f * uiScale_);
        const float dist = glm::length(camera_.position() - insertAt_);
        const float worldPerPx = 2.0f * dist * std::tan(camera_.fovY * 0.5f) / std::max(canvasRect().h, 1.0f);
        Args args{{"x", double(insertAt_.x)}, {"y", double(insertAt_.y)},
                  {"z", double(insertAt_.z)}, {"nx", double(insertNormal_.x)},
                  {"ny", double(insertNormal_.y)}, {"nz", double(insertNormal_.z)},
                  {"size", double(2.0f * px * worldPerPx)},
                  // the drag's direction on screen turns it about the normal
                  {"twist", insertPx_ > 4.0f ? double(glm::degrees(std::atan2(mouseY_ - insertY_, mouseX_ - insertX_))) : 0.0}};
        args = withSymmetryArgs(std::move(args));
        if (insertType_ == "mesh") { // your own mesh: a SubTool's copy or a file (insert_mesh)
            args[insertMeshKey_] = insertMeshValue_;
            runCommand("insert_mesh", args);
        } else {
            args["type"] = insertType_;
            runCommand("insert_primitive", args);
        }
    }
    if (drag_ == Drag::Stroke && !maskClickPending_) steadyCatchUp(); // Steady Stroke: on to the pointer (Precision.cpp)
    if (drag_ == Drag::SizeScrub) status_ = drawSizeStatus();
    if (drag_ == Drag::Stroke && maskClickPending_) maskClickRelease(); // Ctrl+click: blur / sharpen the mask
    else if (drag_ == Drag::Stroke && asyncStroke_) {
        // Predictive strokes: letting go doesn't wait for the dabs still to place; the solver carries on and
        // pumpStroke ends the stroke once they are on screen (anything that needs the document first ends it).
        strokeFinishing_ = true;
        // (even with nothing left to place: the stroke's end work runs on the solver thread, pumpStroke)
    } else if (drag_ == Drag::Stroke) endStroke();
    if (drag_ == Drag::MaskCanvas) maskCanvasRelease();
    if (drag_ == Drag::SubToolClick) subtoolClickRelease();
    if (drag_ == Drag::Guide) guideRelease();
    if (drag_ == Drag::UvTool) uvToolRelease();
    if (drag_ == Drag::Sculptable) sculptableMouseUp();
    if (drag_ == Drag::Capture) captureMouseUp();
    if (drag_ == Drag::ZSphere) zsphereRelease();
    if (drag_ == Drag::AirStroke) airStrokeRelease();
    gizmoRelease();
    drag_ = Drag::None;
    dragFromPen_ = false;
    penNavButton_ = 0;
}

void App::onKey(SDL_Keycode key, SDL_Keymod mod) {
    if (cutPolylineKey(key)) return; // Enter ends a Cut Polyline, Esc drops it (PanelCut.cpp)
    if (key == SDLK_ESCAPE && radial_.menu >= 0) {
        closeRadialMenu();
        return;
    }
    if (key == SDLK_ESCAPE && strokePopup_.open) {
        closeStrokePopup();
        return;
    }
    if (key == SDLK_ESCAPE && !insertType_.empty()) { // Append > Insert (#34) off
        insertType_.clear();
        status_ = "Insert off";
        return;
    }
    if (captureShortcutKey(key, mod)) return; // Ctrl+Alt+click a button, then press its new key
    if (uvEditorKey(key, mod)) return;        // keys over the UV Editor's view (PanelUvEditor.cpp)
    if (key == SDLK_ESCAPE && capture_.armed) { // Capture from view waiting for its square (PanelKelvinlet.cpp)
        capture_.armed = false;
        status_ = "Capture cancelled";
        return;
    }
    if (lightBoxKey(key, mod)) return;        // , LightBox, B / M pickers, S / I pop-ups (LightBox.cpp)
    // Blender's Emulate Numpad: the number row works as the numpad, unless the key itself has an action.
    if (blenderStyle() && uiPrefs_.emulateNumpad && key >= SDLK_0 && key <= SDLK_9 &&
        !actions_.actionForChord(chordFromEvent(key, mod)))
        key = key == SDLK_0 ? SDLK_KP_0 : SDL_Keycode(SDLK_KP_1 + (key - SDLK_1));
    keyDown_ = key; // a radial menu's key holds it open until this key is let go (RadialMenu.cpp)
    actions_.handleKey(key, mod);             // the shortcut map: Config.cpp initActions()
    keyDown_ = SDLK_UNKNOWN;
}

// ---------------------------------------------------------------------------
// Strokes
// ---------------------------------------------------------------------------

bool App::alongNormalBrush(BrushType t) const {
    return t == BrushType::Move || t == BrushType::SnakeHook || t == BrushType::KelvinSnake || t == BrushType::KelvinGrab;
}

// Along the normal (from the discussions): with the invert key held as the stroke begins (Preferences > Input >
// Along the normal), the grab slides along the average normal under the brush rather than across the view. These
// brushes don't use the invert key otherwise (Alt flips only Elastic Tendril's click-without-moving bump).
void App::beginAlongNormal(const RayHit& hit, float radius) {
    strokeAlongNormal_ = false;
    if (!alongNormalBrush(strokeBrush_) || uiPrefs_.alongNormal == 2) return;
    const bool key = (inputMods() & SDL_KMOD_ALT) != 0 || (erasing() && eraserAction() == PenAction::Invert);
    if (uiPrefs_.alongNormal == 0 && !key) return;
    glm::vec3 n = brushAreaNormal(doc_.mesh, doc_.bvh, hit.position, std::max(radius, 1e-6f), focalShift_);
    if (glm::length(n) < 0.5f) n = hit.normal;
    if (glm::length(n) < 0.5f) return;
    if (glm::dot(n, camera_.forward()) > 0.0f && glm::dot(hit.normal, camera_.forward()) < 0.0f) n = -n; // (outward, to the viewer)
    strokeAlongNormal_ = true;
    alongNormalDir_ = glm::normalize(n);
    alongNormalAnchor_ = hit.position;
    alongNormalStartY_ = lastY_;
    strokeInvert_ = invertToggle_; // (the key chose the direction, it doesn't invert the stroke)
    status_ = std::string(brushLabel(strokeBrush_)) + ": along the normal";
}

// The point on the normal line the cursor asks for: nearest the cursor's ray, or, when the normal points at the
// viewer (the cursor can't say how far along it), by how far the cursor went up since the press.
glm::vec3 App::alongNormalTarget(float x, float y) const {
    glm::vec3 origin, dir;
    canvasRay(x, y, origin, dir);
    float s = 0.0f;
    if (!lineRayClosest(alongNormalAnchor_, alongNormalDir_, origin, dir, s))
        s = (alongNormalStartY_ - y) * camera_.worldPerPixel(alongNormalAnchor_, canvasHeight());
    return alongNormalAnchor_ + alongNormalDir_ * s;
}

void App::beginStroke(float x, float y, const RayHit& hit) {
    refreshSymmetryPivot(); // (local symmetry: the active Part's centre as the stroke starts)
    strokeBrush_ = activeBrush();
    strokeAlongNormal_ = false;
    straight_ = {};
    straight_.shiftAtStart = (inputMods() & SDL_KMOD_SHIFT) != 0;
    strokeAltSmooth_ = false;
    precision_.reset(glm::vec2(x, y)); // (Precision.cpp)
    dabAccuracy_ = {};
    precisionActive_ = false;
    steadyCursor_ = glm::vec2(x, y);
    pickSmoother_.reset();
    strokeInvert_ = invertToggle_ != (((inputMods() & SDL_KMOD_ALT) != 0) || (erasing() && eraserAction() == PenAction::Invert));
    // Shift still smooths geometry while the Paint brush is active (ZBrush behaviour).
    strokePaint_ = strokeBrush_ != BrushType::Smooth && (paintMode_ || paintRgb_);
    strokeSculpt_ = !(strokePaint_ && paintMode_);
    const BrushRecipe* recipe = strokeRecipe();
    { // the brush's own pressure curve (Precision.cpp)
        const auto it = brushCurves_.find(pressureCurveKey(strokeBrush_, recipe));
        strokeCurve_ = it == brushCurves_.end() ? PressurePoints() : it->second;
    }
    haveLastDab_ = false;
    doc_.sculptor.draftStrokes = draftStrokes() && asyncStrokeAllowed(); // (AsyncStroke.cpp)
    if (!doc_.sculptor.beginStroke(doc_.mesh,
                                   !strokeSculpt_ ? std::string("Paint") : recipe ? recipe->name : brushLabel(strokeBrush_),
                                   strokeSculpt_ ? doc_.activeLayer : -1)) {
        status_ = "Active layer weight is 0 - raise it to sculpt into the layer";
        drag_ = Drag::Orbit;
        return;
    }
    drag_ = Drag::Stroke;
    playingLayer_ = -1;
    if (strokeType_ == StrokeType::DragRect || strokeType_ == StrokeType::DragDot) doc_.sculptor.holdTopology(); // (they re-stamp)
    // (Only when there are LODs: clearing forgets which mesh was shown, so the next frame sent the whole mesh
    // again, 0.3 s at 21M triangles before the first dab showed.)
    if (!lods_.empty() || previewLod_ >= 0) clearLods();
    beginPaintStroke();
    if (doc_.sculptor.dyntopo.enabled && strokeSculpt_ && !dyntopoBlocker(doc_.mesh).empty())
        status_ = "Dynamic is on, but this Part has subdivision levels: sculpting without remeshing";
    lastX_ = x;
    lastY_ = y;
    strokePath_.reset(x, y);
    dabQueue_.clear();
    dabSeconds_ = prepSeconds_ = 0.0; // measured again: the mesh and the brush may have changed
    // Predictive strokes (AsyncStroke.cpp): the solver thread, the ghost's first estimate (the picked triangle);
    // the stroke's own copies of the recipe and the alpha, which the solver thread reads (DabSettings).
    asyncStroke_ = asyncStrokeAllowed();
    strokeRecipe_ = asyncStroke_ && recipe ? std::make_shared<const BrushRecipe>(*recipe) : nullptr;
    strokeAlpha_ = asyncStroke_ && !doc_.brushAlpha.empty() ? std::make_shared<const BrushAlpha>(doc_.brushAlpha) : nullptr;
    pointerLatest_ = glm::vec2(x, y);
    pointerLatestAt_ = double(SDL_GetTicksNS()) * 1e-9;
    penPredictor_.reset(pointerLatest_, pointerLatestAt_);
    {
        const glm::vec3 n = glm::dot(hit.normal, camera_.forward()) > 0.0f ? -hit.normal : hit.normal;
        ghostFrame_ = {hit.position, glm::length(n) > 0.0f ? glm::normalize(n) : -camera_.forward(), hit.position, true};
        const glm::uvec3 t = hit.triangle < doc_.mesh.triangles.size() ? doc_.mesh.triangles[hit.triangle] : glm::uvec3(0);
        const auto& P = doc_.mesh.positions;
        ghostCurvature_ = 0.0f;
        ghostEdge_ = (glm::distance(P[t.x], P[t.y]) + glm::distance(P[t.y], P[t.z]) + glm::distance(P[t.z], P[t.x])) / 3.0f;
    }

    if (strokeBrush_ == BrushType::Move && !recipe && strokeSculpt_) {
        const float pressure = pressureSize_ ? std::max(strokePressure(), 0.05f) : 1.0f;
        DabParams p;
        p.center = hit.position;
        p.viewDir = camera_.forward();
        p.radius = radiusPx_ * pressure * camera_.worldPerPixel(hit.position, canvasHeight());
        p.strength = strength_;
        p.focal = focalShift_;
        p.connectedOnly = uiPrefs_.connectedOnly;
        p.viewFalloff = uiPrefs_.viewFalloff;
        grabPending_ = false;
        doc_.sculptor.beginGrab(doc_.mesh, doc_.bvh, p, symmetry_);
        grabPlanePoint_ = grabLast_ = hit.position;
        beginAlongNormal(hit, p.radius);
        return;
    }
    if (kelvinStrokeBrush()) { // KelvinGrab / Twist / Scale (PanelKelvinlet.cpp)
        kelvinBegin(hit);
        if (strokeBrush_ == BrushType::KelvinGrab) beginAlongNormal(hit, kelvinRadius_);
        return;
    }
    lazyMouse_.reset(glm::vec2(x, y));
    if ((strokeBrush_ == BrushType::SnakeHook || strokeBrush_ == BrushType::KelvinSnake) && !recipe && strokeSculpt_) {
        grabPlanePoint_ = grabLast_ = hit.position; // the pulled tip (see snakeHookTo)
        beginAlongNormal(hit, radiusPx_ * camera_.worldPerPixel(hit.position, canvasHeight()));
        return;
    }
    if (strokeType_ == StrokeType::DragRect) {
        dragAnchor_ = glm::vec2(x, y);
        dragAnchorHit_ = hit.position;
        return; // the stamp appears once the drag gives it a size
    }
    if (strokeType_ == StrokeType::DragDot) {
        dragDotTo(x, y); // one stamp under the cursor, which follows it until release (PanelVdm.cpp)
        return;
    }
    firstDab(x, y);
}

void App::strokeTo(float x, float y) {
    strokeMovedAt_ = double(SDL_GetTicksNS()) * 1e-9; // (Continuous dabs wait for the pointer to rest)
    if (asyncStroke_) {
        pointerLatest_ = glm::vec2(x, y);
        pointerLatestAt_ = double(SDL_GetTicksNS()) * 1e-9;
        penPredictor_.add(pointerLatest_, pointerLatestAt_);
    }
    // ZBrush's alternate smooth (#40): Shift let go while the stroke it began goes on.
    if (strokeBrush_ == BrushType::Smooth && straight_.shiftAtStart && !strokeAltSmooth_ && !strokeMask_ &&
        !(inputMods() & SDL_KMOD_SHIFT)) {
        strokeAltSmooth_ = true;
        status_ = smoothKeepVolume_ ? "Smooth: plain (Shift let go)" : "Smooth: keeping volume (Shift let go)";
    }
    if (maskClickPending_) { // Ctrl+press on the model: a drag past the click slop paints the mask after all
        if (std::hypot(x - maskClickX_, y - maskClickY_) < 4.0f) return;
        maskClickPending_ = false;
        firstDab(maskClickX_, maskClickY_);
    }
    { // Shift+Alt held: the brush moves at a quarter of the pen's speed (Precision.cpp)
        precisionActive_ = precisionHeld();
        const glm::vec2 p = precision_.follow(glm::vec2(x, y), precisionActive_);
        x = p.x;
        y = p.y;
    }
    const BrushRecipe* recipe = strokeRecipe();
    if (strokeBrush_ == BrushType::Move && !recipe && strokeSculpt_ && strokeAlongNormal_) {
        const glm::vec3 p = alongNormalTarget(x, y);
        queueGrab(p - grabLast_);
        grabLast_ = p;
        return;
    }
    if (strokeBrush_ == BrushType::Move && !recipe && strokeSculpt_) {
        glm::vec3 origin, dir;
        canvasRay(x, y, origin, dir);
        glm::vec3 n = camera_.forward();
        float denom = glm::dot(dir, n);
        if (std::abs(denom) < 1e-6f) return;
        float t = glm::dot(grabPlanePoint_ - origin, n) / denom;
        glm::vec3 p = origin + dir * t;
        queueGrab(p - grabLast_);
        grabLast_ = p;
        return;
    }
    if (kelvinStrokeBrush()) {
        kelvinTo(x, y);
        return;
    }
    if ((strokeBrush_ == BrushType::SnakeHook || strokeBrush_ == BrushType::KelvinSnake) && !recipe && strokeSculpt_) {
        snakeHookTo(x, y);
        return;
    }
    if (steadyOn()) { // Steady Stroke, with every stroke type: the brush trails the pointer on a string
        steadyCursor_ = glm::vec2(x, y);
        const glm::vec2 lazy = lazyMouse_.follow(steadyCursor_);
        x = lazy.x;
        y = lazy.y;
    }
    if (strokeType_ == StrokeType::DragRect && !strokeMask_) { // mask strokes paint dots
        dragRectTo(x, y);
        return;
    }
    if (strokeType_ == StrokeType::DragDot && !strokeMask_) {
        dragDotTo(x, y);
        return;
    }
    { // Shift pressed after the stroke began: a straight line (ZBrush)
        const glm::vec2 c = constrainStroke(x, y);
        x = c.x;
        y = c.y;
    }

    if (firstDabPending_) {
        // The pen touched down without a real pressure yet: the stroke starts where it first presses
        // (a pen that has never reported pressure draws from its first motion).
        if (!penTracker_.readyForFirstDab() && penTracker_.everSampled()) {
            lastX_ = x;
            lastY_ = y;
            strokePath_.reset(x, y);
            return;
        }
        firstDabPending_ = false;
        lastStrokePressure_ = strokePressure();
        if (asyncStroke_) dabQueue_.push({lastX_, lastY_, lastStrokePressure_}); // (the solver thread places it)
        else dabAt(lastX_, lastY_, lastStrokePressure_);
    }

    // Place dabs at even screen-space spacing along the cursor path. Every input sample counts (pens
    // send 100-200+ a second, several per frame); the pressure is interpolated between samples.
    // The dabs are queued, not placed here: on a huge mesh one dab can cost more than a frame, and placing every
    // dab of a long gap before the next look at the pointer made the gaps longer still, until the stroke was one
    // straight line. The interactive loop works through the queue for a limited time per frame (drainStrokeDabs),
    // and a gap between samples that is long is drawn as a curve (core/StrokePath.h).
    const float spacing = dabSpacingPx(radiusPx_, recipe ? recipe->spacing : 0.15f); // (tiny brushes: closer)
    const float p0 = lastStrokePressure_, p1 = strokePressure();
    strokeScratch_.clear();
    // (a Shift line stays straight: no curve through its corner)
    strokePath_.addSample(x, y, p0, p1, spacing, interactiveStrokes_ && !straight_.active, strokeScratch_);
    for (const PathDab& d : strokeScratch_) dabQueue_.push(d);
    lastX_ = x;
    lastY_ = y;
    lastStrokePressure_ = p1;
    if (interactiveStrokes_) {
        // Too far behind the pointer: place every second waiting dab only (a wider spacing under load). The limit
        // is in time, not dabs: what the last dabs cost says how many are kStrokeBacklogSeconds of work, and never
        // fewer than kStrokeBacklogMinDabs. A light mesh places hundreds of dabs a frame, so a small brush moved
        // fast keeps its spacing (several input samples arrive before each frame); on a heavy mesh the brush stays
        // near the pointer (a floor of 24 dabs was a second of work behind it at 21M triangles, and releasing
        // placed all of them). SF_STROKE_BACKLOG=0 never drops dabs; a number is a fixed limit in dabs.
        static const long fixedLimit = [] {
            const char* env = std::getenv("SF_STROKE_BACKLOG");
            return env && *env ? std::max(0L, std::strtol(env, nullptr, 10)) : -1L;
        }();
        dabQueue_.thin(strokeBacklogLimit(dabSeconds_, fixedLimit, kStrokeBacklogSeconds, kStrokeBacklogMinDabs));
    } else {
        drainStrokeDabs(0.0); // scripts, UI tests and recordings place every dab at once, as before
    }
}

void App::drainStrokeDabs(double budgetSeconds) {
    if (dabQueue_.empty()) return;
    static const bool log = std::getenv("SF_STROKE_LOG") != nullptr; // per frame: dabs waiting, placed, ms
    const auto start = std::chrono::steady_clock::now();
    const size_t waiting = dabQueue_.size();
    const SculptTimings before = doc_.sculptor.timings;
    size_t placed = 0;
    while (!dabQueue_.empty()) {
        const PathDab d = dabQueue_.pop();
        const auto dabStart = std::chrono::steady_clock::now();
        dabAt(d.x, d.y, d.pressure);
        ++placed;
        // what a dab costs on this mesh with this brush (the backlog limit in strokeTo)
        const double cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - dabStart).count();
        dabSeconds_ = dabSeconds_ > 0.0 ? dabSeconds_ * 0.8 + cost * 0.2 : cost;
        if (budgetSeconds > 0.0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= budgetSeconds)
            break; // at least one dab per call, so a stroke always moves on
    }
    if (log) {
        const SculptTimings& t = doc_.sculptor.timings;
        SDL_Log("stroke: %llu waiting, %llu placed in %.1f ms (kernel %.1f, normals %.1f, BVH %.1f, %llu vertices), %llu left",
                (unsigned long long)waiting, (unsigned long long)placed,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
                t.kernelMs - before.kernelMs, t.normalsMs - before.normalsMs, t.bvhMs - before.bvhMs,
                (unsigned long long)(t.vertices - before.vertices), (unsigned long long)dabQueue_.size());
    }
}

void App::dabAt(float x, float y, float pressure) {
    // On the solver thread (predictive strokes) the dab sees the view and the brush settings as they were when its
    // batch began (DabSettings), and nothing the main thread changes meanwhile.
    const DabSettings live = dabView_ ? DabSettings() : dabSettings();
    const DabSettings& st = dabView_ ? dabView_->settings : live;
    if (dabView_) {
        if (dabView_->probe) dabView_->probe->emplace_back(x, y);
        if (dabView_->radiusProbe) dabView_->radiusProbe->push_back(st.radiusPx);
    } else if (strokeProbe_) {
        strokeProbe_->emplace_back(x, y);
        if (strokeRadiusProbe_) strokeRadiusProbe_->push_back(st.radiusPx);
    }
    const Camera& cam = dabView_ ? dabView_->camera : camera_;
    const glm::vec2 pathPoint(x, y); // (before any scatter: the rake follows the stroke itself)
    const BrushRecipe* recipe = st.recipe;
    if (recipe && recipe->jitter > 0.0f) {
        // Deterministic per-stroke scatter (xorshift).
        auto rnd = [this] {
            jitterSeed_ ^= jitterSeed_ << 13;
            jitterSeed_ ^= jitterSeed_ >> 17;
            jitterSeed_ ^= jitterSeed_ << 5;
            return float(jitterSeed_ & 0xffff) / 65535.0f * 2.0f - 1.0f;
        };
        x += rnd() * recipe->jitter * st.radiusPx;
        y += rnd() * recipe->jitter * st.radiusPx;
    }
    dabRadiusScale_ = dabStrengthScale_ = 1.0f;
    dabAngle_ = 0.0f;
    if (st.strokeType == StrokeType::Spray) {
        const SprayDab s = spray_.next(st.radiusPx, st.sprayScatter);
        x += s.offset.x;
        y += s.offset.y;
        dabRadiusScale_ = s.scale;
        dabStrengthScale_ = s.strength;
        dabAngle_ = s.angle;
    }
    RayHit hit;
    // Off the surface but the brush still over it (sliding over a silhouette, #84): the nearest surface within it.
    auto cast = [&](float cx, float cy, RayHit& h) { return dabView_ ? pickWith(*dabView_, cx, cy, h) : pick(cx, cy, h); };
    if (!pickNearCursor(x, y, st.radiusPx * dabRadiusScale_, cast, hit)) return;
    if (pressure < 0.0f) pressure = strokePressure();
    pressure = st.pressureCurve.apply(pressure); // the brush's own curve, after the tablet's

    const float canvasH = dabView_ ? dabView_->canvas.h : canvasHeight();
    const float baseRadius = st.worldRadius > 0.0f ? st.worldRadius : st.radiusPx * cam.worldPerPixel(hit.position, canvasH);
    { // The hit's distance along the ray, filtered: on dense meshes it jitters with every tiny bump (Precision.cpp)
        const glm::vec3 eye = cam.position();
        const glm::vec3 ray = cam.ortho ? cam.forward() : hit.position - eye;
        const float len = glm::length(ray);
        if (len > 1e-9f) {
            const glm::vec3 dir = ray / len;
            const glm::vec3 from = cam.ortho ? hit.position - dir * glm::dot(hit.position - eye, dir) : eye;
            const float t = glm::dot(hit.position - from, dir);
            const float filtered = pickSmoother_.filter(t, baseRadius * 0.5f);
            hit.position = from + dir * filtered;
            if (!dabView_) dabAccuracy_.add(t, filtered, baseRadius);
        }
    }
    if (!dabView_) { // where the dab's centre shows on screen, against where it was asked for (UI tests: sub-pixel)
        const CanvasRect cr = canvasRect();
        const glm::vec4 clip = cam.projection() * cam.view() * glm::vec4(hit.position, 1.0f);
        if (clip.w != 0.0f) {
            const glm::vec2 ndc = glm::vec2(clip) / clip.w;
            const glm::vec2 shown(cr.x + (ndc.x + 1.0f) * 0.5f * cr.w, cr.y + (1.0f - ndc.y) * 0.5f * cr.h);
            dabAccuracy_.maxErrorPx = std::max(dabAccuracy_.maxErrorPx, glm::distance(shown, glm::vec2(x, y)));
        }
    }

    DabParams p;
    p.center = hit.position;
    p.viewDir = cam.forward();
    p.radius = baseRadius * (st.pressureSize ? std::max(pressure, 0.05f) : 1.0f) * dabRadiusScale_;
    // Smooth keeps its own Z Intensity (ZBrush), whether picked or held with Shift.
    p.strength = (st.brush == BrushType::Smooth ? st.smoothStrength : st.strength) * dabStrengthScale_;
    p.keepVolume = st.brush == BrushType::Smooth && st.smoothKeepVolume;
    p.groupBorders = st.brush == BrushType::Smooth && st.smoothGroupBorders;
    p.pressure = st.pressureStrength ? pressure : 1.0f;
    p.invert = st.invert;
    p.frontFacesOnly = st.frontFacesOnly;
    p.connectedOnly = st.connectedOnly;
    p.viewFalloff = st.viewFalloff;
    p.focal = st.focal;
    if (!dabView_) lastDabScreen_ = glm::vec2(x, y); // (the solver's: when its batch is committed)
    if (st.mask) {
        p.strength = st.maskStrength;
        doc_.sculptor.dabMask(doc_.mesh, doc_.bvh, p, st.symmetry);
        return;
    }
    p.alpha = st.alpha;
    // Alpha options (#76): the fixed angle, the jitter and (Follow stroke direction) Blender's Rake, on top of
    // Spray's own turn. Custom and Community brushes get them too: dabRecipe turns their alpha by alphaUp.
    if (!haveLastDab_) {
        alphaRake_.reset(pathPoint);
        alphaSeed_ = 0x9e3779b9u;
    }
    alphaRake_.update(pathPoint);
    dabAngle_ += alphaDabAngle(st.alphaOptions, alphaRake_, alphaSeed_);
    lastDabAngle_ = dabAngle_;
    p.alphaUp = cam.orientation() * glm::vec3(-std::sin(dabAngle_), std::cos(dabAngle_), 0.0f); // screenUpVector
    p.vdm = st.vdm;
    p.strokeDelta = haveLastDab_ ? hit.position - lastDabHit_ : glm::vec3(0.0f);
    lastDabHit_ = hit.position;
    haveLastDab_ = true;
    if (st.paint) paintAt(p); // (never on the solver thread: painting strokes stay in the frame loop)
    if (!st.sculpt) return;
    if (recipe) {
        doc_.sculptor.dabRecipe(doc_.mesh, doc_.bvh, *recipe, p, st.symmetry);
        return;
    }
    doc_.sculptor.dab(doc_.mesh, doc_.bvh, st.brush, p, st.symmetry);
}

void App::queueGrab(const glm::vec3& delta) {
    // Move followed every pointer sample with a whole move of its region (positions, normals, BVH): at 240 samples
    // a second on 33M triangles a frame did several, the next frame got more samples, and the view stalled for
    // seconds. Each sample still moves the positions (the cheap part); the normals and the BVH, which depend only on
    // where the vertices end up, follow once a frame (flushGrab): the same mesh, bit for bit. Scripts and
    // recordings do both at once.
    doc_.sculptor.grabMove(doc_.mesh, delta);
    grabPending_ = true;
    if (!interactiveStrokes_) flushGrab();
}

void App::flushGrab() {
    if (!grabPending_) return;
    grabPending_ = false;
    doc_.sculptor.grabFinish(doc_.mesh, doc_.bvh);
}

void App::endStroke() {
    flushGrab(); // (Move: the last samples' move)
    if (asyncStroke_) solverWait(); // (the dabs in flight; then the solver thread is idle)
    drainStrokeDabs(0.0); // the dabs still waiting belong to this stroke
    doc_.sculptor.flushDetailClone(doc_.mesh, doc_.bvh, symmetry_); // a Detail Clone click
    kelvinFinish(); // the rest of a Kelvinlet move (PanelKelvinlet.cpp)
    // (a command can end a Ctrl+click before its release: finishStrokeNow clears the mask click too)
    finishStrokeNow(); // AsyncStroke.cpp
}

// ---------------------------------------------------------------------------
// Commands & files
// ---------------------------------------------------------------------------

void App::runCommand(const std::string& name, const Args& args) {
    // In the app a macro asks before it first runs and a pack is reviewed before it's installed (PanelPacks.cpp),
    // whether they come from a button, a key, the console or a script.
    if (name == "macro_run" && macros_.runDepth == 0) {
        Macro m;
        std::string err;
        const std::string path = argString(args, "path");
        if (!(path.empty() ? findMacro(argString(args, "name"), &commands_, m, err)
                           : readMacroFile(path, &commands_, m, err))) {
            status_ = err;
            lastCommandOk_ = false;
            return;
        }
        lastCommandOk_ = true;
        requestMacroRun(m);
        return;
    }
    if (name == "pack_install") {
        reviewPackFile(argString(args, "path"));
        lastCommandOk_ = packs_.review != nullptr;
        return;
    }
    // A big mesh (Subdivide, Divide, micro, remesh) asks first when it wouldn't fit the memory free now.
    if (!memoryAllows(name, args, [this, name, args] { runCommand(name, args); })) {
        lastCommandOk_ = false;
        return;
    }
    if (doc_.sculptor.strokeActive()) endStroke();
    if (name == "save_project") storeProjectSettings();
    flushBrushEdits(); // commands may replace brushes: save edits first, then watch what's there afterwards
    crashlog::action(name); // (failures reach the log through SDL_Log below)
    CommandResult r = commands_.execute(doc_, name, args);
    status_ = r.message;
    if (name.rfind("uv_", 0) == 0 || name == "texture_create" || name == "texture_delete") uvEditorChanged();
    lastCommandOk_ = r.ok;
    if (r.ok && name != "export_glb" && name != "export_lods" && name != "export_usd" && name != "export_fbx" &&
        name != "export_unreal" && name.rfind("unreal_", 0) != 0 && name != "save_obj" && name != "save_project" &&
        name != "mesh_info" && name.rfind("brush_", 0) != 0 && name.rfind("splat_", 0) != 0 && name != "mesh_to_splats" &&
        name != "set_color" && name != "set_material" && name != "load_matcap" && name != "set_shading" && name != "subtool_shading" &&
        name != "viewport_shading" &&
        name != "export_ply" && name != "face_weights" &&
        name != "face_preset" && name.rfind("face_take", 0) != 0 && name != "face_landmarks_detect") {
        clearLods();
        playingLayer_ = -1;
    }
    if (r.ok && name == "splat_to_mesh") renderSettings_.showSplats = false; // the new SubTool shows; the splats stay
    if (r.ok && name == "save_obj" && !doc_.mesh.layers.empty())
        status_ += " (OBJ stores the current blend; export GLB to keep layers as blend shapes)";
    if (!r.ok) SDL_Log("%s failed: %s", name.c_str(), r.message.c_str());
    if (r.ok && (name == "new_sphere" || name == "new_plane" || name == "new_head" || name == "new_starter_mesh" ||
                 name == "open_obj" || name == "open" || name == "image_relief" || name == "image_inflate" || name == "splat_to_mesh" ||
                 (name == "splat_transform" && args.contains("fit"))))
        camera_.frame(sceneBounds());
    // A new SubTool lands beside the model: bring it into view only if it isn't already.
    if (r.ok && (name == "subtool_append" || name == "subtool_duplicate") && !onScreen(doc_.mesh.bounds()))
        camera_.frame(sceneBounds());
    afterCommand(name, args, r);
    afterVdmCommand(name, r); // the VDM brush after vdm_set / vdm_capture, the top view after vdm_new_plane
    watchActiveBrush();
    updateTitle();
    // The console shows every command as a script line (but not a live face feed, 30 times a second).
    if (name != "face_weights" || !r.ok) consoleEcho(name, args, r);
    if (r.ok && (name == "pack_remove" || name == "pack_enable" || name == "pack_disable")) refreshPacks();
    // Macro > Record keeps what the user runs (not what a running macro runs).
    if (r.ok && macros_.recorder.active() && macros_.runDepth == 0 && name != "face_weights")
        macros_.recorder.record(name, args, macroProjectDir());
}

namespace {

void SDLCALL onFileChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop(); // a file dialog answered (maybe on another thread): redraw now
    if (!files || !files[0]) return;
    auto* req = static_cast<App::DialogRequest*>(userdata);
    req->app->queueFile(req->action, files[0]);
}

} // namespace

void App::queueFile(FileAction action, std::string path) {
    if (action != FileAction::OpenObj && action != FileAction::LoadBrush && action != FileAction::OpenImage &&
        action != FileAction::AppendSubtool && action != FileAction::ComfyWorkflow && action != FileAction::InsertMesh &&
        !path.empty() &&
        std::filesystem::path(std::u8string(path.begin(), path.end())).extension().empty())
        path += action == FileAction::SaveObj      ? ".obj"
                : action == FileAction::SaveBrush  ? ".sfbrush"
                : action == FileAction::ExportUsd  ? ".usdz"
                : action == FileAction::SaveSplats ? ".ply"
                : action == FileAction::ExportUnreal ? ".fbx"
                : action == FileAction::SaveProject ? ".sfproj"
                                                   : ".glb";
    else if (action == FileAction::SaveProject && !path.empty() && !isProjectPath(path))
        path += kProjectExtension; // as save_project does, so the recent list gets the real name
    std::lock_guard lock(pendingMutex_);
    pendingFiles_.push_back({action, std::move(path)});
}

void App::loadMatcapDialog() {
    static const SDL_DialogFileFilter kMatcapFilters[] = {{"Lit Sphere images", "png;jpg;jpeg;exr"}, {"All files", "*"}};
    const std::string folder = matcapFolder();
    SDL_ShowOpenFileDialog(onFileChosen, &dialogRequests_[int(FileAction::LoadMatcap)], window_, kMatcapFilters, 2,
                           folder.empty() ? nullptr : folder.c_str(), false);
}

void App::openFileDialog() {
    SDL_ShowOpenFileDialog(onFileChosen, &dialogRequests_[int(FileAction::OpenObj)], window_, kModelFilters, 2,
                           nullptr, false);
}

// Opens models, brushes or reference images based on the file type.
void App::openPath(const std::string& path) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (Image::isImagePath(path)) {
        if (loadReferenceImage(path)) {
            if (ai::aiEnabled()) showImageTo3D_ = true;
            else status_ = "Image loaded: Relief, Inflate or Stamp it from Mesh > Generate 3D";
        }
    } else if (ext == ".sfbrush") {
        installBrushFile(path);
    } else if (ext == kPackExtension) { // a pack: the review screen first, nothing is installed yet
        reviewPackFile(path);
    } else if (ext == ".sfdetail") { // Focus Detail (prototype): a streamed micro level
        openFocusDetail(path);
    } else if (ext == ".exr" || ext == ".sfvdm") { // a vector displacement map for the VDM brush
        runCommand("vdm_set", {{"path", path}, {"swap_yz", vdmImport_.swapYZ}, {"flip_x", vdmImport_.flipX},
                               {"flip_y", vdmImport_.flipY}, {"flip_z", vdmImport_.flipZ}, {"scale", double(vdmImport_.scale)}});
    } else {
        runCommand("open", {{"path", path}});
    }
}

void App::saveFileDialog(FileAction action) {
    const bool obj = action == FileAction::SaveObj;
    const SDL_DialogFileFilter* filters = obj                                  ? kObjFilters
                                          : action == FileAction::SaveBrush   ? kBrushFilters
                                          : action == FileAction::ExportUsd   ? kUsdFilters
                                          : action == FileAction::SaveSplats  ? kSplatFilters
                                          : action == FileAction::ExportUnreal ? kFbxFilters
                                                                              : kGlbFilters;
    const int count = action == FileAction::SaveSplats ? 3 : 2;
    // Suggest the document's name with .obj (not its .sfproj path, which save_obj refuses).
    std::string suggestion;
    if (obj && !doc_.path.empty()) {
        const std::u8string s = std::filesystem::path(std::u8string(doc_.path.begin(), doc_.path.end()))
                                    .replace_extension(".obj")
                                    .u8string();
        suggestion.assign(s.begin(), s.end());
    }
    SDL_ShowSaveFileDialog(onFileChosen, &dialogRequests_[int(action)], window_, filters, count,
                           suggestion.empty() ? nullptr : suggestion.c_str());
}

bool App::onScreen(const AABB& box) const {
    if (!box.valid()) return true;
    const glm::mat4 viewProj = camera_.projection() * camera_.view();
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z);
        const glm::vec4 p = viewProj * glm::vec4(c, 1.0f);
        if (p.w <= 0.0f || std::abs(p.x / p.w) > 0.95f || std::abs(p.y / p.w) > 0.95f) return false;
    }
    return true;
}

AABB App::sceneBounds() const {
    AABB b = doc_.visibleBounds(); // every SubTool that's shown
    if (!b.valid()) b = doc_.mesh.bounds();
    if (!doc_.splats.empty() && renderSettings_.showSplats) b.expand(doc_.splats.bounds(0.9f));
    return b;
}

void App::processPendingFiles() {
    pollPackDialogs(); // Install Pack, New Pack's save and a macro's "?" files (PanelPacks.cpp)
    std::vector<PendingFile> files;
    {
        std::lock_guard lock(pendingMutex_);
        files.swap(pendingFiles_);
    }
    for (const auto& f : files) {
        if (f.action == FileAction::SaveObj || f.action == FileAction::ExportGlb || f.action == FileAction::ExportLods ||
            f.action == FileAction::ExportUsd || f.action == FileAction::ExportUnreal) {
            const auto folder = std::filesystem::path(std::u8string(f.path.begin(), f.path.end())).parent_path().u8string();
            lastExportFolder_.assign(folder.begin(), folder.end()); // baked maps go beside exports (PanelBake.cpp)
        }
        switch (f.action) {
        case FileAction::OpenObj: openPath(f.path); break;
        case FileAction::OpenImage: openPath(f.path); break;
        case FileAction::SaveObj: runCommand("save_obj", {{"path", f.path}}); break;
        case FileAction::ExportGlb: runCommand("export_glb", {{"path", f.path}}); break;
        case FileAction::ExportLods:
            runCommand("export_lods", {{"path", f.path}, {"levels", double(lodLevels_)}, {"ratio", double(lodRatio_)}});
            break;
        case FileAction::SaveBrush: runCommand("brush_save", {{"path", f.path}}); break;
        case FileAction::LoadBrush: installBrushFile(f.path); break;
        case FileAction::ExportUsd: runCommand("export_usd", {{"path", f.path}}); break;
        case FileAction::SaveSplats: runCommand("splat_save", {{"path", f.path}}); break;
        case FileAction::ExportUnreal: {
            auto p = std::filesystem::path(std::u8string(f.path.begin(), f.path.end()));
            auto folder = p.parent_path().u8string();
            auto stem = p.stem().u8string();
            runCommand("export_unreal", {{"folder", std::string(folder.begin(), folder.end())},
                                         {"name", std::string(stem.begin(), stem.end())},
                                         {"height_cm", double(unrealHeightCm_)},
                                         {"lods", double(lodLevels_)},
                                         {"ratio", double(lodRatio_)}});
            break;
        }
        case FileAction::SaveProject: saveProjectTo(f.path); break;
        case FileAction::AppendSubtool:
            runCommand("subtool_append", {{"path", f.path}});
            if (lastCommandOk_) camera_.frame(sceneBounds()); // a model can be any size, anywhere
            break;
        case FileAction::ComfyWorkflow: setComfyWorkflow(f.path, comfyPickingText_); break;
        case FileAction::LoadMatcap: runCommand("load_matcap", {{"path", f.path}}); break;
        case FileAction::InsertMesh: // Append > Insert on the model > From file (#34): the next drag inserts it
            insertType_ = "mesh";
            insertMeshKey_ = "file";
            insertMeshValue_ = f.path;
            status_ = "Insert " + std::filesystem::path(std::u8string(f.path.begin(), f.path.end())).filename().string() +
                      ": drag on the model (the drag sets its size). Esc stops.";
            break;
        case FileAction::Count: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Sculpt layers & LODs
// ---------------------------------------------------------------------------

Mesh& App::displayedMesh() {
    return previewLod_ >= 0 && previewLod_ < int(lods_.size()) ? lods_[previewLod_] : doc_.mesh;
}

void App::syncScene() {
    if (solverBusy()) return; // the solver thread is changing the mesh: the GPU keeps what it has (AsyncStroke.cpp)
    // Fill layers: Mesh Maps bakes started and landed (live after sculpting), and a painted mask's tiles evaluated.
    updateMeshMaps();
    if (paintsTexture(doc_.mesh) && doc_.mesh.texture->fillRevision != doc_.mesh.texture->revision)
        updateFillLayers(doc_.mesh, mutableTexture(doc_.mesh));
    // The active SubTool's slot shows the LOD being previewed instead of the sculpt.
    Mesh& shown = displayedMesh();
    if (&shown != lastDisplayed_) shown.topologyDirty = true;
    lastDisplayed_ = &shown;
    std::vector<SceneMesh> scene;
    scene.reserve(doc_.subtools.size());
    for (int i = 0; i < doc_.subtoolCount(); ++i) {
        SubTool& s = doc_.subtools[size_t(i)];
        const bool active = i == doc_.activeSubtool;
        Mesh* mesh = active ? &shown : &s.mesh;
        // Solo channel view (channel_view): the texture's shown texels follow before the renderer uploads them.
        if (mesh->texture) syncChannelView(*mesh->texture, doc_.channelView);
        const bool flat = s.shading == Shading::Flat || (s.shading == Shading::Default && renderSettings_.flatShading);
        scene.push_back({s.id, mesh, active, s.visible, bakeNormalMap(s.id, *mesh), renderSettings_.wireframe, flat});
        scene.back().pbr = renderSettings_.pbr();
        if (active && &shown == &doc_.mesh) scene.back().overlay = uvEditorOverlay(s.id); // the UV Editor's colours
    }
    renderer_.syncScene(scene);
    renderer_.syncSplats(doc_.splats, doc_.splatsVersion);
    if (subtoolPanelShown_) updateSubtoolThumbnails(); // from the buffers just synced (PanelSubTools.cpp)
    subtoolPanelShown_ = false;
}

void App::setLayerWeight(int index, float weight) {
    if (solverBusy()) solverWait(); // (the solver thread is moving this mesh: after its batch)
    doc_.mesh.setLayerWeight(index, weight);
    doc_.bvhDirty = true;
    doc_.modified = true;
    for (auto& lod : lods_) lod.setLayerWeight(index, weight); // LODs share the blend shapes
}

void App::animateLayers(float dt) {
    animateFaceTake(dt); // (PanelFace.cpp)
    if (playingLayer_ < 0 || playingLayer_ >= int(doc_.mesh.layers.size())) {
        playingLayer_ = -1;
        return;
    }
    playTime_ += dt;
    setLayerWeight(playingLayer_, 0.5f - 0.5f * std::cos(playTime_ * 3.0f));
}

void App::generateLods() {
    Uint64 start = SDL_GetTicksNS();
    lods_ = sf::generateLods(doc_.mesh, lodLevels_, lodRatio_);
    lastDisplayed_ = nullptr;
    previewLod_ = int(lods_.size()) - 1;
    status_ = "Generated " + std::to_string(lods_.size()) + " LODs in " +
              std::to_string(int(double(SDL_GetTicksNS() - start) * 1e-6)) + " ms";
}

void App::clearLods() {
    lods_.clear();
    previewLod_ = -1;
    lastDisplayed_ = nullptr;
}

void App::updateTitle() {
    std::string name = doc_.path.empty()
                           ? std::string("Untitled")
                           : std::filesystem::path(std::u8string(doc_.path.begin(), doc_.path.end())).filename().string();
    std::string title = (doc_.dirty() ? "* " : "") + name + " - SculptForge";
    if (title == windowTitle_) return; // called every frame
    windowTitle_ = title;
    SDL_SetWindowTitle(window_, title.c_str());
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void App::drawUi() {
    // An Extract preview goes once its source changes or another SubTool is picked (strokes aren't commands).
    if (doc_.extractPreview.id && !doc_.sculptor.strokeActive()) doc_.dropExtractPreview(true);
    drawProjectPopups();
    if (hideUi_) return; // demos: clean beauty shots
    // Draw order (ui-spec §12): shells, LightBox, floating windows, then pop-ups and help.
    drawShell();
    drawLightBoxUi(); // thumbnails, the LightBox over the canvas, pickers, size / strength pop-ups
    drawQuickMenu();  // Space (Input.cpp)
    drawRadialMenu(); // radial menus (RadialMenu.cpp)
    drawStrokePopup(); // the stroke popup and a latched modifier's label at the cursor (StrokePopup.cpp)
    drawSubToolPopups();
    if (showBrushLab_) drawBrushLab();
    drawBrushGallery();
    if (showImageTo3D_) drawImageTo3D();
    drawPhoneWindow(); // File > Capture from phone (PanelPhone.cpp)
    drawUvEditor();    // Tool > Texture Map > UV Editor (PanelUvEditor.cpp)
    drawTimelineUi();  // the Sculpt Timeline strip, timelapse, proof mode HUD (PanelTimeline.cpp, ProofMode.cpp)
    pollAlphaFile();
    syncBrushSettings(); // a brush switch brings back that brush's own settings (#78); before pollVdm's stroke
    syncWorldSize(); // World Size: the brush keeps its size on the surface as you zoom (Precision.cpp)
    pollVdm();
    if (previewLod_ >= 0) {
        const CanvasRect c = canvasRect();
        std::string banner = "Previewing LOD" + std::to_string(previewLod_) + "  -  " +
                             std::to_string(lods_[previewLod_].triangleCount()) + " tris  (sculpting paused)";
        drawViewportHint("lod_preview", banner.c_str(), IM_COL32(255, 190, 120, 255),
                         c.y + c.h - ImGui::GetFrameHeight() - 20.0f * uiScale_);
    }
    const ImVec2 mouse(mouseX_, mouseY_);
    drawGizmo(); // Gizmo 3D handles and panel (Gizmo.cpp)
    drawQRemeshGuides(); // QRemesh Guides curves (PanelRetopo.cpp)
    drawUvEditorOverlay(); // UV seams on the model, the Seam tool's cursor (PanelUvEditor.cpp)
    drawZSphereOverlay(); // the sphere under the cursor (PanelZSpheres.cpp)
    drawZSphereMenu();    // a right click on a sphere
    drawFaceOverlay();   // facial landmarks (PanelFace.cpp)
    drawSplatTrainOverlay(); // splat training progress (PanelSplatTrain.cpp)
    drawSculptableOverlay(); // Make sculptable: crop box, face landmarks (PanelSculptable.cpp)
    drawCaptureOverlay();    // Capture from view: the square, the result window (PanelKelvinlet.cpp)
    if (uiPrefs_.showAxes) drawAxisGizmo(); // X / Y / Z in the canvas corner (#31)
    if (uiPrefs_.showViewFigure) drawViewFigure();
    drawAirStrokeIndicator(); // Start strokes off the model is on (#62, Input.cpp)
    if (drag_ == Drag::Insert) { // the size the insert will get
        insertPx_ = std::hypot(mouse.x - insertX_, mouse.y - insertY_);
        ImGui::GetForegroundDrawList()->AddCircle(ImVec2(insertX_, insertY_), std::max(insertPx_, 12.0f * uiScale_),
                                                  theme::u32(theme::accent), 48, 2.0f * uiScale_);
    }
    // In Gizmo mode (and with the QRemesh Guides brush) the brush ring shows only while Ctrl masks or Shift smooths.
    const bool ringOff = gizmoActive() || (guidesMode_ && !(inputMods() & SDL_KMOD_SHIFT)) || sculptableHot() || zsphereActive() ||
                         uvToolActive();
    if (((!ImGui::GetIO().WantCaptureMouse && canvasRect().contains(mouse.x, mouse.y)) || drag_ == Drag::Stroke) &&
        !hideBrushCursor_ && radial_.menu < 0 && !strokePopup_.open && drag_ != Drag::Select && drag_ != Drag::Guide && // (no ring under a pie or the stroke popup)
        (!ringOff || drag_ == Drag::Stroke || ctrlDown(inputMods())))
        drawBrushCursor();
    drawSelectionOverlay();
    drawBrushLibraryPopups();
    autosaveBrush();

    if (showAbout_) {
        ImGui::SetNextWindowSize(ImVec2(460 * uiScale_, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("About SculptForge", &showAbout_, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("SculptForge %s", SCULPTFORGE_VERSION);
            ImGui::TextWrapped("Free and open-source digital sculpting.");
            ImGui::Spacing();
            ImGui::TextWrapped("%s", ai::kPrivacyLine); // also on the site (website/index.html) and in docs/ai-services.md
            ImGui::Spacing();
            ImGui::TextWrapped("Copyright (C) 2026 The SculptForge contributors. This program is free software: you can "
                               "redistribute it and/or modify it under the terms of the GNU General Public License, "
                               "version 3 or later. It comes with ABSOLUTELY NO WARRANTY.");
            ImGui::Spacing();
            ImGui::TextWrapped("Built with SDL 3 (zlib), Dear ImGui (MIT), GLM (MIT), stb (MIT/public domain), ufbx "
                               "(MIT/public domain), xatlas (MIT), MikkTSpace (zlib), the Inter and JetBrains Mono typefaces "
                               "(SIL OFL 1.1) and the MIT-licensed colours of the Forge Terminal look's schemes. Quad Remesh "
                               "follows Instant Meshes "
                               "(BSD-3-Clause). The phone link uses QR Code generator (MIT), libdatachannel and libjuice (MPL-2.0), usrsctp "
                               "(BSD-3-Clause) and Mbed TLS (Apache-2.0). Uses libcurl (curl license) "
                               "where available.");
            ImGui::Spacing();
            if (ImGui::Button("License")) SDL_OpenURL("https://github.com/islandmed/SculptForge/blob/main/LICENSE");
            ImGui::SameLine();
            if (ImGui::Button("Third-party notices"))
                SDL_OpenURL("https://github.com/islandmed/SculptForge/blob/main/THIRD_PARTY_NOTICES.md");
            ImGui::SameLine();
            if (ImGui::Button("AI services & privacy"))
                SDL_OpenURL("https://github.com/islandmed/SculptForge/blob/main/docs/ai-services.md");
            ImGui::Spacing();
            drawSupportersSection(); // PanelSupport.cpp
        }
        ImGui::End();
    }
    drawSupportUi(); // the first-run welcome, the one support reminder (PanelSupport.cpp)
    guide_.draw(*this);  // Help > User Guide
    drawPacksUi();       // pack review, New Pack, macro confirmation and editor (PanelPacks.cpp)
    drawMemoryConfirm(); // "Not enough free memory" before a big mesh (MemoryConfirm.cpp)
    drawHelpConsoleUi(); // console, command palette, Controls (F1), Ctrl-hover help: last, on top
    drawWhatsNew();      // What's New after an update or from the Help menu (WhatsNewPopup.cpp)
    drawSupportCard();   // the support card at launch, over everything (PanelSupportCard.cpp)
}

void App::drawLayersContents() {
    const float scale = uiScale_;
    ImGui::TextDisabled("Sculpt expressions & correctives;\nexported as glTF morph targets.");

    if (ImGui::RadioButton("Base mesh", doc_.activeLayer < 0)) runCommand("layer_set_active", {{"index", -1.0}});
    constexpr const char* kLayersPath = "Tool > Layers";
    ui::HelpText("Base mesh", nullptr, "Strokes go into the base shape, not a layer.", kLayersPath);

    int removeIndex = -1;
    for (int i = 0; i < int(doc_.mesh.layers.size()); ++i) {
        SculptLayer& layer = doc_.mesh.layers[i];
        ImGui::PushID(i);
        ImGui::Separator();
        if (ImGui::RadioButton(layer.name.c_str(), doc_.activeLayer == i))
            runCommand("layer_set_active", {{"index", double(i)}});
        ui::HelpText(layer.name.c_str(), nullptr,
                     "Select to sculpt into this layer (ZBrush's REC). With subdivision levels, record at any level: a "
                     "layer keeps its own detail on every level and follows Lower Res / Higher Res and Divide.",
                     kLayersPath);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52 * scale);
        const bool playing = playingLayer_ == i;
        if (ImGui::SmallButton(playing ? "Stop" : "Play")) {
            playingLayer_ = playing ? -1 : i;
            playTime_ = 0.0f;
            if (playing) setLayerWeight(i, 1.0f);
        }
        ui::HelpText(playing ? "Stop" : "Play", nullptr, "Animate this blend shape 0 -> 1 -> 0 (Stop sets it back to 1).",
                     kLayersPath);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) removeIndex = i;
        ui::HelpText("Delete layer", nullptr, "Delete this layer on every subdivision level (undoable).", kLayersPath);
        float w = layer.weight;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##weight", &w, 0.0f, 1.0f, "weight %.2f")) {
            if (playingLayer_ == i) playingLayer_ = -1;
            setLayerWeight(i, w);
        }
        ui::HelpText("Weight", nullptr,
                     "How strongly the layer shows: 0 = off (ZBrush's eye), 1 = as sculpted. One weight for every "
                     "subdivision level.",
                     kLayersPath);
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        if (playingLayer_ == removeIndex) playingLayer_ = -1;
        runCommand("layer_remove", {{"index", double(removeIndex)}});
    }

    ImGui::Separator();
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("+ Add").x - ImGui::GetStyle().FramePadding.x * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
    bool enter = ImGui::InputText("##newlayer", newLayerName_, sizeof newLayerName_,
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("+ Add") || enter) runCommand("layer_add", {{"name", std::string(newLayerName_)}});
    ui::HelpText("+ Add", nullptr, "Add a sculpt layer with the name typed beside it; strokes go into it.", kLayersPath);
}

void App::sendToUnreal() {
    if (pendingUnreal_.valid()) return;
    UnrealExportOptions o;
    o.heightCm = unrealHeightCm_;
    o.lods = lodLevels_;
    o.ratio = lodRatio_;
    o.folder = (std::filesystem::temp_directory_path() / "sculptforge_unreal").string();
    UnrealExportResult r;
    std::string err;
    if (!exportForUnreal(doc_, o, r, err)) {
        status_ = "Export failed: " + err;
        return;
    }
    link::UnrealImport import;
    import.fbxPath = r.path;
    import.destination = unrealDestination_;
    import.assetName = r.assetName;
    import.skeletal = r.skeletal;
    import.lods = r.lodCount > 1;
    import.normalMapPath = bakedNormalMapFile(import.normalMapDirectX); // Tool > Bake's last normal map, if it fits
    // A texture with material channels goes along as T_<name>_BC / _N / _ORM and a built M_<name> material.
    if (hasTexture(doc_.mesh) && !link::addUnrealMaterialMaps(*doc_.mesh.texture, o.folder, import, err)) {
        status_ = "Export failed: " + err;
        return;
    }
    status_ = "Sending " + r.assetName + " to Unreal...";
    auto promise = std::make_shared<std::promise<std::string>>();
    pendingUnreal_ = promise->get_future();
    std::thread([promise, import, network = unrealNetwork_] {
        std::string message;
        link::sendToUnreal(network ? link::UnrealEndpoint::network() : link::UnrealEndpoint(), import, message);
        promise->set_value(message);
    }).detach();
}

void App::pollUnreal() {
    if (!pendingUnreal_.valid() || pendingUnreal_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    status_ = pendingUnreal_.get();
}

void App::drawGameExportContents() {
    ImGui::PushItemWidth(-ImGui::CalcTextSize("Reduction").x - ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::SliderInt("LODs", &lodLevels_, 2, 6)) clearLods();
    if (ImGui::SliderFloat("Reduction", &lodRatio_, 0.1f, 0.9f, "x%.2f / level")) clearLods();
    ImGui::PopItemWidth();
    if (ImGui::Button(lods_.empty() ? "Generate LOD chain" : "Regenerate", ImVec2(-1, 0))) generateLods();

    if (!lods_.empty()) {
        if (ImGui::BeginTable("lods", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Level");
            ImGui::TableSetupColumn("Triangles");
            ImGui::TableSetupColumn("Budget");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable("Sculpt", previewLod_ < 0, ImGuiSelectableFlags_SpanAllColumns)) {
                previewLod_ = -1;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%zu", doc_.mesh.triangleCount());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("editable");
            for (int i = 0; i < int(lods_.size()); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                std::string label = "LOD" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), previewLod_ == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    previewLod_ = i;
                    renderSettings_.wireframe = true;
                }
                ImGui::TableNextColumn();
                ImGui::Text("%zu", lods_[i].triangleCount());
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", 100.0 * double(lods_[i].triangleCount()) / double(lods_[0].triangleCount()));
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Click a level to inspect it in the viewport.");
    }

    ImGui::SeparatorText("Unreal Engine");
    ImGui::PushItemWidth(-ImGui::CalcTextSize("Folder").x - ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::InputText("Folder", unrealDestination_, sizeof unrealDestination_);
    if (unrealHeightCm_ <= 0.0f) unrealHeightCm_ = std::max(1.0f, doc_.mesh.bounds().extent().y * 100.0f);
    ImGui::DragFloat("Height", &unrealHeightCm_, 1.0f, 1.0f, 100000.0f, "%.0f cm");
    ImGui::PopItemWidth();
    ImGui::Checkbox("Unreal is on another computer", &unrealNetwork_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("In Unreal: Project Settings > Plugins > Python > Advanced:\nMulticast Time-To-Live = 1, "
                          "Multicast Bind Address = 0.0.0.0");
    if (pendingUnreal_.valid()) {
        ImGui::ProgressBar(-1.0f * float(ImGui::GetTime()), ImVec2(-1, 0), "Sending to Unreal...");
    } else if (ImGui::Button("Send to Unreal", ImVec2(-1, 0))) {
        sendToUnreal();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Imports (or updates) the asset in the open Unreal Editor.\nNeeds the Python Editor Script "
                          "Plugin and Project Settings > Python > Enable Remote Execution.");
    if (ImGui::Button("Export FBX for Unreal...", ImVec2(-1, 0))) saveFileDialog(FileAction::ExportUnreal);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (doc_.mesh.layers.empty()) ImGui::TextWrapped("Static mesh with %d LODs.", lodLevels_);
    else ImGui::TextWrapped("Skeletal mesh with %zu morph targets.", doc_.mesh.layers.size());
    ImGui::PopStyleColor();

    ImGui::SeparatorText("Export");
    if (ImGui::Button("GLB + blend shapes", ImVec2(-1, 0))) saveFileDialog(FileAction::ExportGlb);
    if (ImGui::Button("LOD chain (GLB per level)", ImVec2(-1, 0))) saveFileDialog(FileAction::ExportLods);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Unreal, Unity, Godot, Blender, three.js");
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Brush Lab: describe a brush in words, get a recipe, tweak it with sliders
// ---------------------------------------------------------------------------

const BrushRecipe* App::activeRecipe() const {
    return doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size()) ? &doc_.brushes[doc_.activeBrush]
                                                                                  : nullptr;
}

void App::sendBrushRequest(const std::string& text) {
    if (text.empty() || pendingBrush_.valid()) return;
    chat_.push_back(ai::composeUserMessage(text, refineCurrent_ ? activeRecipe() : nullptr));
    chatLog_.push_back({ChatLine::Kind::User, text});
    scrollChat_ = true;
    busyTime_ = 0.0f;

    if (!ai::aiEnabled()) return; // (the Brush Lab is closed then; requestBrush refuses too)
    // Detached worker with its own copies, so quitting never waits on the network.
    auto promise = std::make_shared<std::promise<BrushReply>>();
    pendingBrush_ = promise->get_future();
    std::thread([promise, settings = aiSettings_, history = chat_] {
        BrushReply reply;
        reply.suggestion = ai::requestBrush(settings, history, &reply.raw);
        promise->set_value(std::move(reply));
    }).detach();
}

void App::pollBrushRequest() {
    if (!pendingBrush_.valid() || pendingBrush_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    BrushReply reply = pendingBrush_.get();
    scrollChat_ = true;
    if (!reply.suggestion.ok) {
        chat_.pop_back(); // keep the model's history to successful turns
        chatLog_.push_back({ChatLine::Kind::Error, reply.suggestion.error});
        return;
    }
    chat_.push_back({ai::ChatMessage::Role::Assistant, reply.raw, reply.suggestion.reply});
    flushBrushEdits();
    doc_.activeBrush = doc_.addBrush(reply.suggestion.recipe);
    chatLog_.push_back({ChatLine::Kind::Assistant, "[" + reply.suggestion.recipe.name + "]  " + reply.suggestion.reply});
    status_ = "Brush '" + reply.suggestion.recipe.name + "' is ready - try it on the model";
    saveBrushEdits(doc_.activeBrush); // Brush Lab brushes go straight into the library (status says if that fails)
    watchActiveBrush();
}

void App::drawRecipeEditor(BrushRecipe& recipe) {
    char name[64];
    std::snprintf(name, sizeof name, "%s", recipe.name.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue) && name[0] && recipe.name != name)
        pendingRename_ = name; // brush_rename after the panel: it renames the file and may add a brush
    ImGui::PushItemWidth(-ImGui::CalcTextSize("Pressure curve").x - ImGui::GetStyle().ItemInnerSpacing.x);
    for (const auto& p : recipeParams()) {
        switch (p.kind) {
        case RecipeParam::Kind::Float:
            ImGui::SliderFloat(p.label, recipe.field(p.key), p.min, p.max, "%.2f");
            break;
        case RecipeParam::Kind::Bool:
            ImGui::Checkbox(p.label, &recipe.frontFacesOnly);
            ImGui::Checkbox("Connected only##recipe", &recipe.connectedOnly); // (falloff options, not recipeParams)
            ImGui::Checkbox("View falloff##recipe", &recipe.viewFalloff);
            break;
        case RecipeParam::Kind::Enum: {
            int idx = recipe.enumIndex(p.key);
            if (ImGui::Combo(p.label, &idx, p.options.data(), int(p.options.size()))) recipe.setEnumIndex(p.key, idx);
            break;
        }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", p.help);
    }
    ImGui::PopItemWidth();
    drawPressureCurveEditor("lab_brush_curve", ImGui::GetContentRegionAvail().x); // this brush's own (Precision.cpp)
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Save .sfbrush...", ImVec2(half, 0))) saveFileDialog(FileAction::SaveBrush);
    ImGui::SameLine();
    if (ImGui::Button("Install .sfbrush...", ImVec2(half, 0)))
        SDL_ShowOpenFileDialog(onFileChosen, &dialogRequests_[int(FileAction::LoadBrush)], window_, kBrushFilters, 2,
                               nullptr, false);
}

bool App::beginPanel(const char* name, bool* open, float width) {
    // Auto-sized panels grow with their content (an opened section, a long chat):
    // never taller than the screen (they scroll instead), and never off its bottom edge.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float margin = 6.0f * uiScale_;
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, std::max(100.0f, vp->WorkSize.y - 2 * margin)));
    const bool visible = ImGui::Begin(name, open, ImGuiWindowFlags_AlwaysAutoResize);
    const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const float bottom = vp->WorkPos.y + vp->WorkSize.y - margin;
    if (pos.y + size.y > bottom) ImGui::SetWindowPos(ImVec2(pos.x, std::max(vp->WorkPos.y + margin, bottom - size.y)));
    return visible;
}

void App::drawAiSettings() {
    using P = ai::AiSettings::Provider;
    const char* providers[] = {"Free - no account or key", "Anthropic (Claude) - your key",
                               "OpenAI-compatible (OpenAI, Ollama, LM Studio...)"};
    int provider = int(aiSettings_.provider);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##provider", &provider, providers, 3)) {
        // Only the brush provider changes; the image-to-3D settings stay.
        const ai::AiSettings d = ai::AiSettings::defaults(P(provider));
        aiSettings_.provider = d.provider;
        aiSettings_.endpoint = d.endpoint;
        aiSettings_.model = d.model;
    }
    auto textField = [](const char* label, std::string& value, ImGuiInputTextFlags flags = 0) {
        char buf[512];
        std::snprintf(buf, sizeof buf, "%s", value.c_str());
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Endpoint").x - ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::InputText(label, buf, sizeof buf, flags)) value = buf;
    };
    if (aiSettings_.provider == P::OpenAICompatible) {
        // One-click presets for the two common local servers.
        const float third = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        if (ImGui::Button("Ollama", ImVec2(third, 0))) {
            aiSettings_.endpoint = "http://localhost:11434/v1/chat/completions";
            modelsChecked_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("LM Studio", ImVec2(third, 0))) {
            aiSettings_.endpoint = "http://localhost:1234/v1/chat/completions";
            modelsChecked_ = false;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(pendingModels_.valid());
        if (ImGui::Button(pendingModels_.valid() ? "Checking..." : "Find models", ImVec2(third, 0))) {
            auto promise = std::make_shared<std::promise<ModelList>>();
            pendingModels_ = promise->get_future();
            std::thread([promise, settings = aiSettings_] {
                ModelList list;
                list.models = ai::listModels(settings, list.error);
                promise->set_value(std::move(list));
            }).detach();
        }
        ImGui::EndDisabled();
        if (pendingModels_.valid() && pendingModels_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            localModels_ = pendingModels_.get();
            modelsChecked_ = true;
            if (!localModels_.models.empty() &&
                std::find(localModels_.models.begin(), localModels_.models.end(), aiSettings_.model) ==
                    localModels_.models.end())
                aiSettings_.model = localModels_.models.front();
        }
    }
    if (aiSettings_.provider != P::Free) {
        textField("Endpoint", aiSettings_.endpoint);
        if (aiSettings_.provider == P::OpenAICompatible && modelsChecked_ && !localModels_.models.empty()) {
            ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Endpoint").x - ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::BeginCombo("Model", aiSettings_.model.c_str())) {
                for (const auto& m : localModels_.models)
                    if (ImGui::Selectable(m.c_str(), m == aiSettings_.model)) aiSettings_.model = m;
                ImGui::EndCombo();
            }
        } else {
            textField("Model", aiSettings_.model);
        }
        textField("API key", aiSettings_.apiKey, ImGuiInputTextFlags_Password);
    }
    if (aiSettings_.provider == P::OpenAICompatible && modelsChecked_) {
        const bool ok = localModels_.error.empty();
        ImGui::PushStyleColor(ImGuiCol_Text, ok ? ImVec4(0.45f, 0.8f, 0.5f, 1) : ImVec4(0.95f, 0.6f, 0.4f, 1));
        if (ok)
            ImGui::TextWrapped("Connected - %d model%s installed", int(localModels_.models.size()),
                               localModels_.models.size() == 1 ? "" : "s");
        else
            ImGui::TextWrapped("%s", localModels_.error.c_str());
        ImGui::PopStyleColor();
    }
    if (aiSettings_.provider == P::OpenAICompatible && ImGui::TreeNode("Run a model on this computer")) {
        // Steps for the platform we're on; the guide covers the others.
#if defined(_WIN32)
        ImGui::TextWrapped("1. Install Ollama from ollama.com/download, or in PowerShell:\n"
                           "      winget install Ollama.Ollama\n"
                           "2. Download the model (in PowerShell):\n"
                           "      ollama pull qwen3:4b-instruct\n"
                           "3. Ollama runs in the system tray. Click Ollama above, then Find models.");
#elif defined(__APPLE__)
        ImGui::TextWrapped("1. Install Ollama from ollama.com/download and drag it to Applications.\n"
                           "2. Download the model (in Terminal):\n"
                           "      ollama pull qwen3:4b-instruct\n"
                           "3. Ollama runs in the menu bar. Click Ollama above, then Find models.");
#else
        ImGui::TextWrapped("1. Install Ollama (or use your package manager, e.g. sudo pacman -S ollama):\n"
                           "      curl -fsSL https://ollama.com/install.sh | sh\n"
                           "2. Start it if needed:\n"
                           "      sudo systemctl enable --now ollama\n"
                           "3. Download the model:\n"
                           "      ollama pull qwen3:4b-instruct\n"
                           "4. Click Ollama above, then Find models.");
#endif
        ImGui::TextWrapped("The 2.5 GB model needs about 4 GB of free memory and works without a graphics card "
                           "(10-20 s per brush on a laptop CPU, a few seconds with a GPU).");
        if (ImGui::SmallButton("Full guide: Windows, macOS, Linux, LM Studio"))
            SDL_OpenURL("https://github.com/islandmed/SculptForge/blob/main/docs/local-ai.md");
        ImGui::TreePop();
    }
    if (aiSettings_.provider == P::Anthropic) {
        const char* efforts[] = {"low", "medium", "high"};
        int e = aiSettings_.effort == "high" ? 2 : aiSettings_.effort == "medium" ? 1 : 0;
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Endpoint").x - ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Combo("Effort", &e, efforts, 3)) aiSettings_.effort = efforts[e];
    }
    if (ImGui::Button("Save settings", ImVec2(-1, 0))) {
        std::string err;
        status_ = aiSettingsPath_.empty() || !aiSettings_.save(aiSettingsPath_, err) ? "Could not save AI settings " + err
                                                                                     : "AI settings saved";
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (aiSettings_.provider == P::Free)
        ImGui::TextWrapped("Free: an open model (GPT-OSS 20B) hosted by Pollinations.ai, no sign-up. Only your "
                           "brush description and settings are sent. Rate limited to a request every few seconds; "
                           "if it can't be reached, a running Ollama is used instead.");
    else
        ImGui::TextWrapped("Requests go only to the endpoint above. Keys are stored on this computer only "
                           "(owner-readable ai.json); leave empty to use ANTHROPIC_API_KEY / OPENAI_API_KEY. "
                           "For private, offline use, run Ollama and pick OpenAI-compatible.");
    ImGui::PopStyleColor();
}

void App::drawBrushLab() {
    const float scale = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // First use: the canvas's top-left corner, clear of the model.
    const CanvasRect canvas = canvasRect();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + canvas.x + 16 * scale, vp->Pos.y + canvas.y + 16 * scale), ImGuiCond_FirstUseEver);
    if (!beginPanel("Brush Lab", &showBrushLab_, 420 * scale)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Describe a brush the way you'd describe it to another sculptor.");

    // Conversation: just a hint until there is one, then a box that grows with it and scrolls past ~8 lines.
    if (chatLog_.empty() && !pendingBrush_.valid()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Try an idea below, or ask for changes to the selected brush, like \"sharper\" or "
                           "\"less buildup\".");
        ImGui::PopStyleColor();
    } else {
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, 150 * scale));
    }
    const bool showChat = !chatLog_.empty() || pendingBrush_.valid();
    if (showChat && ImGui::BeginChild("chat", ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        for (const auto& line : chatLog_) {
            ImVec4 col = line.kind == ChatLine::Kind::User    ? ImVec4(1.0f, 0.70f, 0.45f, 1.0f)
                         : line.kind == ChatLine::Kind::Error ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
                                                              : ImVec4(0.85f, 0.88f, 0.95f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextWrapped("%s %s", line.kind == ChatLine::Kind::User ? ">" : " ", line.text.c_str());
            ImGui::PopStyleColor();
        }
        if (pendingBrush_.valid()) {
            busyTime_ += ImGui::GetIO().DeltaTime;
            int dots = int(busyTime_ * 3.0f) % 4;
            ImGui::TextDisabled("  Designing brush%.*s", dots, "...");
        }
        if (scrollChat_) {
            ImGui::SetScrollHereY(1.0f);
            scrollChat_ = false;
        }
    }
    if (showChat) ImGui::EndChild(); // always paired with BeginChild, whatever it returned

    // Starter ideas
    for (int i = 0; i < int(std::size(kExamplePrompts)); ++i) {
        float w = ImGui::CalcTextSize(kExamplePrompts[i]).x + ImGui::GetStyle().FramePadding.x * 2;
        if (i > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <
                         ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
            ImGui::SameLine();
        ImGui::BeginDisabled(pendingBrush_.valid());
        if (ImGui::SmallButton(kExamplePrompts[i])) sendBrushRequest(kExamplePrompts[i]);
        ImGui::EndDisabled();
    }

    // Input
    const char* sendLabel = "Create";
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize(sendLabel).x - ImGui::GetStyle().FramePadding.x * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
    bool submit = ImGui::InputTextWithHint("##prompt", "e.g. bark of an old oak tree", chatInput_, sizeof chatInput_,
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::BeginDisabled(pendingBrush_.valid());
    submit |= ImGui::Button(sendLabel);
    ImGui::EndDisabled();
    if (submit && chatInput_[0] && !pendingBrush_.valid()) {
        sendBrushRequest(chatInput_);
        chatInput_[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::BeginDisabled(!activeRecipe());
    ImGui::Checkbox("Refine the selected brush", &refineCurrent_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear chat")) {
        chat_.clear();
        chatLog_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Paste brush")) pasteBrush();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Install a brush someone shared: a brush code (sfbrush1:...) or .sfbrush text on the "
                          "clipboard (Ctrl+V in the viewport)");

    if (BrushRecipe* recipe = doc_.activeBrush >= 0 ? &doc_.brushes[doc_.activeBrush] : nullptr) {
        if (ImGui::CollapsingHeader(("Recipe: " + recipe->name + "###recipe").c_str())) drawRecipeEditor(*recipe);
    }
    if (ImGui::CollapsingHeader("AI provider")) drawAiSettings();
    ImGui::End();
    if (!pendingRename_.empty()) {
        runCommand("brush_rename", {{"to", pendingRename_}});
        pendingRename_.clear();
    }
}

// ---------------------------------------------------------------------------
// Image to 3D
// ---------------------------------------------------------------------------

bool App::loadReferenceImage(const std::string& path) {
    Image img;
    std::string err;
    if (!Image::load(path, img, err)) {
        status_ = err;
        return false;
    }
    refImage_ = std::move(img);
    refImagePath_ = path;
    if (!refTexture_) gl::GenTextures(1, &refTexture_);
    gl::BindTexture(GL_TEXTURE_2D, refTexture_);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), refImage_.width, refImage_.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   refImage_.rgba.data());
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    gl::BindTexture(GL_TEXTURE_2D, 0);
    status_ = "Loaded image " + std::to_string(refImage_.width) + "x" + std::to_string(refImage_.height);
    return true;
}

void App::projectImageFromView() {
    if (refImage_.empty()) return;
    const CanvasRect view = canvasRect();
    const AABB b = doc_.mesh.bounds();
    ProjectOptions o;
    o.depth = projectDepth_ * glm::length(b.extent());
    o.invert = projectInvert_;
    Mesh m = doc_.mesh;
    size_t n = projectImage(m, doc_.activeLayer, refImage_, camera_.projection() * camera_.view(),
                            view.w / std::max(view.h, 1.0f), camera_.position(), o);
    if (n == 0) {
        status_ = "The image didn't land on the model - aim the view at it first";
        return;
    }
    doc_.replaceMeshUndoable(std::move(m), "Project Image");
    clearLods();
    status_ = "Stamped image onto " + std::to_string(n) + " vertices (Ctrl+Z to undo)";
    updateTitle();
}

void App::startGenerator() {
    if (pendingGenerator_.valid()) return;
    if (textMode_ ? textPrompt_[0] == '\0' : refImagePath_.empty()) return;
    aiSettings_.generatorCommand = generatorCommand_;
    aiSettings_.falKey = falKey_;
    aiSettings_.falModel = falModel_;
    aiSettings_.meshyKey = meshyKey_;
    aiSettings_.hfToken = hfToken_;
    aiSettings_.comfyUrl = comfyUrl_;
    // With no workflow chosen, ComfyUI runs the bundled one the panel shows (not saved, so updates reach it).
    ai::AiSettings settings = aiSettings_;
    const bool hunyuan = settings.generatorService == "huggingface"
                             ? ai::hunyuanModel(ai::resolveHfPreset(settings, textMode_).id)
                             : settings.generatorService == "fal" && ai::hunyuanModel(settings.falModel);
    if (hunyuan) showHunyuanNoticeOnce();
    if (settings.comfyImageWorkflow.empty()) settings.comfyImageWorkflow = comfyDefaultWorkflow(false);
    if (settings.comfyTextWorkflow.empty()) settings.comfyTextWorkflow = comfyDefaultWorkflow(true);
    auto stem = (std::filesystem::temp_directory_path() / ("sculptforge_generated_" + std::to_string(SDL_GetTicksNS()))).u8string();
    generatorTime_ = 0.0f;
    generatorProgress_ = std::make_shared<ai::GenerationProgress>();
    generatorProgress_->setStatus("Starting...");
    auto promise = std::make_shared<std::promise<GeneratedModel>>();
    pendingGenerator_ = promise->get_future();
    std::thread([promise, settings, in = refImagePath_, text = textMode_ ? std::string(textPrompt_) : std::string(),
                 out = std::string(stem.begin(), stem.end()), progress = generatorProgress_,
                 mode = GeneratedRemesh(generatedRemesh_), resolution = remeshResolution_] {
        GeneratedModel g;
        g.result = text.empty() ? ai::generate3D(settings, in, out, progress.get())
                                : ai::generate3DFromText(settings, text, out, progress.get());
        if (g.result.ok && mode != GeneratedRemesh::Off) {
            // Made sculptable here, so the window stays responsive; the main thread opens the file and swaps this in.
            Document scratch;
            const CommandResult opened = CommandRegistry().execute(scratch, "open", {{"path", g.result.outputPath}});
            progress->percent = 0;
            progress->setStatus(mode == GeneratedRemesh::Quad ? "Making it sculptable: Quad Remesh..." : "Making it sculptable: Voxel Remesh...");
            Mesh m;
            if (!opened.ok) {
                g.remeshError = opened.message;
            } else if (remeshGenerated(scratch.mesh, mode, resolution, 1, m, g.remeshError, &g.stats, [progress](float f) {
                           progress->percent = int(f * 100.0f);
                           return !progress->cancel.load();
                       })) {
                g.remeshed = std::make_shared<Mesh>(std::move(m));
            }
        }
        promise->set_value(std::move(g));
    }).detach();
}

void App::pollGenerator() {
    if (!pendingGenerator_.valid() || pendingGenerator_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    GeneratedModel g = pendingGenerator_.get();
    const ai::GeneratorResult& r = g.result;
    const std::string note = r.note.empty() ? std::string() : " - " + r.note;
    if (!r.ok) {
        status_ = "Image to 3D failed: " + r.error + note;
        return;
    }
    runCommand("open", {{"path", r.outputPath}});
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(std::u8string(r.outputPath.begin(), r.outputPath.end())), ec);
    const size_t raw = doc_.mesh.triangleCount();
    const auto mode = GeneratedRemesh(generatedRemesh_);
    if (g.remeshed) doc_.replaceMeshUndoable(std::move(*g.remeshed), mode == GeneratedRemesh::Quad ? "Quad Remesh" : "Remesh");
    doc_.path.clear();
    const std::string remeshNote = g.remeshed ? ", " + generatedRemeshNote(g.stats, mode) : std::string();
    status_ = !g.remeshError.empty()
                  ? "Generated model imported (" + std::to_string(raw) + " tris, not remeshed: " + g.remeshError + ")" + note
                  : "Generated model imported (" + std::to_string(raw) + " tris" + remeshNote + ") - ready to sculpt" + note;
    updateTitle();
}

void App::showHunyuanNoticeOnce() {
    if (aiSettings_.hunyuanNoticeShown) return;
    aiSettings_.hunyuanNoticeShown = true;
    openHunyuanNotice_ = true;
    if (aiSettingsPath_.empty()) return;
    // Only the flag: keys typed into the panel are saved when the user presses Save settings.
    ai::AiSettings saved = ai::AiSettings::load(aiSettingsPath_);
    saved.hunyuanNoticeShown = true;
    std::string err;
    saved.save(aiSettingsPath_, err);
}

void App::drawAiPrefsSection() {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##hftoken", "Hugging Face token (hf_...)", hfToken_, sizeof hfToken_, ImGuiInputTextFlags_Password);
    ui::HelpText("Hugging Face token", nullptr,
                 "Free at huggingface.co/settings/tokens (read access). Raises the free daily allowance in "
                 "Generate 3D, and TRELLIS.2 needs it. Empty = the HF_TOKEN environment variable.",
                 "Preferences > AI");
    if (ImGui::Button("Save", ImVec2(-1, 0))) {
        // Saves the token into the settings as they are on disk, so other unsaved panel edits stay unsaved.
        aiSettings_.hfToken = hfToken_;
        ai::AiSettings saved = aiSettingsPath_.empty() ? aiSettings_ : ai::AiSettings::load(aiSettingsPath_);
        saved.hfToken = hfToken_;
        std::string err;
        status_ = !aiSettingsPath_.empty() && saved.save(aiSettingsPath_, err) ? "Hugging Face token saved"
                                                                               : "Save failed " + err;
    }
}

std::string App::comfyDefaultWorkflow(bool text) {
    if (!comfyWorkflowsScanned_) {
        comfyWorkflows_ = ai::comfy::bundledWorkflows();
        comfyWorkflowsScanned_ = true;
    }
    for (const auto& w : comfyWorkflows_)
        if (w.kind == (text ? "text" : "image")) return w.path;
    return {};
}

void App::setComfyWorkflow(const std::string& path, bool text) {
    (text ? aiSettings_.comfyTextWorkflow : aiSettings_.comfyImageWorkflow) = path;
    comfyWorkflowError_.clear();
    Json workflow;
    if (!ai::comfy::loadWorkflow(path, workflow, comfyWorkflowError_)) return;
    const std::string kind = ai::comfy::workflowKind(workflow);
    if (!text && kind != "image")
        comfyWorkflowError_ = "This workflow has no Load Image node, so it can't take the picture - is it a text-to-3D workflow?";
    else if (text && kind == "image")
        comfyWorkflowError_ = "This workflow starts from a picture - use it from the From image tab";
}

void App::drawComfyUi() {
    auto hint = [](const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    };
    hint("Runs a workflow on your own ComfyUI - this computer or a studio GPU server - with whatever models it has "
         "(Hunyuan3D, TRELLIS and more). Nothing goes anywhere else.");
    ImGui::InputText("Server", comfyUrl_, sizeof comfyUrl_);
    ImGui::BeginDisabled(pendingComfyCheck_.valid());
    if (ImGui::Button(pendingComfyCheck_.valid() ? "Connecting..." : "Test connection", ImVec2(-1, 0))) {
        auto promise = std::make_shared<std::promise<ComfyCheck>>();
        pendingComfyCheck_ = promise->get_future();
        std::thread([promise, url = std::string(comfyUrl_)] {
            ComfyCheck check;
            ai::comfy::ServerInfo info;
            check.ok = ai::comfy::checkServer(url, info, check.message);
            if (check.ok) check.message = "Connected: " + info.summary();
            promise->set_value(std::move(check));
        }).detach();
    }
    ImGui::EndDisabled();
    if (pendingComfyCheck_.valid() && pendingComfyCheck_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        comfyCheck_ = pendingComfyCheck_.get();
        comfyChecked_ = true;
    }
    if (comfyChecked_) {
        ImGui::PushStyleColor(ImGuiCol_Text, comfyCheck_.ok ? ImVec4(0.45f, 0.8f, 0.5f, 1) : ImVec4(0.95f, 0.6f, 0.4f, 1));
        ImGui::TextWrapped("%s", comfyCheck_.message.c_str());
        ImGui::PopStyleColor();
    }

    // Workflow: a bundled one, or any API-format file.
    const std::string& chosen = textMode_ ? aiSettings_.comfyTextWorkflow : aiSettings_.comfyImageWorkflow;
    const std::string current = chosen.empty() ? comfyDefaultWorkflow(textMode_) : chosen;
    std::string preview = "Choose a workflow file...";
    if (!current.empty()) {
        const auto name = std::filesystem::path(std::u8string(current.begin(), current.end())).stem().u8string();
        preview.assign(name.begin(), name.end());
    }
    auto browse = [this] {
        comfyPickingText_ = textMode_;
        SDL_ShowOpenFileDialog(onFileChosen, &dialogRequests_[int(FileAction::ComfyWorkflow)], window_, kWorkflowFilters, 2,
                               nullptr, false);
    };
    if (ImGui::BeginCombo("Workflow", preview.c_str())) {
        for (const auto& w : comfyWorkflows_) {
            if (w.kind != (textMode_ ? "text" : "image")) continue;
            if (ImGui::Selectable((w.name + " (bundled)").c_str(), current == w.path)) setComfyWorkflow(w.path, textMode_);
        }
        if (ImGui::Selectable("Other file (API format)...")) browse();
        ImGui::EndCombo();
    }
    if (!comfyWorkflowError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.6f, 0.4f, 1));
        ImGui::TextWrapped("%s", comfyWorkflowError_.c_str());
        ImGui::PopStyleColor();
    }
    hint(textMode_ ? "In ComfyUI use Workflow > Export (API). The node titled sf_prompt gets the description; the "
                     "first mesh it saves is imported. See docs/comfyui.md."
                   : "In ComfyUI use Workflow > Export (API). The Load Image node (title it sf_image) gets the picture; "
                     "the first mesh it saves is imported. See docs/comfyui.md.");
}

void App::drawImageTo3D() {
    const float scale = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const CanvasRect canvas = canvasRect(); // first use: the canvas's top-right corner
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + canvas.x + canvas.w - 306 * scale, vp->Pos.y + canvas.y + 16 * scale),
                            ImGuiCond_FirstUseEver);
    if (!beginPanel("Generate 3D", &showImageTo3D_, 290 * scale)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##source")) {
        const bool wasText = textMode_;
        if (ImGui::BeginTabItem("From image")) {
            textMode_ = false;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("From text")) {
            textMode_ = true;
            ImGui::EndTabItem();
        }
        if (textMode_ != wasText) comfyWorkflowError_.clear(); // it was about the other tab's workflow
        ImGui::EndTabBar();
    }
    if (textMode_) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##textprompt", textPrompt_, sizeof textPrompt_, ImVec2(-1, ImGui::GetTextLineHeight() * 3.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Describe one object: \"a stylised dragon head with curled horns\", \"a cartoon fox sitting\".");
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button(refImage_.empty() ? "Choose image..." : "Change image...", ImVec2(-1, 0)))
            SDL_ShowOpenFileDialog(onFileChosen, &dialogRequests_[int(FileAction::OpenImage)], window_, kImageFilters, 2,
                                   nullptr, false);
        if (refImage_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Or drop a sketch, concept, logo, photo or height map onto the window.");
            ImGui::PopStyleColor();
            ImGui::End();
            return;
        }
        const float thumbW = ImGui::GetContentRegionAvail().x;
        const float thumbH = std::min(thumbW / refImage_.aspect(), 180.0f * scale);
        const float drawW = thumbH * refImage_.aspect();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (thumbW - drawW) * 0.5f);
        ImGui::Image(ImTextureID(uintptr_t(refTexture_)), ImVec2(drawW, thumbH));
        auto fileName = std::filesystem::path(std::u8string(refImagePath_.begin(), refImagePath_.end())).filename().u8string();
        ImGui::TextDisabled("%s  (%dx%d)", std::string(fileName.begin(), fileName.end()).c_str(), refImage_.width,
                            refImage_.height);
    }

    auto hint = [](const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    };
    const float labelW = ImGui::CalcTextSize("Resolution").x + ImGui::GetStyle().ItemInnerSpacing.x;

    ImGui::SeparatorText(textMode_ ? "Text to 3D (AI)" : "Full 3D model (AI reconstruction)");
    hint(textMode_ ? "Builds a whole model from the description with a generative 3D model, then makes it sculptable."
                   : "Builds the whole model - limbs, face, back - from one picture with a reconstruction model, then "
                     "makes it sculptable. Characters on a transparent background work best.");
    static const char* services[] = {"Hugging Face - free models", "fal.ai - paid, your key", "Meshy - paid, your key",
                                     "ComfyUI - your server", "My generator command", "Offline blockout (inflate silhouette)"};
    static const char* serviceIds[] = {"huggingface", "fal", "meshy", "comfyui", "command", "inflate"};
    constexpr int kServices = int(std::size(serviceIds));
    int service = 0;
    for (int i = 0; i < kServices; ++i)
        if (aiSettings_.generatorService == serviceIds[i]) service = i;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##service", &service, services, kServices)) aiSettings_.generatorService = serviceIds[service];
    const std::string serviceId = serviceIds[service];
    ImGui::PushItemWidth(-labelW);
    if (serviceId == "huggingface") {
        const auto& presets = ai::hfPresets();
        const char* preview = presets[0].label;
        for (const auto& p : presets)
            if (aiSettings_.hfModel == p.id) preview = p.label;
        if (ImGui::BeginCombo("Model", preview)) {
            for (const auto& p : presets)
                if (ImGui::Selectable(p.label, aiSettings_.hfModel == p.id)) {
                    aiSettings_.hfModel = p.id;
                    aiSettings_.hfModelChosen = true;
                }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", ai::kHunyuanLicenceNote);
        const bool hasToken = hfToken_[0] || !ai::AiSettings{}.resolvedHfToken().empty(); // field, else HF_TOKEN
        if (ai::hunyuanModel(aiSettings_.hfModel)) hint(ai::kHunyuanLicenceNote);
        if (aiSettings_.hfModel == "trellis-2" && !hasToken)
            hint("TRELLIS.2 needs a free Hugging Face token; without one, Hunyuan3D 2.1 runs instead.");
        ImGui::InputText("HF token", hfToken_, sizeof hfToken_, ImGuiInputTextFlags_Password);
        hint(hasToken
                 ? "Runs on Hugging Face's shared GPUs with your account's free daily allowance."
                 : "Runs free on Hugging Face's shared GPUs with a small daily allowance. A free token "
                   "(huggingface.co/settings/tokens, read access) raises it; TRELLIS.2 needs one.");
    } else if (serviceId == "fal") {
        const auto& models = ai::falModels();
        const char* preview = falModel_;
        for (const auto& m : models)
            if (std::string(falModel_) == m.id) preview = m.label;
        if (ImGui::BeginCombo("Model", preview)) {
            for (const auto& m : models)
                if (ImGui::Selectable(m.label, std::string(falModel_) == m.id))
                    std::snprintf(falModel_, sizeof falModel_, "%s", m.id);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", ai::kHunyuanLicenceNote);
        ImGui::InputText("Model id", falModel_, sizeof falModel_);
        if (ai::hunyuanModel(falModel_)) hint(ai::kHunyuanLicenceNote);
        ImGui::InputText("API key", falKey_, sizeof falKey_, ImGuiInputTextFlags_Password);
        if (!falKey_[0]) hint(std::getenv("FAL_KEY") ? "Using FAL_KEY from the environment." : "Get a key at fal.ai/dashboard/keys");
    } else if (serviceId == "meshy") {
        ImGui::InputText("API key", meshyKey_, sizeof meshyKey_, ImGuiInputTextFlags_Password);
        if (!meshyKey_[0])
            hint(std::getenv("MESHY_API_KEY") ? "Using MESHY_API_KEY from the environment." : "Get a key at meshy.ai (API settings)");
    } else if (serviceId == "comfyui") {
        drawComfyUi();
    } else if (serviceId == "command") {
        hint("Any local model or script: {input} = image, {output} = .glb/.obj to write. See docs/generators.md.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##generator", generatorCommand_, sizeof generatorCommand_,
                                  ImVec2(-1, ImGui::GetTextLineHeight() * 4));
    } else {
        hint("Offline and instant, but only a puffy blockout of the outline - not a real reconstruction.");
    }
    if (serviceId != "inflate") {
        static const char* remeshModes[] = {"Quad Remesh", "Voxel remesh", "Off (raw mesh)"};
        ImGui::Combo("Remesh", &generatedRemesh_, remeshModes, 3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Quad Remesh (default): clean quads ready for sculpting and subdividing, with one subdivision "
                              "level holding the detail. Voxel remesh: an even, watertight triangle surface. Off: the "
                              "model as the service made it.");
        if (generatedRemesh_ != int(GeneratedRemesh::Off)) ImGui::SliderInt("Resolution", &remeshResolution_, 96, 400);
        if (generatedRemesh_ == int(GeneratedRemesh::Quad))
            ImGui::TextDisabled("About %d quads, then 1 subdivision level", generatedQuadTarget(remeshResolution_));
    }
    ImGui::PopItemWidth();
    if (pendingGenerator_.valid()) {
        generatorTime_ += ImGui::GetIO().DeltaTime;
        const int pct = generatorProgress_ ? generatorProgress_->percent.load() : -1;
        ImGui::ProgressBar(pct >= 0 ? float(pct) / 100.0f : -1.0f * float(ImGui::GetTime()), ImVec2(-1, 0),
                           generatorProgress_ ? generatorProgress_->status().c_str() : "Working...");
        if (ImGui::Button("Cancel", ImVec2(-1, 0)) && generatorProgress_) generatorProgress_->cancel = true;
    } else if (ImGui::Button("Generate 3D model", ImVec2(-1, 0))) {
        startGenerator();
    }
    if (openHunyuanNotice_) {
        ImGui::OpenPopup("Hunyuan3D licence");
        openHunyuanNotice_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(320 * scale, 0));
    if (ImGui::BeginPopupModal("Hunyuan3D licence", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", ai::kHunyuanLicenceNote);
        ImGui::TextDisabled("This stays in the Model tooltip.");
        if (ImGui::Button("OK", ImVec2(-1, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::SmallButton("Save settings")) {
        aiSettings_.generatorCommand = generatorCommand_;
        aiSettings_.falKey = falKey_;
        aiSettings_.falModel = falModel_;
        aiSettings_.meshyKey = meshyKey_;
        aiSettings_.hfToken = hfToken_;
        aiSettings_.comfyUrl = comfyUrl_;
        std::string err;
        status_ = !aiSettingsPath_.empty() && aiSettings_.save(aiSettingsPath_, err) ? "Image to 3D settings saved"
                                                                                        : "Save failed " + err;
    }

    if (textMode_) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Quick shapes (offline)");
    if (ImGui::TreeNode("Silhouette blockout")) {
        hint("Inflates the outline into a rounded blob - good for simple shapes, not characters.");
        ImGui::PushItemWidth(-labelW);
        ImGui::SliderFloat("Thickness", &inflateThickness_, 0.2f, 2.0f, "%.2f");
        ImGui::SliderInt("Detail##inflate", &inflateDetail_, 64, 384);
        ImGui::PopItemWidth();
        if (ImGui::Button("Inflate", ImVec2(-1, 0)))
            runCommand("image_inflate", {{"path", refImagePath_},
                                         {"thickness", double(inflateThickness_)},
                                         {"resolution", double(inflateDetail_)},
                                         {"subdivisions", 2.0}});
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Relief")) {
        hint("Light areas rise: logos, coins, emblems, height maps, texture photos.");
        ImGui::PushItemWidth(-labelW);
        ImGui::SliderFloat("Depth##relief", &reliefDepth_, 0.01f, 0.5f, "%.2f");
        ImGui::SliderInt("Detail##relief", &reliefDetail_, 64, 1024);
        ImGui::PopItemWidth();
        ImGui::Checkbox("Dark is high##relief", &reliefInvert_);
        if (ImGui::Button("Make relief", ImVec2(-1, 0)))
            runCommand("image_relief", {{"path", refImagePath_},
                                        {"depth", double(reliefDepth_)},
                                        {"resolution", double(reliefDetail_)},
                                        {"invert", reliefInvert_}});
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Stamp onto model")) {
        hint("Projects the image onto the model from the current view as surface detail (uses the active layer).");
        ImGui::PushItemWidth(-labelW);
        ImGui::SliderFloat("Depth##project", &projectDepth_, 0.001f, 0.1f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::PopItemWidth();
        ImGui::Checkbox("Dark is high##project", &projectInvert_);
        if (ImGui::Button("Stamp from this view", ImVec2(-1, 0))) projectImageFromView();
        ImGui::TreePop();
    }
    ImGui::End();
}

// The Splats palette (it comes into the right tray by itself when splats arrive, see drawShell).
void App::drawPalette_Splats() {
    if (doc_.splats.empty()) {
        drawSplatTrainSection(); // train one from a phone capture (PanelSplatTrain.cpp)
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Open or drop a .ply capture (Polycam, Luma, Postshot, Scaniverse...) to sculpt against it.");
        ImGui::PopStyleColor();
        if (ImGui::Button("Sculpt to splats", ImVec2(-1, 0))) runCommand("mesh_to_splats");
        if (ImGui::Button("Try a test head##splats", ImVec2(-1, 0))) runCommand("splat_test_capture", {{"kind", std::string("head")}});
        ui::HelpText("Try a test head", nullptr,
                     "Load a built-in test capture (a stylised head as Gaussian splats, with a few floaters) to try Make "
                     "sculptable without a scan of your own. splat_test_capture kind=object loads a mug instead.",
                     "Splats");
        return;
    }
    ImGui::Text("%zu splats (SH %d)", doc_.splats.size(), doc_.splats.shDegree);
    ImGui::Checkbox("Show##splats", &renderSettings_.showSplats);
    ImGui::SameLine();
    ImGui::Checkbox("Show mesh", &renderSettings_.showMesh);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##splatopacity", &renderSettings_.splatOpacity, 0.05f, 1.0f, "opacity %.2f");
    const float third = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    if (ImGui::Button("Flip", ImVec2(third, 0))) runCommand("splat_transform", {{"rotate_x", 180.0}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn upside down (most captures are)");
    ImGui::SameLine();
    if (ImGui::Button("Turn 90", ImVec2(third, 0))) runCommand("splat_transform", {{"rotate_y", 90.0}});
    ImGui::SameLine();
    if (ImGui::Button("Fit", ImVec2(third, 0))) runCommand("splat_transform", {{"fit", true}});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Centre and scale to the sculpt");
    if (ImGui::Button("Clean floaters", ImVec2(-1, 0))) runCommand("splat_clean", {{"min_opacity", 0.1}, {"keep", 0.97}});
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Save splats...", ImVec2(half, 0))) saveFileDialog(FileAction::SaveSplats);
    ImGui::SameLine();
    if (ImGui::Button("Clear##splats", ImVec2(half, 0))) runCommand("splat_clear");
    drawSplatTrainSection();
    drawSculptableSection(); // Make sculptable (PanelSculptable.cpp)
}

bool App::drawSurfaceCursor(ImDrawList* dl, float radiusPx, ImU32 col, bool ring) {
    // Only what brushes work on: the active, visible SubTool with an up-to-date BVH (a stale one, after a
    // whole-mesh change, is rebuilt at the next press; until then the flat ring shows), not an LOD preview.
    if (previewLod_ >= 0 || doc_.bvhDirty) return false;
    // Not while the solver thread is moving this mesh (predictive strokes): reading it then races the dabs.
    if (solverBusy() || (asyncStroke_ && drag_ == Drag::Stroke)) return false;
    RayHit hit;
    if (!pick(mouseX_, mouseY_, hit)) return false; // one ray, as a press would cast
    // The world radius a dab here gets (App::firstDab / the brushes: Draw Size times world size of a pixel).
    const float radius = radiusPx * camera_.worldPerPixel(hit.position, canvasHeight());
    if (!(radius > 0.0f) || !std::isfinite(radius)) return false;
    // A steady normal over the brush's area (32 rays), then the ring and the Focal Shift ring as true circles in
    // that plane (#88): no per-point pressing, so bumps can't bend or break the ring. Never a pass over the mesh.
    const glm::vec3 n = surfaceCursorNormal(doc_.mesh, doc_.bvh, hit, radius);
    surfaceCursorCircle(hit.position, n, radius, 64, cursorRing_);
    surfaceCursorCircle(hit.position, n, radius * focalRingFraction(focalShift_), 48, cursorInner_);

    const glm::vec3 eye = camera_.position(), forward = camera_.forward();
    const bool ortho = camera_.ortho;
    auto facing = [&](const glm::vec3& p, const glm::vec3& pn) {
        return glm::dot(pn, ortho ? -forward : eye - p) > 0.0f;
    };
    auto drawRing = [&](const std::vector<glm::vec3>& points, ImU32 colour, float width) {
        ImVec2 sp[64];
        const size_t count = std::min<size_t>(points.size(), 64);
        for (size_t i = 0; i < count; ++i)
            if (!gizmoProject(points[i], sp[i])) return; // (behind the camera: a huge brush close up)
        dl->AddPolyline(sp, int(count), colour, ImDrawFlags_Closed, width);
    };
    auto fade = [](ImU32 colour, float k) {
        const ImU32 a = ImU32(float((colour >> IM_COL32_A_SHIFT) & 0xFF) * k);
        return (colour & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    };
    // The mirrored and radial copies (Sculptor::copies; the first is this one) as dots where their dabs land
    // (#88): one ring per copy crowded the view. A copy that lands on the cursor itself (hovering on the plane or
    // the axis) or faces away adds nothing.
    refreshSymmetryPivot();
    const std::vector<SymCopy> mirrors = Sculptor::copies(symmetry_);
    symmetryCopiesDrawn_ = 1; // this one
    for (size_t k = 1; k < mirrors.size(); ++k) {
        const SymCopy& m = mirrors[k];
        const glm::vec3 q = m.point(hit.position);
        if (glm::length(q - hit.position) < radius * 0.05f || !facing(q, m.dir(n))) continue;
        ImVec2 at;
        if (!gizmoProject(q, at)) continue;
        ++symmetryCopiesDrawn_;
        dl->AddCircleFilled(at, 3.0f * uiScale_, IM_COL32(0, 0, 0, 110));
        dl->AddCircleFilled(at, 2.25f * uiScale_, fade(col, 0.8f));
    }
    if (ring) {
        drawRing(cursorRing_, col, 2.0f);
        drawRing(cursorInner_, IM_COL32(255, 255, 255, 90), 1.0f);
    }
    // The surface's direction: a short line out of the centre (with a dot where the brush touches).
    ImVec2 c0, c1;
    if (gizmoProject(hit.position, c0)) {
        if (ring && gizmoProject(hit.position + n * (radius * 0.4f), c1)) dl->AddLine(c0, c1, fade(col, 0.85f), 1.5f);
        dl->AddCircleFilled(c0, 2.0f * uiScale_, col);
    }
    return true;
}

std::array<AxisGizmoEnd, 6> App::axisGizmoLayout() const {
    // Top left of the canvas, below the timeline strip when that is at the top (timelineTopInset_).
    const CanvasRect canvas = canvasRect();
    const float s = 34.0f * axisGizmoUnit();
    return axisGizmoEnds(glm::mat3(camera_.view()),
                         glm::vec2(canvas.x + s + 18.0f * uiScale_, canvas.y + timelineTopInset_ + s + 18.0f * uiScale_), s);
}

float App::axisGizmoUnit() const { return uiScale_ * axisGizmoScale(uiPrefs_.axisGizmoSize); }

float App::viewFigureRadius() const { return 34.0f * axisGizmoUnit(); }

glm::vec2 App::viewFigureCentre() const {
    // In the axes' place when they are off, else just below them.
    const CanvasRect canvas = canvasRect();
    const float r = viewFigureRadius();
    const float top = canvas.y + timelineTopInset_ + 18.0f * uiScale_;
    const float below = uiPrefs_.showAxes ? 2.0f * 34.0f * axisGizmoUnit() + 14.0f * uiScale_ : 0.0f;
    return glm::vec2(canvas.x + 18.0f * uiScale_ + 34.0f * axisGizmoUnit(), top + below + r);
}

bool App::axisGizmoMouseDown(float x, float y) {
    // A click on an end snaps to that side view, orthographic (#43, as in Blender's navigation gizmo); a click on
    // the View Figure to the side view nearest the current one.
    const auto ends = axisGizmoLayout();
    int hit = uiPrefs_.showAxes ? axisGizmoHit(ends, x, y, 9.0f * axisGizmoUnit()) : -1;
    if (hit < 0 && uiPrefs_.showViewFigure &&
        glm::length(glm::vec2(x, y) - viewFigureCentre()) <= viewFigureRadius())
        hit = axisGizmoFacing(ends);
    if (hit < 0) return false;
    if (const Action* a = actions_.find(axisGizmoView(ends[size_t(hit)].axis, ends[size_t(hit)].negative).action); a && a->run) a->run();
    return true;
}

void App::drawAxisGizmo() {
    // The world axes as the camera sees them, under the UI: red X, green Y, blue Z, a labelled ball at the
    // positive end and a plain one at the negative end; the ends pointing away are drawn first and dimmer. A click
    // on an end snaps to that side (axisGizmoMouseDown); the end under the mouse gets a ring.
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float unit = axisGizmoUnit();
    auto ends = axisGizmoLayout();
    const glm::vec2 c = (ends[0].at + ends[3].at) * 0.5f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int hover = ImGui::GetIO().WantCaptureMouse ? -1 : axisGizmoHit(ends, mouse.x, mouse.y, 9.0f * unit);
    const AxisGizmoEnd hovered = hover >= 0 ? ends[size_t(hover)] : AxisGizmoEnd{};
    std::sort(ends.begin(), ends.end(), [](const AxisGizmoEnd& a, const AxisGizmoEnd& b) { return a.depth < b.depth; });
    static constexpr ImU32 colours[3] = {IM_COL32(230, 80, 80, 255), IM_COL32(110, 200, 90, 255), IM_COL32(80, 140, 240, 255)};
    static constexpr const char* names[3] = {"X", "Y", "Z"};
    for (const AxisGizmoEnd& e : ends) {
        const ImVec2 tip(e.at.x, e.at.y);
        ImU32 col = e.depth < -0.2f ? (colours[e.axis] & 0x00ffffffu) | 0x90000000u : colours[e.axis];
        if (e.negative) col = (col & 0xff000000u) | ((col & 0x00fefefeu) >> 1); // the negative end: darker
        else dl->AddLine(ImVec2(c.x, c.y), tip, col, 2.0f * unit);
        dl->AddCircleFilled(tip, (e.negative ? 5.5f : 7.0f) * unit, col);
        if (hover >= 0 && e.axis == hovered.axis && e.negative == hovered.negative)
            dl->AddCircle(tip, 9.0f * unit, IM_COL32(255, 255, 255, 220), 0, 1.5f * unit);
        if (e.negative) continue;
        // The letter shrinks with the ball (Small) but never grows past the UI font.
        const float fontSize = ImGui::GetFontSize() * std::min(1.0f, unit / std::max(uiScale_, 0.01f));
        const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, names[e.axis]);
        dl->AddText(ImGui::GetFont(), fontSize, ImVec2(tip.x - ts.x * 0.5f, tip.y - ts.y * 0.5f), IM_COL32(20, 20, 22, 255),
                    names[e.axis]);
    }
}

void App::drawViewFigure() {
    // A low-resolution copy of the benchmark's procedural head (our own formula, core/Benchmark.cpp), turned with
    // the camera and shaded on the CPU into the background draw list: facing triangles only, far ones first. A
    // click snaps to the nearest side view (axisGizmoMouseDown); a ring shows under the pointer.
    static const Mesh head = makeBenchHead(3000);
    static std::vector<std::pair<float, uint32_t>> order;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const glm::vec2 c = viewFigureCentre();
    const float r = viewFigureRadius();
    const float k = r / 1.25f; // the head with its neck is about 2.5 units tall
    const glm::mat3 rot(camera_.view());
    const glm::vec3 light = glm::normalize(glm::vec3(-0.45f, 0.6f, 0.75f));
    order.clear();
    for (uint32_t t = 0; t < uint32_t(head.triangles.size()); ++t) {
        const glm::uvec3 f = head.triangles[t];
        const glm::vec3 n = rot * (head.normals[f.x] + head.normals[f.y] + head.normals[f.z]);
        if (n.z <= 0.0f) continue;
        const float depth = (rot * (head.positions[f.x] + head.positions[f.y] + head.positions[f.z])).z;
        order.emplace_back(depth, t);
    }
    std::sort(order.begin(), order.end());
    auto at = [&](uint32_t v) {
        const glm::vec3 p = rot * head.positions[v];
        return ImVec2(c.x + p.x * k, c.y - p.y * k);
    };
    // Raw triangles with a colour per corner (smooth shading, no anti-aliasing fringe between triangles).
    auto shade = [&](uint32_t v) {
        const float lit = 0.22f + 0.78f * std::max(0.0f, glm::dot(glm::normalize(rot * head.normals[v]), light));
        const int g = int(std::clamp(lit, 0.0f, 1.0f) * 205.0f + 30.0f);
        return IM_COL32(g, std::min(255, g + 4), std::min(255, g + 12), 255);
    };
    const ImVec2 uv = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    dl->PrimReserve(int(order.size()) * 3, int(order.size()) * 3);
    for (const auto& [depth, t] : order) {
        const glm::uvec3 f = head.triangles[t];
        const ImDrawIdx base = ImDrawIdx(dl->_VtxCurrentIdx);
        for (int i = 0; i < 3; ++i) dl->PrimWriteVtx(at(f[i]), uv, shade(f[i]));
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(ImDrawIdx(base + 1));
        dl->PrimWriteIdx(ImDrawIdx(base + 2));
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (!ImGui::GetIO().WantCaptureMouse && glm::length(glm::vec2(mouse.x, mouse.y) - c) <= r)
        dl->AddCircle(ImVec2(c.x, c.y), r, IM_COL32(255, 255, 255, 140), 0, 1.5f * uiScale_);
}

void App::drawBrushCursor() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const CanvasRect canvas = canvasRect();
    dl->PushClipRect(ImVec2(canvas.x, canvas.y), ImVec2(canvas.x + canvas.w, canvas.y + canvas.h), true);
    struct PopClip {
        ImDrawList* dl;
        ~PopClip() { dl->PopClipRect(); }
    } popClip{dl};
    const float r = radiusPx_ * (pressureSize_ && lastInputPen_ ? std::max(strokePressure(), 0.05f) : 1.0f);
    const glm::vec2 at = predictedPointer(); // during a predictive stroke, where the pen will be as this frame shows
    const ImVec2 c(at.x, at.y);
    const SDL_Keymod mod = inputMods();
    const bool inverted = drag_ == Drag::Stroke ? strokeInvert_
                                                : invertToggle_ != (((mod & SDL_KMOD_ALT) != 0) ||
                                                                    (erasing() && eraserAction() == PenAction::Invert));
    const bool masking = drag_ == Drag::Stroke ? strokeMask_ : ctrlDown(mod) && !(mod & SDL_KMOD_SHIFT);
    if (masking && drag_ != Drag::Stroke && maskShape_ != SelectShape::Pen) { // MaskLasso / MaskRect: a crosshair
        const float arm = 8.0f * uiScale_;
        dl->AddLine(ImVec2(c.x - arm, c.y), ImVec2(c.x + arm, c.y), IM_COL32(230, 230, 230, 220), 1.5f);
        dl->AddLine(ImVec2(c.x, c.y - arm), ImVec2(c.x, c.y + arm), IM_COL32(230, 230, 230, 220), 1.5f);
        return;
    }
    const BrushType shownBrush = drag_ == Drag::Stroke ? strokeBrush_ : activeBrush(); // Shift mid-stroke draws a line
    ImU32 col = masking                                ? IM_COL32(230, 230, 230, 200)
                : shownBrush == BrushType::Smooth    ? IM_COL32(120, 200, 255, 200)
                : inverted                         ? IM_COL32(255, 110, 110, 200)
                                                   : IM_COL32(255, 190, 120, 200);
    if (!masking && paintMode_ && shownBrush != BrushType::Smooth) { // Paint brush: ring in the paint colour
        const glm::vec3 pc = linearToSrgb(inverted ? glm::vec3(1.0f) : doc_.paintColor);
        col = ImGui::GetColorU32(ImVec4(pc.x, pc.y, pc.z, 0.9f));
    }
    // Preferences > Cursor > Hide ring while stroking (#88): the dot and the symmetry dots stay.
    const bool ring = !(uiPrefs_.hideRingWhileStroking && drag_ == Drag::Stroke);
    surfaceCursorShown_ = uiPrefs_.cursorFollowsSurface && drawSurfaceCursor(dl, r, col, ring);
    ringShown_ = ring;
    if (!surfaceCursorShown_) {
        if (ring) {
            dl->AddCircle(c, r, col, 64, 1.5f);
            // The inner ring is Focal Shift, as in ZBrush: where the falloff drops to half.
            dl->AddCircle(c, r * focalRingFraction(focalShift_), IM_COL32(255, 255, 255, 90), 48, 1.0f);
        } else {
            dl->AddCircleFilled(c, 2.0f * uiScale_, col);
        }
    }
    drawPrecisionCursor(dl, c, r, col); // Steady Stroke's string, the precision drag, the size in mm (Precision.cpp)
}

} // namespace sf
