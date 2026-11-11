// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supporting SculptForge: the supporters list (docs/supporters.txt) and the Founding Members (website/founding.json)
// parsed and sanitised for Help > About, and when the membership cards may show.
#include "TestHarness.h"

#include "app/Support.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace sf;

TEST(supporters_parse_skips_comments_and_blanks) {
    CHECK(parseSupporters("").empty());
    CHECK(parseSupporters("# one name per line\n\n   \n#Not a name\n").empty());
    const auto names = parseSupporters("# header\nAda Lovelace\r\n  Grace Hopper  \n\nLinus\n");
    CHECK(names.size() == 3);
    CHECK(names[0] == "Ada Lovelace" && names[1] == "Grace Hopper" && names[2] == "Linus");
    CHECK(std::string(kSupportUrl) == "https://sculptforge.app/support.html");
    CHECK(std::string(kDiscordUrl) == "https://discord.gg/gWrYkEmQhH");
}

TEST(supporters_names_are_sanitised) {
    // Control characters, escapes and invisible formatting go; the rest stays as plain text.
    CHECK(sanitizeSupporterName("Bob\x1b[31m\x07") == "Bob[31m");
    CHECK(sanitizeSupporterName("A\tB\x7f" "C") == "ABC");
    CHECK(sanitizeSupporterName("\xe2\x80\xae" "evil\xe2\x80\xac") == "evil");        // bidi override (U+202E, U+202C)
    CHECK(sanitizeSupporterName("zero\xe2\x80\x8bwidth\xef\xbb\xbf") == "zerowidth"); // U+200B, BOM
    CHECK(sanitizeSupporterName("\xc2\x85" "next") == "next");                         // C1 control (U+0085)
    CHECK(sanitizeSupporterName("Ren\xc3\xa9 \xe5\xb1\xb1\xe7\x94\xb0") == "Ren\xc3\xa9 \xe5\xb1\xb1\xe7\x94\xb0"); // UTF-8 kept
    CHECK(sanitizeSupporterName("bad\xff\xc0\xafutf8\xed\xa0\x80") == "badutf8");     // invalid, overlong, surrogate
    CHECK(sanitizeSupporterName("%s%n{{markup}}") == "%s%n{{markup}}");               // shown as text, not interpreted
    // 40 characters at most, counted in characters, never cutting one in half.
    CHECK(sanitizeSupporterName(std::string(100, 'x')).size() == kSupporterNameMax);
    std::string accents;
    for (int i = 0; i < 50; ++i) accents += "\xc3\xa9";
    CHECK(sanitizeSupporterName(accents).size() == kSupporterNameMax * 2);
    CHECK(sanitizeSupporterName(" \x01 \x02 ").empty());
}

TEST(supporters_count_is_capped) {
    std::string text;
    for (int i = 0; i < 2500; ++i) text += "Name " + std::to_string(i) + "\n";
    const auto names = parseSupporters(text);
    CHECK(names.size() == kSupportersMax);
    CHECK(names.back() == "Name 1999");
}

TEST(member_card_sculpting_moment) {
    // Stroke time grows by frames spent in strokes, a stalled frame counting at most a quarter second.
    double t = 0.0;
    t = addSculptTime(t, 0.016);
    CHECK(t > 0.015 && t < 0.017);
    CHECK(addSculptTime(t, 3600.0) - t <= kSculptTimeMaxStep + 1e-9);
    CHECK(addSculptTime(t, -5.0) == t);
    CHECK(addSculptTime(-1.0, 0.1) <= 0.1 + 1e-9); // a damaged value starts again
    // About 20 minutes of active sculpting in this session (the maintainer, 11-05: an hour was too long), once per session.
    CHECK(kMemberCardSculptSeconds == 20.0 * 60.0);
    CHECK(!sculptMomentDue(kMemberCardSculptSeconds - 1.0, false));
    CHECK(sculptMomentDue(kMemberCardSculptSeconds, false));
    CHECK(!sculptMomentDue(kMemberCardSculptSeconds * 3.0, true));
}

