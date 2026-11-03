// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supporting SculptForge (memberships on the website; the app stays free and behaves the same for everyone). The
// app only ever opens the fixed links below in the browser (the membership page, its four Stripe checkout pages and the
// community Discord invite): no
// payment code, no network, no tracking, no licence checks. Pure helpers, unit tested: the supporters list compiled
// in from docs/supporters.txt (shown as plain text in Help > About), when the one optional reminder may show, and
// when the support card may show at launch.
#pragma once

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
// The same Stripe checkout links as website/support.html (tests/test_support.cpp checks they match).
inline constexpr const char* kSupportUrlSupporter = "https://buy.stripe.com/aFacN5h1t6L8cP14ad4F201"; // $5 / month
inline constexpr const char* kSupportUrlInsider = "https://buy.stripe.com/dRmdR94eH4D06qD9ux4F202";   // $20 / month
// Studio ($300 / month, the whole company). Founding Studio: the first 10 studios lock in $150 / month for as long as
// they stay subscribed. When the 10 are taken, set kFoundingStudioOpen = false here and FOUNDING_STUDIO_OPEN = false in
// website/studio-offer.js (the one switch on the site); nothing else changes.
inline constexpr bool kFoundingStudioOpen = true;
inline constexpr const char* kSupportUrlStudioFounding = "https://buy.stripe.com/3cI00j12v5H4g1dgWZ4F204"; // $150 / month, locked
inline constexpr const char* kSupportUrlStudioRegular = "https://buy.stripe.com/9B66oH8uXd9w16j2254F205"; // $300 / month
inline constexpr const char* kSupportUrlStudio = kFoundingStudioOpen ? kSupportUrlStudioFounding : kSupportUrlStudioRegular;
inline constexpr const char* kSupportUrlOnce = "https://buy.stripe.com/4gM7sL8uXc5s4iv5eh4F200";      // one-time Supporter (pay what you want, $5 minimum)
// The promise next to Insider preview builds. Worded the same in the app, on the membership page and in the guide
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

// --- The one optional reminder (a dismissible note, never a modal, never at launch) ---
inline constexpr double kSupportReminderSculptSeconds = 10.0 * 3600.0; // cumulative stroke time first
inline constexpr double kSupportReminderSessionSeconds = 10.0 * 60.0;  // and this long into the session
inline constexpr double kSculptTimeMaxStep = 0.25; // one frame's share at most (a stall or a clock jump adds little)

// Adds one frame's `dt` seconds spent in a stroke to the cumulative sculpting time.
inline double addSculptTime(double total, double dt) {
    if (!(total >= 0.0) || total > 1e9) total = 0.0; // (a damaged prefs value)
    if (!(dt > 0.0)) return total;
    return total + std::min(dt, kSculptTimeMaxStep);
}

// Whether the reminder shows now: after kSupportReminderSculptSeconds of sculpting, at least
// kSupportReminderSessionSeconds into this session, between strokes, and only if it was never shown or dismissed.
inline bool supportReminderDue(double sculptSeconds, double sessionSeconds, bool stroking, bool alreadyShown) {
    return !alreadyShown && !stroking && sculptSeconds >= kSupportReminderSculptSeconds &&
           sessionSeconds >= kSupportReminderSessionSeconds;
}

// --- The support card at launch (PanelSupportCard.cpp) ---
// Shown only while the app starts, never mid-session: on the first start after install, on the first start of each
// new version, and otherwise at most once every seven days. "Maybe later" hides it for 30 days, updates included. Never
// once "I'm already a supporter" was chosen or it was turned off in Preferences > Support, and never in tests, renders,
// demos, benchmarks or CI (testMode).
inline constexpr int64_t kSupportCardIntervalSeconds = 7 * 24 * 3600;
inline constexpr int64_t kSupportCardLaterSeconds = 30 * 24 * 3600;

struct SupportCardPrefs {
    bool enabled = true;         // Preferences > Support > Show the support card at launch
    bool supporter = false;      // "I'm already a supporter": never again
    int64_t shownAt = 0;         // seconds since 1970 when it last showed (0: never)
    std::string shownVersion;    // the version it last showed in ("" before 0.10.4)
    int64_t laterAt = 0;         // seconds since 1970 when "Maybe later" was chosen (0: never)
};

enum class SupportCardReason { None, FirstRun, Updated, Weekly };

inline SupportCardReason supportCardDue(const SupportCardPrefs& p, bool firstRun, std::string_view version, int64_t now,
                                        bool testMode) {
    if (testMode || !p.enabled || p.supporter) return SupportCardReason::None;
    if (firstRun) return SupportCardReason::FirstRun;
    // (a clock set back before "Maybe later" ends the 30 days, so a damaged value can't hide it for good)
    if (p.laterAt > 0 && now >= p.laterAt && now - p.laterAt < kSupportCardLaterSeconds) return SupportCardReason::None;
    if (p.shownVersion != version) return SupportCardReason::Updated;
    const int64_t since = now - p.shownAt;
    // (a clock set far back counts as due, so a damaged value can't hide it for good, nor show it every start)
    if (p.shownAt <= 0 || since >= kSupportCardIntervalSeconds || since <= -kSupportCardIntervalSeconds)
        return SupportCardReason::Weekly;
    return SupportCardReason::None;
}

} // namespace sf
