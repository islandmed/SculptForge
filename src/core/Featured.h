// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The welcome window's "Featured creator" card: website/featured.json, a sibling of the update feed on the same host
// (core/UpdateCheck.h), names a few videos by artists who made something about SculptForge. The app reads it with the
// daily update check, keeps it and the thumbnail on this computer, shows one at a time (a different one each start)
// and opens its link in the browser: no embedded player, no other network host, nothing about the user is sent.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

// The feed is a sibling of latest.json; the thumbnails sit under /featured/ on the same host.
inline constexpr size_t kFeaturedMax = 8;           // entries read from the feed
inline constexpr size_t kFeaturedThumbMax = 1 << 20; // bytes of one thumbnail

struct FeaturedItem {
    std::string creator, title, url, thumbUrl;
    int64_t startDay = INT64_MIN, endDay = INT64_MAX; // days since 1970-01-01 (UTC), both ends included
};

// {"featured": [{"creator": "askNK", "title": "...", "url": "https://www.youtube.com/watch?v=...",
// "thumb_url": "https://sculptforge.app/featured/x.jpg", "start": "2026-11-10", "end": "2026-12-10"}]}.
// An entry needs a creator, a title, a watch link that passes featuredUrlAllowed and a thumbnail link that passes
// featuredThumbAllowed (the entry without one of them is skipped, never the whole feed); dates are optional.
// Text is printable (creator 48 bytes at most, title 120). False (with `error`) only when it isn't JSON of that shape.
bool parseFeaturedFeed(const std::string& json, std::vector<FeaturedItem>& out, std::string& error);

// Where the watch link may go: https, and the host exactly youtube.com (www., m.), youtu.be or vimeo.com (www.).
// No user name, no port, nothing after a backslash or control character.
bool featuredUrlAllowed(const std::string& url);
// Where thumbnails may come from: https on sculptforge.app or its GitHub mirror (core/UpdateCheck.h), nowhere else.
bool featuredThumbAllowed(const std::string& url);

// The featured feed beside the update feed `source` (a URL or a local file ending in latest.json); "" when it isn't one.
std::string featuredFeedSource(const std::string& updateFeedSource);
// The file name a thumbnail is kept under: the same URL always gives the same name.
std::string featuredThumbFile(const std::string& thumbUrl);

// Days since 1970-01-01 (UTC) of a unix time, and of "YYYY-MM-DD" (INT64_MIN when it isn't a date).
int64_t featuredDay(int64_t unixSeconds);
int64_t featuredParseDay(const std::string& date);

// Which entry this start shows: those in their dates on `day` that the user didn't hide (`hidden` holds links), in
// the feed's order, the `launch`th start choosing (so each start moves on). -1 when there is none.
int pickFeatured(const std::vector<FeaturedItem>& items, int64_t day, const std::vector<std::string>& hidden,
                 uint64_t launch);

} // namespace sf
