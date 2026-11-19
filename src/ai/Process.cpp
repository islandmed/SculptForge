// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Process.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <cwchar>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace sf::ai {

namespace fs = std::filesystem;

namespace {
fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
} // namespace

std::vector<std::string> currentEnvironment() {
    std::vector<std::string> out;
#if defined(_WIN32)
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* e = block; *e; e += std::wcslen(e) + 1)
            if (*e != L'=') { // (skips the hidden "=C:=C:\\..." drive entries)
                const int n = WideCharToMultiByte(CP_UTF8, 0, e, -1, nullptr, 0, nullptr, nullptr);
                std::string u(n > 0 ? size_t(n - 1) : 0, '\0');
                if (n > 1) WideCharToMultiByte(CP_UTF8, 0, e, -1, u.data(), n, nullptr, nullptr);
                out.push_back(std::move(u));
            }
        FreeEnvironmentStringsW(block);
    }
#else
    for (char** e = environ; e && *e; ++e) out.emplace_back(*e);
#endif
    return out;
}

std::string findOnPath(const std::string& name) {
    if (name.empty()) return {};
    std::error_code ec;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return fs::exists(u8path(name), ec) ? name : std::string();
    const char* path = std::getenv("PATH");
    if (!path) return {};
#if defined(_WIN32)
    const char sep = ';';
    const std::vector<std::string> exts = {".exe", ".bat", ""};
#else
    const char sep = ':';
    const std::vector<std::string> exts = {""};
#endif
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, sep)) {
        if (dir.empty()) continue;
        for (const std::string& ext : exts) {
            const fs::path candidate = u8path(dir) / u8path(name + ext);
            if (fs::is_regular_file(candidate, ec)) {
#if !defined(_WIN32)
                if (::access(candidate.c_str(), X_OK) != 0) continue;
#endif
                return pathString(candidate);
            }
        }
    }
    return {};
}

namespace {
std::string envString(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// The first regular file `name` in each directory of `dirs` (a "*" in a directory name's last part matches any folder:
// winget's package folders are Gyan.FFmpeg_<hash>/ffmpeg-<version>-full_build/bin).
std::string firstIn(const std::vector<std::string>& dirs, const std::string& name) {
    std::error_code ec;
    for (const std::string& d : dirs) {
        if (d.empty()) continue;
        const size_t star = d.find('*');
        if (star == std::string::npos) {
            const fs::path c = u8path(d) / u8path(name);
            if (fs::is_regular_file(c, ec)) return pathString(c);
            continue;
        }
        // "<parent>/<prefix>*/<rest>": every child of <parent> starting with <prefix>, newest name first
        const size_t slash = d.find_last_of("/\\", star);
        const std::string parent = slash == std::string::npos ? "." : d.substr(0, slash);
        const size_t afterStar = d.find_first_of("/\\", star);
        const std::string prefix = d.substr(slash == std::string::npos ? 0 : slash + 1,
                                            star - (slash == std::string::npos ? 0 : slash + 1));
        const std::string rest = afterStar == std::string::npos ? std::string() : d.substr(afterStar + 1);
        std::vector<fs::path> matches;
        for (fs::directory_iterator it(u8path(parent), ec), end; !ec && it != end; it.increment(ec)) {
            const std::string n = pathString(it->path().filename());
            if (n.compare(0, prefix.size(), prefix) == 0) matches.push_back(it->path());
        }
        std::sort(matches.rbegin(), matches.rend());
        for (const fs::path& m : matches) {
            const std::string inner = rest.empty() ? pathString(m) : pathString(m / u8path(rest));
            if (const std::string f = firstIn({inner}, name); !f.empty()) return f;
        }
    }
    return {};
}
} // namespace

std::string findFfmpegExe() {
    if (const std::string e = envString("SCULPTFORGE_FFMPEG"); !e.empty()) return findOnPath(e);
    if (const std::string p = findOnPath("ffmpeg"); !p.empty()) return p;
#if defined(_WIN32)
    const std::string local = envString("LOCALAPPDATA"), data = envString("ProgramData"), user = envString("USERPROFILE"),
                      files = envString("ProgramFiles");
    return firstIn({local + "\\Microsoft\\WinGet\\Links", local + "\\Microsoft\\WinGet\\Packages\\Gyan.FFmpeg*\\ffmpeg*\\bin",
                    data + "\\chocolatey\\bin", user + "\\scoop\\shims", files + "\\ffmpeg\\bin", "C:\\ffmpeg\\bin"},
                   "ffmpeg.exe");
#else
    const std::string home = envString("HOME");
    return firstIn({"/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin", "/usr/bin", "/snap/bin",
                    "/home/linuxbrew/.linuxbrew/bin", home.empty() ? std::string() : home + "/.local/bin",
                    home.empty() ? std::string() : home + "/.linuxbrew/bin"},
                   "ffmpeg");
#endif
}

ChildProcess::~ChildProcess() {
    stopWatchdog();
    closeStdin();
    if (started_ && !finished_) kill(0.5);
#if defined(_WIN32)
    if (job_) CloseHandle(job_); // kill-on-close: ends anything the finished child left running
#endif
}

namespace {
long long steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1;
}
} // namespace

