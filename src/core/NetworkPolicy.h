// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Studio mode's network gate (a studio build, a studio profile or --offline; core/StudioProfile.h). Off (the
// default) changes nothing. On, the process opens no outside connection:
//   - HTTP (ai/Http.cpp, every request the app, CLI, MCP and Python make) goes only to the URLs the profile allows
//     (its update channel or mirror, and anything it lists), never follows a redirect, and is refused before
//     anything is sent otherwise;
//   - sockets (link/Socket.cpp) open only on loopback unless the profile allows the local network (Unreal link,
//     phone capture);
//   - Discord presence, the community brush gallery and the AI features are off (they are refused here too).
// There is no telemetry in SculptForge to turn off.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sf::net {

struct NetworkPolicy {
    bool offline = false;              // studio mode: everything below applies
    std::vector<std::string> allow;    // URL prefixes ("https://mirror.studio.internal/sculptforge/")
    bool localNetwork = false;         // sockets beyond loopback (LAN bridges, phone capture)
};

// Set once at start-up (before any worker thread runs); reads are lock-free after that.
void setNetworkPolicy(const NetworkPolicy& policy);
NetworkPolicy networkPolicy();
bool offlineMode();

// "https://Host:443/a/b?q" -> scheme "https", host "host", port 443, path "/a/b?q". False for anything else
// (no scheme, user info, empty host, a bad port).
struct UrlParts {
    std::string scheme, host, path;
    int port = 0;
};
bool parseUrl(const std::string& url, UrlParts& out);
// True when URL starts with PREFIX in the sense of a channel: same scheme, host (any case) and port, and the path
// begins with the prefix's path at a '/' boundary ("https://a/x/" allows "https://a/x/y", not "https://a/xy").
bool urlUnderPrefix(const std::string& url, const std::string& prefix);

// May the process request URL? Always true when not offline. Refusals count (blockedCount) and say why.
bool urlAllowed(const std::string& url, std::string* why = nullptr);
// May a socket connect to (or listen on) ADDRESS (a numeric IPv4 address or "localhost")?
bool socketAllowed(const std::string& address, std::string* why = nullptr);
// Multicast (Unreal discovery) is always the local network.
bool multicastAllowed(std::string* why = nullptr);
// Local IPC to another app (Discord): never in studio mode.
bool localIpcAllowed();

uint64_t blockedCount();
void resetBlockedCount();

bool isLoopbackAddress(const std::string& address);

} // namespace sf::net
