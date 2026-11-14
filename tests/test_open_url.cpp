// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// app/OpenUrl: the browser is started in the environment the app was started with (owner's report, Omarchy: Support
// opened a pile of crashing Chromium windows, because the opener inherited the AppImage's and the test run's
// environment). Never a real browser here: a shim xdg-open / gio on PATH logs its argv and environment.
#include "TestHarness.h"

#include "app/OpenUrl.h"

#include <fstream>
#include <sstream>

#if defined(__linux__) && !defined(__ANDROID__)
#include <unistd.h>
#endif

using namespace sf;

namespace {

bool has(const std::vector<std::string>& env, const std::string& entry) {
    for (const auto& e : env)
        if (e == entry) return true;
    return false;
}
bool hasName(const std::vector<std::string>& env, const std::string& name) {
    for (const auto& e : env)
        if (e.compare(0, name.size() + 1, name + "=") == 0) return true;
    return false;
}

} // namespace

TEST(open_url_environment_drops_appimage_entries) {
    const std::vector<std::string> env = {"HOME=/home/u",
                                          "APPDIR=/tmp/.mount_SculptXYZ",
                                          "APPIMAGE=/home/u/SculptForge.AppImage",
                                          "ARGV0=./SculptForge.AppImage",
                                          "OWD=/home/u",
                                          "LD_LIBRARY_PATH=/tmp/.mount_SculptXYZ/usr/lib:/opt/mine/lib",
                                          "LD_PRELOAD=/tmp/.mount_SculptXYZ/usr/lib/libx.so",
                                          "GTK_PATH=/tmp/.mount_SculptXYZ/usr/lib/gtk-3.0",
                                          "XDG_DATA_DIRS=/tmp/.mount_SculptXYZ/usr/share:/usr/share",
                                          "XDG_CONFIG_HOME=/home/u/.config",
                                          "PATH=/tmp/.mount_SculptXYZ/usr/bin:/usr/bin:/bin"};
    const auto out = opener::cleanEnvironment(env);
    for (const char* gone : {"APPDIR", "APPIMAGE", "ARGV0", "OWD", "LD_PRELOAD", "GTK_PATH"}) CHECK(!hasName(out, gone));
    CHECK(has(out, "LD_LIBRARY_PATH=/opt/mine/lib")); // what the user had stays
    CHECK(has(out, "XDG_DATA_DIRS=/usr/share"));
    CHECK(has(out, "PATH=/usr/bin:/bin"));
    CHECK(has(out, "HOME=/home/u"));
    CHECK(has(out, "XDG_CONFIG_HOME=/home/u/.config"));
    // A plain run (no APPDIR) is left exactly alone.
    const std::vector<std::string> plain = {"HOME=/home/u", "LD_LIBRARY_PATH=/opt/mine/lib:", "PATH=/usr/bin"};
    CHECK(opener::cleanEnvironment(plain) == plain);
    // An explicit APPDIR (a custom mount) works the same.
    CHECK(has(opener::cleanEnvironment({"LD_LIBRARY_PATH=/mnt/app/usr/lib"}, "/mnt/app/"), "LD_LIBRARY_PATH=") == false);
}

TEST(open_url_debounce) {
    opener::Debounce d;
    CHECK(d.allow("https://a", 10.0));
    CHECK(!d.allow("https://a", 10.5)); // the same page again within 2 s
    CHECK(d.allow("https://b", 10.5));  // another page is fine
    CHECK(!d.allow("https://a", 11.9));
    CHECK(d.allow("https://a", 12.1)); // a later click opens again
}

