// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The update check's feed and version comparison (src/core/UpdateCheck.cpp).
#include "TestHarness.h"

#include "app/AndroidHttp.h"
#include "core/UpdateCheck.h"

#include <fstream>
#include <iterator>

using namespace sf;

TEST(update_versions_compare_numerically) {
    CHECK(compareVersions("0.6.0", "0.5.0") > 0);
    CHECK(compareVersions("0.5.0", "0.6.0") < 0);
    CHECK(compareVersions("0.10.0", "0.9.2") > 0); // not as text
    CHECK(compareVersions("0.6", "0.6.0") == 0);
    CHECK(compareVersions("v0.6.1", "0.6.0") > 0);
    CHECK(compareVersions("0.6.0-rc1", "0.6.0") < 0); // a prerelease comes before its version
    CHECK(compareVersions("0.6.0+build7", "0.6.0") == 0);
    CHECK(compareVersions("1.0.0", "0.99.99") > 0);
    CHECK(compareVersions("", "0.1.0") < 0);
}

TEST(update_feed_parses) {
    UpdateInfo u;
    std::string err;
    CHECK(parseUpdateFeed(R"({"version":"0.6.0","download":"https://example.org/dl","notes":"https://example.org/new",
                              "highlights":["Faster","Lighter","",3,"More","Even more","Too many"]})",
                          u, err));
    CHECK(u.version == "0.6.0" && u.downloadUrl == "https://example.org/dl" && u.notesUrl == "https://example.org/new");
    CHECK(u.highlights.size() == 4 && u.highlights[0] == "Faster" && u.highlights[2] == "More");
    // Notes fall back to the download page.
    CHECK(parseUpdateFeed(R"({"version":"0.6.0","download":"https://example.org/dl"})", u, err) &&
          u.notesUrl == "https://example.org/dl" && u.highlights.empty());
}

// The macOS in-app update is offered only when the feed's top-level self_update.macos is a JSON true; anything else
// (missing, a string, a number, null, the key at the wrong level, or inside a channel) keeps the hold.
TEST(update_feed_mac_self_update_hold) {
    const std::string head = "{\"version\":\"0.14.2\",\"download\":\"https://sculptforge.app/#download\",\"notes\":\"https://sculptforge.app/guide/whats-new.html\"";
    UpdateInfo u;
    std::string err;
    CHECK(parseUpdateFeed(head + "}", u, err) && !u.macSelfUpdate);
    CHECK(parseUpdateFeed(head + ",\"self_update\":{\"macos\":true}}", u, err) && u.macSelfUpdate);
    for (const char* bad : {",\"self_update\":{\"macos\":false}}", ",\"self_update\":{\"macos\":\"true\"}}", ",\"self_update\":{\"macos\":1}}",
                            ",\"self_update\":{\"macos\":null}}", ",\"self_update\":true}", ",\"self_update\":{}}", ",\"self_update\":[true]}",
                            ",\"macos\":true}", ",\"self_update\":{\"linux\":true}}",
                            // duplicate keys, either order: the strict read refuses the feed for the hold (the banner still works)
                            ",\"self_update\":{\"macos\":false,\"macos\":true}}", ",\"self_update\":{\"macos\":true,\"macos\":false}}",
                            ",\"self_update\":{\"macos\":true},\"self_update\":{\"macos\":false}}",
                            ",\"self_update\":{\"macos\":false},\"self_update\":{\"macos\":true}}",
                            ",\"self_update\":{\"macos\":true},\"highlights\":[],\"highlights\":[]}",
                            ",\"channels\":{\"preview\":{\"version\":\"0.15.0-beta.1\",\"download\":\"https://sculptforge.app/#download\",\"self_update\":{\"macos\":true}}}}"}) {
        UpdateInfo v;
        CHECK(parseUpdateFeed(head + bad, v, err) && !v.macSelfUpdate && v.version == "0.14.2"); // (the update itself still parses)
        CHECK(parseUpdateFeed(head + bad, v, err, UpdateChannel::Preview) && !v.macSelfUpdate);
    }
    // Set at the top level, it holds for the preview entry too (one hold for the site).
    CHECK(parseUpdateFeed(head + ",\"self_update\":{\"macos\":true},\"channels\":{\"preview\":{\"version\":\"0.15.0-beta.1\",\"download\":\"https://sculptforge.app/#download\"}}}",
                          u, err, UpdateChannel::Preview) && u.version == "0.15.0-beta.1" && u.macSelfUpdate);
    // The live feed ships with the hold on (nothing to flip until the swap has been tested on a real Mac).
    std::ifstream f(SF_TEST_DATA_DIR "/../../website/latest.json", std::ios::binary);
    CHECK(bool(f));
    const std::string live((std::istreambuf_iterator<char>(f)), {});
    CHECK(parseUpdateFeed(live, u, err) && !u.macSelfUpdate);
}