TEST(member_card_weekly_until_member) {
    const int64_t day = 24 * 3600, now = 1'800'000'000;
    SupportCardPrefs p;
    CHECK(memberMomentAllowed(p, false, now, false)); // never shown: yes
    p.shownAt = now;
    CHECK(!memberMomentAllowed(p, false, now + 6 * day, false)); // at most once a week
    CHECK(memberMomentAllowed(p, false, now + 7 * day, false));
    CHECK(memberMomentAllowed(p, false, now + 70 * day, false)); // and weekly again, for as long as it takes
    CHECK(memberMomentAllowed(p, false, now - 30 * day, false)); // a clock set back can't hide it for good
    CHECK(!memberMomentAllowed(p, false, now - day, false));
    // Never in tests, renders or CI; never for a member signed in for member builds.
    CHECK(!memberMomentAllowed(p, false, now + 30 * day, true));
    CHECK(!memberMomentAllowed(p, true, now + 30 * day, false));
    // Never mid-stroke, during a background job or over another card: it waits.
    CHECK(memberCardMayOpen(false, false, false));
    CHECK(!memberCardMayOpen(true, false, false));
    CHECK(!memberCardMayOpen(false, true, false));
    CHECK(!memberCardMayOpen(false, false, true));
}

TEST(member_card_choices) {
    const int64_t day = 24 * 3600, now = 1'800'000'000;
    // Not now: a week.
    SupportCardPrefs p;
    p.shownAt = now;
    applyMemberCardChoice(p, MemberCardChoice::NotNow, now);
    CHECK(!memberMomentAllowed(p, false, now + 6 * day, false));
    CHECK(memberMomentAllowed(p, false, now + 7 * day, false));
    CHECK(!p.supporter);
    // Become a member opens the checkout; the card comes back in a week unless they sign in.
    SupportCardPrefs b;
    applyMemberCardChoice(b, MemberCardChoice::Become, now);
    CHECK(!memberMomentAllowed(b, false, now + 6 * day, false));
    CHECK(memberMomentAllowed(b, false, now + 7 * day, false));
    // I'm already a member, signed in (GitHub, access to the member repository): gone for good.
    SupportCardPrefs ok;
    applyMemberCardChoice(ok, MemberCardChoice::SignedIn, now);
    CHECK(ok.supporter);
    for (int64_t d : {7, 30, 365, 3650}) CHECK(!memberMomentAllowed(ok, false, now + d * day, false));
    CHECK(supportCardDue(ok, true, false) == SupportCardReason::None);
    // I'm already a member, no sign-in (cancelled, declined, no access): three months, then weekly again.
    SupportCardPrefs no;
    no.shownAt = now;
    applyMemberCardChoice(no, MemberCardChoice::NoSignIn, now);
    CHECK(!no.supporter);
    CHECK(!memberMomentAllowed(no, false, now + 7 * day, false));
    CHECK(!memberMomentAllowed(no, false, now + 89 * day, false));
    CHECK(memberMomentAllowed(no, false, now + 90 * day, false));
    no.shownAt = now + 90 * day;
    CHECK(!memberMomentAllowed(no, false, now + 96 * day, false));
    CHECK(memberMomentAllowed(no, false, now + 97 * day, false));
    // A snooze further off than any choice makes (a damaged value, a clock set back) doesn't hide it.
    SupportCardPrefs bad;
    bad.hiddenUntil = now + 400 * day;
    CHECK(memberMomentAllowed(bad, false, now, false));
}

