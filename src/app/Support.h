// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supporting SculptForge (memberships on the website; the app stays free and behaves the same for everyone). The
// app only ever opens kSupportUrl in the browser: no payment code, no network, no tracking, no licence checks.
// Pure helpers, unit tested: the supporters list compiled in from docs/supporters.txt (shown as plain text in
// Help > About) and when the one optional reminder may show.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

inline constexpr const char* kSupportUrl = "https://sculptforge.app/donate.html";
// What Insider members get, worded for while early builds aren't running yet (Tony, 2026-11-01).
inline constexpr const char* kInsiderNote =
    "Insiders will get each release about two weeks early; early builds start with upcoming releases.";

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

} // namespace sf