TEST(update_versions_order_prereleases) {
    CHECK(compareVersions("9.0.0-beta.1", "9.0.0") < 0);
    CHECK(compareVersions("9.0.0", "9.0.0-beta.1") > 0);
    CHECK(compareVersions("9.0.0-beta.1", "8.10.11") > 0);
    CHECK(compareVersions("9.0.0-beta.1", "9.0.0-beta.2") < 0);
    CHECK(compareVersions("9.0.0-beta.2", "9.0.0-beta.10") < 0); // numbers numerically
    CHECK(compareVersions("9.0.0-beta.2", "9.0.0-rc.1") < 0);
    CHECK(compareVersions("9.0.0-beta", "9.0.0-beta.1") < 0);
    CHECK(compareVersions("v9.0.0-beta.1", "9.0.0-beta.1") == 0);
    CHECK(versionLabel("9.0.0", "beta.1") == "9.0.0 Beta 1");
    CHECK(versionLabel("9.0.0", "rc.2") == "9.0.0 RC 2");
    CHECK(versionLabel("8.10.11", "") == "8.10.11");
    CHECK(shownVersion("8.10.11", "", "").text() == "8.10.11");
    CHECK(shownVersion("8.10.11", "", "").tag.empty());
    CHECK(shownVersion("9.0.0", "beta.2", "").text() == "9.0.0 beta.2");
    CHECK(shownVersion("9.0.0", "beta.2", "").tag == "beta.2");
    CHECK(shownVersion("8.10.11", "", "1a2b3c4d5e").tag == "dev 1a2b3c4");
    CHECK(shownVersion("9.0.0", "rc.1", "1a2b3c4").text() == "9.0.0 rc.1 dev 1a2b3c4");
}

// latest.json while 9.0.0 Beta 1 is out: old apps and Stable read the top level, Preview the channel's entry.
static const char* kPreviewFeed = R"({"version":"8.10.11","download":"https://sculptforge.app/#download",
    "highlights":["Stable one"],
    "channels":{"preview":{"version":"9.0.0-beta.1","download":"https://github.com/islandmed/SculptForge/releases/tag/v9.0.0-beta.1",
                           "notes":"https://sculptforge.app/guide/whats-new.html","highlights":["Beta one","Beta two"]}}})";

TEST(update_feed_preview_channel) {
    UpdateInfo u;
    std::string err;
    CHECK(parseUpdateFeed(kPreviewFeed, u, err) && u.version == "8.10.11" && u.highlights.size() == 1); // Stable
    CHECK(parseUpdateFeed(kPreviewFeed, u, err, UpdateChannel::Preview));
    CHECK(u.version == "9.0.0-beta.1" && u.highlights.size() == 2 && u.highlights[0] == "Beta one");
    CHECK(u.downloadUrl.find("/releases/tag/v9.0.0-beta.1") != std::string::npos);
    // Once stable passes the beta, Preview is offered stable.
    std::string promoted = kPreviewFeed;
    promoted.replace(promoted.find("8.10.11"), 7, "9.0.0");
    CHECK(parseUpdateFeed(promoted, u, err, UpdateChannel::Preview) && u.version == "9.0.0");
    // A broken or missing preview entry falls back to stable; it never breaks the check.
    CHECK(parseUpdateFeed(R"({"version":"8.10.11","download":"https://example.org/dl",
                              "channels":{"preview":{"version":"9.0.0-beta.1","download":"http://example.org"}}})",
                          u, err, UpdateChannel::Preview) && u.version == "8.10.11");
    CHECK(parseUpdateFeed(R"({"version":"8.10.11","download":"https://example.org/dl","channels":[]})", u, err,
                          UpdateChannel::Preview) && u.version == "8.10.11");
    CHECK(parseUpdateFeed(R"({"version":"8.10.11","download":"https://example.org/dl"})", u, err,
                          UpdateChannel::Preview) && u.version == "8.10.11");
}

