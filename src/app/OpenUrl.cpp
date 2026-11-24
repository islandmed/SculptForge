// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/OpenUrl.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <utility>

#if defined(__linux__) && !defined(__ANDROID__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#define SF_OPENER_POSIX 1
#endif

namespace sf::opener {

namespace {

#ifdef SF_OPENER_POSIX
std::vector<std::string> readEnviron() {
    std::vector<std::string> out;
    for (char** e = environ; e && *e; ++e) out.emplace_back(*e);
    return out;
}
// Before main(): nothing has called setenv yet.
const std::vector<std::string> g_startupEnv = readEnviron();
#else
const std::vector<std::string> g_startupEnv;
#endif

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

std::string valueOf(const std::vector<std::string>& env, const std::string& name) {
    for (const auto& e : env)
        if (startsWith(e, name + "=")) return e.substr(name.size() + 1);
    return {};
}

bool inAppImage(const std::string& entry, const std::string& appDir) {
    return (!appDir.empty() && startsWith(entry, appDir)) || startsWith(entry, "/tmp/.mount_");
}

} // namespace

const std::vector<std::string>& startupEnvironment() { return g_startupEnv; }

std::vector<std::string> cleanEnvironment(const std::vector<std::string>& env, std::string appDir) {
    if (appDir.empty()) appDir = valueOf(env, "APPDIR");
    while (appDir.size() > 1 && appDir.back() == '/') appDir.pop_back();
    std::vector<std::string> out;
    for (const auto& e : env) {
        const size_t eq = e.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = e.substr(0, eq), value = e.substr(eq + 1);
        if (name == "APPDIR" || name == "APPIMAGE" || name == "ARGV0" || name == "OWD") continue;
        std::vector<std::string> items;
        bool changed = false;
        size_t pos = 0;
        while (pos <= value.size()) {
            size_t end = value.find(':', pos);
            if (end == std::string::npos) end = value.size();
            const std::string item = value.substr(pos, end - pos);
            if (!item.empty() && inAppImage(item, appDir)) changed = true;
            else items.push_back(item);
            pos = end + 1;
        }
        std::string kept;
        for (size_t i = 0; i < items.size(); ++i) kept += (i ? ":" : "") + items[i];
        if (!changed) out.push_back(e);
        else if (!kept.empty()) out.push_back(name + "=" + kept);
    }
    return out;
}

bool Debounce::allow(const std::string& url, double nowSec, double windowSec) {
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
                                 [&](const auto& r) { return nowSec - r.second >= windowSec || nowSec < r.second; }),
                  recent_.end());
    for (const auto& r : recent_)
        if (r.first == url) return false;
    recent_.emplace_back(url, nowSec);
    return true;
}

#ifdef SF_OPENER_POSIX

namespace {

std::string findExecutable(const std::string& name, const std::vector<std::string>& env) {
    if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0 ? name : std::string();
    std::string path = valueOf(env, "PATH");
    if (path.empty()) path = "/usr/local/bin:/usr/bin:/bin";
    size_t pos = 0;
    while (pos <= path.size()) {
        size_t end = path.find(':', pos);
        if (end == std::string::npos) end = path.size();
        const std::string dir = path.substr(pos, end - pos);
        const std::string full = (dir.empty() ? std::string(".") : dir) + "/" + name;
        if (access(full.c_str(), X_OK) == 0) return full;
        pos = end + 1;
    }
    return {};
}

// "cmd arg %s" words (BROWSER: %s is the URL, none = the URL goes last); no quoting.
std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> words;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ') ++j;
        if (j > i) words.push_back(s.substr(i, j - i));
        i = j;
    }
    return words;
}

struct Candidate {
    std::string label;
    std::vector<std::string> argv; // argv[0] resolved
};

std::vector<Candidate> candidates(const std::string& url, const std::vector<std::string>& env) {
    std::vector<Candidate> out;
    auto add = [&](std::string label, std::vector<std::string> words) {
        if (words.empty()) return;
        bool hasUrl = false;
        for (auto& w : words) {
            size_t at;
            while ((at = w.find("%s")) != std::string::npos) w.replace(at, 2, url), hasUrl = true;
        }
        if (!hasUrl) words.push_back(url);
        const std::string exe = findExecutable(words[0], env);
        if (exe.empty()) return;
        words[0] = exe;
        out.push_back({std::move(label), std::move(words)});
    };
    add("xdg-open", {"xdg-open"});
    add("gio open", {"gio", "open"});
    const std::string browser = valueOf(env, "BROWSER");
    size_t pos = 0;
    while (!browser.empty() && pos <= browser.size()) {
        size_t end = browser.find(':', pos);
        if (end == std::string::npos) end = browser.size();
        const auto words = splitWords(browser.substr(pos, end - pos));
        if (!words.empty()) add("$BROWSER " + words[0], words);
        pos = end + 1;
    }
    return out;
}

// Forks the opener. Everything the child needs is built before the fork (it is a threaded process: only
// async-signal-safe calls after it).
pid_t spawnDetached(const std::vector<std::string>& argv, const std::vector<std::string>& env) {
    std::vector<char*> a, e;
    for (const auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    for (const auto& s : env) e.push_back(const_cast<char*>(s.c_str()));
    e.push_back(nullptr);
    const pid_t pid = fork();
    if (pid != 0) return pid;
    setsid();
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    const int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, 0), dup2(devnull, 1), dup2(devnull, 2);
    }
    for (int fd = 3; fd < 4096; ++fd) close(fd);
    execve(a[0], a.data(), e.data());
    _exit(127);
}

// True: exited 0, or still running at the deadline (a browser starting). False: failed within it.
bool runOne(const Candidate& c, const std::vector<std::string>& env, int quickMs) {
    const pid_t pid = spawnDetached(c.argv, env);
    if (pid < 0) return false;
    for (int waited = 0;; waited += 10) {
        int status = 0;
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (r < 0 && errno != EINTR) return true; // reaped elsewhere
        if (waited >= quickMs) break;
        usleep(10 * 1000);
    }
    std::thread([pid] { // the browser outlives the opener's verdict: reap it when it ends
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }).detach();
    return true;
}

} // namespace

bool launchWith(const std::string& url, const std::vector<std::string>& env, std::string* via, int quickMs) {
    for (const auto& c : candidates(url, env)) {
        if (runOne(c, env, quickMs)) {
            if (via) *via = c.label;
            return true;
        }
    }
    return false;
}

bool launch(const std::string& url) {
    const auto env = cleanEnvironment(startupEnvironment());
    if (candidates(url, env).empty()) return false;
    std::thread([url, env] { launchWith(url, env); }).detach();
    return true;
}

#else // not Linux: NativeDialog.cpp keeps SDL_OpenURL

bool launchWith(const std::string&, const std::vector<std::string>&, std::string*, int) { return false; }
bool launch(const std::string&) { return false; }

#endif

} // namespace sf::opener
