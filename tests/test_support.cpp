// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supporting SculptForge: the supporters list (docs/supporters.txt) parsed and sanitised for Help > About, and
// when the one optional reminder may show.
#include "TestHarness.h"

#include "app/Support.h"

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

TEST(support_reminder_timing) {
    // Sculpting time grows by frames spent in strokes, a stalled frame counting at most a quarter second.
    double t = 0.0;
    t = addSculptTime(t, 0.016);
    CHECK(t > 0.015 && t < 0.017);
    CHECK(addSculptTime(t, 3600.0) - t <= kSculptTimeMaxStep + 1e-9);
    CHECK(addSculptTime(t, -5.0) == t);
    CHECK(addSculptTime(-1.0, 0.1) <= 0.1 + 1e-9); // a damaged stored value starts again
    const double ten = kSupportReminderSculptSeconds, session = kSupportReminderSessionSeconds;
    CHECK(!supportReminderDue(ten - 1.0, session, false, false)); // not before ten hours
    CHECK(!supportReminderDue(ten, 0.0, false, false));            // never at launch
    CHECK(!supportReminderDue(ten, session - 1.0, false, false));
    CHECK(!supportReminderDue(ten, session, true, false));         // never mid-stroke
    CHECK(!supportReminderDue(ten, session, false, true));         // once only (shown or dismissed)
    CHECK(supportReminderDue(ten, session, false, false));
    CHECK(supportReminderDue(ten * 3.0, session * 6.0, false, false));
}

TEST(support_card_links_match_the_website) {
    // The card opens the same four Stripe checkout pages as the membership page (never links of its own).
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../website/support.html", std::ios::binary);
    CHECK(f.good());
    const std::string page{std::istreambuf_iterator<char>(f), {}};
    for (const char* url : {kSupportUrlSupporter, kSupportUrlInsider, kSupportUrlStudio, kSupportUrlOnce}) {
        CHECK(std::string(url).rfind("https://buy.stripe.com/", 0) == 0);
        CHECK(page.find(std::string("href=\"") + url + "\"") != std::string::npos);
    }
    CHECK(page.find(kFreeReleaseNote) != std::string::npos); // the same honest wording on both
    CHECK(page.find(std::string("href=\"") + kDiscordUrl + "\"") != std::string::npos); // the same community invite
}

TEST(studio_offer_matches_the_website) {
    // Founding Studio: one switch in Support.h and one in website/studio-offer.js, set the same way, same $300 link.
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../website/studio-offer.js", std::ios::binary);
    CHECK(f.good());
    const std::string js{std::istreambuf_iterator<char>(f), {}};
    CHECK(std::string(kSupportUrlStudioRegular).rfind("https://buy.stripe.com/", 0) == 0);
    CHECK(std::string(kSupportUrlStudioFounding).rfind("https://buy.stripe.com/", 0) == 0);
    CHECK(js.find(std::string("const STUDIO_URL = '") + kSupportUrlStudioRegular + "'") != std::string::npos);
    CHECK(js.find(kFoundingStudioOpen ? "const FOUNDING_STUDIO_OPEN = true;" : "const FOUNDING_STUDIO_OPEN = false;") !=
          std::string::npos);
}

TEST(support_card_timing) {
    const int64_t day = 24 * 3600, now = 1'800'000'000;
    SupportCardPrefs p;
    // The first start after install, then the first start of each new version.
    CHECK(supportCardDue(p, true, "0.10.4", now, false) == SupportCardReason::FirstRun);
    CHECK(supportCardDue(p, false, "0.10.4", now, false) == SupportCardReason::Updated); // from a version before it
    p.shownVersion = "0.10.4";
    p.shownAt = now;
    CHECK(supportCardDue(p, false, "0.10.4", now + 60, false) == SupportCardReason::None);
    CHECK(supportCardDue(p, false, "0.10.4", now + 6 * day, false) == SupportCardReason::None); // at most once a week
    CHECK(supportCardDue(p, false, "0.10.4", now + 7 * day, false) == SupportCardReason::Weekly);
    CHECK(supportCardDue(p, false, "0.10.5", now + day, false) == SupportCardReason::Updated);
    CHECK(supportCardDue(p, false, "0.10.4", now - 30 * day, false) == SupportCardReason::Weekly); // clock set back
    CHECK(supportCardDue(p, false, "0.10.4", now - day, false) == SupportCardReason::None);
    // "Maybe later": 30 days, new versions included; then as before.
    SupportCardPrefs later = p;
    later.laterAt = now;
    CHECK(supportCardDue(later, false, "0.10.4", now + 7 * day, false) == SupportCardReason::None);
    CHECK(supportCardDue(later, false, "0.10.5", now + 29 * day, false) == SupportCardReason::None);
    CHECK(supportCardDue(later, false, "0.10.5", now + 30 * day, false) == SupportCardReason::Updated);
    CHECK(supportCardDue(later, false, "0.10.4", now + 30 * day, false) == SupportCardReason::Weekly);
    CHECK(supportCardDue(later, false, "0.10.4", now - 30 * day, false) == SupportCardReason::Weekly); // clock set back
    // Never in tests, renders or CI; never once turned off; never for a supporter.
    CHECK(supportCardDue(p, true, "0.10.5", now + 30 * day, true) == SupportCardReason::None);
    SupportCardPrefs off = p;
    off.enabled = false;
    CHECK(supportCardDue(off, true, "0.10.5", now + 30 * day, false) == SupportCardReason::None);
    SupportCardPrefs member = p;
    member.supporter = true;
    CHECK(supportCardDue(member, true, "0.10.5", now + 30 * day, false) == SupportCardReason::None);
}