#if defined(__linux__) && !defined(__ANDROID__)
namespace {

struct Shims {
    std::filesystem::path dir;
    explicit Shims(const char* tag) {
        dir = std::filesystem::path(sf::test::tempPath("sf-open-url-")) += tag;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }
    ~Shims() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    // A script named NAME that logs argv, its stat line (session) and environment to LOG-NAME, then exits CODE.
    void shim(const std::string& name, int code) const {
        const auto path = dir / name;
        std::ofstream(path) << "#!/bin/sh\n{ echo \"argv: $0 $*\"; read -r stat < /proc/$$/stat; echo \"stat: $stat\"; "
                               "ls -l /proc/$$/fd; env; } > '"
                            << (dir / ("log-" + name)).string() << "' 2>&1\nexit " << code << "\n";
        std::filesystem::permissions(path, std::filesystem::perms::owner_all);
    }
    std::string log(const std::string& name) const {
        std::ifstream f(dir / ("log-" + name));
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
    std::vector<std::string> env(std::vector<std::string> extra = {}) const {
        std::vector<std::string> e = {"PATH=" + dir.string() + ":/usr/bin:/bin", "HOME=/home/u", "XDG_CONFIG_HOME=/home/u/.config"};
        for (auto& x : extra) e.push_back(std::move(x));
        return e;
    }
};

} // namespace

TEST(open_url_runs_xdg_open_once_with_the_clean_environment) {
    Shims s("a");
    s.shim("xdg-open", 0);
    s.shim("gio", 0);
    // What the app has at that point: the AppImage's entries and a test run's temporary XDG folder.
    const auto started = s.env({"APPDIR=/tmp/.mount_SculptAB", "LD_LIBRARY_PATH=/tmp/.mount_SculptAB/usr/lib"});
    auto env = started;
    env.push_back("SF_AI_LIVE_DIR=/tmp/x");
    std::string via;
    int leak[2];
    CHECK(pipe(leak) == 0); // open and inheritable in the app: must not reach the opener
    const bool ok = opener::launchWith("https://sculptforge.app/support.html", opener::cleanEnvironment(env), &via);
    close(leak[0]), close(leak[1]);
    CHECK(ok);
    CHECK(via == "xdg-open");
    const std::string log = s.log("xdg-open");
    CHECK(log.find("argv: " + (s.dir / "xdg-open").string() + " https://sculptforge.app/support.html\n") != std::string::npos);
    CHECK(log.find("\nXDG_CONFIG_HOME=/home/u/.config\n") != std::string::npos);
    CHECK(log.find("APPDIR") == std::string::npos);
    CHECK(log.find("LD_LIBRARY_PATH") == std::string::npos);
    CHECK(log.find("/tmp/.mount_") == std::string::npos);
    CHECK(s.log("gio").empty()); // one call, no second opener

    // Detached: its own session (the session id is the shell's pid), nothing but stdio open.
    const size_t at = log.find("stat: ");
    CHECK(at != std::string::npos);
    std::istringstream stat(log.substr(at + 6, log.find('\n', at) - at - 6));
    std::string pid, name;
    stat >> pid >> name;
    while (!name.empty() && name.back() != ')') {
        std::string more;
        if (!(stat >> more)) break;
        name += more;
    }
    std::string state, ppid, pgrp, session;
    stat >> state >> ppid >> pgrp >> session;
    CHECK(!pid.empty() && session == pid);
    CHECK(session != std::to_string(getsid(0)));
    CHECK(log.find("/dev/null") != std::string::npos); // stdio went to /dev/null
    CHECK(log.find("pipe:") == std::string::npos);      // the descriptor the app had open did not come along
}

TEST(open_url_falls_back_to_gio_then_browser_when_the_opener_fails) {
    Shims s("b");
    s.shim("xdg-open", 3); // no tool to open it with
    s.shim("gio", 4);
    s.shim("mybrowser", 0);
    std::string via;
    CHECK(opener::launchWith("https://x.test/", s.env({"BROWSER=nothing-here:mybrowser --new-tab %s"}), &via));
    CHECK(via == "$BROWSER mybrowser");
    CHECK(!s.log("xdg-open").empty() && !s.log("gio").empty());
    CHECK(s.log("gio").find("argv: " + (s.dir / "gio").string() + " open https://x.test/\n") != std::string::npos);
    CHECK(s.log("mybrowser").find("mybrowser --new-tab https://x.test/\n") != std::string::npos);

    Shims none("c"); // nothing at all: false, not a crash
    CHECK(!opener::launchWith("https://x.test/", {"PATH=" + none.dir.string()}));
}
#endif
