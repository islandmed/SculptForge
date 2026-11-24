// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The welcome window's featured creator card: its feed, the links it may open, the dates and which entry a start shows
// (src/core/Featured.cpp).
#include "TestHarness.h"

#include "core/Featured.h"

using namespace sf;

namespace {
const char* kGood = R"({"featured":[
  {"creator":"askNK","title":"SculptForge Is Now Super Powerful","url":"https://www.youtube.com/watch?v=8oMCKB_7TRs",
   "thumb_url":"https://sculptforge.app/featured/a.jpg","start":"2026-11-10","end":"2026-12-10"},
  {"creator":"Someone","title":"Second","url":"https://youtu.be/abc","thumb_url":"https://sculptforge.app/featured/b.jpg"},
  {"creator":"Third","title":"Third video","url":"https://vimeo.com/123","thumb_url":"https://islandmed.github.io/featured/c.jpg"}]})";
}

TEST(featured_feed_parses) {
    std::vector<FeaturedItem> items;
    std::string err;
    CHECK(parseFeaturedFeed(kGood, items, err));
    CHECK(items.size() == 3);
    CHECK(items[0].creator == "askNK" && items[0].title == "SculptForge Is Now Super Powerful");
    CHECK(items[0].startDay == featuredParseDay("2026-11-10") && items[0].endDay == featuredParseDay("2026-12-10"));
    CHECK(items[1].startDay == INT64_MIN && items[1].endDay == INT64_MAX);
    CHECK(parseFeaturedFeed(R"({"featured":[]})", items, err) && items.empty()); // an empty list: nothing shown
    CHECK(!parseFeaturedFeed("[1,2]", items, err));
    CHECK(!parseFeaturedFeed("{", items, err));
    CHECK(!parseFeaturedFeed(R"({"version":"1"})", items, err)); // not this feed
}

TEST(featured_feed_skips_bad_entries_not_the_feed) {
    std::vector<FeaturedItem> items;
    std::string err;
    CHECK(parseFeaturedFeed(R"({"featured":[
      {"creator":"","title":"No creator","url":"https://youtu.be/a","thumb_url":"https://sculptforge.app/f/a.jpg"},
      {"creator":"A","title":"Not https","url":"http://youtu.be/a","thumb_url":"https://sculptforge.app/f/a.jpg"},
      {"creator":"B","title":"Other site","url":"https://example.com/v","thumb_url":"https://sculptforge.app/f/a.jpg"},
      {"creator":"C","title":"Thumb elsewhere","url":"https://youtu.be/a","thumb_url":"https://i.ytimg.com/vi/a/max.jpg"},
      {"creator":"D","title":"Bad date","url":"https://youtu.be/a","thumb_url":"https://sculptforge.app/f/a.jpg","end":"soon"},
      {"creator":"E","title":"No thumb","url":"https://youtu.be/a"},
      3, "x",
      {"creator":"Fine","title":"  Kept\u0001  ","url":"https://youtu.be/ok","thumb_url":"https://sculptforge.app/f/ok.jpg"}]})",
                             items, err));
    CHECK(items.size() == 1 && items[0].creator == "Fine" && items[0].title == "Kept");
    std::string many = R"({"featured":[)";
    for (int i = 0; i < 20; ++i)
        many += std::string(i ? "," : "") + R"({"creator":"c","title":"t","url":"https://youtu.be/)" + std::to_string(i) +
                R"(","thumb_url":"https://sculptforge.app/f/a.jpg"})";
    CHECK(parseFeaturedFeed(many + "]}", items, err) && items.size() == kFeaturedMax);
    // Text is cut on a character boundary.
    std::string longTitle(200, 'x'), twoByte;
    for (int i = 0; i < 100; ++i) twoByte += "\xc3\xa9";
    CHECK(parseFeaturedFeed(R"({"featured":[{"creator":")" + twoByte + R"(","title":")" + longTitle +
                                R"(","url":"https://youtu.be/a","thumb_url":"https://sculptforge.app/f/a.jpg"}]})",
                            items, err));
    CHECK(items.size() == 1 && items[0].title.size() == 120 && items[0].creator.size() == 48 && items[0].creator.size() % 2 == 0);
}

TEST(featured_watch_links_https_to_known_video_sites_only) {
    CHECK(featuredUrlAllowed("https://www.youtube.com/watch?v=8oMCKB_7TRs"));
    CHECK(featuredUrlAllowed("https://youtube.com/watch?v=x"));
    CHECK(featuredUrlAllowed("https://m.youtube.com/watch?v=x"));
    CHECK(featuredUrlAllowed("https://youtu.be/8oMCKB_7TRs?t=5"));
    CHECK(featuredUrlAllowed("https://vimeo.com/12345"));
    CHECK(featuredUrlAllowed("https://WWW.YouTube.com/watch?v=x")); // host case doesn't matter
    CHECK(!featuredUrlAllowed("http://www.youtube.com/watch?v=x"));
    CHECK(!featuredUrlAllowed("ftp://youtu.be/x"));
    CHECK(!featuredUrlAllowed("javascript:alert(1)"));
    CHECK(!featuredUrlAllowed("file:///etc/passwd"));
    CHECK(!featuredUrlAllowed("youtube.com/watch?v=x"));
    CHECK(!featuredUrlAllowed(""));
    CHECK(!featuredUrlAllowed("https://"));
    CHECK(!featuredUrlAllowed("https://youtube.com.evil.example/watch"));   // look-alike hosts
    CHECK(!featuredUrlAllowed("https://evil.example/youtu.be/x"));
    CHECK(!featuredUrlAllowed("https://notyoutube.com/x"));
    CHECK(!featuredUrlAllowed("https://youtube.com@evil.example/x"));       // user name tricks
    CHECK(!featuredUrlAllowed("https://evil.example@youtube.com/x"));
    CHECK(!featuredUrlAllowed("https://youtube.com:8443/x"));               // a port
    CHECK(!featuredUrlAllowed("https://youtube.com\\@evil.example/x"));
    CHECK(!featuredUrlAllowed("https://youtu.be/x y"));
    CHECK(!featuredUrlAllowed("https://youtu.be/x\n--cmd"));
    CHECK(!featuredUrlAllowed("https://www.vimeo.com.evil.example/"));
    CHECK(!featuredUrlAllowed("https://player.vimeo.com/video/1")); // no embed hosts
}