bool ChildProcess::start(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
                         const std::vector<std::pair<std::string, std::string>>& env, std::string& error) {
    return spawn(program, args, logPath, env, false, error);
}

bool ChildProcess::startWithInput(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
                                  const std::vector<std::pair<std::string, std::string>>& env, std::string& error,
                                  double stallSeconds) {
    if (!spawn(program, args, logPath, env, true, error)) return false;
    stallSeconds_ = stallSeconds;
    stalled_ = false;
    watchStop_ = false;
    writeSince_ = 0;
    watchdog_ = std::thread([this] {
        std::unique_lock<std::mutex> lock(watchMutex_);
        while (!watchStop_) {
            watchCv_.wait_for(lock, std::chrono::milliseconds(100));
            const long long since = writeSince_.load();
            if (since != 0 && double(steadyMs() - since) > stallSeconds_ * 1000.0) {
                stalled_ = true;
                terminateNow(); // the blocked write fails now
                writeSince_ = 0;
            }
        }
    });
    return true;
}

void ChildProcess::stopWatchdog() {
    if (!watchdog_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(watchMutex_);
        watchStop_ = true;
    }
    watchCv_.notify_all();
    watchdog_.join();
}

#if defined(_WIN32)

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
// Quoting for CommandLineToArgvW / the MSVC runtime.
void appendQuoted(std::wstring& cmd, const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    for (size_t i = 0;; ++i) {
        size_t slashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++slashes;
        }
        if (i == arg.size()) {
            cmd.append(slashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            cmd.append(slashes * 2 + 1, L'\\');
            cmd += L'"';
        } else {
            cmd.append(slashes, L'\\');
            cmd += arg[i];
        }
    }
    cmd += L'"';
}
} // namespace

bool ChildProcess::spawn(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
                         const std::vector<std::pair<std::string, std::string>>& env, bool pipeStdin, std::string& error) {
    const std::string exe = findOnPath(program);
    if (exe.empty()) {
        error = "Cannot find " + program;
        return false;
    }
    std::wstring cmd;
    appendQuoted(cmd, widen(exe));
    for (const std::string& a : args) {
        cmd += L' ';
        appendQuoted(cmd, widen(a));
    }
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE log = INVALID_HANDLE_VALUE;
    if (!logPath.empty()) {
        log = CreateFileW(widen(logPath).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    } else {
        log = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    }
    // The child inherits our environment; the overrides are only tool settings (RUST_LOG), so setting them
    // on ourselves is harmless and avoids building an environment block. A whole environment (setEnvironment)
    // is a block of its own, sorted without case as Windows expects.
    std::wstring block;
    if (hasFullEnv_) {
        std::vector<std::wstring> entries;
        for (const std::string& e : fullEnv_) {
            const size_t eq = e.find('=');
            bool replaced = false;
            for (const auto& kv : env)
                if (eq != std::string::npos && _stricmp(e.substr(0, eq).c_str(), kv.first.c_str()) == 0) replaced = true;
            if (!replaced && eq != std::string::npos && eq > 0) entries.push_back(widen(e));
        }
        for (const auto& [k, v] : env) entries.push_back(widen(k + "=" + v));
        std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        });
        for (const std::wstring& e : entries) block += e + L'\0';
        block += L'\0';
    } else {
        for (const auto& [k, v] : env) _wputenv_s(widen(k).c_str(), widen(v).c_str());
    }
    // The child's stdin: our pipe, or NUL (a GUI app has no console stdin to hand on: an invalid handle there
    // fails CreateProcess with ERROR_INVALID_HANDLE).
    HANDLE inRead = INVALID_HANDLE_VALUE, inWrite = INVALID_HANDLE_VALUE;
    if (pipeStdin) {
        // (a 1 MB buffer: the default 4 KB makes every frame write wait for ffmpeg to read it in pieces)
        if (!CreatePipe(&inRead, &inWrite, &sa, 1u << 20)) {
            error = "Cannot create a pipe for " + exe + " (error " + std::to_string(GetLastError()) + ")";
            if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
            return false;
        }
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0); // (only the child's end is inherited)
    } else {
        inRead = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = log;
    si.hStdError = log;
    PROCESS_INFORMATION pi{};
    // CREATE_NO_WINDOW: no console for a GUI app's child (the black cmd.exe window of a shell launch).
    // Started suspended and put in a job of its own before it runs, so that kill() also ends what it starts
    // (a launcher's real program, a renderer's worker processes) and closing the app ends them all.
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED |
                                       (hasFullEnv_ ? CREATE_UNICODE_ENVIRONMENT : 0),
                                   hasFullEnv_ ? block.data() : nullptr, nullptr, &si, &pi);
    const DWORD createError = ok ? 0 : GetLastError();
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (inRead != INVALID_HANDLE_VALUE) CloseHandle(inRead);
    if (!ok) {
        if (inWrite != INVALID_HANDLE_VALUE) CloseHandle(inWrite);
        error = "Cannot start " + exe + " (error " + std::to_string(createError) + ")";
        return false;
    }
    if (job_) CloseHandle(job_);
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof limits) ||
            !AssignProcessToJobObject(job_, pi.hProcess)) {
            CloseHandle(job_); // (a job we are in that forbids nesting): the child alone, as before
            job_ = nullptr;
        }
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    if (pipeStdin) stdin_ = inWrite;
    process_ = pi.hProcess;
    started_ = true;
    finished_ = false;
    exitCode_ = -1;
    return true;
}