TEST(update_feed_rejects_bad_feeds) {
    UpdateInfo u;
    std::string err;
    CHECK(!parseUpdateFeed("not json", u, err));
    CHECK(!parseUpdateFeed("[]", u, err));
    CHECK(!parseUpdateFeed(R"({"download":"https://example.org/dl"})", u, err));
    CHECK(!parseUpdateFeed(R"({"version":"soon","download":"https://example.org/dl"})", u, err));
    CHECK(!parseUpdateFeed(R"({"version":"0.6.0","download":"http://example.org/dl"})", u, err)); // https only
    CHECK(!parseUpdateFeed(R"x({"version":"0.6.0","download":"javascript:alert(1)"})x", u, err));
    CHECK(!err.empty());
}

TEST(update_check_once_a_day) {
    CHECK(updateCheckDue(0, 1000));
    CHECK(!updateCheckDue(1000, 1000 + kUpdateCheckInterval - 1));
    CHECK(updateCheckDue(1000, 1000 + kUpdateCheckInterval));
    CHECK(updateCheckDue(5000, 1000)); // the clock went back
}

// The start-up check (the same gate on every platform, Android included): the Preferences switch, SF_NO_UPDATE_CHECK and
// the local-only runs turn it off; otherwise it runs at most once in 24 hours.
TEST(update_check_start_up_gate_and_off_switch) {
    const int64_t t0 = 1'700'000'000, day = kUpdateCheckInterval;
    CHECK(updateCheckWanted(true, false, false, 0, t0));                    // first run
    CHECK(!updateCheckWanted(true, false, false, t0, t0 + day - 1));        // checked less than a day ago
    CHECK(updateCheckWanted(true, false, false, t0, t0 + day));             // a day later
    CHECK(!updateCheckWanted(false, false, false, 0, t0));                  // the switch is off
    CHECK(!updateCheckWanted(false, false, false, t0, t0 + 10 * day));      // ... however old the last check is
    CHECK(!updateCheckWanted(true, true, false, 0, t0));                    // SF_NO_UPDATE_CHECK
    CHECK(!updateCheckWanted(true, false, true, 0, t0));                    // a UI test or a take
}

TEST(update_feed_on_the_website_parses) {
    std::ifstream f(SF_TEST_DATA_DIR "/../../website/latest.json", std::ios::binary);
    CHECK(bool(f));
    UpdateInfo u;
    std::string err;
    CHECK(parseUpdateFeed(std::string(std::istreambuf_iterator<char>(f), {}), u, err));
    CHECK(!u.highlights.empty());
}

TEST(update_feed_challenge_results) {
    // The monthly challenge whose results are out: the membership card's moment (Support.h), once per id.
    UpdateInfo u;
    std::string err;
    CHECK(parseUpdateFeed(R"({"version":"0.6.0","download":"https://example.org/dl",
        "challenge":{"id":"2026-11","title":"Creatures\u0007 of the deep"}})", u, err));
    CHECK(u.challengeId == "2026-11" && u.challengeTitle == "Creatures of the deep"); // control characters dropped
    CHECK(parseUpdateFeed(R"({"version":"0.6.0","download":"https://example.org/dl"})", u, err) && u.challengeId.empty());
    for (const char* bad : {R"("id":"../x")", R"("id":"")", R"("id":7)", R"("id":"0123456789012345678901234567890123")"}) {
        CHECK(parseUpdateFeed(std::string(R"({"version":"0.6.0","download":"https://example.org/dl","challenge":{)") + bad + "}}",
                              u, err));
        CHECK(u.challengeId.empty() && u.challengeTitle.empty());
    }
    // A long title is cut to 48 bytes, never inside a character.
    std::string title;
    for (int i = 0; i < 40; ++i) title += "\xc3\xa9";
    // (Built outside CHECK: MSVC's preprocessor misreads a raw string ending in a quote inside a macro argument.)
    const std::string feed =
        std::string(R"({"version":"0.6.0","download":"https://example.org/dl","challenge":{"id":"x","title":")") + title + "\"}}";
    CHECK(parseUpdateFeed(feed, u, err));
    CHECK(u.challengeTitle.size() == 48 && u.challengeTitle == title.substr(0, 48));
}

TEST(installer_update_choice_applies_once_per_installer_run) {
    const InstallerChoice off = parseInstallerChoice(R"({"check_updates": false})");
    const InstallerChoice on = parseInstallerChoice(R"({"check_updates": true})");
    CHECK(off.present && !off.checkUpdates && on.present && on.checkUpdates);
    // No file, or one that isn't ours: nothing is read, nothing changes.
    CHECK(!parseInstallerChoice("").present && !parseInstallerChoice("{}").present &&
          !parseInstallerChoice(R"({"check_updates": "no"})").present && !parseInstallerChoice("[1]").present);
    int64_t applied = 0;
    bool check = true;
    CHECK(!applyInstallerChoice(InstallerChoice{}, 100, applied, check) && check && applied == 0);
    // First start after the installer: its choice wins and the run is recorded.
    CHECK(applyInstallerChoice(off, 100, applied, check) && !check && applied == 100);
    // Later starts: the same run is not applied again, so Preferences > Updates can switch it back on and stay on.
    check = true;
    CHECK(!applyInstallerChoice(off, 100, applied, check) && check);
    // A reinstall (a newer file) applies its choice again, either way.
    CHECK(applyInstallerChoice(off, 250, applied, check) && !check && applied == 250);
    CHECK(applyInstallerChoice(on, 300, applied, check) && check && applied == 300);
    // A file without a usable time is ignored.
    check = false;
    CHECK(!applyInstallerChoice(on, 0, applied, check) && !check);
}

// Android's own transport (app/AndroidHttp.h) only ever asks for the update feed's two addresses.
TEST(android_update_feed_addresses_and_answers) {
    using namespace sf::android;
    CHECK(isUpdateFeedUrl(kUpdateFeedUrl));
    CHECK(isUpdateFeedUrl(kUpdateFeedMirrorUrl));
    CHECK(!isUpdateFeedUrl("http://sculptforge.app/latest.json"));                // https only
    CHECK(!isUpdateFeedUrl("https://sculptforge.app.evil.example/latest.json"));  // the host, not a prefix of it
    CHECK(!isUpdateFeedUrl("https://sculptforge.app@evil.example/latest.json"));
    CHECK(!isUpdateFeedUrl("https://sculptforge.app/latest.json?v=0.14.2"));      // no query: nothing is sent about the app
    CHECK(!isUpdateFeedUrl("https://sculptforge.app/../x"));
    CHECK(!isUpdateFeedUrl("https://api.github.com/repos/x"));
    const auto ok = parseFeedAnswer("200\n\n{\"version\":\"0.15.0\"}\nmore");
    CHECK(ok.status == 200 && ok.error.empty() && ok.body == "{\"version\":\"0.15.0\"}\nmore");
    const auto no = parseFeedAnswer("0\nUnable to resolve host\n");
    CHECK(no.status == 0 && no.error == "Unable to resolve host" && no.body.empty());
    CHECK(!parseFeedAnswer("garbage").error.empty());
    CHECK(!parseFeedAnswer("x\ny\nz").error.empty());
}