TEST(founding_members_json) {
    const FoundingMembers none = parseFoundingMembers("");
    CHECK(none.limit == 100 && none.taken == 0 && none.open() && none.left() == 100 && none.names.empty());
    const FoundingMembers f = parseFoundingMembers(R"({"limit": 100, "taken": 37, "names": ["Ada", "\u202eevil", 5, ""]})");
    CHECK(f.taken == 37 && f.left() == 63 && f.open());
    CHECK(f.names.size() == 2 && f.names[0] == "Ada" && f.names[1] == "evil"); // sanitised, non-strings and blanks skipped
    const FoundingMembers full = parseFoundingMembers(R"({"limit": 100, "taken": 140})");
    CHECK(full.taken == 100 && !full.open() && full.left() == 0); // the founding note switches off at 100
    CHECK(parseFoundingMembers(R"({"limit": -3, "taken": -1})").limit == 100);
    CHECK(parseFoundingMembers(R"({"limit": -3, "taken": -1})").taken == 0);
    // The repository's file (compiled into the app, read by the website) parses as written.
    std::ifstream in(std::string(SF_TEST_DATA_DIR) + "/../../website/founding.json", std::ios::binary);
    CHECK(in.good());
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    Json j;
    std::string error;
    CHECK(Json::parse(text, j, error) && j["limit"].isNumber() && j["taken"].isNumber() && j["names"].isArray());
}

TEST(featured_videos_parse_and_drop_the_odd) {
    CHECK(parseFeaturedVideos("").empty());
    CHECK(parseFeaturedVideos("{\"videos\": []}").empty()); // the bundled list today: the Welcome shows no section
    const std::string ok = "{\"title\": \"Honest review\", \"creator\": \"Ada\", \"url\": \"https://example.org/v\", \"thumb\": \"ada.jpg\"}";
    auto one = [](const std::string& title, const std::string& url, const std::string& thumb) {
        return "{\"videos\": [{\"title\": \"" + title + "\", \"creator\": \"Ada\", \"url\": \"" + url + "\", \"thumb\": \"" + thumb + "\"}]}";
    };
    const auto v = parseFeaturedVideos("{\"videos\": [" + ok + "]}");
    CHECK(v.size() == 1 && v[0].title == "Honest review" && v[0].creator == "Ada" && v[0].thumb == "ada.jpg");
    CHECK(parseFeaturedVideos(one("T", "http://example.org/v", "a.jpg")).empty());      // https only
    CHECK(parseFeaturedVideos(one("T", "javascript:alert(1)", "a.jpg")).empty());
    CHECK(parseFeaturedVideos(one("T", "https://example.org/v", "../a.jpg")).empty()); // a file name, not a path
    CHECK(parseFeaturedVideos(one("T", "https://example.org/v", "")).empty());
    CHECK(parseFeaturedVideos(one("\\u202e", "https://example.org/v", "a.jpg")).empty()); // a title that sanitises to nothing
    std::string many = "{\"videos\": [";
    for (int i = 0; i < 6; ++i) many += (i ? "," : "") + ok;
    CHECK(parseFeaturedVideos(many + "]}").size() == kFeaturedMax);
    // The bundled file in the repository parses as written.
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../assets/featured/featured.json", std::ios::binary);
    CHECK(f.good());
    Json j;
    std::string error;
    CHECK(Json::parse(std::string{std::istreambuf_iterator<char>(f), {}}, j, error) && j["videos"].isArray());
}

TEST(social_links_are_the_websites_https_pages) {
    // The Welcome icon row opens the same accounts as the site's socials row, over https, and nothing else.
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../website/index.html", std::ios::binary);
    CHECK(f.good());
    const std::string page{std::istreambuf_iterator<char>(f), {}};
    CHECK(kSocialCount == 6);
    for (const SocialLink& link : kSocialLinks) {
        const std::string url = link.url, base = url.substr(0, url.find('?'));
        CHECK(url.rfind("https://", 0) == 0);
        if (std::string(link.name) == "Facebook") { // the page the owner gave; the site gets the same link in its own branch
            CHECK(url.rfind("https://www.facebook.com/profile.php?id=61596196961617&", 0) == 0);
            continue;
        }
        if (page.find("href=\"" + base) == std::string::npos) std::printf("  not on the site: %s\n", link.url);
        CHECK(page.find("href=\"" + base) != std::string::npos);
    }
}

