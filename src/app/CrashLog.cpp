// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CrashLog.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>
#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define SF_HAVE_BACKTRACE 1
#endif
#endif

namespace sf::crashlog {

namespace {

constexpr const char* kClosed = "SculptForge closed normally.";
constexpr size_t kMaxBytes = 8u << 20; // a runaway log stops growing here (crash notes are still written)

std::mutex g_mutex;
FILE* g_file = nullptr;
int g_fd = -1; // the same file, for the crash handlers (which can't take locks or allocate)
std::string g_path, g_previous;
bool g_previousCrashed = false;
size_t g_bytes = 0;
SDL_LogOutputFunction g_sdlDefault = nullptr;
void* g_sdlDefaultData = nullptr;
// The last command, as a C string the crash handlers can read without allocating.
char g_last[256] = "(none yet)";
std::atomic<bool> g_crashing{false};

std::filesystem::path fsPath(const std::string& utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string timeStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void writeLocked(const std::string& line) {
    if (!g_file || (g_bytes > kMaxBytes && line.rfind("CRASH", 0) != 0)) return;
    const std::string text = timeStamp() + "  " + line + "\n";
    std::fwrite(text.data(), 1, text.size(), g_file);
    std::fflush(g_file);
    g_bytes += text.size();
}

// --- Crash handlers: async-signal-safe writes of fixed text to g_fd ---

void rawWrite(const char* s) {
    if (g_fd < 0 || !s) return;
#if defined(_WIN32)
    _write(g_fd, s, unsigned(std::strlen(s)));
#else
    size_t n = std::strlen(s);
    while (n > 0) {
        const ssize_t w = ::write(g_fd, s, n);
        if (w <= 0) break;
        s += w;
        n -= size_t(w);
    }
#endif
}

[[maybe_unused]] void rawWriteNumber(unsigned long long v, int base) {
    char buf[24];
    int i = sizeof buf - 1;
    buf[i] = 0;
    do {
        buf[--i] = "0123456789abcdef"[v % unsigned(base)];
        v /= unsigned(base);
    } while (v && i > 0);
    if (base == 16) rawWrite("0x");
    rawWrite(buf + i);
}

void crashHeader(const char* what, const char* detail = nullptr) {
    rawWrite("\nCRASH: SculptForge " SCULPTFORGE_VERSION " crashed: ");
    rawWrite(what);
    rawWrite(detail);
    rawWrite("\nCRASH: last thing done: ");
    rawWrite(g_last);
    rawWrite("\n");
}

#if !defined(_WIN32)
void onSignal(int sig) {
    if (!g_crashing.exchange(true)) {
        const char* name = sig == SIGSEGV ? "segmentation fault (SIGSEGV)"
                           : sig == SIGABRT ? "aborted (SIGABRT)"
                           : sig == SIGFPE  ? "arithmetic error (SIGFPE)"
                           : sig == SIGILL  ? "illegal instruction (SIGILL)"
                           : sig == SIGBUS  ? "bus error (SIGBUS)"
                                            : "signal";
        crashHeader(name);
#if defined(SF_HAVE_BACKTRACE)
        void* frames[64];
        const int n = backtrace(frames, 64);
        rawWrite("CRASH: stack:\n");
        if (g_fd >= 0) backtrace_symbols_fd(frames, n, g_fd);
#endif
    }
    std::signal(sig, SIG_DFL); // the system's own handling (core dump, crash reporter) follows
    std::raise(sig);
}
#else
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

LONG WINAPI onException(EXCEPTION_POINTERS* info) {
    // An uncaught C++ exception (MSVC's code 0xE06D7363): the C runtime's own filter, which this one replaced,
    // turns it into std::terminate, where onTerminate writes the exception's text. Hand it on.
    constexpr DWORD kCppException = 0xE06D7363;
    if (info && info->ExceptionRecord && info->ExceptionRecord->ExceptionCode == kCppException && g_previousFilter)
        return g_previousFilter(info);
    if (!g_crashing.exchange(true)) {
        crashHeader("unhandled exception");
        const EXCEPTION_RECORD* r = info ? info->ExceptionRecord : nullptr;
        if (r) {
            rawWrite("CRASH: code ");
            rawWriteNumber(r->ExceptionCode, 16);
            rawWrite(" at ");
            rawWriteNumber(reinterpret_cast<uintptr_t>(r->ExceptionAddress), 16);
            HMODULE module = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(r->ExceptionAddress), &module) && module) {
                char name[MAX_PATH] = {};
                GetModuleFileNameA(module, name, MAX_PATH);
                const char* base = std::strrchr(name, '\\');
                rawWrite(" (");
                rawWrite(base ? base + 1 : name);
                rawWrite(" + ");
                rawWriteNumber(reinterpret_cast<uintptr_t>(r->ExceptionAddress) - reinterpret_cast<uintptr_t>(module), 16);
                rawWrite(")");
            }
            rawWrite("\n");
        }
    }
    return EXCEPTION_CONTINUE_SEARCH; // Windows' own crash handling follows
}

void onAbortSignal(int) {
    if (!g_crashing.exchange(true)) crashHeader("aborted (abort() or a failed check)");
    std::signal(SIGABRT, SIG_DFL);
    std::raise(SIGABRT);
}
#endif

void onTerminate() {
    if (!g_crashing.exchange(true)) {
        const char* what = "uncaught exception";
        try {
            if (std::exception_ptr e = std::current_exception()) std::rethrow_exception(e);
        } catch (const std::exception& e) {
            crashHeader("uncaught exception: ", e.what());
            what = nullptr;
        } catch (...) {
        }
        if (what) crashHeader(what);
    }
    std::abort();
}

void SDLCALL onSdlLog(void*, int category, SDL_LogPriority priority, const char* message) {
    if (g_sdlDefault) g_sdlDefault(g_sdlDefaultData, category, priority, message);
    static const char* const names[] = {"", "trace", "verbose", "debug", "info", "warning", "error", "critical"};
    const int p = int(priority);
    std::lock_guard lock(g_mutex);
    writeLocked(p >= 5 && p <= 7 ? std::string(names[p]) + ": " + (message ? message : "") : (message ? message : ""));
}

} // namespace

