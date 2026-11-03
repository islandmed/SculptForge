// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/WhatsNew.h"

#include <algorithm>
#include <cstdlib>

namespace sf::whatsnew {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::vector<int> parts(std::string_view v) {
    std::vector<int> out;
    while (!v.empty()) {
        const size_t dot = v.find('.');
        out.push_back(std::atoi(std::string(v.substr(0, dot)).c_str()));
        if (dot == std::string_view::npos) break;
        v.remove_prefix(dot + 1);
    }
    return out;
}

std::string series(std::string_view v) { // "0.10.4" -> "0.10"
    const std::vector<int> p = parts(v);
    return std::to_string(p.empty() ? 0 : p[0]) + "." + std::to_string(p.size() > 1 ? p[1] : 0);
}

} // namespace

int compareVersions(std::string_view a, std::string_view b) {
    std::vector<int> x = parts(a), y = parts(b);
    const size_t n = std::max(x.size(), y.size());
    x.resize(n, 0);
    y.resize(n, 0);
    for (size_t i = 0; i < n; ++i)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

std::pair<std::string, std::string> splitHighlight(std::string_view h) {
    const size_t colon = h.find(": ");
    if (colon == std::string_view::npos) return {"", std::string(h)};
    return {std::string(trim(h.substr(0, colon))), std::string(trim(h.substr(colon + 1)))};
}

bool parseFeatures(std::string_view text, Version& v, std::string& error) {
    size_t lineNo = 0;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        const std::string_view line = trim(text.substr(0, nl));
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        ++lineNo;
        if (line.empty() || line.front() == '#') continue;
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            error = "line " + std::to_string(lineNo) + ": expected 'key: value'";
            return false;
        }
        const std::string_view key = trim(line.substr(0, colon));
        const std::string value(trim(line.substr(colon + 1)));
        if (key == "title") {
            v.cards.push_back({value, "", "", "", ""});
            continue;
        }
        if (key == "line" || key == "image" || key == "action" || key == "link") {
            if (v.cards.empty()) {
                error = "line " + std::to_string(lineNo) + ": '" + std::string(key) + "' before the card's 'title'";
                return false;
            }
            Card& c = v.cards.back();
            (key == "line" ? c.line : key == "image" ? c.image : key == "action" ? c.action : c.link) = value;
        } else if (key == "date") {
            v.date = value;
        } else if (key == "tagline") {
            v.tagline = value;
        } else if (key == "highlight") {
            v.highlights.push_back(value);
        } else {
            error = "line " + std::to_string(lineNo) + ": unknown key '" + std::string(key) + "'";
            return false;
        }
    }
    for (const Card& c : v.cards)
        if (c.title.empty() || c.line.empty() || c.action.empty()) {
            error = "card '" + c.title + "' needs a title, a line and an action";
            return false;
        }
    return true;
}

Page build(const guide::Document& doc, std::string_view current, int detailed) {
    Page page;
    const int n = int(doc.blocks.size());
    page.introEnd = n;
    for (int i = 0; i < n; ++i) {
        const guide::Block& b = doc.blocks[size_t(i)];
        if (b.kind != guide::BlockKind::Heading || b.level != 2) continue;
        const std::string title = guide::plainText(b.spans);
        if (title.rfind("Version ", 0) != 0) continue;
        page.introEnd = std::min(page.introEnd, i);
        Version v;
        v.version = title.substr(8);
        v.anchor = b.anchor;
        v.heading = i;
        v.first = i + 1;
        if (v.first < n && doc.blocks[size_t(v.first)].kind == guide::BlockKind::Code &&
            doc.blocks[size_t(v.first)].lang == "features") {
            std::string error;
            if (!parseFeatures(doc.blocks[size_t(v.first)].text, v, error)) page.problems.push_back("Version " + v.version + ": " + error);
            ++v.first;
        }
        v.end = v.first;
        while (v.end < n && !(doc.blocks[size_t(v.end)].kind == guide::BlockKind::Heading && doc.blocks[size_t(v.end)].level <= 2))
            ++v.end;
        if (!current.empty() && compareVersions(v.version, current) > 0) continue; // not released in this build
        page.versions.push_back(std::move(v));
    }
    page.detailed = std::min(detailed, int(page.versions.size()));

    // Folded versions: the next kSeriesGroups series get a group each, everything older one last group.
    std::vector<std::string> seriesSeen;
    for (int i = page.detailed; i < int(page.versions.size()); ++i) {
        const std::string s = series(page.versions[size_t(i)].version);
        if (std::find(seriesSeen.begin(), seriesSeen.end(), s) == seriesSeen.end()) seriesSeen.push_back(s);
        const int slot = std::min(int(std::find(seriesSeen.begin(), seriesSeen.end(), s) - seriesSeen.begin()), kSeriesGroups);
        if (slot >= int(page.groups.size())) page.groups.resize(size_t(slot) + 1);
        page.groups[size_t(slot)].versions.push_back(i);
    }
    for (size_t g = 0; g < page.groups.size(); ++g) {
        Group& group = page.groups[g];
        const Version& newest = page.versions[size_t(group.versions.front())];
        const Version& oldest = page.versions[size_t(group.versions.back())];
        if (group.versions.size() == 1) {
            group.label = newest.version;
        } else if (int(g) == kSeriesGroups) {
            group.label = series(newest.version) + " and earlier";
        } else {
            bool partial = false; // the series continues in a detailed version
            for (int i = 0; i < page.detailed; ++i) partial |= series(page.versions[size_t(i)].version) == series(newest.version);
            group.label = partial ? oldest.version + " to " + newest.version : series(newest.version) + ".x";
        }
        group.anchor = "versions-" + guide::slugify(group.label);
        // Highlights: each version's first, then their second ones..., up to kMaxHighlights; bigger releases (more
        // highlights) pick first, ties newest first. Shown newest first.
        std::vector<int> order = group.versions;
        std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
            return page.versions[size_t(x)].highlights.size() > page.versions[size_t(y)].highlights.size();
        });
        std::vector<std::pair<int, std::string>> picked; // (version index, highlight)
        for (size_t round = 0; picked.size() < size_t(kMaxHighlights); ++round) {
            bool any = false;
            for (int vi : order) {
                const auto& h = page.versions[size_t(vi)].highlights;
                if (round >= h.size()) continue;
                any = true;
                if (picked.size() < size_t(kMaxHighlights)) picked.emplace_back(vi, h[round]);
            }
            if (!any) break;
        }
        std::stable_sort(picked.begin(), picked.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        for (auto& p : picked) group.highlights.push_back(std::move(p.second));
        for (const std::string& h : group.highlights) {
            const std::string t = splitHighlight(h).first;
            if (t.empty()) continue;
            group.summary += (group.summary.empty() ? "" : ", ") + t;
        }
    }
    return page;
}

} // namespace sf::whatsnew