void ChildProcess::detach() {
    if (job_) { // it outlives us: closing the job must no longer end it
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof limits);
        CloseHandle(job_);
        job_ = nullptr;
    }
    if (process_) CloseHandle(process_);
    process_ = nullptr;
    started_ = false;
}

bool ChildProcess::running() {
    if (!started_ || finished_) return false;
    if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return true;
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    exitCode_ = int(code);
    CloseHandle(process_);
    process_ = nullptr;
    finished_ = true;
    return false;
}

void ChildProcess::terminateNow() {
    if (process_) TerminateProcess(process_, 1);
}

bool ChildProcess::writeStdin(const void* data, size_t bytes) {
    if (!stdin_) return false;
    const char* p = static_cast<const char*>(data);
    writeSince_ = steadyMs();
    while (bytes > 0) {
        DWORD wrote = 0;
        const DWORD chunk = DWORD(std::min<size_t>(bytes, 1u << 20));
        if (!WriteFile(stdin_, p, chunk, &wrote, nullptr) || wrote == 0) {
            writeSince_ = 0;
            return false; // (the child ended: ERROR_BROKEN_PIPE / ERROR_NO_DATA)
        }
        p += wrote;
        bytes -= wrote;
        writeSince_ = steadyMs(); // (progress: the stall clock restarts)
    }
    writeSince_ = 0;
    return true;
}

void ChildProcess::closeStdin() {
    if (stdin_) CloseHandle(stdin_);
    stdin_ = nullptr;
}

void ChildProcess::kill(double) {
    stopWatchdog();
    closeStdin();
    if (running()) {
        if (!job_ || !TerminateJobObject(job_, 1)) TerminateProcess(process_, 1);
        wait();
    } else if (job_) {
        TerminateJobObject(job_, 1); // the child is gone but what it started is not
    }
    if (job_) CloseHandle(job_);
    job_ = nullptr;
}

int ChildProcess::wait() {
    if (!started_) return -1;
    if (!finished_) WaitForSingleObject(process_, INFINITE);
    running();
    return exitCode_;
}

#else