TEST(support_card_links_match_the_website) {
    // The card opens the same Stripe checkout pages as the membership page and the studio page (never links of its own).
    auto read = [](const char* name) {
        std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../website/" + name, std::ios::binary);
        CHECK(f.good());
        return std::string{std::istreambuf_iterator<char>(f), {}};
    };
    const std::string page = read("support.html"), studios = read("studios.html");
    for (const char* url : {kSupportUrlMember, kSupportUrlStudio}) {
        CHECK(std::string(url).rfind("https://buy.stripe.com/", 0) == 0);
        CHECK(page.find(std::string("href=\"") + url + "\"") != std::string::npos);
    }
    CHECK(studios.find(std::string("href=\"") + kSupportUrlStudio + "\"") != std::string::npos);
    // The yearly Member checkout: the same on both, or empty on both (the yearly choice hidden until it exists).
    const std::string yearly(kSupportUrlMemberYearly);
    CHECK(yearly.empty() || yearly.rfind("https://buy.stripe.com/", 0) == 0);
    CHECK(page.find("data-yearly=\"" + yearly + "\"") != std::string::npos);
    CHECK(page.find(kFreeReleaseNote) != std::string::npos); // the same honest wording on both
    CHECK(page.find(std::string("href=\"") + kDiscordUrl + "\"") != std::string::npos); // the same community invite
    // Retired: the Insider tier, one-time payments and the monthly pack, and Studio's per-artist price (owner 11-06: a flat
    // $300 / month for any number of artists, Founding Studio a flat $150); the old wording that contradicts it is gone.
    for (const std::string& p : {page, studios})
        for (const char* old : {"Insider", "One-time", "one-time", "$20", "monthly pack", "pack every month", "whole company",
                                "no seat count", "5 custom items", "10 artists", "$25"}) {
            if (p.find(old) != std::string::npos) std::printf("  retired wording on the site: \"%s\"\n", old);
            CHECK(p.find(old) == std::string::npos);
        }
    for (const std::string& p : {page, studios})
        for (const char* now : {"$300", "Founding Studio", "$150", "3 custom items", "no outside connections", "any number of artists"}) {
            if (p.find(now) == std::string::npos) std::printf("  missing on the site: \"%s\"\n", now);
            CHECK(p.find(now) != std::string::npos);
        }
    CHECK(studios.find("GPL-3.0") != std::string::npos && studios.find("no exclusive or secret features") != std::string::npos);
    CHECK(std::string(kStudioPageUrl) == "https://sculptforge.app/studios.html#membership");
    // Terms & refunds: the terms page exists, carries the refund wording and is linked from both pages.
    CHECK(std::string(kTermsUrl) == "https://sculptforge.app/terms.html");
    const std::string terms = read("terms.html");
    CHECK(terms.find("id=\"refunds\"") != std::string::npos && terms.find("Memberships aren't refundable.") != std::string::npos);
    CHECK(page.find("href=\"terms.html#refunds\"") != std::string::npos);
    CHECK(studios.find("href=\"terms.html") != std::string::npos);
    // Founding Members and Member Requests on the membership page; its counter reads founding.json.
    CHECK(page.find("Founding Member") != std::string::npos);
    CHECK(page.find("Member Requests") != std::string::npos);
    CHECK(page.find("founding.json") != std::string::npos);
}

TEST(support_card_opens_only_its_fixed_links) {
    for (const char* url : {kSupportUrlMember, kSupportUrlMemberYearly, kSupportUrlStudio, kStudioPageUrl, kTermsUrl})
        CHECK(url[0] == '\0' || supportLinkAllowed(url));
    for (const char* url : {"", "https://sculptforge.app/terms.html.evil.example", "https://sculptforge.app/terms.htm",
                            "file:///etc/passwd", "https://buy.stripe.com/", "javascript:alert(1)"})
        CHECK(!supportLinkAllowed(url));
}

