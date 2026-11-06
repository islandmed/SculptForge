// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Oidc.h"

#include "core/Json.h"
#include "core/Keychain.h"
#include "core/ManagedPolicy.h"
#include "core/NetworkPolicy.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <thread>

namespace sf::ai::oidc {

namespace {

std::string formEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += char(c);
        else if (c == ' ')
            out += '+';
        else
            out += '%', out += hex[c >> 4], out += hex[c & 15];
    }
    return out;
}

bool endpointOk(const std::string& url) {
    net::UrlParts u;
    if (!net::parseUrl(url, u)) return false;
    return u.scheme == "https" || (u.scheme == "http" && net::isLoopbackAddress(u.host));
}

std::string trimSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string base64UrlDecode(const std::string& in) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-' || c == '+') return 62;
        if (c == '_' || c == '/') return 63;
        return -1;
    };
    std::string out;
    int bits = 0, acc = 0;
    for (char c : in) {
        const int v = value(c);
        if (v < 0) break; // '=' padding or anything else ends it
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((acc >> bits) & 0xff);
        }
    }
    return out;
}

// Safe for every keychain (Windows target names are ASCII here).
std::string accountFor(const std::string& clientId) {
    std::string a = "sso/";
    for (char c : clientId) a += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') ? c : '_';
    return a;
}

std::mutex gMutex;
Transport gTestTransport;
std::function<void(int)> gSleep;
std::function<int64_t()> gClock;
Tokens gTokens;
bool gLoaded = false;