bool ChildProcess::spawn(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
                         const std::vector<std::pair<std::string, std::string>>& env, bool pipeStdin, std::string& error) {
#ifdef __ANDROID__
    // An Android app can't run the desktop's tools (splat trainer, COLMAP, ffmpeg): there are none to find.
    (void)args, (void)logPath, (void)env, (void)pipeStdin;
    error = program + " can't run on Android";
    return false;
#endif
    const std::string exe = findOnPath(program);
    if (exe.empty()) {
        error = "Cannot find " + program;
        return false;
    }
    std::vector<std::string> argStore;
    argStore.push_back(exe);
    argStore.insert(argStore.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : argStore) argv.push_back(a.data());
    argv.push_back(nullptr);

    std::vector<std::string> envStore;
    const std::vector<std::string> base = hasFullEnv_ ? fullEnv_ : currentEnvironment();
    for (const std::string& entry : base) {
        bool replaced = false;
        for (const auto& kv : env)
            if (entry.compare(0, kv.first.size() + 1, kv.first + "=") == 0) replaced = true;
        if (!replaced) envStore.push_back(entry);
    }
    for (const auto& [k, v] : env) envStore.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (std::string& e : envStore) envp.push_back(e.data());
    envp.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    const std::string out = logPath.empty() ? std::string("/dev/null") : logPath;
    posix_spawn_file_actions_addopen(&actions, 1, out.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    int inPipe[2] = {-1, -1};
    if (pipeStdin) {
        if (::pipe(inPipe) != 0) {
            error = "Cannot create a pipe for " + exe + ": " + std::strerror(errno);
            posix_spawn_file_actions_destroy(&actions);
            return false;
        }
        ::fcntl(inPipe[0], F_SETFD, FD_CLOEXEC); // (the dup2 onto 0 below is the child's only copy)
        ::fcntl(inPipe[1], F_SETFD, FD_CLOEXEC);
        posix_spawn_file_actions_adddup2(&actions, inPipe[0], 0);
        std::signal(SIGPIPE, SIG_IGN); // a dead child is a failed write, never the app's death
    } else {
        posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    }
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // Its own process group, so stopping it also stops what it started.
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, exe.c_str(), &actions, &attr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (inPipe[0] >= 0) ::close(inPipe[0]);
    if (rc != 0) {
        if (inPipe[1] >= 0) ::close(inPipe[1]);
        error = "Cannot start " + exe + ": " + std::strerror(rc);
        return false;
    }
    if (pid <= 1) { // (never; but a pid of 0 or 1 would turn every later signal into one on us or on init)
        if (inPipe[1] >= 0) ::close(inPipe[1]);
        error = "Cannot start " + exe + ": no process id";
        return false;
    }
    stdin_ = inPipe[1];
    pid_ = pid;
    started_ = true;
    finished_ = false;
    exitCode_ = -1;
    return true;
}

bool ChildProcess::running() {
    if (!started_ || finished_) return false;
    int status = 0;
    const pid_t r = ::waitpid(pid_, &status, WNOHANG);
    if (r == 0) return true;
    if (r == pid_) exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    finished_ = true; // (reaped, or not ours any more (ECHILD): the pid may be anyone's now)
    pid_ = -1;
    return false;
}

void ChildProcess::detach() { started_ = false; } // (it was spawned in its own process group)

bool signalChildGroup(int pid, int sig) {
    // kill(0) is our own group, kill(-1) everything we may signal, and a stale pid may be anyone's: on a desktop
    // launcher's session that ended the user's session (#183). Only a real child, in a group of its own, is
    // signalled by group.
    if (pid <= 1 || pid == int(::getpid())) return false;
    const pid_t group = ::getpgid(pid);
    if (group < 0) return false; // gone, or never ours
    if (group == ::getpgrp()) return ::kill(pid, sig) == 0; // it shares our group (the spawn's setpgroup did not happen): it alone
    if (group == pid) return ::kill(-pid, sig) == 0;
    return ::kill(pid, sig) == 0; // someone else's group: only the child itself
}

void ChildProcess::terminateNow() {
    const int pid = pid_;
    if (pid > 1) signalChildGroup(pid, SIGKILL);
}

bool ChildProcess::writeStdin(const void* data, size_t bytes) {
    if (stdin_ < 0) return false;
    const char* p = static_cast<const char*>(data);
    writeSince_ = steadyMs();
    while (bytes > 0) {
        const ssize_t n = ::write(stdin_, p, bytes);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            writeSince_ = 0;
            return false; // (EPIPE: the child ended)
        }
        p += n;
        bytes -= size_t(n);
        writeSince_ = steadyMs(); // (progress: the stall clock restarts)
    }
    writeSince_ = 0;
    return true;
}

void ChildProcess::closeStdin() {
    if (stdin_ >= 0) ::close(stdin_);
    stdin_ = -1;
}

void ChildProcess::kill(double graceSeconds) {
    stopWatchdog();
    closeStdin();
    if (!running()) return; // (reaped, never started or detached = nothing of ours to signal)
    signalChildGroup(pid_, SIGTERM);
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(int(graceSeconds * 1000));
    while (running() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (running()) {
        signalChildGroup(pid_, SIGKILL);
        wait();
    }
}

int ChildProcess::wait() {
    if (!started_) return -1;
    if (!finished_) {
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
        }
        exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        finished_ = true;
    }
    return exitCode_;
}

#endif

int runProcess(const std::string& program, const std::vector<std::string>& args, std::string& output,
               const std::string& scratchDir) {
    std::error_code ec;
    fs::create_directories(u8path(scratchDir), ec);
    static std::atomic<int> counter{0};
    const std::string log = pathString(u8path(scratchDir) / ("run-" + std::to_string(++counter) + ".log"));
    fs::remove(u8path(log), ec);
    ChildProcess p;
    if (!p.start(program, args, log, {}, output)) return -1;
    const int code = p.wait();
    std::ifstream f(u8path(log), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    output = ss.str();
    f.close();
    fs::remove(u8path(log), ec);
    return code;
}

} // namespace sf::ai
