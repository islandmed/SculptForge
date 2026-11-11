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
#include "core/UpdateCheck.h"

#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

inline constexpr const char* kSupportUrl = "https://sculptforge.app/support.html";
// The official community Discord (a permanent invite; the same link as on the website and in the README).
inline constexpr const char* kDiscordUrl = "https://discord.gg/gWrYkEmQhH";
// The community places the Welcome dialog's icon row opens (in this order, like the strip in assets/branding/social); the same
// accounts as the website's footer. Opened on a click only; the app makes no request to any of them.
struct SocialLink { const char* name; const char* url; };
inline constexpr SocialLink kSocialLinks[] = {
    {"Discord", kDiscordUrl},
    {"GitHub", "https://github.com/islandmed/SculptForge"},
    {"X", "https://x.com/SculptForgeApp?utm_source=app&utm_medium=welcome"},
    {"Bluesky", "https://bsky.app/profile/sculptforge.bsky.social?utm_source=app&utm_medium=welcome"},
    {"Instagram", "https://www.instagram.com/sculptforge.app?utm_source=app&utm_medium=welcome"},
};
inline constexpr int kSocialCount = int(sizeof(kSocialLinks) / sizeof(kSocialLinks[0]));

// Two memberships: Member ($5 / month or $48 / year) and Studio (a flat $300 / month for the whole studio, any number of
// artists; the first 5 studios pay a flat $150 as Founding Studios). The same Stripe checkout links as website/support.html
// and studios.html (tests/test_support.cpp checks they match). An empty yearly link hides the yearly choice in the
// app and on the site. The card's Studio button opens the studio page (its own build is set up by email first).
inline constexpr const char* kSupportUrlMember = "https://buy.stripe.com/aFacN5h1t6L8cP14ad4F201";   // $5 / month
inline constexpr const char* kSupportUrlMemberYearly = "https://buy.stripe.com/aFa5kD4eHfhE2an6il4F206"; // $48 / year
inline constexpr const char* kSupportUrlStudio = "https://buy.stripe.com/3cI00j12v5H4g1dgWZ4F204";   // Founding Studio, $150 / month
inline constexpr const char* kStudioPageUrl = "https://sculptforge.app/studios.html#membership";
// The membership terms (refunds, cancelling): the page website/support.html and studios.html link to.
inline constexpr const char* kTermsUrl = "https://sculptforge.app/terms.html";
// The fixed links the membership cards may open (App::openSupportLink refuses anything else).
inline bool supportLinkAllowed(std::string_view url) {
    for (const char* u : {kSupportUrlMember, kSupportUrlMemberYearly, kSupportUrlStudio, kStudioPageUrl, kTermsUrl})
        if (u[0] != '\0' && url == u) return true;
    return false;
}
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
// Render, the first start after an update, the monthly challenge's results (website/latest.json's `challenge`), or a
// milestone (the 10th start, 10 hours of sculpting, the 100th save: counted on this computer only, never sent).
// Never mid-stroke or while a background job runs, never in tests, renders, demos or CI, never for a member signed in
// for member builds (or verified once). Its buttons: Become a member, Not now (a week; after the 3rd a month; after
// the 6th only the first start after an update) and I'm already a member, which signs in with GitHub (Preferences >
// Updates > Member builds' sign-in): access to the member repository hides it for good and turns member builds on;
// no sign-in (cancelled, declined, no access) hides it for three months.
// The full card at launch (the memberships side by side) shows on the first start only and from Help > Support.
inline constexpr double kMemberCardSculptSeconds = 20.0 * 60.0; // active stroke time in this session
inline constexpr double kSculptTimeMaxStep = 0.25; // one frame's share at most (a stall or a clock jump adds little)
inline constexpr int64_t kMemberCardIntervalSeconds = 7 * 24 * 3600; // at most once a week (and "Not now")
inline constexpr int64_t kMemberCardNoSignInSeconds = 90 * 24 * 3600; // "I'm already a member" without a sign-in
inline constexpr int64_t kMemberCardMonthlySeconds = 30 * 24 * 3600; // after the 3rd "Not now"
inline constexpr int kMemberCardMonthlyAfter = 3;  // "Not now"s before the card is monthly
inline constexpr int kMemberCardUpdatesOnlyAfter = 6; // "Not now"s before it only comes with an update
// The milestones (once each): the 10th start, 10 hours of sculpting (stroke time, every session), the 100th save.
inline constexpr int kMilestoneStarts = 10;
inline constexpr double kMilestoneSculptSeconds = 10.0 * 3600.0;
inline constexpr int kMilestoneSaves = 100;

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
    int notNow = 0;              // "Not now"s so far (the backoff)
    int shownCount = 0;          // small cards shown (which honest line comes next)
    // The milestones' counts, on this computer only (project.json; nothing is ever sent).
    int starts = 0;              // interactive starts
    double sculptSeconds = 0.0;  // stroke time, every session
    int saves = 0;               // project saves
    unsigned milestonesShown = 0; // bits: 1 starts, 2 sculpting hours, 4 saves
    SupportFeed feed;            // the last update check's "support" block (the card's honest line)
};