int64_t now() {
    if (gClock) return gClock();
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void sleepFor(int seconds) {
    if (gSleep) gSleep(seconds);
    else std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

HttpResponse doGetBody(const std::string& url) {
    if (gTestTransport.get) return gTestTransport.get(url);
    // The discovery document: this one URL, no redirect followed, at most 1 MB.
    std::string body;
    HttpResponse r = httpGetRestricted(
        url, [&url](const std::string& u) { return u == url; }, 15,
        [&body](const char* d, size_t n) {
            body.append(d, n);
            return body.size() < (1u << 20);
        });
    r.body = body;
    return r;
}

HttpResponse doPost(const std::string& url, const std::string& body) {
    if (gTestTransport.post) return gTestTransport.post(url, body);
    return httpPostNoRedirect(url, {"Content-Type: application/x-www-form-urlencoded", "Accept: application/json"}, body, 20);
}

bool discover(const policy::Policy& p, Discovery& d, std::string& error) {
    const std::string url = discoveryUrl(p.issuer);
    if (std::string why; !net::urlAllowed(url, &why)) return (error = "The sign-in page can't be reached: " + why, false);
    HttpResponse r = doGetBody(url);
    if (r.status != 200) {
        error = r.status ? "The identity provider answered " + std::to_string(r.status) + " for its configuration"
                         : "Can't reach the identity provider" + (r.error.empty() ? std::string() : ": " + r.error);
        return false;
    }
    return parseDiscovery(r.body, p.issuer, d, error);
}

void saveLocked(const policy::Policy& p) { keychain::store(accountFor(p.clientId), tokensToSecret(gTokens)); }

} // namespace

std::string discoveryUrl(const std::string& issuer) { return trimSlash(issuer) + "/.well-known/openid-configuration"; }

bool parseDiscovery(const std::string& json, const std::string& expectedIssuer, Discovery& out, std::string& error) {
    Json j;
    if (!Json::parse(json, j, error) || !j.isObject()) return (error = "The identity provider's configuration isn't JSON", false);
    Discovery d;
    d.issuer = j["issuer"].asString();
    d.deviceEndpoint = j["device_authorization_endpoint"].asString();
    d.tokenEndpoint = j["token_endpoint"].asString();
    if (trimSlash(d.issuer) != trimSlash(expectedIssuer))
        return (error = "The identity provider names another issuer (" + d.issuer.substr(0, 120) + "): check signIn.issuer", false);
    if (d.deviceEndpoint.empty())
        return (error = "This identity provider doesn't offer device sign-in: turn on the device authorization grant "
                        "for the app",
                false);
    if (!endpointOk(d.deviceEndpoint) || !endpointOk(d.tokenEndpoint))
        return (error = "The identity provider's endpoints must be https", false);
    out = d;
    return true;
}

bool parseDeviceCode(const std::string& json, int64_t t, DeviceCode& out, std::string& error) {
    Json j;
    if (!Json::parse(json, j, error) || !j.isObject()) return (error = "Unexpected answer from the identity provider", false);
    if (j["error"].isString()) {
        error = "The identity provider refused: " + j["error"].asString().substr(0, 80);
        if (j["error_description"].isString()) error += " (" + j["error_description"].asString().substr(0, 160) + ")";
        return false;
    }
    DeviceCode d;
    d.deviceCode = j["device_code"].asString();
    d.userCode = j["user_code"].asString();
    // (Google calls it verification_url)
    d.verificationUri = j["verification_uri"].isString() ? j["verification_uri"].asString() : j["verification_url"].asString();
    d.verificationUriComplete = j["verification_uri_complete"].asString();
    d.interval = std::max(1, int(j["interval"].asNumber(5)));
    d.expiresAt = t + int64_t(j["expires_in"].asNumber(600));
    if (d.deviceCode.empty() || d.userCode.empty() || d.userCode.size() > 32 || !endpointOk(d.verificationUri))
        return (error = "Unexpected answer from the identity provider", false);
    if (!d.verificationUriComplete.empty() && !endpointOk(d.verificationUriComplete)) d.verificationUriComplete.clear();
    out = d;
    return true;
}

TokenReply parseTokenReply(long status, const std::string& json, int64_t t, Tokens& tokens, std::string& error) {
    Json j;
    if (!Json::parse(json, j, error) || !j.isObject()) {
        error = "Unexpected answer from the identity provider (HTTP " + std::to_string(status) + ")";
        return TokenReply::Error;
    }
    if (j["error"].isString()) {
        const std::string e = j["error"].asString();
        if (e == "authorization_pending") return TokenReply::Pending;
        if (e == "slow_down") return TokenReply::SlowDown;
        if (e == "expired_token" || e == "invalid_grant") return (error = "The sign-in expired: sign in again", TokenReply::Expired);
        if (e == "access_denied") return (error = "Sign-in was declined", TokenReply::Denied);
        error = "The identity provider refused: " + e.substr(0, 80);
        return TokenReply::Error;
    }
    if (status != 200 || !j["access_token"].isString() || j["access_token"].asString().empty()) {
        error = "Unexpected answer from the identity provider (HTTP " + std::to_string(status) + ")";
        return TokenReply::Error;
    }
    const std::string type = j["token_type"].asString();
    if (!type.empty() && type != "Bearer" && type != "bearer") return (error = "Unsupported token type", TokenReply::Error);
    Tokens out;
    out.access = j["access_token"].asString();
    out.refresh = j["refresh_token"].isString() ? j["refresh_token"].asString() : tokens.refresh; // (a refresh may not rotate it)
    out.id = j["id_token"].isString() ? j["id_token"].asString() : tokens.id;
    out.accessExpiresAt = t + int64_t(j["expires_in"].asNumber(3600));
    tokens = out;
    return TokenReply::Ok;
}

namespace {
std::string secretPart(const std::string& clientSecret) {
    return clientSecret.empty() ? std::string() : "&client_secret=" + formEncode(clientSecret);
}
} // namespace

std::string deviceBody(const std::string& clientId, const std::vector<std::string>& scopes, const std::string& audience,
                       const std::string& clientSecret) {
    std::string scope;
    for (const std::string& s : scopes.empty() ? std::vector<std::string>{"openid", "profile", "email", "offline_access"} : scopes)
        scope += (scope.empty() ? "" : " ") + s;
    std::string b = "client_id=" + formEncode(clientId) + "&scope=" + formEncode(scope);
    if (!audience.empty()) b += "&audience=" + formEncode(audience); // (Auth0: the API the token is for)
    return b + secretPart(clientSecret);
}

std::string pollBody(const std::string& clientId, const std::string& deviceCode, const std::string& clientSecret) {
    return "grant_type=" + formEncode("urn:ietf:params:oauth:grant-type:device_code") + "&device_code=" +
           formEncode(deviceCode) + "&client_id=" + formEncode(clientId) + secretPart(clientSecret);
}

std::string refreshBody(const std::string& clientId, const std::string& refreshToken, const std::string& clientSecret) {
    return "grant_type=refresh_token&refresh_token=" + formEncode(refreshToken) + "&client_id=" + formEncode(clientId) +
           secretPart(clientSecret);
}

Identity parseIdToken(const std::string& idToken) {
    Identity id;
    const size_t a = idToken.find('.'), b = a == std::string::npos ? a : idToken.find('.', a + 1);
    if (b == std::string::npos) return id;
    Json j;
    std::string err;
    if (!Json::parse(base64UrlDecode(idToken.substr(a + 1, b - a - 1)), j, err) || !j.isObject()) return id;
    auto clean = [](const std::string& s) {
        std::string o;
        for (unsigned char c : s)
            if (c >= 0x20 && c != 0x7f && o.size() < 80) o += char(c);
        return o;
    };
    id.subject = clean(j["sub"].asString());
    id.email = clean(j["email"].asString());
    id.name = clean(j["name"].isString() ? j["name"].asString() : j["preferred_username"].asString());
    return id;
}

std::string tokensToSecret(const Tokens& t) {
    Json j = Json::object();
    j.set("access", t.access);
    j.set("refresh", t.refresh);
    j.set("id", t.id);
    j.set("expires", double(t.accessExpiresAt));
    return j.dump();
}

bool tokensFromSecret(const std::string& secret, Tokens& t) {
    Json j;
    std::string err;
    if (!Json::parse(secret, j, err) || !j.isObject()) return false;
    Tokens out;
    out.access = j["access"].asString();
    out.refresh = j["refresh"].asString();
    out.id = j["id"].asString();
    out.accessExpiresAt = int64_t(j["expires"].asNumber(0));
    if (out.access.empty() && out.refresh.empty()) return false;
    t = out;
    return true;
}

void setTransportForTests(Transport t) {
    std::lock_guard lock(gMutex);
    gTestTransport = std::move(t);
    gTokens = {};
    gLoaded = true; // (tests start signed out: never the real keychain's tokens)
}
void setSleepForTests(std::function<void(int)> sleep) { gSleep = std::move(sleep); }
void setClockForTests(std::function<int64_t()> clock) { gClock = std::move(clock); }

State state() {
    const policy::Policy& p = policy::current();
    if (!p.signInConfigured()) return State::NotConfigured;
    std::lock_guard lock(gMutex);
    return gTokens.access.empty() && gTokens.refresh.empty() ? State::SignedOut : State::SignedIn;
}

std::string signedInAs() {
    std::lock_guard lock(gMutex);
    if (gTokens.access.empty() && gTokens.refresh.empty()) return {};
    const std::string d = parseIdToken(gTokens.id).display();
    return d.empty() ? "signed in" : d;
}

void loadSaved() {
    const policy::Policy& p = policy::current();
    if (!p.signInConfigured()) return;
    {
        std::lock_guard lock(gMutex);
        if (gLoaded) return;
    }
    std::string secret;
    Tokens t;
    const bool ok = keychain::load(accountFor(p.clientId), secret) && tokensFromSecret(secret, t);
    std::lock_guard lock(gMutex);
    if (ok && gTokens.access.empty()) gTokens = t;
    gLoaded = true;
}

bool signIn(const std::function<bool(const DeviceCode&)>& onCode, const std::function<bool()>& cancel, std::string& error) {
    const policy::Policy& p = policy::current();
    if (!p.signInConfigured()) return (error = "No studio sign-in is set up (the policy's signIn)", false);
    Discovery d;
    if (!discover(p, d, error)) return false;
    HttpResponse r = doPost(d.deviceEndpoint, deviceBody(p.clientId, p.scopes, p.audience, p.clientSecret));
    if (r.status == 0) return (error = "Can't reach the identity provider" + (r.error.empty() ? "" : ": " + r.error), false);
    DeviceCode code;
    if (!parseDeviceCode(r.body, now(), code, error)) return false;
    if (onCode && !onCode(code)) return (error = "Sign-in cancelled", false);
    int interval = code.interval;
    for (;;) {
        // Sleep in one-second steps so a cancel is quick.
        for (int i = 0; i < interval; ++i) {
            if (cancel && cancel()) return (error = "Sign-in cancelled", false);
            sleepFor(1);
        }
        if (now() >= code.expiresAt) return (error = "The code expired: sign in again", false);
        HttpResponse pr = doPost(d.tokenEndpoint, pollBody(p.clientId, code.deviceCode, p.clientSecret));
        if (pr.status == 0) continue; // the network blinked: keep polling until the code expires
        Tokens t;
        switch (parseTokenReply(pr.status, pr.body, now(), t, error)) {
        case TokenReply::Ok: {
            std::lock_guard lock(gMutex);
            gTokens = t;
            saveLocked(p);
            error.clear();
            return true;
        }
        case TokenReply::Pending: break;
        case TokenReply::SlowDown: interval += 5; break; // RFC 8628 3.5
        default: return false;
        }
    }
}

void signOut() {
    const policy::Policy& p = policy::current();
    std::lock_guard lock(gMutex);
    gTokens = {};
    if (p.signInConfigured()) keychain::erase(accountFor(p.clientId));
}

std::string accessToken() {
    const policy::Policy& p = policy::current();
    if (!p.signInConfigured()) return {};
    loadSaved();
    Tokens t;
    {
        std::lock_guard lock(gMutex);
        t = gTokens;
    }
    if (t.usable(now())) return t.access;
    if (t.refresh.empty()) return {};
    Discovery d;
    std::string error;
    if (!discover(p, d, error)) return {};
    HttpResponse r = doPost(d.tokenEndpoint, refreshBody(p.clientId, t.refresh, p.clientSecret));
    const TokenReply reply = parseTokenReply(r.status, r.body, now(), t, error);
    std::lock_guard lock(gMutex);
    if (reply == TokenReply::Ok) {
        gTokens = t;
        saveLocked(p);
        return t.access;
    }
    if (reply == TokenReply::Expired || reply == TokenReply::Denied) { // the refresh token is gone: signed out
        gTokens = {};
        keychain::erase(accountFor(p.clientId));
    }
    return {};
}

bool usesSignIn(const std::string& url) {
    const policy::Policy& p = policy::current();
    if (!p.signInConfigured()) return false;
    for (const std::string& prefix : p.signInUseFor.empty() ? p.aiEndpoints : p.signInUseFor)
        if (net::urlUnderPrefix(url, prefix)) return true;
    return false;
}

std::string bearerHeaderFor(const std::string& url) {
    if (!usesSignIn(url)) return {};
    const std::string token = accessToken();
    return token.empty() ? std::string() : "Authorization: Bearer " + token;
}

} // namespace sf::ai::oidc
