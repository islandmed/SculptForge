// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supporting SculptForge (memberships on the website; the app stays free and behaves the same for everyone). The
// app only ever opens the fixed links below in the browser (the membership page, its Stripe checkout pages and the
// community Discord invite): no payment code, no tracking, no licence checks. "I'm already a member" uses the member
// builds' GitHub sign-in (core/MemberBuilds.h), nothing of its own. Pure helpers, unit tested: the supporters list
// compiled in from docs/supporters.txt and the Founding Members from website/founding.json (plain text in Help >
// About), and when the membership cards may show.
#pragma once

#include "core/Json.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

inline constexpr const char* kSupportUrl = "https://sculptforge.app/support.html";
// The official community Discord (a permanent invite; the same link as on the website and in the README).
inline constexpr const char* kDiscordUrl = "https://discord.gg/gWrYkEmQhH";
// Two memberships: Member ($5 / month or $48 / year) and Studio ($150 / month, up to 10 seats). The same Stripe
// checkout links as website/support.html and studios.html (tests/test_support.cpp checks they match). An empty yearly
// link hides the yearly choice in the app and on the site.
inline constexpr const char* kSupportUrlMember = "https://buy.stripe.com/aFacN5h1t6L8cP14ad4F201";   // $5 / month
inline constexpr const char* kSupportUrlMemberYearly = "https://buy.stripe.com/aFa5kD4eHfhE2an6il4F206"; // $48 / year
inline constexpr const char* kSupportUrlStudio = "https://buy.stripe.com/3cI00j12v5H4g1dgWZ4F204";   // $150 / month
// The promise next to members' early builds. Worded the same in the app, on the membership page and in the guide
// (tests/test_support.cpp checks the membership page).
inline constexpr const char* kFreeReleaseNote =
    "Every release stays free for everyone; bug-fix and security releases go out to everyone the same day.";

inline constexpr size_t kSupporterNameMax = 40;   // characters (code points) per name; longer ones are cut
inline constexpr size_t kSupportersMax = 2000;    // names kept

// One line of docs/supporters.txt as it may be shown: valid UTF-8 only (invalid bytes dropped), no control or
// invisible formatting characters (C0, DEL, C1, zero-width, bidi overrides and isolates, BOM), spaces trimmed,
// at most kSupporterNameMax characters.
inline std::string sanitizeSupporterName(std::string_view line) {
    std::string out;
    size_t chars = 0;
    for (size_t i = 0; i < line.size() && chars < kSupporterNameMax;) {
        const auto b = static_cast<unsigned char>(line[i]);
        uint32_t cp = 0;
        size_t len = 0;
        if (b < 0x80) cp = b, len = 1;
        else if ((b & 0xE0) == 0xC0) cp = b & 0x1F, len = 2;
        else if ((b & 0xF0) == 0xE0) cp = b & 0x0F, len = 3;
        else if ((b & 0xF8) == 0xF0) cp = b & 0x07, len = 4;
        bool ok = len > 0 && i + len <= line.size();
        for (size_t k = 1; ok && k < len; ++k) {
            const auto c = static_cast<unsigned char>(line[i + k]);
            ok = (c & 0xC0) == 0x80;
            cp = (cp << 6) | (c & 0x3F);
        }
        // overlong forms, surrogates and values past U+10FFFF are invalid too
        static constexpr uint32_t kMinForLength[5] = {0, 0, 0x80, 0x800, 0x10000};
        ok = ok && cp >= kMinForLength[len] && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
        if (!ok) {
            ++i; // drop one byte and resynchronise
            continue;
        }
        const bool hidden = cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || (cp >= 0x200B && cp <= 0x200F) ||
                            (cp >= 0x2028 && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x2069) || cp == 0xFEFF;
        if (!hidden) {
            out.append(line.substr(i, len));
            ++chars;
        }
        i += len;
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(' ') - first + 1);
}

// docs/supporters.txt: one name per line; empty lines and lines starting with '#' are skipped. At most
// kSupportersMax names, each sanitised (sanitizeSupporterName); names that sanitise to nothing are skipped.
inline std::vector<std::string> parseSupporters(std::string_view text) {
    std::vector<std::string> names;
    size_t start = 0;
    while (start < text.size() && names.size() < kSupportersMax) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        const size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string_view::npos || line[first] == '#') continue;
        std::string name = sanitizeSupporterName(line);
        if (!name.empty()) names.push_back(std::move(name));
    }
    return names;
}

// --- Founding Members (website/founding.json, compiled in; the coordinator updates it by hand) ---
// The first 100 members are Founding Members for good: a badge (the Discord role), their name in the credits (Help >
// About) and on the website's members page, and their price locked for as long as they stay. The card and the
// website stop offering it once `taken` reaches `limit`.
struct FoundingMembers {
    int limit = 100;
    int taken = 0;
    std::vector<std::string> names; // sanitised (sanitizeSupporterName); some Founding Members choose not to be listed
    bool open() const { return taken < limit; }
    int left() const { return std::max(0, limit - taken); }
};

