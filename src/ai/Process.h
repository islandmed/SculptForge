// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A child process with its output going to a log file: external tools that run
// for minutes (the Brush splat trainer, COLMAP, ffmpeg) without a shell, so
// paths need no quoting and the process can be stopped. posix_spawn on
// Linux/macOS, CreateProcessW on Windows.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sf::ai {

class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess(); // kills a process that is still running
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Starts `program` (a path, or a name looked up on PATH) with `args`. stdout
    // and stderr are appended to `logPath` ("" = discarded). `env` adds or
    // replaces environment variables for the child.
    bool start(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
               const std::vector<std::pair<std::string, std::string>>& env, std::string& error);
    // True while it runs; once it has ended, exitCode() is valid.
    bool running();
    int exitCode() const { return exitCode_; }
    // Stops it (SIGTERM, then SIGKILL after `graceSeconds`; TerminateProcess on Windows) and waits.
    void kill(double graceSeconds = 2.0);
    // Waits for it to end; returns the exit code.
    int wait();
    bool started() const { return started_; }
    // Lets it run on after this object (and this app) is gone: the updater's relaunch.
    void detach();

private:
    bool started_ = false;
    bool finished_ = false;
    int exitCode_ = -1;
#if defined(_WIN32)
    void* process_ = nullptr;
#else
    int pid_ = -1;
#endif
};

#if !defined(_WIN32)
// Sends `sig` to the process group of child `pid` when the child leads it. Never signals pid <= 1, ourselves or our
// own group (kill(0) and kill(-1) end the user's session); a child outside a group of its own gets the signal alone.
// False when nothing was sent.
bool signalChildGroup(int pid, int sig);
#endif

// The full path of `name` on PATH (with .exe on Windows), or "" when it isn't there.
std::string findOnPath(const std::string& name);

// Runs a program to completion with its output in `output` (both streams). Returns the exit code, or -1 if it
// could not start (`output` then holds the reason). For short tools only (version checks, tar).
int runProcess(const std::string& program, const std::vector<std::string>& args, std::string& output,
               const std::string& scratchDir);

} // namespace sf::ai