TEST(support_card_timing) {
    // The full card: the first start only (Help > Support and the welcome open it by hand).
    SupportCardPrefs p;
    CHECK(supportCardDue(p, true, false) == SupportCardReason::FirstRun);
    CHECK(supportCardDue(p, false, false) == SupportCardReason::None);
    CHECK(supportCardDue(p, true, true) == SupportCardReason::None); // never in tests, renders or CI
    SupportCardPrefs member = p;
    member.supporter = true;
    CHECK(supportCardDue(member, true, false) == SupportCardReason::None);
}

TEST(member_card_backoff) {
    const int64_t day = 24 * 3600, now = 1'800'000'000;
    SupportCardPrefs p;
    // Weekly for the first two "Not now"s.
    for (int i = 1; i <= 2; ++i) {
        p.shownAt = now;
        applyMemberCardChoice(p, MemberCardChoice::NotNow, now);
        CHECK(p.notNow == i);
        CHECK(!memberMomentAllowed(p, false, now + 6 * day, false));
        CHECK(memberMomentAllowed(p, false, now + 7 * day, false));
    }
    // After the 3rd: monthly (the snooze and the gap between cards).
    applyMemberCardChoice(p, MemberCardChoice::NotNow, now);
    CHECK(p.notNow == 3);
    CHECK(!memberMomentAllowed(p, false, now + 29 * day, false));
    CHECK(memberMomentAllowed(p, false, now + 30 * day, false));
    p.hiddenUntil = 0;
    CHECK(!memberMomentAllowed(p, false, now + 8 * day, false)); // shown a week ago: not yet
    // Become a member snoozes as long without counting as a "Not now".
    SupportCardPrefs b = p;
    applyMemberCardChoice(b, MemberCardChoice::Become, now);
    CHECK(b.notNow == 3 && b.hiddenUntil == now + 30 * day);
    // After the 6th: only the first start after an update (still at most monthly).
    for (int i = 0; i < 3; ++i) applyMemberCardChoice(p, MemberCardChoice::NotNow, now);
    CHECK(p.notNow == 6);
    for (MemberMoment m : {MemberMoment::Sculpted, MemberMoment::SavedRender, MemberMoment::Challenge, MemberMoment::Starts,
                           MemberMoment::SculptHours, MemberMoment::Saves, MemberMoment::None})
        CHECK(!memberMomentAllowed(p, false, now + 60 * day, false, m));
    CHECK(!memberMomentAllowed(p, false, now + 29 * day, false, MemberMoment::Updated));
    CHECK(memberMomentAllowed(p, false, now + 60 * day, false, MemberMoment::Updated));
    // Members and tests: never, whatever the moment.
    CHECK(!memberMomentAllowed(p, true, now + 60 * day, false, MemberMoment::Updated));
    CHECK(!memberMomentAllowed(p, false, now + 60 * day, true, MemberMoment::Updated));
}

