// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Member builds: Member and Studio members have read access (org team "insiders") to the private repository
// sculptforge/sculptforge-insiders, where the members' betas are published. Preferences > Updates > Member builds
// signs in with GitHub's device flow for the GitHub App "SculptForge Member Builds", which is installed only on that
// repository with Contents: read. Its user-to-server token reaches only repositories both the app and the user can
// access, so it can read that repository's releases and nothing else. No server of ours, no client secret (the
// device flow and its refresh don't use one), and the app's private key is never used.
// The token lives in the OS keychain (core/Keychain.h), never in the preferences or logs. Member builds are checked
// exactly like public ones (core/SelfUpdate.h): the same SHA256SUMS.txt + Ed25519 signature, the same key.
#pragma once

#include "core/UpdateCheck.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf::member {

inline constexpr const char* kClientId = "Iv23liK7goArrLDUGKEk"; // public (GitHub App "SculptForge Member Builds")
inline constexpr const char* kDeviceCodeUrl = "https://github.com/login/device/code";
inline constexpr const char* kTokenUrl = "https://github.com/login/oauth/access_token";
inline constexpr const char* kVerifyUrl = "https://github.com/login/device"; // where the user types the code
inline constexpr const char* kReleasesUrl = "https://api.github.com/repos/sculptforge/sculptforge-insiders/releases?per_page=20";
inline constexpr const char* kUserUrl = "https://api.github.com/user";
inline constexpr const char* kAssetUrlPrefix = "https://api.github.com/repos/sculptforge/sculptforge-insiders/releases/assets/";
inline constexpr const char* kKeychainAccount = "member-builds"; // (core/Keychain.h)
inline constexpr size_t kMaxTokenLength = 4096; // longest access/refresh/device code accepted (GitHub: ~520 today)

// The member API URLs the app may call with the token: the release list, one asset by number, the signed-in user.
bool isMemberApiUrl(const std::string& url);
bool isMemberAssetUrl(const std::string& url);
// Headers for a request to `url`: the token only for the member API URLs above (never for a redirect's target).
std::vector<std::string> headersFor(const std::string& url, const std::string& accessToken);

struct DeviceCode {
    std::string deviceCode; // secret-ish: only sent back to GitHub
    std::string userCode;   // shown: "WDJB-MJHT"
    int interval = 5;       // seconds between polls
    int64_t expiresAt = 0;  // unix time
};
bool parseDeviceCode(const std::string& json, int64_t now, DeviceCode& out, std::string& error);

struct Tokens {
    std::string access, refresh;
    int64_t accessExpiresAt = 0, refreshExpiresAt = 0; // unix time; 0 = doesn't expire
    bool usable(int64_t now) const { return !access.empty() && (accessExpiresAt == 0 || now < accessExpiresAt - 60); }
    bool refreshable(int64_t now) const { return !refresh.empty() && (refreshExpiresAt == 0 || now < refreshExpiresAt); }
};

enum class TokenReply {
    Ok,       // `tokens` filled
    Pending,  // the user hasn't entered the code yet: poll again after the interval
    SlowDown, // poll less often (interval + 5 s)
    Expired,  // the code (or the refresh token) is no longer valid: sign in again
    Denied,   // the user said no
    Error,    // anything else (`error` says what; never the token)
};
// GitHub's answer from kTokenUrl (to a device poll or a refresh).
TokenReply parseTokenReply(const std::string& json, int64_t now, Tokens& tokens, std::string& error);

// Form bodies for kDeviceCodeUrl and kTokenUrl.
std::string deviceCodeBody();
std::string pollBody(const std::string& deviceCode);
std::string refreshBody(const std::string& refreshToken);

// What the keychain holds (a small JSON object), and back.
std::string tokensToSecret(const Tokens& t);
bool tokensFromSecret(const std::string& secret, Tokens& t);

// The newest usable release in the member repository's list (GET kReleasesUrl): drafts skipped, the tag must be a
// safe version ("v0.11.1-preview.3"), and its files are reachable as kAssetUrlPrefix + asset id (UpdateInfo::files).
bool parseMemberReleases(const std::string& json, UpdateInfo& out, std::string& error);

// The login from GET kUserUrl ("" when the answer isn't one).
std::string parseLogin(const std::string& json);

} // namespace sf::member
