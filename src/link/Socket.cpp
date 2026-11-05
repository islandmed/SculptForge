// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link/Socket.h"

#include "core/NetworkPolicy.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using SockLen = int;
#define SF_CLOSESOCKET closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SockLen = socklen_t;
#define SF_CLOSESOCKET ::close
#endif
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL; // a closed editor connection must not kill us with SIGPIPE
#else
constexpr int kSendFlags = 0;
#endif

#include <cstring>
#include <mutex>

namespace sf::link {

namespace {

#if defined(_WIN32)
using Native = SOCKET;
Native native(int64_t fd) { return fd < 0 ? INVALID_SOCKET : Native(fd); }
bool invalid(Native s) { return s == INVALID_SOCKET; }
std::string lastError() { return "socket error " + std::to_string(WSAGetLastError()); }
#else
using Native = int;
Native native(int64_t fd) { return Native(fd); }
bool invalid(Native s) { return s < 0; }
std::string lastError() { return std::strerror(errno); }
#endif

bool parseAddress(const std::string& address, uint16_t port, sockaddr_in& out) {
    std::memset(&out, 0, sizeof out);
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    return inet_pton(AF_INET, address.c_str(), &out.sin_addr) == 1;
}

} // namespace

bool initSockets() {
#if defined(_WIN32)
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return ok;
#else
    return true;
#endif
}

Socket& Socket::operator=(Socket&& o) noexcept {
    if (this != &o) {
        close();
        fd_ = o.fd_;
        o.fd_ = -1;
    }
    return *this;
}

void Socket::close() {
    if (fd_ >= 0) SF_CLOSESOCKET(native(fd_));
    fd_ = -1;
}

bool Socket::waitReadable(int timeoutMs) const {
#if defined(_WIN32)
    WSAPOLLFD p{native(fd_), POLLRDNORM, 0};
    return WSAPoll(&p, 1, timeoutMs) > 0;
#else
    pollfd p{native(fd_), POLLIN, 0};
    return poll(&p, 1, timeoutMs) > 0;
#endif
}

bool Socket::openMulticast(const std::string& group, uint16_t port, const std::string& bindAddress, int ttl,
                           std::string& error) {
    close();
    if (!net::multicastAllowed(&error)) return false; // studio mode (core/NetworkPolicy.h)
    if (!initSockets()) {
        error = "Networking is unavailable";
        return false;
    }
    Native s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (invalid(s)) {
        error = lastError();
        return false;
    }
    fd_ = int64_t(s);
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
#if defined(SO_REUSEPORT)
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&yes), sizeof yes);
#endif
    sockaddr_in local;
    // Bind to the group port on all interfaces so other nodes' multicasts reach us.
    if (!parseAddress("0.0.0.0", port, local) || bind(s, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0) {
        error = "Cannot bind UDP port " + std::to_string(port) + ": " + lastError();
        close();
        return false;
    }
    ip_mreq mreq{};
    in_addr iface{};
    if (inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr) != 1 || inet_pton(AF_INET, bindAddress.c_str(), &iface) != 1) {
        error = "Invalid multicast address";
        close();
        return false;
    }
    mreq.imr_interface = iface;
    bool joined = setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof mreq) == 0;
    if (!joined && iface.s_addr != htonl(INADDR_ANY)) {
        // Some systems can't multicast on the requested interface (e.g. Linux loopback): let the OS pick.
        mreq.imr_interface.s_addr = iface.s_addr = htonl(INADDR_ANY);
        joined = setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof mreq) == 0;
    }
    if (!joined) {
        error = "Cannot join multicast group " + group + ": " + lastError();
        close();
        return false;
    }
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&iface), sizeof iface);
    const unsigned char loop = 1, t = static_cast<unsigned char>(ttl);
#if defined(_WIN32)
    const DWORD loopW = loop, ttlW = t;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loopW), sizeof loopW);
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttlW), sizeof ttlW);
#else
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &t, sizeof t);
#endif
    return true;
}

bool Socket::sendTo(const std::string& data, const std::string& address, uint16_t port, std::string& error) {
    sockaddr_in to;
    if (!parseAddress(address, port, to)) {
        error = "Invalid address " + address;
        return false;
    }
    const auto sent = sendto(native(fd_), data.data(), int(data.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
    if (sent < 0 || size_t(sent) != data.size()) {
        error = lastError();
        return false;
    }
    return true;
}

bool Socket::receive(std::string& data, int timeoutMs, std::string* from) {
    if (!valid() || !waitReadable(timeoutMs)) return false;
    char buf[65536];
    sockaddr_in sender{};
    SockLen len = sizeof sender;
    const auto n = recvfrom(native(fd_), buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&sender), &len);
    if (n <= 0) return false;
    data.assign(buf, size_t(n));
    if (from) {
        char text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &sender.sin_addr, text, sizeof text);
        *from = text;
    }
    return true;
}

