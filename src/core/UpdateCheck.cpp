// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/UpdateCheck.h"

#include "core/Json.h"

#include <algorithm>
#include <cctype>

namespace sf {

namespace {

bool httpsUrl(const std::string& s) { return s.rfind("https://", 0) == 0 && s.size() > 8; }

std::vector<uint64_t> versionParts(const std::string& v) {
    std::vector<uint64_t> parts;
    size_t i = v.size() > 1 && (v[0] == 'v' || v[0] == 'V') && std::isdigit(static_cast<unsigned char>(v[1])) ? 1 : 0;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
        uint64_t n = 0;
        for (; i < v.size() && std::isdigit(static_cast<unsigned char>(v[i])); ++i)
            n = n < (UINT64_MAX - 9) / 10 ? n * 10 + uint64_t(v[i] - '0') : n;
        parts.push_back(n);
        if (i + 1 < v.size() && v[i] == '.' && std::isdigit(static_cast<unsigned char>(v[i + 1]))) ++i;
        else break;
    }
    return parts;
}

// The prerelease after the numbers ("beta.1" in "0.11.0-beta.1"), "" when there is none.
std::string prerelease(const std::string& v) {
    const size_t dash = v.find('-');
    if (dash == std::string::npos || versionParts(v.substr(0, dash)).empty()) return {};
    return v.substr(dash + 1, v.find('+', dash) - dash - 1);
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

// SemVer's prerelease order: dot-separated, numbers numerically and below words, words as text, fewer parts first.
int comparePrerelease(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return a.empty() == b.empty() ? 0 : a.empty() ? 1 : -1; // none sorts above any
    size_t i = 0, j = 0;
    while (i <= a.size() && j <= b.size()) {
        const size_t ei = std::min(a.find('.', i), a.size()), ej = std::min(b.find('.', j), b.size());
        const std::string x = a.substr(i, ei - i), y = b.substr(j, ej - j);
        if (x != y) {
            const bool nx = allDigits(x), ny = allDigits(y);
            if (nx && ny) {
                if (x.size() != y.size()) return x.size() < y.size() ? -1 : 1;
                return x < y ? -1 : 1;
            }
            if (nx != ny) return nx ? -1 : 1;
            return x < y ? -1 : 1;
        }
        i = ei + 1, j = ej + 1;
        if (i > a.size() || j > b.size()) return i > a.size() && j > b.size() ? 0 : i > a.size() ? -1 : 1;
    }
    return 0;
}

// One release out of the feed (its top level or a channel's entry); false when it lacks a version or https link.
bool readRelease(const Json& j, UpdateInfo& info) {
    if (!j.isObject()) return false;
    if (j["version"].isString()) info.version = j["version"].asString();
    if (j["download"].isString()) info.downloadUrl = j["download"].asString();
    if (j["notes"].isString()) info.notesUrl = j["notes"].asString();
    if (versionParts(info.version).empty() || !httpsUrl(info.downloadUrl)) return false;
    if (!httpsUrl(info.notesUrl)) info.notesUrl = info.downloadUrl;
    for (const auto& h : j["highlights"].items())
        if (h.isString() && !h.asString().empty() && info.highlights.size() < 4)
            info.highlights.push_back(h.asString().substr(0, 200));
    return true;
}

} // namespace

bool parseUpdateFeed(const std::string& json, UpdateInfo& out, std::string& error, UpdateChannel channel) {
    Json j;
    if (!Json::parse(json, j, error)) return false;
    if (!j.isObject()) {
        error = "the update feed is not a JSON object";
        return false;
    }
    UpdateInfo info;
    if (!readRelease(j, info)) {
        error = versionParts(info.version).empty() ? "the update feed has no version"
                                                   : "the update feed has no https download link";
        return false;
    }
    if (channel == UpdateChannel::Preview && j["channels"].isObject()) {
        UpdateInfo preview;
        if (readRelease(j["channels"]["preview"], preview) && compareVersions(preview.version, info.version) > 0)
            info = std::move(preview); // never older than stable: once stable passes the beta, it is offered
    }
    out = std::move(info);
    return true;
}

int compareVersions(const std::string& a, const std::string& b) {
    const auto pa = versionParts(a), pb = versionParts(b);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const uint64_t x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return comparePrerelease(prerelease(a), prerelease(b));
}

std::string versionLabel(const std::string& version, const std::string& prerelease) {
    if (prerelease.empty()) return version;
    std::string label = version;
    size_t i = 0;
    while (i <= prerelease.size()) { // "beta.1" -> " Beta 1", "rc.2" -> " RC 2"
        const size_t e = std::min(prerelease.find('.', i), prerelease.size());
        std::string part = prerelease.substr(i, e - i);
        if (part == "rc") part = "RC";
        else if (!part.empty()) part[0] = char(std::toupper(static_cast<unsigned char>(part[0])));
        if (!part.empty()) label += " " + part;
        i = e + 1;
    }
    return label;
}

} // namespace sf