enum class SupportCardReason { None, FirstRun, Manual };

// The full card at launch: the first start after install only (Help > Support and the welcome open it by hand).
inline SupportCardReason supportCardDue(const SupportCardPrefs& p, bool firstRun, bool testMode) {
    if (testMode || p.supporter || !firstRun) return SupportCardReason::None;
    return SupportCardReason::FirstRun;
}

enum class MemberMoment { None, Sculpted, SavedRender, Updated, Challenge, Starts, SculptHours, Saves };

// How long between cards: a week; a month after the 3rd "Not now" (and from then on).
inline int64_t memberCardInterval(int notNow) {
    return notNow >= kMemberCardMonthlyAfter ? kMemberCardMonthlySeconds : kMemberCardIntervalSeconds;
}

// Whether a moment may bring the card at all (the weekly or monthly limit, the snooze, members; after the 6th "Not
// now", only an update's): checked when the moment happens; a moment that may not is dropped, not saved for later
// (a milestone stays due: it is checked again at the next start, save or sculpting minute).
inline bool memberMomentAllowed(const SupportCardPrefs& p, bool signedInMember, int64_t now, bool testMode,
                                MemberMoment m = MemberMoment::None) {
    if (testMode || p.supporter || signedInMember) return false;
    if (p.notNow >= kMemberCardUpdatesOnlyAfter && m != MemberMoment::Updated) return false;
    // (a clock set back past a snooze, or a snooze longer than it can be, ends it: a damaged value can't hide it for good)
    if (p.hiddenUntil > now && p.hiddenUntil - now <= kMemberCardNoSignInSeconds) return false;
    const int64_t since = now - p.shownAt, interval = memberCardInterval(p.notNow);
    return p.shownAt <= 0 || since >= interval || since <= -interval;
}

inline unsigned milestoneBit(MemberMoment m) {
    return m == MemberMoment::Starts ? 1u : m == MemberMoment::SculptHours ? 2u : m == MemberMoment::Saves ? 4u : 0u;
}

// The milestone reached and not yet shown (starts first, then sculpting hours, then saves); None for none.
inline MemberMoment milestoneDue(const SupportCardPrefs& p) {
    const auto due = [&](MemberMoment m, bool reached) { return reached && !(p.milestonesShown & milestoneBit(m)); };
    if (due(MemberMoment::Starts, p.starts >= kMilestoneStarts)) return MemberMoment::Starts;
    if (due(MemberMoment::SculptHours, p.sculptSeconds >= kMilestoneSculptSeconds)) return MemberMoment::SculptHours;
    if (due(MemberMoment::Saves, p.saves >= kMilestoneSaves)) return MemberMoment::Saves;
    return MemberMoment::None;
}

// One line of the card, translatable: `format` (an English tr() key with at most one %d / %s pair) and its values.
struct CardLine {
    const char* format = nullptr; // nullptr: no line
    int a = 0, b = 0;
    std::string text;             // the %s value (a list of names)
    bool textFirst = false;       // the format takes %s before the numbers
};

// `format` is line.format or its translation (same placeholders, same order).
inline std::string formatCardLine(const CardLine& line, const char* format) {
    if (!line.format || !format) return {};
    char buf[512];
    if (line.textFirst) std::snprintf(buf, sizeof buf, format, line.text.c_str());
    else std::snprintf(buf, sizeof buf, format, line.a, line.b);
    return buf;
}

