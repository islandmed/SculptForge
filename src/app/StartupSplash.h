// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The start-up splash (idea by @lowenski, #176): our brand bust and wordmark on the dark brand background, shown only while start-up
// takes longer than kShowAfterMs and gone when the main window has drawn its first frame (but not before it has been up for
// kMinVisibleMs: a splash that flashes for a frame is worse than none). Start-up is checked between its steps and once more
// before the first frame; a start that is over within kShowAfterMs never shows it (on a fast laptop that is every start). This file is the part with no
// window system in it (the decisions, the settings file, the picture); StartupSplashWindow.cpp opens the window.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf {
struct Image;
}

namespace sf::splash {

constexpr double kShowAfterMs = 300.0; // quicker starts never show it
constexpr double kMinVisibleMs = 700.0; // once shown, it stays at least this long (never added to a start that didn't show it)
constexpr int kLogicalWidth = 640, kLogicalHeight = 360; // 16:9, like the invite splash
constexpr uint8_t kBackground[3] = {0x0f, 0x0f, 0x12}; // the site's --bg

// What decides whether this run may have a splash at all.
struct Setting {
    bool interactive = false;  // a person's window: not tests, renders, recordings, benchmarks, the CLI
    bool preference = true;    // Preferences > Interface > Show startup splash
    std::string_view videoDriver; // SDL's driver: "offscreen" and "dummy" draw nowhere
};

// True: the main window starts hidden (no flash of an empty window) and the splash may show. False: as before.
bool enabled(const Setting& s);

// A checkpoint of start-up: open the splash now? (once; only when enabled, and start-up is past showAfterMs)
bool due(bool enabled, bool alreadyShown, double elapsedMs, double showAfterMs = kShowAfterMs);

// How much longer the open splash stays before the main window may replace it (0: it has been up long enough).
double holdMs(double visibleMs);

// SF_SPLASH=1: show the splash on this run whatever the start-up time or the preference (it stays kMinVisibleMs), to see it.
bool forcedFromEnv(const char* value);

// ui.json's "prefs": "startupSplash" (true when absent or unreadable) and "language" ("en" when absent).
bool preferenceFromUiJson(const std::string& text);
std::string languageFromUiJson(const std::string& text);

// Where the splash's parts go in a width x height picture (pixels): the brand bust (a rounded tile) at the right, the
// wordmark at the left, the version line under it, left-aligned with it. Nothing else.
struct Layout {
    int bustX, bustY, bustSize;
    int wordX, wordY, wordW, wordH;
    int versionX, versionY; // the left edge and the vertical centre of the version line's text
};
Layout layout(int width, int height, double wordmarkAspect);

// The splash picture, width x height RGBA: the background, the bust, the wordmark (PNGs with transparency).
void compose(const Image& wordmark, const Image& bust, int width, int height, std::vector<uint8_t>& rgba, Layout& where);

} // namespace sf::splash
