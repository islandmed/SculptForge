// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Featured.h"

#include "core/Json.h"
#include "core/UpdateCheck.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <initializer_list>

namespace sf {

namespace {

// Feed text as it may be shown: no control characters, cut to `max` bytes on a UTF-8 boundary, trimmed.
std::string shownText(const std::string& in, size_t max) {
    std::string out;
    for (char c : in)
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) out += c;
    if (out.size() > max) {
        size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    const size_t a = out.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    return out.substr(a, out.find_last_not_of(' ') - a + 1);
}

// The lower-case host of an https URL, "" when the URL has a user name, a port, a space, a backslash or a control
// character, or no host at all.
std::string httpsHost(const std::string& url) {
    static const char kScheme[] = "https://";
    if (url.rfind(kScheme, 0) != 0 || url.size() > 2048) return {};
    for (char c : url)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f || c == '\\') return {};
    const size_t start = sizeof kScheme - 1, end = url.find_first_of("/?#", start);
    std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (host.empty() || host.find_first_of("@:[]%") != std::string::npos) return {};
    for (char& c : host) c = char(std::tolower(static_cast<unsigned char>(c)));
    return host;
}

bool hostIsOneOf(const std::string& host, std::initializer_list<const char*> allowed) {
    return !host.empty() && std::any_of(allowed.begin(), allowed.end(), [&](const char* a) { return host == a; });
}

// "https://sculptforge.app/latest.json" -> host sculptforge.app (kUpdateFeedUrl); the mirror's host.
std::string hostOf(const char* url) { return httpsHost(url); }

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

} // namespace

bool featuredUrlAllowed(const std::string& url) {
    return hostIsOneOf(httpsHost(url), {"youtube.com", "www.youtube.com", "m.youtube.com", "youtu.be", "vimeo.com", "www.vimeo.com"});
}

bool featuredThumbAllowed(const std::string& url) {
    const std::string host = httpsHost(url);
    return !host.empty() && (host == hostOf(kUpdateFeedUrl) || host == hostOf(kUpdateFeedMirrorUrl));
}

std::string featuredFeedSource(const std::string& source) {
    static const std::string kLatest = "latest.json";
    if (!endsWith(source, kLatest)) return {};
    if (source.size() > kLatest.size()) {
        const char before = source[source.size() - kLatest.size() - 1];
        if (before != '/' && before != '\\') return {};
    }
    return source.substr(0, source.size() - kLatest.size()) + "featured.json";
}

std::string featuredThumbFile(const std::string& thumbUrl) {
    uint64_t h = 1469598103934665603ull; // FNV-1a
    for (unsigned char c : thumbUrl) h = (h ^ c) * 1099511628211ull;
    char name[40];
    std::snprintf(name, sizeof name, "featured-%016llx.img", static_cast<unsigned long long>(h));
    return name;
}

int64_t featuredDay(int64_t unixSeconds) {
    return unixSeconds >= 0 ? unixSeconds / 86400 : -((-unixSeconds + 86399) / 86400);
}

int64_t featuredParseDay(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return INT64_MIN;
    for (size_t i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return INT64_MIN;
    const int y = std::stoi(s.substr(0, 4)), m = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2));
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return INT64_MIN;
    // days from civil (Howard Hinnant)
    const int yy = y - (m <= 2);
    const int era = yy / 400, yoe = yy - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return int64_t(era) * 146097 + doe - 719468;
}

bool parseFeaturedFeed(const std::string& json, std::vector<FeaturedItem>& out, std::string& error) {
    Json j;
    if (!Json::parse(json, j, error)) return false;
    if (!j.isObject() || !j["featured"].isArray()) {
        error = "the featured feed has no \"featured\" list";
        return false;
    }
    std::vector<FeaturedItem> items;
    for (const Json& e : j["featured"].items()) {
        if (items.size() >= kFeaturedMax) break;
        if (!e.isObject() || !e["creator"].isString() || !e["title"].isString() || !e["url"].isString() ||
            !e["thumb_url"].isString())
            continue;
        FeaturedItem it;
        it.creator = shownText(e["creator"].asString(), 48);
        it.title = shownText(e["title"].asString(), 120);
        it.url = e["url"].asString();
        it.thumbUrl = e["thumb_url"].asString();
        if (it.creator.empty() || it.title.empty() || !featuredUrlAllowed(it.url) || !featuredThumbAllowed(it.thumbUrl))
            continue;
        bool datesOk = true;
        for (const char* key : {"start", "end"}) {
            if (!e[key].isString()) continue; // (a date that is there but wrong skips the entry: it would show too long)
            const int64_t day = featuredParseDay(e[key].asString());
            if (day == INT64_MIN) datesOk = false;
            else (key[0] == 's' ? it.startDay : it.endDay) = day;
        }
        if (datesOk) items.push_back(std::move(it));
    }
    out = std::move(items);
    return true;
}

int pickFeatured(const std::vector<FeaturedItem>& items, int64_t day, const std::vector<std::string>& hidden,
                 uint64_t launch) {
    std::vector<int> shown;
    for (size_t i = 0; i < items.size(); ++i) {
        const FeaturedItem& it = items[i];
        if (day < it.startDay || day > it.endDay) continue;
        if (std::find(hidden.begin(), hidden.end(), it.url) != hidden.end()) continue;
        shown.push_back(int(i));
    }
    return shown.empty() ? -1 : shown[launch % shown.size()];
}

} // namespace sf