// --- The membership card at good moments (PanelSupportCard.cpp) ---
// A small card along the bottom of the canvas, never a modal, at most once a week, until someone becomes a member.
// It waits for the moment: ~20 minutes of active sculpting in this session (stroke time, not idle time), a Save
// Render, the first start after an update, or the monthly challenge's results (website/latest.json's `challenge`).
// Never mid-stroke or while a background job runs, never in tests, renders, demos or CI, never for a member signed in
// for member builds (or verified once). Its buttons: Become a member, Not now (a week) and I'm already a member,
// which signs in with GitHub (Preferences > Updates > Member builds' sign-in): access to the member repository hides
// it for good and turns member builds on; no sign-in (cancelled, declined, no access) hides it for three months.
// The full card at launch (the memberships side by side) shows on the first start only and from Help > Support.
inline constexpr double kMemberCardSculptSeconds = 20.0 * 60.0; // active stroke time in this session
inline constexpr double kSculptTimeMaxStep = 0.25; // one frame's share at most (a stall or a clock jump adds little)
inline constexpr int64_t kMemberCardIntervalSeconds = 7 * 24 * 3600; // at most once a week (and "Not now")
inline constexpr int64_t kMemberCardNoSignInSeconds = 90 * 24 * 3600; // "I'm already a member" without a sign-in

// Adds one frame's `dt` seconds spent in a stroke to the sculpting time.
inline double addSculptTime(double total, double dt) {
    if (!(total >= 0.0) || total > 1e9) total = 0.0; // (a damaged value)
    if (!(dt > 0.0)) return total;
    return total + std::min(dt, kSculptTimeMaxStep);
}

struct SupportCardPrefs {
    bool supporter = false;      // a member: never again (a verified sign-in; or "I'm already a member" before 0.11.1)
    int64_t shownAt = 0;         // seconds since 1970 when a membership card last showed (0: never)
    int64_t hiddenUntil = 0;     // "Not now" or a sign-in that didn't happen: nothing before this (0: none)
    std::string challengeSeen;   // the last monthly challenge result the card knew of (latest.json's challenge.id)
};

enum class SupportCardReason { None, FirstRun, Manual };

// The full card at launch: the first start after install only (Help > Support and the welcome open it by hand).
inline SupportCardReason supportCardDue(const SupportCardPrefs& p, bool firstRun, bool testMode) {
    if (testMode || p.supporter || !firstRun) return SupportCardReason::None;
    return SupportCardReason::FirstRun;
}

enum class MemberMoment { None, Sculpted, SavedRender, Updated, Challenge };

// Whether a moment may bring the card at all (the weekly limit, the snooze, members): checked when the moment
// happens; a moment that may not is dropped, not saved for later.
inline bool memberMomentAllowed(const SupportCardPrefs& p, bool signedInMember, int64_t now, bool testMode) {
    if (testMode || p.supporter || signedInMember) return false;
    // (a clock set back past a snooze, or a snooze longer than it can be, ends it: a damaged value can't hide it for good)
    if (p.hiddenUntil > now && p.hiddenUntil - now <= kMemberCardNoSignInSeconds) return false;
    const int64_t since = now - p.shownAt;
    return p.shownAt <= 0 || since >= kMemberCardIntervalSeconds || since <= -kMemberCardIntervalSeconds;
}

// A moment allowed, the card waits for a calm frame: no stroke, no background job, no other card or dialog.
inline bool memberCardMayOpen(bool stroking, bool jobRunning, bool otherCardOpen) {
    return !stroking && !jobRunning && !otherCardOpen;
}

// The sculpting moment: this session's stroke time reached kMemberCardSculptSeconds (once per session).
inline bool sculptMomentDue(double sessionSculptSeconds, bool alreadyThisSession) {
    return !alreadyThisSession && sessionSculptSeconds >= kMemberCardSculptSeconds;
}

enum class MemberCardChoice { Become, NotNow, SignedIn, NoSignIn };

// What a choice on the card leaves in the preferences. Become a member opens the checkout and counts as "Not now"
// (the card comes back in a week unless they sign in or say they're a member).
inline void applyMemberCardChoice(SupportCardPrefs& p, MemberCardChoice c, int64_t now) {
    switch (c) {
    case MemberCardChoice::Become:
    case MemberCardChoice::NotNow: p.hiddenUntil = now + kMemberCardIntervalSeconds; break;
    case MemberCardChoice::SignedIn: p.supporter = true; break;
    case MemberCardChoice::NoSignIn: p.hiddenUntil = now + kMemberCardNoSignInSeconds; break;
    }
}

// The compiled-in website/founding.json (PanelSupport.cpp).
const FoundingMembers& foundingMembers();

// website/founding.json: {"limit": 100, "taken": 37, "names": ["...", ...]}. Anything missing or odd keeps the
// defaults (100, 0, none); taken is clamped to 0..limit, names sanitised and capped like the supporters list.
inline FoundingMembers parseFoundingMembers(const std::string& json) {
    FoundingMembers f;
    Json j;
    std::string error;
    if (!Json::parse(json, j, error) || !j.isObject()) return f;
    const double limit = j["limit"].asNumber(100.0), taken = j["taken"].asNumber(0.0);
    if (limit >= 1.0 && limit <= 100000.0) f.limit = int(limit);
    if (taken >= 0.0) f.taken = int(std::min(taken, double(f.limit)));
    if (j["names"].isArray())
        for (const Json& n : j["names"].items()) {
            if (f.names.size() >= kSupportersMax) break;
            if (!n.isString()) continue;
            std::string name = sanitizeSupporterName(n.asString());
            if (!name.empty()) f.names.push_back(std::move(name));
        }
    return f;
}

} // namespace sf
