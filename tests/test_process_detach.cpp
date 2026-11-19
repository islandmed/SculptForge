// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A detached child outlives the app (the updater's relaunch helper: Windows jobs end every child when the app's last job
// handle closes), and a normal child never does (Stop and quitting end render / splat / timelapse children).
// Windows runs it for real on the CI shards; elsewhere it checks the same promise with sh.
#include "TestHarness.h"

#include "ai/Process.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

using namespace sf::ai;
namespace fs = std::filesystem;

namespace {

// Sleeps ~3 s, then writes `marker`: only a child that is still alive by then leaves the file.
bool startMarkerChild(ChildProcess& c, const fs::path& marker) {
    const std::u8string u = marker.u8string();
    const std::string m(u.begin(), u.end());
    std::string err;
#if defined(_WIN32)
    return c.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command",
                                      "Start-Sleep -Seconds 3; Set-Content -LiteralPath '" + m + "' -Value ok"},
                   "", {}, err);
#else
    return c.start("sh", {"-c", "sleep 3; echo ok > '" + m + "'"}, "", {}, err);
#endif
}

bool waitFor(const fs::path& f, double seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(long(seconds * 1000));
    std::error_code ec;
    while (std::chrono::steady_clock::now() < end) {
        if (fs::exists(f, ec)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return fs::exists(f, ec);
}

} // namespace

TEST(process_detached_child_outlives_us_and_a_normal_child_does_not) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / ("sf-detach-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir, ec);
    const fs::path detached = dir / "detached", normal = dir / "normal", stopped = dir / "stopped";
    {
        // The updater's way: started (in the kill-on-close job), detached at once; then the app (this object) goes.
        auto a = std::make_unique<ChildProcess>();
        CHECK(startMarkerChild(*a, detached));
        a->detach();
        a.reset();
        // Never detached: destroying the object (the app quitting) ends it.
        auto c = std::make_unique<ChildProcess>();
        CHECK(startMarkerChild(*c, normal));
        c.reset();
        // Stop: kill() ends it and what it started.
        ChildProcess d;
        CHECK(startMarkerChild(d, stopped));
        d.kill(0.5);
        CHECK(!d.running());
    }
    CHECK(waitFor(detached, 30));
    std::this_thread::sleep_for(std::chrono::seconds(2)); // (the killed ones would have written by now)
    CHECK(!fs::exists(normal, ec));
    CHECK(!fs::exists(stopped, ec));
    fs::remove_all(dir, ec);
}