// The milestone's words, with the true number at the time it shows (the hours whole, rounded down).
inline CardLine milestoneLine(MemberMoment m, const SupportCardPrefs& p) {
    CardLine l;
    switch (m) {
    case MemberMoment::Starts:
        l.format = "You've opened SculptForge %d times. Members keep it free and growing.";
        l.a = p.starts;
        break;
    case MemberMoment::SculptHours:
        l.format = "You've sculpted %d hours in SculptForge. Members keep it free and growing.";
        l.a = int(std::min(p.sculptSeconds / 3600.0, 1e6));
        break;
    case MemberMoment::Saves:
        l.format = "You've saved your work %d times in SculptForge. Members keep it free and growing.";
        l.a = p.saves;
        break;
    default: break;
    }
    return l;
}

// The honest lines the feed's "support" block allows (none: the card keeps its usual Founding Members line). Only
// what the numbers say: no countdowns, no "only N left", nothing that isn't in the feed.
inline std::vector<CardLine> supportFeedLines(const SupportFeed& f) {
    std::vector<CardLine> out;
    if (!f.funded.empty()) {
        CardLine l;
        l.format = "Members funded this month: %s.";
        l.textFirst = true;
        for (size_t i = 0; i < f.funded.size(); ++i) {
            const std::string name = sanitizeSupporterName(f.funded[i]);
            if (name.empty()) continue;
            if (!l.text.empty()) l.text += ", ";
            l.text += name;
        }
        if (!l.text.empty()) out.push_back(std::move(l));
    }
    if (f.foundingLimit >= 1 && f.foundingTaken >= 0 && f.foundingTaken < f.foundingLimit) {
        CardLine l;
        l.format = "Founding Members so far: %d of %d. A badge, your name in the credits and your price locked for good.";
        l.a = f.foundingTaken, l.b = f.foundingLimit;
        out.push_back(std::move(l));
    }
    if (f.memberRequests > 0) {
        CardLine l;
        l.format = f.memberRequests == 1 ? "%d change in this release was a member's request."
                                         : "%d changes in this release were members' requests.";
        l.a = f.memberRequests;
        out.push_back(std::move(l));
    }
    return out;
}

// The line for the `shown`th card: the feed's lines in turn; no line (format nullptr) without them.
inline CardLine supportFeedLine(const SupportFeed& f, int shown) {
    const std::vector<CardLine> lines = supportFeedLines(f);
    if (lines.empty()) return {};
    return lines[size_t(std::max(0, shown)) % lines.size()];
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

// What a choice on the card leaves in the preferences. Become a member opens the checkout and snoozes like "Not now"
// without counting as one (the card comes back in a week unless they sign in or say they're a member).
inline void applyMemberCardChoice(SupportCardPrefs& p, MemberCardChoice c, int64_t now) {
    switch (c) {
    case MemberCardChoice::Become: p.hiddenUntil = now + memberCardInterval(p.notNow); break;
    case MemberCardChoice::NotNow:
        p.notNow = std::min(p.notNow + 1, 1000);
        p.hiddenUntil = now + memberCardInterval(p.notNow);
        break;
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

// assets/featured/featured.json: {"videos": [{"title", "creator", "url", "thumb"}]}, the creator videos the Welcome dialog
// features (bundled with the app, nothing fetched; an empty list hides the section). At most kFeaturedMax; an entry without
// a title, an https url or a thumbnail file name is dropped; title and creator are sanitised like the supporters.
inline constexpr size_t kFeaturedMax = 3;
struct FeaturedVideo {
    std::string title, creator, url, thumb;
};
inline std::vector<FeaturedVideo> parseFeaturedVideos(const std::string& json) {
    std::vector<FeaturedVideo> out;
    Json j;
    std::string error;
    if (!Json::parse(json, j, error) || !j.isObject() || !j["videos"].isArray()) return out;
    for (const Json& v : j["videos"].items()) {
        if (out.size() >= kFeaturedMax) break;
        if (!v.isObject() || !v["title"].isString() || !v["url"].isString() || !v["thumb"].isString()) continue;
        FeaturedVideo f{sanitizeSupporterName(v["title"].asString()),
                        v["creator"].isString() ? sanitizeSupporterName(v["creator"].asString()) : std::string(),
                        v["url"].asString(), v["thumb"].asString()};
        const bool plainName = !f.thumb.empty() && f.thumb.find_first_of("/\\:") == std::string::npos && f.thumb[0] != '.';
        if (f.title.empty() || f.url.rfind("https://", 0) != 0 || f.url.size() > 300 || !plainName) continue;
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace sf
