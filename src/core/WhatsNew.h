// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What's New (docs/guide/whats-new.md) as data: each "## Version X" section may start with a ```features block,
//
//   ```features
//   date: 2026-11-01
//   tagline: One line under the popup's title.
//   highlight: Short title: one line, used when the version is folded into a group
//   title: A bold 3-5 word title          <- each title starts a feature card
//   line: One sentence.
//   image: images/wn-0107-look.jpg        (600x340)
//   action: look:new                      (what Try it does; see App::runWhatsNewAction)
//   link: interface.md#looks-and-layouts  (optional: where the website's card links; guide: actions link there anyway)
//   ```
//
// and the prose notes below it are the details. The popup (app/WhatsNewPopup.cpp), the guide chapter
// (app/UserGuide.cpp) and the website (tools/guide/build_site.py, which mirrors this file) read the same data. The
// newest `kDetailed` versions are shown in full; older ones fold into groups, so each release slides the window:
// the rest of the newest series ("0.10.0 to 0.10.4"), whole series ("0.9.x"), then "0.8 and earlier".
#pragma once

#include "core/Guide.h"

#include <string>
#include <string_view>
#include <vector>

namespace sf::whatsnew {

inline constexpr int kDetailed = 3;      // versions shown in full
inline constexpr int kSeriesGroups = 2;  // folded series with a group of their own; older ones share the last group
inline constexpr int kMaxHighlights = 6; // one-line highlights per group

struct Card {
    std::string title, line, image, action, link;
};

struct Version {
    std::string version; // "0.10.7"
    std::string anchor;  // the heading's anchor, "version-0107"
    std::string date, tagline;
    std::vector<std::string> highlights; // "Title: one line"
    std::vector<Card> cards;
    int heading = -1;         // block index of "## Version X"
    int first = -1, end = -1; // its detail blocks [first, end), the features block left out
};

struct Group {
    std::string label;   // "0.10.0 to 0.10.4", "0.9.x", "0.8 and earlier"
    std::string anchor;  // "earlier-0100-to-0104"
    std::string summary; // the highlights' titles: "Micro levels, PBR painting, ..."
    std::vector<int> versions;           // indices into Page::versions, newest first
    std::vector<std::string> highlights; // up to kMaxHighlights, "Title: one line"
};

struct Page {
    int introEnd = 0;            // blocks before the first version (title and intro)
    std::vector<Version> versions; // newest first, released ones only (none newer than the build)
    int detailed = 0;            // versions[0 .. detailed) are shown in full
    std::vector<Group> groups;
    std::vector<std::string> problems; // malformed features blocks (the guide test fails on them)
};

// -1, 0, 1 like strcmp, numerically per part: "0.9.7" < "0.10.0", "0.8" == "0.8.0".
int compareVersions(std::string_view a, std::string_view b);
// The features block's text into V (date, tagline, highlights, cards); false with ERROR on an unknown key or a
// card without a title, line or action.
bool parseFeatures(std::string_view text, Version& v, std::string& error);
// The chapter's versions, those newer than CURRENT left out ("" keeps all), grouped as described above.
Page build(const guide::Document& doc, std::string_view current, int detailed = kDetailed);
// "Title: one line" -> {"Title", "one line"}; no colon: {"", text}.
std::pair<std::string, std::string> splitHighlight(std::string_view highlight);

} // namespace sf::whatsnew
