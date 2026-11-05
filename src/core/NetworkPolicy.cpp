// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/NetworkPolicy.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <mutex>

namespace sf::net {

namespace {

std::mutex gMutex;
std::shared_ptr<const NetworkPolicy> gPolicy = std::make_shared<NetworkPolicy>();
std::atomic<bool> gOffline{false};
std::atomic<uint64_t> gBlocked{0};

std::shared_ptr<const NetworkPolicy> current() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gPolicy;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool refuse(std::string* why, const std::string& text) {
    ++gBlocked;
    if (why) *why = text;
    return false;
}

} // namespace

void setNetworkPolicy(const NetworkPolicy& policy) {
    auto p = std::make_shared<const NetworkPolicy>(policy);
    std::lock_guard<std::mutex> lock(gMutex);
    gPolicy = std::move(p);
    gOffline = policy.offline;
}

NetworkPolicy networkPolicy() { return *current(); }
bool offlineMode() { return gOffline; }

bool parseUrl(const std::string& url, UrlParts& out) {
    const size_t sep = url.find("://");
    if (sep == std::string::npos || sep == 0) return false;
    out.scheme = lower(url.substr(0, sep));
    if (out.scheme != "http" && out.scheme != "https") return false;
    const size_t start = sep + 3;
    size_t end = url.find_first_of("/?#", start);
    if (end == std::string::npos) end = url.size();
    const std::string authority = url.substr(start, end - start);
    // No user info (a "trusted.host@evil.host" trick), no IPv6 literals, no backslashes or spaces anywhere.
    if (authority.empty() || authority.find_first_of("@[]\\ ") != std::string::npos) return false;
    if (url.find_first_of("\\ \t\r\n") != std::string::npos) return false;
    const size_t colon = authority.find(':');
    out.host = lower(authority.substr(0, colon));
    if (out.host.empty()) return false;
    out.port = out.scheme == "https" ? 443 : 80;
    if (colon != std::string::npos) {
        const std::string port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return false;
        out.port = std::stoi(port);
        if (out.port < 1 || out.port > 65535) return false;
    }
    out.path = end < url.size() ? url.substr(end) : "/";
    if (out.path[0] != '/') out.path = "/" + out.path; // "?q" or "#f" straight after the host
    return true;
}

bool urlUnderPrefix(const std::string& url, const std::string& prefix) {
    UrlParts u, p;
    if (!parseUrl(url, u) || !parseUrl(prefix, p)) return false;
    if (u.scheme != p.scheme || u.host != p.host || u.port != p.port) return false;
    if (u.path.find("..") != std::string::npos || u.path.find('%') != std::string::npos) return false; // no escapes out
    if (p.path == "/") return true;
    if (u.path.rfind(p.path, 0) != 0) return false;
    if (p.path.back() == '/' || u.path.size() == p.path.size()) return true;
    const char next = u.path[p.path.size()];
    return next == '/' || next == '?' || next == '#';
}

bool urlAllowed(const std::string& url, std::string* why) {
    if (!gOffline) return true;
    const auto p = current();
    for (const std::string& prefix : p->allow)
        if (urlUnderPrefix(url, prefix)) return true;
    UrlParts u;
    const std::string host = parseUrl(url, u) ? u.host : url;
    return refuse(why, "Studio mode: no outside connections (" + host + " is not on this studio's list)");
}

bool isLoopbackAddress(const std::string& address) {
    const std::string a = lower(address);
    return a == "localhost" || a == "::1" || a.rfind("127.", 0) == 0;
}

bool socketAllowed(const std::string& address, std::string* why) {
    if (!gOffline || isLoopbackAddress(address) || current()->localNetwork) return true;
    return refuse(why, "Studio mode: no network connections (" + address + ")");
}

bool multicastAllowed(std::string* why) {
    if (!gOffline || current()->localNetwork) return true;
    return refuse(why, "Studio mode: no local network discovery");
}

bool localIpcAllowed() { return !gOffline; }

uint64_t blockedCount() { return gBlocked; }
void resetBlockedCount() { gBlocked = 0; }

} // namespace sf::net
