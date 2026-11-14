// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The update check's feed: website/latest.json names the newest release, where to get it and what's in it. The
// app downloads it at most once a day (app/PanelProject.cpp); nothing about the user is sent.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sf {

inline constexpr const char* kUpdateFeedUrl = "https://sculptforge.app/latest.json";
// The same file on the website's GitHub copy, for networks that block sculptforge.app (new domains often are).
inline constexpr const char* kUpdateFeedMirrorUrl = "https://islandmed.github.io/latest.json";
inline constexpr int64_t kUpdateCheckInterval = 24 * 60 * 60; // seconds

// The membership card's honest line (the feed's optional "support" block, app/Support.h): {"funded_this_month":
// ["Cloth brush", ...], "founding": {"taken": 2, "limit": 100}, "member_requests_this_release": 3}. Only real
// numbers the coordinator writes by hand; anything missing or odd stays unset (-1, empty) and the card keeps its
// usual words. Names printable text (48 bytes at most, 5 at most).
struct SupportFeed {
    bool present = false;              // the feed had a "support" object
    std::vector<std::string> funded;   // funded this month
    int foundingTaken = -1, foundingLimit = -1;
    int memberRequests = -1;           // features in this release that members asked for
};

struct UpdateInfo {
    std::string version;                 // "0.6.0"
    std::string downloadUrl;             // the download page
    std::string notesUrl;                // what's new
    std::vector<std::string> highlights; // a few short lines for the banner
    bool member = false;                 // a member build (core/MemberBuilds.h)
    std::map<std::string, std::string> files; // member builds: release file name -> its API URL
    // The monthly challenge whose results are out (the feed's optional "challenge": {"id": "2026-11", "title":
    // "Creatures"}): the membership card's moment, once per id. Empty when missing or odd; id letters, digits and
    // '-' (32 at most), title printable text (48 bytes at most).
    std::string challengeId, challengeTitle;
    SupportFeed support;
};

// Which releases the update check offers (Preferences > Updates): Stable, or Preview, which adds the betas
// (the feed's channels.preview, published by scripts/release.sh X.Y.Z-beta.N).
enum class UpdateChannel { Stable, Preview };

// Reads the feed; false (with `error`) when it isn't one: the version and an https download link are required.
// On Preview the feed's channels.preview entry is offered when it is newer than the stable release; otherwise (and
// when the entry is missing or broken) the top level, which is what Stable reads.
bool parseUpdateFeed(const std::string& json, UpdateInfo& out, std::string& error,
                     UpdateChannel channel = UpdateChannel::Stable);

// Compares dotted version numbers ("0.10.0" > "0.9.2"; a missing part counts as 0): < 0, 0 or > 0. A prerelease
// sorts below its version, and its parts compare like SemVer's: 0.11.0-beta.1 < 0.11.0-beta.2 < 0.11.0-rc.1 < 0.11.0.
// "+build" text is ignored.
int compareVersions(const std::string& a, const std::string& b);

// What people read: "0.11.0" and "beta.1" make "0.11.0 Beta 1" (title bar, About); no prerelease, the version.
std::string versionLabel(const std::string& version, const std::string& prerelease);

// The version as the window title and the top bar show it, for support screenshots: the number, and a tag the top bar
// draws in the accent colour: "0.11.13"; "0.12.0" + "beta.2"; a developer's build "0.11.13" + "dev 1a2b3c4".
struct ShownVersion {
    std::string number, tag;
    std::string text() const { return tag.empty() ? number : number + " " + tag; }
};
ShownVersion shownVersion(const std::string& version, const std::string& prerelease, const std::string& devCommit);

// The check runs when the last one is a day old (or in the future: the clock was changed).
inline bool updateCheckDue(int64_t lastCheck, int64_t now) {
    return lastCheck <= 0 || now < lastCheck || now - lastCheck >= kUpdateCheckInterval;
}

} // namespace sf
