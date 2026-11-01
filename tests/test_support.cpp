// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supporting SculptForge: the supporters list (docs/supporters.txt) parsed and sanitised for Help > About, and
// when the one optional reminder may show.
#include "TestHarness.h"

#include "app/Support.h"

#include <string>

using namespace sf;

TEST(supporters_parse_skips_comments_and_blanks) {
    CHECK(parseSupporters("").empty());
    CHECK(parseSupporters("# one name per line\n\n   \n#Not a name\n").empty());
    const auto names = parseSupporters("# header\nAda Lovelace\r\n  Grace Hopper  \n\nLinus\n");
    CHECK(names.size() == 3);
    CHECK(names[0] == "Ada Lovelace" && names[1] == "Grace Hopper" && names[2] == "Linus");
    CHECK(std::string(kSupportUrl) == "https://sculptforge.app/donate.html");
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
