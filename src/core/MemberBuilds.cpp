// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MemberBuilds.h"

#include "core/Json.h"
#include "core/SelfUpdate.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sf::member {

namespace {

bool allDigits(const std::string& s) {
    return !s.empty() && s.size() <= 20 && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

std::string formEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else out += '%', out += hex[c >> 4], out += hex[c & 15];
    }
    return out;
}

std::string str(const Json& j, const char* key) { return j[key].isString() ? j[key].asString() : std::string(); }

// Tokens are short printable strings; anything else is not a token.
bool tokenLike(const std::string& s) {
    return !s.empty() && s.size() <= 512 &&
           std::all_of(s.begin(), s.end(), [](char c) { return c > 32 && c < 127 && c != '"' && c != '\\'; });
}

} // namespace

bool isMemberAssetUrl(const std::string& url) {
    const std::string prefix = kAssetUrlPrefix;
    return url.size() > prefix.size() && url.compare(0, prefix.size(), prefix) == 0 && allDigits(url.substr(prefix.size()));
}

bool isMemberApiUrl(const std::string& url) { return url == kReleasesUrl || url == kUserUrl || isMemberAssetUrl(url); }

std::vector<std::string> headersFor(const std::string& url, const std::string& accessToken) {
    if (!isMemberApiUrl(url) || accessToken.empty()) return {};
    return {"Authorization: Bearer " + accessToken,
            isMemberAssetUrl(url) ? "Accept: application/octet-stream" : "Accept: application/vnd.github+json",
            "X-GitHub-Api-Version: 2022-11-28"};
}

bool parseDeviceCode(const std::string& json, int64_t now, DeviceCode& out, std::string& error) {
    Json j;
    if (!Json::parse(json, j, error) || !j.isObject()) {
        if (error.empty()) error = "GitHub's answer isn't JSON";
        return false;
    }
    if (j["error"].isString()) {
        error = j["error"].asString() == "device_flow_disabled" ? "sign-in is switched off for the app"
                                                                : "GitHub refused: " + j["error"].asString().substr(0, 60);
        return false;
    }
    out.deviceCode = str(j, "device_code");
    out.userCode = str(j, "user_code");
    out.interval = std::clamp(int(j["interval"].asNumber(5)), 1, 60);
    out.expiresAt = now + int64_t(std::clamp(j["expires_in"].asNumber(900), 60.0, 3600.0));
    // The page to open is ours to choose (kVerifyUrl), not the answer's: it is checked to be the same place.
    const std::string uri = str(j, "verification_uri");
    if (!tokenLike(out.deviceCode) || out.userCode.empty() || out.userCode.size() > 16 || uri != kVerifyUrl) {
        error = "GitHub's sign-in answer is not what was expected";
        return false;
    }
    return true;
}

TokenReply parseTokenReply(const std::string& json, int64_t now, Tokens& tokens, std::string& error) {
    Json j;
    std::string err;
    if (!Json::parse(json, j, err) || !j.isObject()) {
        error = "GitHub's answer isn't JSON";
        return TokenReply::Error;
    }
    const std::string e = str(j, "error");
    if (e == "authorization_pending") return TokenReply::Pending;
    if (e == "slow_down") return TokenReply::SlowDown;
    if (e == "expired_token" || e == "bad_refresh_token" || e == "token_expired") return TokenReply::Expired;
    if (e == "access_denied") return TokenReply::Denied;
    if (!e.empty()) {
        error = "GitHub refused: " + e.substr(0, 60);
        return TokenReply::Error;
    }
    Tokens t;
    t.access = str(j, "access_token");
    t.refresh = str(j, "refresh_token");
    if (!tokenLike(t.access) || (!t.refresh.empty() && !tokenLike(t.refresh)) ||
        (j["token_type"].isString() && j["token_type"].asString() != "bearer")) {
        error = "GitHub's answer has no usable token";
        return TokenReply::Error;
    }
    if (j["expires_in"].isNumber()) t.accessExpiresAt = now + int64_t(j["expires_in"].asNumber(0));
    if (j["refresh_token_expires_in"].isNumber()) t.refreshExpiresAt = now + int64_t(j["refresh_token_expires_in"].asNumber(0));
    tokens = std::move(t);
    return TokenReply::Ok;
}

std::string deviceCodeBody() { return "client_id=" + formEncode(kClientId); }
std::string pollBody(const std::string& deviceCode) {
    return "client_id=" + formEncode(kClientId) + "&device_code=" + formEncode(deviceCode) +
           "&grant_type=" + formEncode("urn:ietf:params:oauth:grant-type:device_code");
}
std::string refreshBody(const std::string& refreshToken) {
    return "client_id=" + formEncode(kClientId) + "&grant_type=refresh_token&refresh_token=" + formEncode(refreshToken);
}

std::string tokensToSecret(const Tokens& t) {
    Json j = Json::object();
    j.set("access", t.access);
    j.set("refresh", t.refresh);
    j.set("access_expires", double(t.accessExpiresAt));
    j.set("refresh_expires", double(t.refreshExpiresAt));
    return j.dump();
}

bool tokensFromSecret(const std::string& secret, Tokens& t) {
    Json j;
    std::string err;
    if (!Json::parse(secret, j, err) || !j.isObject()) return false;
    Tokens r;
    r.access = str(j, "access");
    r.refresh = str(j, "refresh");
    r.accessExpiresAt = int64_t(j["access_expires"].asNumber(0));
    r.refreshExpiresAt = int64_t(j["refresh_expires"].asNumber(0));
    if (!tokenLike(r.access) || (!r.refresh.empty() && !tokenLike(r.refresh))) return false;
    t = std::move(r);
    return true;
}

bool parseMemberReleases(const std::string& json, UpdateInfo& out, std::string& error) {
    Json j;
    if (!Json::parse(json, j, error) || !j.isArray()) {
        if (error.empty()) error = "the member release list isn't one";
        return false;
    }
    bool found = false;
    for (const Json& r : j.items()) {
        if (!r.isObject() || r["draft"].asBool(false)) continue;
        std::string tag = str(r, "tag_name");
        if (tag.size() < 2 || tag[0] != 'v' || !update::safeVersion(tag.substr(1))) continue;
        const std::string version = tag.substr(1);
        if (found && compareVersions(version, out.version) <= 0) continue;
        UpdateInfo info;
        info.version = version;
        info.member = true;
        info.downloadUrl = "https://github.com/islandmed/sculptforge-insiders/releases/tag/" + tag; // (built here, not read)
        info.notesUrl = info.downloadUrl;
        info.highlights.push_back("Member build (the next release, early)");
        for (const Json& a : r["assets"].items()) {
            const std::string name = str(a, "name");
            const double id = a["id"].asNumber(-1);
            if (name.empty() || name.size() > 128 || !(id >= 1) || id > 9e15 || std::floor(id) != id) continue;
            info.files[name] = std::string(kAssetUrlPrefix) + std::to_string(int64_t(id));
        }
        out = std::move(info);
        found = true;
    }
    if (!found) error = "no member build yet";
    return found;
}

std::string parseLogin(const std::string& json) {
    Json j;
    std::string err;
    if (!Json::parse(json, j, err) || !j.isObject()) return {};
    const std::string login = str(j, "login");
    const bool ok = !login.empty() && login.size() <= 39 &&
                    std::all_of(login.begin(), login.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; });
    return ok ? login : std::string();
}

} // namespace sf::member
