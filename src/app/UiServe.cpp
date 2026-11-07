// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// sculptforge --ui-serve: see UiServe.h.
#include "app/UiServe.h"
#include "app/App.h"
#include "app/I18n.h"
#include "ai/AiPolicy.h"
#include "ai/Http.h"
#include "core/Bake.h"
#include "core/LayerStack.h"
#include "core/LitSphere.h"
#include "core/Macro.h"
#include "core/MemoryGuard.h"
#include "core/Pack.h"
#include "core/ThreadPool.h"
#include "core/TierConfig.h"
#include "render/GL.h"
#include "render/Renderer.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#define fileno _fileno
#define fdopen _fdopen
#else
#include <unistd.h>
extern char** environ;
#endif

namespace sf {

namespace {

std::map<std::string, std::string> environment() {
    std::map<std::string, std::string> env;
#if defined(_WIN32)
    char** list = _environ;
#else
    char** list = environ;
#endif
    for (char** e = list; e && *e; ++e) {
        const std::string kv = *e;
        const size_t eq = kv.find('=', 1); // (Windows keeps "=C:=C:\..." entries)
        if (eq != std::string::npos) env[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    return env;
}

void setEnv(const std::string& name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

void unsetEnv(const std::string& name) {
#if defined(_WIN32)
    _putenv_s(name.c_str(), "");
#else
    unsetenv(name.c_str());
#endif
}

// The environment as it was at `saved` (scripts' `env`, the request's -e, isolateSettings' XDG folders).
void restoreEnvironment(const std::map<std::string, std::string>& saved) {
    for (const auto& [name, value] : environment()) {
        const auto it = saved.find(name);
        if (it == saved.end()) unsetEnv(name);
        else if (it->second != value) setEnv(name, it->second);
    }
    for (const auto& [name, value] : saved)
        if (!std::getenv(name.c_str())) setEnv(name, value);
}

// After each script: can this context still draw offscreen? A script that makes the GPU drop the device (WSL's
// d3d12: "Removing Device") leaves the server unable to run any other; it then quits and is started again.
bool glHealthy() {
    while (gl::GetError() != GL_NO_ERROR) {
    }
    GLuint fbo = 0, rb = 0;
    gl::GenFramebuffers(1, &fbo);
    gl::GenRenderbuffers(1, &rb);
    gl::BindRenderbuffer(GL_RENDERBUFFER, rb);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 4, 4);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    const bool complete = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &fbo);
    gl::DeleteRenderbuffers(1, &rb);
    return complete && gl::GetError() == GL_NO_ERROR;
}

// Globals a run leaves set, back to a new process's values. App::init sets the rest again (the AI policy, the
// packs and macro folders, the bake listener and mesh map requester point at the App, hardware tier...).
void resetGlobals() {
    setBakeListener({});
    setMeshMapRequester({});
    setCurrentTierConfig(TierConfig{});
    setAvailableMemoryOverride(0);
    setLanguage(Language::English);
    setPacksSafeMode(false);
    setPacksRoot("");
    setUserMacroDir("");
    ai::setAiForcedOff(false);
    ai::setAiEnabled(true);
    ai::resetHttpRequestCount();
    clearCustomLitSpheres();
    int joysticks = 0; // a script's virtual SpaceMouse (UiTest.cpp), which a process's end would take away
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&joysticks)) {
        for (int i = 0; i < joysticks; ++i)
            if (SDL_IsJoystickVirtual(ids[i])) SDL_DetachVirtualJoystick(ids[i]);
        SDL_free(ids);
    }
    resetConfigGlobals();
    resetDemoGlobals();
    resetPickerGlobals();
    ui::resetUiGlobals();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    SDL_ClearClipboardData(); // a new process owns no clipboard (CMakeLists: clipboard scripts run serially)
    std::srand(1);
}

// A file descriptor programs the scripts start (fake tools, a QRemesh worker) don't inherit.
int privateDup(int fd) {
    const int copy = dup(fd);
#if !defined(_WIN32)
    if (copy >= 0) fcntl(copy, F_SETFD, FD_CLOEXEC);
#endif
    return copy;
}

bool readLine(FILE* in, std::string& line) {
    line.clear();
    for (int c; (c = std::fgetc(in)) != EOF;) {
        if (c == '\n') return true;
        line += char(c);
    }
    return !line.empty();
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        const size_t tab = line.find('\t', at);
        out.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
        if (tab == std::string::npos) return out;
        at = tab + 1;
    }
}

} // namespace

