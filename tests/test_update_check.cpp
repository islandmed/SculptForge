// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The update check's feed and version comparison (src/core/UpdateCheck.cpp).
#include "TestHarness.h"

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

TEST(update_versions_order_prereleases) {
    CHECK(compareVersions("0.11.0-beta.1", "0.11.0") < 0);
    CHECK(compareVersions("0.11.0", "0.11.0-beta.1") > 0);
    CHECK(compareVersions("0.11.0-beta.1", "0.10.11") > 0);
    CHECK(compareVersions("0.11.0-beta.1", "0.11.0-beta.2") < 0);
    CHECK(compareVersions("0.11.0-beta.2", "0.11.0-beta.10") < 0); // numbers numerically
    CHECK(compareVersions("0.11.0-beta.2", "0.11.0-rc.1") < 0);
    CHECK(compareVersions("0.11.0-beta", "0.11.0-beta.1") < 0);
    CHECK(compareVersions("v0.11.0-beta.1", "0.11.0-beta.1") == 0);
    CHECK(versionLabel("0.11.0", "beta.1") == "0.11.0 Beta 1");
    CHECK(versionLabel("0.11.0", "rc.2") == "0.11.0 RC 2");
    CHECK(versionLabel("0.10.11", "") == "0.10.11");
}

// latest.json while 0.11.0 Beta 1 is out: old apps and Stable read the top level, Preview the channel's entry.
static const char* kPreviewFeed = R"({"version":"0.10.11","download":"https://sculptforge.app/#download",
    "highlights":["Stable one"],
    "channels":{"preview":{"version":"0.11.0-beta.1","download":"https://github.com/islandmed/SculptForge/releases/tag/v0.11.0-beta.1",
                           "notes":"https://sculptforge.app/guide/whats-new.html","highlights":["Beta one","Beta two"]}}})";

TEST(update_feed_preview_channel) {
    UpdateInfo u;
    std::string err;
    CHECK(parseUpdateFeed(kPreviewFeed, u, err) && u.version == "0.10.11" && u.highlights.size() == 1); // Stable
    CHECK(parseUpdateFeed(kPreviewFeed, u, err, UpdateChannel::Preview));
    CHECK(u.version == "0.11.0-beta.1" && u.highlights.size() == 2 && u.highlights[0] == "Beta one");
    CHECK(u.downloadUrl.find("/releases/tag/v0.11.0-beta.1") != std::string::npos);
    // Once stable passes the beta, Preview is offered stable.
    std::string promoted = kPreviewFeed;
    promoted.replace(promoted.find("0.10.11"), 7, "0.11.0");
    CHECK(parseUpdateFeed(promoted, u, err, UpdateChannel::Preview) && u.version == "0.11.0");
    // A broken or missing preview entry falls back to stable; it never breaks the check.
    CHECK(parseUpdateFeed(R"({"version":"0.10.11","download":"https://example.org/dl",
                              "channels":{"preview":{"version":"0.11.0-beta.1","download":"http://example.org"}}})",
                          u, err, UpdateChannel::Preview) && u.version == "0.10.11");
    CHECK(parseUpdateFeed(R"({"version":"0.10.11","download":"https://example.org/dl","channels":[]})", u, err,
                          UpdateChannel::Preview) && u.version == "0.10.11");
    CHECK(parseUpdateFeed(R"({"version":"0.10.11","download":"https://example.org/dl"})", u, err,
                          UpdateChannel::Preview) && u.version == "0.10.11");
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

TEST(update_feed_on_the_website_parses) {
    std::ifstream f(SF_TEST_DATA_DIR "/../../website/latest.json", std::ios::binary);
    CHECK(bool(f));
    UpdateInfo u;
    std::string err;
    CHECK(parseUpdateFeed(std::string(std::istreambuf_iterator<char>(f), {}), u, err));
    CHECK(!u.highlights.empty());
}