std::string localAddressToward(const std::string& remote) {
    if (!initSockets()) return {};
    Native s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (invalid(s)) return {};
    sockaddr_in to;
    std::string result;
    // Connecting a UDP socket sends nothing; it just picks the route and source address.
    if (parseAddress(remote, 9, to) && connect(s, reinterpret_cast<sockaddr*>(&to), sizeof to) == 0) {
        sockaddr_in local{};
        SockLen len = sizeof local;
        if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
            char text[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &local.sin_addr, text, sizeof text);
            result = text;
        }
    }
    SF_CLOSESOCKET(s);
    return result;
}

std::string hostName() {
    if (!initSockets()) return {};
    char name[256] = {};
    return gethostname(name, sizeof name - 1) == 0 ? std::string(name) : std::string();
}

bool Socket::listen(const std::string& address, uint16_t port, uint16_t& boundPort, std::string& error, int backlog) {
    close();
    if (!net::socketAllowed(address, &error)) return false; // studio mode: loopback only
    if (!initSockets()) {
        error = "Networking is unavailable";
        return false;
    }
    Native s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (invalid(s)) {
        error = lastError();
        return false;
    }
    fd_ = int64_t(s);
#if !defined(_WIN32)
    // A port whose last connections are still in TIME_WAIT can be listened on again at once (on Windows the
    // option would let another program take the port over, and isn't needed there).
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif
    sockaddr_in local;
    if (!parseAddress(address, port, local) || bind(s, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0 ||
        ::listen(s, backlog) != 0) {
        error = "Cannot listen on " + address + ": " + lastError();
        close();
        return false;
    }
    SockLen len = sizeof local;
    getsockname(s, reinterpret_cast<sockaddr*>(&local), &len);
    boundPort = ntohs(local.sin_port);
    return true;
}

bool Socket::accept(Socket& client, int timeoutMs, std::string* from) {
    if (!valid() || !waitReadable(timeoutMs)) return false;
    sockaddr_in peer{};
    SockLen len = sizeof peer;
    Native c = ::accept(native(fd_), reinterpret_cast<sockaddr*>(&peer), &len);
    if (invalid(c)) return false;
    if (from) {
        char text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &peer.sin_addr, text, sizeof text);
        *from = text;
    }
    client.close();
    client.fd_ = int64_t(c);
#if defined(SO_NOSIGPIPE)
    int yes = 1;
    setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
    return true;
}

bool Socket::connect(const std::string& address, uint16_t port, std::string& error) {
    close();
    if (!net::socketAllowed(address, &error)) return false; // studio mode: loopback only
    if (!initSockets()) {
        error = "Networking is unavailable";
        return false;
    }
    sockaddr_in to;
    if (!parseAddress(address, port, to)) {
        error = "Invalid address " + address;
        return false;
    }
    Native s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (invalid(s)) {
        error = lastError();
        return false;
    }
    fd_ = int64_t(s);
    if (::connect(s, reinterpret_cast<sockaddr*>(&to), sizeof to) != 0) {
        error = "Cannot connect to " + address + ":" + std::to_string(port) + ": " + lastError();
        close();
        return false;
    }
#if defined(SO_NOSIGPIPE)
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
    return true;
}

bool Socket::sendAll(const std::string& data, std::string& error) {
    size_t sent = 0;
    while (sent < data.size()) {
        const auto n = send(native(fd_), data.data() + sent, int(data.size() - sent), kSendFlags);
        if (n <= 0) {
            error = lastError();
            return false;
        }
        sent += size_t(n);
    }
    return true;
}

bool Socket::receiveSome(std::string& data, size_t maxBytes, int timeoutMs) {
    if (!valid() || !waitReadable(timeoutMs)) return false;
    data.resize(maxBytes);
    const auto n = recv(native(fd_), data.data(), int(maxBytes), 0);
    if (n <= 0) {
        data.clear();
        return false;
    }
    data.resize(size_t(n));
    return true;
}

long long Socket::readSome(char* buffer, size_t size, int timeoutMs) {
    if (!valid()) return -1;
    if (!waitReadable(timeoutMs)) return 0;
    const auto n = recv(native(fd_), buffer, int(size), 0);
    return n > 0 ? (long long)n : -1;
}

} // namespace sf::link