void syncSdlEnvironment() {
    SDL_Environment* sdl = SDL_GetEnvironment();
    const auto now = environment();
    if (char** vars = SDL_GetEnvironmentVariables(sdl)) {
        for (char** v = vars; *v; ++v) {
            const std::string kv = *v;
            const size_t eq = kv.find('=', 1);
            if (eq != std::string::npos && !now.count(kv.substr(0, eq))) SDL_UnsetEnvironmentVariable(sdl, kv.substr(0, eq).c_str());
        }
        SDL_free(vars);
    }
    for (const auto& [name, value] : now) SDL_SetEnvironmentVariable(sdl, name.c_str(), value.c_str(), true);
}

int runUiServer(int argc, char** argv, int (*runOne)(int argc, char** argv)) {
    int width = 1600, height = 960;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--size") std::sscanf(argv[++i], "%dx%d", &width, &height);
        // Servers side by side share the CPU: each pool spinning on every thread would only slow them all down.
        // (Results don't depend on the thread count: core/ThreadPool.h.)
        else if (a == "--threads") ThreadPool::setSharedThreadCount(std::atoi(argv[++i]));
    }
    // As App::init sets them for a UI test, before SDL starts.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_PEN_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_PEN_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    App::glWindowAttributes();
    SDL_Window* window = SDL_CreateWindow("SculptForge", width, height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) {
        std::fprintf(stderr, "No window or OpenGL 3.3 context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    if (const char* missing = gl::load([](const char* n) { return reinterpret_cast<gl::GenericProc>(SDL_GL_GetProcAddress(n)); })) {
        std::fprintf(stderr, "Missing OpenGL function: %s\n", missing);
        return 1;
    }
    App::shareWindow(window, context);
    Renderer::keepPrograms(true); // shaders compile once, not for every script

    // Requests come on the standard input we were given and answers go to its standard output, on private copies:
    // the scripts' programs get an empty input, and each script's own output goes to its log.
    std::fflush(stdout);
    const int answerFd = privateDup(fileno(stdout)), savedErr = privateDup(fileno(stderr)), requestFd = privateDup(fileno(stdin));
    FILE* answers = fdopen(answerFd, "w");
    FILE* requests = fdopen(requestFd, "r");
    if (!answers || !requests) return 1;
#if defined(_WIN32)
    const int empty = _open("NUL", _O_RDONLY);
#else
    const int empty = open("/dev/null", O_RDONLY);
#endif
    if (empty >= 0) {
        dup2(empty, fileno(stdin));
        close(empty);
    }
    const auto savedEnv = environment();
    std::error_code ec;
    const auto savedDir = std::filesystem::current_path(ec);
    std::fprintf(answers, "ready\n");
    std::fflush(answers);

    std::string line;
    while (readLine(requests, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto f = splitTabs(line);
        if (f[0] == "quit") break;
        if (f[0] != "run" || f.size() < 3) {
            std::fprintf(answers, "error\tunknown request\n");
            std::fflush(answers);
            continue;
        }
        std::vector<std::string> args{"sculptforge"};
        for (size_t i = 2; i < f.size(); ++i) {
            if (f[i] == "-e" && i + 1 < f.size()) {
                const std::string& kv = f[++i];
                const size_t eq = kv.find('=');
                if (eq != std::string::npos) setEnv(kv.substr(0, eq), kv.substr(eq + 1));
            } else if (f[i] == "-C" && i + 1 < f.size()) {
                std::filesystem::current_path(std::filesystem::path(std::u8string(f[i + 1].begin(), f[i + 1].end())), ec);
                ++i;
            } else
                args.push_back(f[i]);
        }
        std::vector<char*> argvOne;
        for (auto& a : args) argvOne.push_back(a.data());
        argvOne.push_back(nullptr);

        resetGlobals();
        std::fflush(stdout);
        std::fflush(stderr);
#if defined(_WIN32)
        const int log = _open(f[1].c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0600);
#else
        const int log = open(f[1].c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
#endif
        if (log >= 0) {
            dup2(log, fileno(stdout));
            dup2(log, fileno(stderr));
            close(log);
        }
        const auto start = std::chrono::steady_clock::now();
        const int code = runOne(int(args.size()), argvOne.data());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::fflush(stdout);
        std::fflush(stderr);
        dup2(savedErr, fileno(stdout)); // (not our answers: stray output from a straggling thread goes to stderr)
        dup2(savedErr, fileno(stderr));
        restoreEnvironment(savedEnv);
        std::filesystem::current_path(savedDir, ec);
        const bool healthy = glHealthy();
        std::fprintf(answers, "done\t%d\t%.0f%s\n", code, ms, healthy ? "" : "\tlost");
        std::fflush(answers);
        if (!healthy) break;
    }
    App::shareWindow(nullptr, nullptr);
    Renderer::keepPrograms(false);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace sf