TEST(featured_thumbnails_come_from_our_host_only) {
    CHECK(featuredThumbAllowed("https://sculptforge.app/featured/x.jpg"));
    CHECK(featuredThumbAllowed("https://islandmed.github.io/featured/x.jpg")); // the update feed's mirror
    CHECK(!featuredThumbAllowed("http://sculptforge.app/featured/x.jpg"));
    CHECK(!featuredThumbAllowed("https://i.ytimg.com/vi/x/maxresdefault.jpg"));
    CHECK(!featuredThumbAllowed("https://sculptforge.app.evil.example/x.jpg"));
    CHECK(!featuredThumbAllowed("https://sculptforge.app@evil.example/x.jpg"));
    CHECK(!featuredThumbAllowed("https://sculptforge.app:444/x.jpg"));
    CHECK(!featuredThumbAllowed(""));
}

TEST(featured_feed_sits_beside_the_update_feed) {
    CHECK(featuredFeedSource("https://sculptforge.app/latest.json") == "https://sculptforge.app/featured.json");
    CHECK(featuredFeedSource("https://islandmed.github.io/latest.json") == "https://islandmed.github.io/featured.json");
    CHECK(featuredFeedSource("/tmp/feeds/latest.json") == "/tmp/feeds/featured.json");
    CHECK(featuredFeedSource("latest.json") == "featured.json");
    CHECK(featuredFeedSource("https://sculptforge.app/notlatest.json").empty());
    CHECK(featuredFeedSource("/tmp/feeds/update_newer.json").empty());
    CHECK(featuredThumbFile("https://sculptforge.app/a.jpg") == featuredThumbFile("https://sculptforge.app/a.jpg"));
    CHECK(featuredThumbFile("https://sculptforge.app/a.jpg") != featuredThumbFile("https://sculptforge.app/b.jpg"));
}

TEST(featured_dates) {
    CHECK(featuredParseDay("1970-01-01") == 0);
    CHECK(featuredParseDay("1970-01-02") == 1);
    CHECK(featuredParseDay("2000-03-01") == 11017);
    CHECK(featuredParseDay("2026-11-10") == 20767);
    CHECK(featuredParseDay("2026-11-1") == INT64_MIN);
    CHECK(featuredParseDay("2026-13-01") == INT64_MIN);
    CHECK(featuredParseDay("2026/11/10") == INT64_MIN);
    CHECK(featuredParseDay("") == INT64_MIN);
    CHECK(featuredDay(0) == 0 && featuredDay(86399) == 0 && featuredDay(86400) == 1);
    CHECK(featuredDay(1794268800) == featuredParseDay("2026-11-10")); // 2026-11-10 00:00 UTC
}

TEST(featured_rotation_dates_and_hiding) {
    std::vector<FeaturedItem> items;
    std::string err;
    CHECK(parseFeaturedFeed(kGood, items, err) && items.size() == 3);
    const int64_t inside = featuredParseDay("2026-11-20"), before = featuredParseDay("2026-11-09"),
                  after = featuredParseDay("2026-12-11");
    // Inside the first entry's dates all three show, one a start, in order, round and round.
    CHECK(pickFeatured(items, inside, {}, 0) == 0 && pickFeatured(items, inside, {}, 1) == 1 &&
          pickFeatured(items, inside, {}, 2) == 2 && pickFeatured(items, inside, {}, 3) == 0);
    // Before its start, or after its end, the dated entry is skipped (the others rotate).
    CHECK(pickFeatured(items, before, {}, 0) == 1 && pickFeatured(items, before, {}, 1) == 2 && pickFeatured(items, before, {}, 2) == 1);
    CHECK(pickFeatured(items, after, {}, 0) == 1);
    CHECK(pickFeatured(items, featuredParseDay("2026-11-10"), {}, 0) == 0 && pickFeatured(items, featuredParseDay("2026-12-10"), {}, 0) == 0); // both ends count
    // The x: a hidden video never shows; all hidden = nothing.
    CHECK(pickFeatured(items, inside, {"https://youtu.be/abc"}, 1) == 2);
    CHECK(pickFeatured(items, inside, {items[0].url, items[1].url, items[2].url}, 0) == -1);
    // An empty list shows nothing.
    CHECK(pickFeatured({}, inside, {}, 5) == -1);
}