void start(const std::string& folder) {
    std::lock_guard lock(g_mutex);
    if (g_file) return;
    std::string dir = folder;
    if (dir.empty()) {
        if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
            dir = pref;
            SDL_free(pref);
        }
    }
    if (dir.empty()) return;
    if (dir.back() != '/' && dir.back() != '\\') dir += '/';
    g_path = dir + "sculptforge.log";
    g_previous = dir + "sculptforge-previous.log";

    std::error_code ec;
    std::filesystem::create_directories(fsPath(dir), ec);
    // The last session's log: kept beside, and a crash if it didn't end with the closing line.
    if (std::filesystem::exists(fsPath(g_path), ec)) {
        std::ifstream in(fsPath(g_path), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), {});
        in.close();
        g_previousCrashed = text.find(kClosed) == std::string::npos;
        std::filesystem::remove(fsPath(g_previous), ec);
        std::filesystem::rename(fsPath(g_path), fsPath(g_previous), ec);
    }
#if defined(_WIN32)
    g_file = _wfopen(fsPath(g_path).c_str(), L"wb");
    if (g_file) g_fd = _fileno(g_file);
#else
    g_file = std::fopen(g_path.c_str(), "wb");
    if (g_file) g_fd = fileno(g_file);
#endif
    if (!g_file) {
        g_path.clear();
        return;
    }
    writeLocked(std::string("SculptForge ") + SCULPTFORGE_VERSION + " on " + SDL_GetPlatform() + ", " +
                std::to_string(SDL_GetNumLogicalCPUCores()) + " CPU threads, " + std::to_string(SDL_GetSystemRAM()) +
                " MB RAM, SDL " + std::to_string(SDL_MAJOR_VERSION) + "." + std::to_string(SDL_MINOR_VERSION) + "." +
                std::to_string(SDL_MICRO_VERSION) + " (linked " + std::to_string(SDL_VERSIONNUM_MAJOR(SDL_GetVersion())) +
                "." + std::to_string(SDL_VERSIONNUM_MINOR(SDL_GetVersion())) + "." +
                std::to_string(SDL_VERSIONNUM_MICRO(SDL_GetVersion())) + ")");
    if (g_previousCrashed)
        writeLocked("The last session did not close normally: its log, with the last command it ran, is " + g_previous);

    SDL_GetLogOutputFunction(&g_sdlDefault, &g_sdlDefaultData);
    SDL_SetLogOutputFunction(onSdlLog, nullptr);
    std::set_terminate(onTerminate);
#if defined(_WIN32)
    g_previousFilter = SetUnhandledExceptionFilter(onException);
    ULONG guarantee = 64 * 1024; // room for the note after a stack overflow
    SetThreadStackGuarantee(&guarantee);
    std::signal(SIGABRT, onAbortSignal);
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // our note instead of a dialog box
#endif
#else
    // On a stack of their own, so a stack overflow can still write its note.
    static std::vector<char> altStack(std::max<size_t>(64 * 1024, size_t(SIGSTKSZ)));
    stack_t ss{};
    ss.ss_sp = altStack.data();
    ss.ss_size = altStack.size();
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) sigaction(sig, &sa, nullptr);
#if defined(SF_HAVE_BACKTRACE)
    void* warm[1];
    backtrace(warm, 1); // loads the unwinder now: the first call allocates, which a crash handler must not
#endif
#endif
}

void note(const std::string& line) {
    std::lock_guard lock(g_mutex);
    writeLocked(line);
}

void action(const std::string& what) {
    std::lock_guard lock(g_mutex);
    // Commands that run on every slider step would bury the rest: a repeat of the last one isn't written again.
    static std::string last;
    std::snprintf(g_last, sizeof g_last, "%s", what.c_str());
    if (what == last) return;
    last = what;
    writeLocked(what);
}

void quietSystemCrashDialog() {
#if defined(_WIN32)
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#elif defined(__linux__)
    // No core dump either: systemd-coredump would store one and the desktop would pop a crash notice per test run.
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
}

void stop() {
    std::lock_guard lock(g_mutex);
    if (!g_file) return;
    writeLocked(kClosed);
    SDL_SetLogOutputFunction(g_sdlDefault, g_sdlDefaultData);
    std::fclose(g_file);
    g_file = nullptr;
    g_fd = -1;
}

std::string path() {
    std::lock_guard lock(g_mutex);
    return g_path;
}

std::string folderUrl() {
    std::string p = path();
    if (p.empty()) return {};
    for (char& c : p)
        if (c == '\\') c = '/';
    p.erase(p.find_last_of('/') + 1);
    std::string url = p.rfind('/', 0) == 0 ? "file://" : "file:///";
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (std::isalnum(c) || std::strchr("/:-_.~", c)) url += char(c);
        else url += {'%', hex[c >> 4], hex[c & 15]};
    }
    return url;
}

std::string previousPath() {
    std::lock_guard lock(g_mutex);
    return g_previous;
}

bool previousCrashed() {
    std::lock_guard lock(g_mutex);
    return g_previousCrashed;
}

} // namespace sf::crashlog
