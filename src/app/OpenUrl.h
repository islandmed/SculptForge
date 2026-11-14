// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Handing a URL or a folder to the desktop on Linux (NativeDialog.cpp's openUrl). SDL_OpenURL runs xdg-open in
// whatever environment the app has by then: an AppImage's APPDIR and library paths, and the temporary XDG folders a
// test run sets (main.cpp isolateSettings), reach the browser, which then starts with a fresh profile or bundled
// libraries and crashes (owner's report on Omarchy: Support opened a pile of crashing Chromium windows).
// So the opener starts in the environment the app was started with, cleaned of the AppImage's own entries, detached.
// The pieces are plain functions so tests/test_open_url.cpp runs them with a shim xdg-open instead of a browser.
#pragma once

#include <string>
#include <vector>

namespace sf::opener {

// "NAME=value" strings as environ holds them, captured before main() (so before any setenv).
const std::vector<std::string>& startupEnvironment();

// ENV without the AppImage's entries: APPDIR/APPIMAGE/ARGV0/OWD, and from every path list the entries under APPDIR
// (or any AppImage mount, /tmp/.mount_*), so LD_LIBRARY_PATH, LD_PRELOAD, GTK_PATH, XDG_DATA_DIRS... keep only what
// the user had. APPDIR empty: taken from ENV's APPDIR.
std::vector<std::string> cleanEnvironment(const std::vector<std::string>& env, std::string appDir = {});

// Starts xdg-open, then gio open, then each $BROWSER entry (first one found in ENV's PATH) on URL: detached (own
// session, no inherited descriptors, stdio to /dev/null), in ENV. An opener that fails within `quickMs` (nothing
// to open it with) passes to the next; one still running then is the browser starting, and counts. `via` names the
// opener used. Waits for the verdict (up to quickMs): tests call it, the app uses launch().
bool launchWith(const std::string& url, const std::vector<std::string>& env, std::string* via = nullptr, int quickMs = 400);

// The app's call: false at once when nothing to open it with is on PATH, else the chain runs on a thread. Cleaned
// startup environment.
bool launch(const std::string& url);

// The same URL at most once per `windowSec`: a double click, a key repeat or two handlers for one button.
class Debounce {
public:
    bool allow(const std::string& url, double nowSec, double windowSec = 2.0); // false: a repeat, drop it
private:
    std::vector<std::pair<std::string, double>> recent_;
};

} // namespace sf::opener
