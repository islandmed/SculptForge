// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The update check's feed: website/latest.json names the newest release, where to get it and what's in it. The
// app downloads it at most once a day (app/PanelProject.cpp); nothing about the user is sent.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

inline constexpr const char* kUpdateFeedUrl = "https://sculptforge.app/latest.json";
// The same file on the website's GitHub copy, for networks that block sculptforge.app (new domains often are).
inline constexpr const char* kUpdateFeedMirrorUrl = "https://islandmed.github.io/latest.json";
inline constexpr int64_t kUpdateCheckInterval = 24 * 60 * 60; // seconds

struct UpdateInfo {
    std::string version;                 // "0.6.0"
    std::string downloadUrl;             // the download page
    std::string notesUrl;                // what's new
    std::vector<std::string> highlights; // a few short lines for the banner
};

// Reads the feed; false (with `error`) when it isn't one: the version and an https download link are required.
bool parseUpdateFeed(const std::string& json, UpdateInfo& out, std::string& error);

// Compares dotted version numbers ("0.10.0" > "0.9.2"; a missing part counts as 0, text after the numbers such as
// "-rc1" is ignored): < 0, 0 or > 0.
int compareVersions(const std::string& a, const std::string& b);

// The check runs when the last one is a day old (or in the future: the clock was changed).
inline bool updateCheckDue(int64_t lastCheck, int64_t now) {
    return lastCheck <= 0 || now < lastCheck || now - lastCheck >= kUpdateCheckInterval;
}

} // namespace sf
