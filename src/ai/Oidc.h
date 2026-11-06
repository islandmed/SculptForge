// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Studio sign-in: OpenID Connect with the OAuth 2.0 device authorization grant (RFC 8628) against the studio's
// identity provider (Okta, Microsoft Entra ID, Google Workspace, Auth0, Keycloak, ...). The issuer and client id come
// from the studio's policy (core/ManagedPolicy.h "signIn"); the client is a public one (no secret).
//
// Device flow only: SculptForge shows a short code and opens the provider's page; the artist signs in there, in their
// browser, with the studio's usual login and MFA. No local listener, no redirect URI, no embedded browser, so nothing
// on this computer can intercept the sign-in.
//
// The tokens live in the OS keychain (core/Keychain.h, account "sso/<client id>"), never in files or logs. The access
// token is sent as "Authorization: Bearer" only to the URLs the policy lists in signIn.useFor (default: its
// ai.endpoints): the studio's AI gateway (a LiteLLM proxy) or other studio services, which validate it. SculptForge
// reads the ID token's name and email only to show who is signed in.
#pragma once

#include "ai/Http.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sf::ai::oidc {

struct Discovery {
    std::string issuer, deviceEndpoint, tokenEndpoint;
};
// The provider's /.well-known/openid-configuration. Its issuer must be EXPECTED (a trailing '/' aside), and the
// endpoints https (http only on this computer, for tests); a provider without device authorization is an error.
bool parseDiscovery(const std::string& json, const std::string& expectedIssuer, Discovery& out, std::string& error);
std::string discoveryUrl(const std::string& issuer);

struct DeviceCode {
    std::string deviceCode;              // only sent back to the token endpoint
    std::string userCode;                // shown: "WDJB-MJHT"
    std::string verificationUri;         // where to type it
    std::string verificationUriComplete; // the same with the code filled in ("" if the provider has none)
    int interval = 5;
    int64_t expiresAt = 0;
};
bool parseDeviceCode(const std::string& json, int64_t now, DeviceCode& out, std::string& error);

struct Tokens {
    std::string access, refresh, id;
    int64_t accessExpiresAt = 0; // unix time; 0 = unknown (treated as an hour after it was issued)
    bool usable(int64_t now) const { return !access.empty() && (accessExpiresAt == 0 || now < accessExpiresAt - 60); }
};

enum class TokenReply { Ok, Pending, SlowDown, Expired, Denied, Error };
// The token endpoint's answer to a device poll or a refresh (RFC 8628 3.5, RFC 6749 5.1/5.2).
TokenReply parseTokenReply(long status, const std::string& json, int64_t now, Tokens& tokens, std::string& error);

// Form bodies (application/x-www-form-urlencoded).
// CLIENT_SECRET only for providers that require one from installed apps (Google; "" = left out).
std::string deviceBody(const std::string& clientId, const std::vector<std::string>& scopes, const std::string& audience,
                       const std::string& clientSecret = {});
std::string pollBody(const std::string& clientId, const std::string& deviceCode, const std::string& clientSecret = {});
std::string refreshBody(const std::string& clientId, const std::string& refreshToken, const std::string& clientSecret = {});

// Who is signed in, from the ID token's payload (not verified: display only; services verify the access token).
struct Identity {
    std::string subject, email, name;
    std::string display() const { return !name.empty() ? name : !email.empty() ? email : subject; }
};
Identity parseIdToken(const std::string& idToken);

std::string tokensToSecret(const Tokens& t);
bool tokensFromSecret(const std::string& secret, Tokens& t);

// How requests are made (tests: a mock identity provider).
struct Transport {
    std::function<HttpResponse(const std::string& url)> get;
    std::function<HttpResponse(const std::string& url, const std::string& formBody)> post;
};
void setTransportForTests(Transport t); // {} = the real one
void setSleepForTests(std::function<void(int seconds)> sleep);
void setClockForTests(std::function<int64_t()> now);

// The process's sign-in (the policy's issuer and client id). Blocking calls: worker threads (or the CLI).
enum class State { NotConfigured, SignedOut, SignedIn };
State state();
std::string signedInAs(); // "Ana Ruiz" ("" when signed out)
// Reads the saved tokens from the keychain (start-up).
void loadSaved();
// The whole device flow: discovery, the code (ON_CODE shows it; return false to stop), then polling until the
// artist finishes, says no, the code expires or CANCEL becomes true. True when signed in.
bool signIn(const std::function<bool(const DeviceCode&)>& onCode, const std::function<bool()>& cancel, std::string& error);
void signOut();
// A valid access token, refreshed if it expired ("" when signed out or the refresh failed).
std::string accessToken();
// The bearer header for URL when it is one the policy sends the sign-in to ("" otherwise).
std::string bearerHeaderFor(const std::string& url);
// True when URL gets the sign-in (signIn.useFor, else ai.endpoints).
bool usesSignIn(const std::string& url);

} // namespace sf::ai::oidc