TEST(member_card_milestones) {
    SupportCardPrefs p;
    CHECK(milestoneDue(p) == MemberMoment::None);
    p.starts = kMilestoneStarts - 1;
    p.sculptSeconds = kMilestoneSculptSeconds - 1.0;
    p.saves = kMilestoneSaves - 1;
    CHECK(milestoneDue(p) == MemberMoment::None);
    // The 10th start, 10 hours of sculpting, the 100th save; each once, in that order when several are due.
    p.starts = 14;
    p.sculptSeconds = 12.7 * 3600.0;
    p.saves = 100;
    CHECK(milestoneDue(p) == MemberMoment::Starts);
    p.milestonesShown |= milestoneBit(MemberMoment::Starts);
    CHECK(milestoneDue(p) == MemberMoment::SculptHours);
    p.milestonesShown |= milestoneBit(MemberMoment::SculptHours);
    CHECK(milestoneDue(p) == MemberMoment::Saves);
    p.milestonesShown |= milestoneBit(MemberMoment::Saves);
    CHECK(milestoneDue(p) == MemberMoment::None);
    CHECK(milestoneBit(MemberMoment::Sculpted) == 0);
    // The words state the true number as it shows (hours whole, rounded down).
    auto words = [&](MemberMoment m) {
        const CardLine l = milestoneLine(m, p);
        return formatCardLine(l, l.format);
    };
    CHECK(words(MemberMoment::Starts) == "You've opened SculptForge 14 times. Members keep it free and growing.");
    CHECK(words(MemberMoment::SculptHours) == "You've sculpted 12 hours in SculptForge. Members keep it free and growing.");
    CHECK(words(MemberMoment::Saves) == "You've saved your work 100 times in SculptForge. Members keep it free and growing.");
    CHECK(words(MemberMoment::Updated).empty());
    // A milestone still counts against the weekly limit like any moment.
    const int64_t day = 24 * 3600, now = 1'800'000'000;
    SupportCardPrefs q;
    q.shownAt = now;
    CHECK(!memberMomentAllowed(q, false, now + 3 * day, false, MemberMoment::Starts));
    CHECK(memberMomentAllowed(q, false, now + 7 * day, false, MemberMoment::Starts));
}

TEST(member_card_feed_lines) {
    // No "support" block: no line (the card keeps its Founding Members words).
    SupportFeed none;
    CHECK(supportFeedLines(none).empty());
    CHECK(supportFeedLine(none, 3).format == nullptr);
    // The feed's block, parsed and checked (core/UpdateCheck.h).
    UpdateInfo u;
    std::string err;
    CHECK(parseUpdateFeed(R"({"version": "9.0.0", "download": "https://sculptforge.app/#download",
        "support": {"funded_this_month": ["Cloth brush", "Faster \u0007remesh", 7, "", "c", "d", "e", "f"],
                    "founding": {"taken": 2, "limit": 100}, "member_requests_this_release": 3}})",
                          u, err));
    CHECK(u.support.present);
    CHECK(u.support.funded.size() == 5 && u.support.funded[1] == "Faster remesh");
    CHECK(u.support.foundingTaken == 2 && u.support.foundingLimit == 100 && u.support.memberRequests == 3);
    const std::vector<CardLine> lines = supportFeedLines(u.support);
    CHECK(lines.size() == 3);
    auto words = [](const CardLine& l) { return formatCardLine(l, l.format); };
    CHECK(words(lines[0]) == "Members funded this month: Cloth brush, Faster remesh, c, d, e.");
    CHECK(words(lines[1]) ==
          "Founding Members so far: 2 of 100. A badge, your name in the credits and your price locked for good.");
    CHECK(words(lines[2]) == "3 changes in this release were members' requests.");
    // They take turns, card by card.
    CHECK(words(supportFeedLine(u.support, 0)) == words(lines[0]));
    CHECK(words(supportFeedLine(u.support, 4)) == words(lines[1]));
    // Odd values are left out, never guessed: a full founding list, taken over the limit, fractions, negatives.
    UpdateInfo odd;
    CHECK(parseUpdateFeed(R"({"version": "9.0.0", "download": "https://sculptforge.app/#download",
        "support": {"founding": {"taken": 120, "limit": 100}, "member_requests_this_release": 2.5}})",
                          odd, err));
    CHECK(odd.support.present && odd.support.foundingTaken == -1 && odd.support.memberRequests == -1);
    CHECK(supportFeedLines(odd.support).empty());
    SupportFeed full;
    full.foundingTaken = full.foundingLimit = 100;
    full.memberRequests = 1;
    const std::vector<CardLine> one = supportFeedLines(full); // (no founding line once they're all taken)
    CHECK(one.size() == 1 && words(one[0]) == "1 change in this release was a member's request.");
    // No block at all: not present.
    UpdateInfo plain;
    CHECK(parseUpdateFeed(R"({"version": "9.0.0", "download": "https://sculptforge.app/#download"})", plain, err));
    CHECK(!plain.support.present);
}
