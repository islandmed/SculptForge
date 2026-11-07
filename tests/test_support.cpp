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
    // Retired: the Insider tier, one-time payments and the monthly pack, and Studio's per-artist price (Tony 11-06: a flat
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
    // Founding Members and Member Requests on the membership page; its counter reads founding.json.
    CHECK(page.find("Founding Member") != std::string::npos);
    CHECK(page.find("Member Requests") != std::string::npos);
    CHECK(page.find("founding.json") != std::string::npos);
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
