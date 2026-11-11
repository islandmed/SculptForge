// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Process.h"

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
#else
#include <cerrno>
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

ChildProcess::~ChildProcess() {
    if (started_ && !finished_) kill(0.5);
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

bool ChildProcess::start(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
                         const std::vector<std::pair<std::string, std::string>>& env, std::string& error) {
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
    // on ourselves is harmless and avoids building an environment block.
    for (const auto& [k, v] : env) _wputenv_s(widen(k).c_str(), widen(v).c_str());
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) {
        error = "Cannot start " + exe + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    started_ = true;
    finished_ = false;
    exitCode_ = -1;
    return true;
}

void ChildProcess::detach() {
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

void ChildProcess::kill(double) {
    if (!running()) return;
    TerminateProcess(process_, 1);
    wait();
}

int ChildProcess::wait() {
    if (!started_) return -1;
    if (!finished_) WaitForSingleObject(process_, INFINITE);
    running();
    return exitCode_;
}

#else

bool ChildProcess::start(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
                         const std::vector<std::pair<std::string, std::string>>& env, std::string& error) {
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
    for (char** e = environ; e && *e; ++e) {
        const std::string entry(*e);
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
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // Its own process group, so stopping it also stops what it started.
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, exe.c_str(), &actions, &attr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        error = "Cannot start " + exe + ": " + std::strerror(rc);
        return false;
    }
    if (pid <= 1) { // (never; but a pid of 0 or 1 would turn every later signal into one on us or on init)
        error = "Cannot start " + exe + ": no process id";
        return false;
    }
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

void ChildProcess::kill(double graceSeconds) {
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
