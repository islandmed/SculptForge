// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What's New (docs/guide/whats-new.md) as data: each "## Version X" section may start with a ```features block,
//
//   ```features
//   date: 2026-11-01
//   tagline: One line under the popup's title.
//   theme: Splats, straight into Blender.   (optional: the hero band's headline, at most 6 words; else the tagline)
//   hero: images/wn-0127-hero.jpg         (the hero band's picture, 2880x708: the bust in forge light, no text)
//   highlight: Short title: one line, used when the version is folded into a group
//   title: A bold 3-5 word title          <- each title starts a feature card
//   line: One sentence (at most 90 characters).
//   icon: sparkles                        (the Lucide icon its picture shows; tools/whats_new_cards.py draws it)
//   image: images/wn-0127-splats.jpg      (1200x680, drawn by tools/whats_new_cards.py from the icon: no captures, no text)
//   stat: 33 fps                          (optional: a measured win, shown big above the title)
//   action: look:new                      (what Try it does; see App::runWhatsNewAction)
//   link: interface.md#looks-and-layouts  (optional: where the website's card links; guide: actions link there anyway)
//   clip: images/wn-0111-lucy.mp4         (optional: the website plays it in the card, the image as its poster)
//   frames: images/wn-0111-lucy-frames.jpg 8x10 8  (optional: the app plays the same clip from one picture of
//                                          8 x 10 frames, left to right then down, at 8 a second; fitted, not cropped)
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
    std::string icon, stat; // the picture's icon (for the script), a measured win ("33 fps")
    std::string clip;   // the website's video
    std::string frames; // the app's frame sheet (empty: the still image)
    int frameCols = 0, frameRows = 0, frameFps = 0;
};

// "images/x.jpg 8x6 5" into C's frames, frameCols, frameRows, frameFps; false on anything else.
bool parseFrames(std::string_view value, Card& c);

struct Version {
    std::string version; // "0.10.7"
    std::string anchor;  // the heading's anchor, "version-0107"
    std::string date, tagline;
    std::string theme, hero; // the hero band's headline and picture
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
