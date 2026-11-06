// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link/AiLink.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace sf::link {

namespace {

using Clock = std::chrono::steady_clock;

int remainingMs(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left > 0 ? int(left) : 0;
}

#if !defined(_WIN32)
// The folder must be ours and closed to everyone else; made 0700 if missing.
bool safeDir(const std::string& dir, std::string& error) {
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        error = "can't create " + dir + ": " + std::strerror(errno);
        return false;
    }
    struct stat st{};
    if (::lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        error = dir + " is not a folder";
        return false;
    }
    if (st.st_uid != ::getuid()) {
        error = dir + " belongs to another user: live mode stays off";
        return false;
    }
    if ((st.st_mode & 077) != 0 && ::chmod(dir.c_str(), 0700) != 0) {
        error = dir + " is open to other users and can't be closed";
        return false;
    }
    return true;
}

bool peerIsMe(int fd) {
#if defined(SO_PEERCRED)
    struct ucred cred{};
    socklen_t len = sizeof cred;
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) return false;
    return cred.uid == ::getuid();
#else
    uid_t uid = 0;
    gid_t gid = 0;
    if (::getpeereid(fd, &uid, &gid) != 0) return false;
    return uid == ::getuid();
#endif
}

bool fillAddress(const std::string& path, sockaddr_un& addr, std::string& error) {
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) {
        error = "socket path too long: " + path;
        return false;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return true;
}
#else
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string userName() {
    wchar_t name[256];
    DWORD len = 256;
    if (!GetUserNameW(name, &len)) return "user";
    std::string out;
    for (DWORD i = 0; i + 1 < len; ++i) {
        const wchar_t c = name[i];
        out += (c < 128 && (std::isalnum(int(c)) || c == '-' || c == '_')) ? char(c) : '_';
    }
    return out.empty() ? "user" : out;
}

// A DACL granting the current user (only) full access: the security descriptor is LocalFree'd by the caller.
PSECURITY_DESCRIPTOR currentUserOnly() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::string buffer(size, '\0');
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        LPWSTR sid = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) {
            // Protected DACL: this user, full access; nobody else (not even Administrators or Everyone).
            const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
            ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr);
            LocalFree(sid);
        }
    }
    CloseHandle(token);
    return sd;
}

// The user SID of a process's token (empty if it can't be read).
std::string processUserSid(HANDLE process) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::string buffer(size, '\0'), out;
    if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        LPSTR sid = nullptr;
        if (ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) {
            out = sid;
            LocalFree(sid);
        }
    }
    CloseHandle(token);
    return out;
}

// True when the pipe's server runs as this user: another account could create the name first (before any app
// window), with a DACL open to us; the AI tool's requests must not go to it.
bool serverIsMe(HANDLE pipe) {
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid)) return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    const std::string server = processUserSid(process);
    CloseHandle(process);
    const std::string me = processUserSid(GetCurrentProcess());
    return !me.empty() && server == me;
}

HANDLE asHandle(intptr_t h) { return reinterpret_cast<HANDLE>(h); }
intptr_t fromHandle(HANDLE h) { return h == INVALID_HANDLE_VALUE || !h ? AiLinkConn::kInvalid : reinterpret_cast<intptr_t>(h); }

HANDLE createInstance(const std::string& endpoint, bool first) {
    PSECURITY_DESCRIPTOR sd = currentUserOnly();
    if (!sd) return INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa{sizeof sa, sd, FALSE};
    const HANDLE h = CreateNamedPipeW(widen(endpoint).c_str(),
                                      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 2, // (a second client is told "busy")
                                      1 << 16, 1 << 16, 0, &sa);
    LocalFree(sd);
    return h;
}
#endif

} // namespace

std::string aiLinkEndpoint(std::string& error) {
#if defined(_WIN32)
    (void)error;
    std::string name = "\\\\.\\pipe\\sculptforge-ai-live-" + userName();
    if (const char* own = std::getenv("SF_AI_LIVE_DIR"); own && *own) // tests and isolated runs (app/main.cpp)
        name += "-" + std::to_string(std::hash<std::string>{}(own) % 1000000007u);
    return name;
#else
    std::string dir;
    if (const char* own = std::getenv("SF_AI_LIVE_DIR"); own && *own) { // tests and isolated runs (app/main.cpp)
        dir = std::string(own) + "/sculptforge";
    } else if (const char* run = std::getenv("XDG_RUNTIME_DIR"); run && *run) {
        dir = std::string(run) + "/sculptforge";
    } else {
        const char* tmp = std::getenv("TMPDIR");
        std::string base = tmp && *tmp ? tmp : "/tmp";
        while (base.size() > 1 && base.back() == '/') base.pop_back();
        dir = base + "/sculptforge-" + std::to_string(::getuid());
    }
    // A Unix socket path must fit sun_path (104 bytes on macOS, whose $TMPDIR and test folders are long): then a
    // short owner-only folder in /tmp, one per original folder so isolated runs never share the user's socket.
    if (dir.size() + sizeof("/ai-live.sock") > sizeof(sockaddr_un{}.sun_path))
        dir = "/tmp/sf-live-" + std::to_string(::getuid()) + "-" +
              std::to_string(std::hash<std::string>{}(dir) % 1000000007u);
    if (!safeDir(dir, error)) return {};
    return dir + "/ai-live.sock";
#endif
}

int aiLinkFileMode(const std::string& path) {
#if defined(_WIN32)
    (void)path;
    return -1;
#else
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) return -1;
    return int(st.st_mode & 0777);
#endif
}

// ---------------------------------------------------------------------------
// AiLinkConn
// ---------------------------------------------------------------------------

AiLinkConn::AiLinkConn(AiLinkConn&& o) noexcept : handle_(o.handle_), server_(o.server_), buffer_(std::move(o.buffer_)) {
    o.handle_ = kInvalid;
}

AiLinkConn& AiLinkConn::operator=(AiLinkConn&& o) noexcept {
    if (this != &o) {
        close();
        handle_ = o.handle_;
        server_ = o.server_;
        buffer_ = std::move(o.buffer_);
        o.handle_ = kInvalid;
    }
    return *this;
}

void AiLinkConn::close() {
    if (handle_ == kInvalid) return;
#if defined(_WIN32)
    if (server_) DisconnectNamedPipe(asHandle(handle_));
    CloseHandle(asHandle(handle_));
#else
    ::close(int(handle_));
#endif
    handle_ = kInvalid;
    buffer_.clear();
}

void AiLinkConn::shutdown() {
    if (handle_ == kInvalid) return;
#if defined(_WIN32)
    CancelIoEx(asHandle(handle_), nullptr);
    if (server_) DisconnectNamedPipe(asHandle(handle_));
#else
    ::shutdown(int(handle_), SHUT_RDWR);
#endif
}

int AiLinkConn::readLine(std::string& line, int timeoutMs, size_t maxLine) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        if (const size_t nl = buffer_.find('\n'); nl != std::string::npos) {
            if (nl > maxLine) { // (it came whole: still too long)
                close();
                return -1;
            }
            line.assign(buffer_, 0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buffer_.erase(0, nl + 1);
            return 1;
        }
        if (buffer_.size() > maxLine) {
            close();
            return -1;
        }
        if (handle_ == kInvalid) return -1;
        char chunk[16384];
        long long got = 0;
#if defined(_WIN32)
        DWORD avail = 0;
        if (!PeekNamedPipe(asHandle(handle_), nullptr, 0, nullptr, &avail, nullptr)) {
            close();
            return -1;
        }
        if (avail == 0) {
            if (remainingMs(deadline) == 0) return 0;
            Sleep(5);
            continue;
        }
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD n = 0;
        BOOL ok = ReadFile(asHandle(handle_), chunk, DWORD(std::min<size_t>(sizeof chunk, avail)), &n, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) ok = GetOverlappedResult(asHandle(handle_), &ov, &n, TRUE);
        CloseHandle(ov.hEvent);
        if (!ok || n == 0) {
            close();
            return -1;
        }
        got = n;
#else
        pollfd p{int(handle_), POLLIN, 0};
        const int r = ::poll(&p, 1, remainingMs(deadline));
        if (r == 0) return 0;
        if (r < 0) {
            if (errno == EINTR) continue;
            close();
            return -1;
        }
        got = ::recv(int(handle_), chunk, sizeof chunk, 0);
        if (got <= 0) {
            if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            close();
            return -1;
        }
#endif
        buffer_.append(chunk, size_t(got));
    }
}

bool AiLinkConn::writeLine(const std::string& line) {
    if (handle_ == kInvalid || line.find('\n') != std::string::npos) return false;
    const std::string data = line + "\n";
    size_t sent = 0;
    while (sent < data.size()) {
#if defined(_WIN32)
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD n = 0;
        BOOL ok = WriteFile(asHandle(handle_), data.data() + sent, DWORD(std::min<size_t>(data.size() - sent, 1 << 20)), &n, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) ok = GetOverlappedResult(asHandle(handle_), &ov, &n, TRUE);
        CloseHandle(ov.hEvent);
        if (!ok) return false;
#else
#if defined(MSG_NOSIGNAL)
        const ssize_t n = ::send(int(handle_), data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(int(handle_), data.data() + sent, data.size() - sent, 0);
#endif
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
#endif
        sent += size_t(n);
    }
    return true;
}

// ---------------------------------------------------------------------------
// AiLinkListener
// ---------------------------------------------------------------------------

bool AiLinkListener::open(const std::string& endpoint, std::string& error) {
    close();
    if (endpoint.empty()) {
        error = "no endpoint";
        return false;
    }
#if defined(_WIN32)
    const HANDLE h = createInstance(endpoint, true);
    if (h == INVALID_HANDLE_VALUE) {
        error = GetLastError() == ERROR_ACCESS_DENIED ? "another SculptForge window already offers live mode"
                                                      : "can't create the pipe (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    pending_ = fromHandle(h);
    event_ = fromHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    handle_ = pending_;
#else
    sockaddr_un addr;
    if (!fillAddress(endpoint, addr, error)) return false;
    // A socket file left by a crashed app: nobody answers it, so it's replaced. A live one is another window's.
    if (aiLinkFileMode(endpoint) >= 0) {
        const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
        const bool live = probe >= 0 && ::connect(probe, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
        if (probe >= 0) ::close(probe);
        if (live) {
            error = "another SculptForge window already offers live mode";
            return false;
        }
        ::unlink(endpoint.c_str());
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    const mode_t old = ::umask(0177); // the socket file is born 0600 (no window where it is wider)
    const int bound = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    ::umask(old);
    if (bound != 0 || ::chmod(endpoint.c_str(), 0600) != 0 || ::listen(fd, 2) != 0) {
        error = std::string("can't listen on ") + endpoint + ": " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    handle_ = fd;
#endif
    endpoint_ = endpoint;
    return true;
}

bool AiLinkListener::accept(AiLinkConn& client, int timeoutMs, bool* refused) {
    if (refused) *refused = false;
    if (handle_ == AiLinkConn::kInvalid) return false;
#if defined(_WIN32)
    HANDLE pipe = asHandle(pending_);
    HANDLE ev = asHandle(event_);
    OVERLAPPED ov{};
    ov.hEvent = ev;
    ResetEvent(ev);
    bool connected = false;
    if (ConnectNamedPipe(pipe, &ov)) connected = true;
    else if (GetLastError() == ERROR_PIPE_CONNECTED) connected = true;
    else if (GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ev, DWORD(timeoutMs)) == WAIT_OBJECT_0) {
            DWORD n = 0;
            connected = GetOverlappedResult(pipe, &ov, &n, FALSE) != 0;
        } else {
            CancelIoEx(pipe, &ov);
            DWORD n = 0;
            GetOverlappedResult(pipe, &ov, &n, TRUE);
            return false;
        }
    }
    if (!connected) {
        DisconnectNamedPipe(pipe);
        return false;
    }
    // The connected instance becomes the client's; a fresh one waits for the next client.
    client.close();
    client.handle_ = pending_;
    client.server_ = true;
    const HANDLE next = createInstance(endpoint_, false);
    pending_ = fromHandle(next);
    handle_ = pending_;
    return true;
#else
    pollfd p{int(handle_), POLLIN, 0};
    if (::poll(&p, 1, timeoutMs) <= 0) return false;
    const int fd = ::accept(int(handle_), nullptr, nullptr);
    if (fd < 0) return false;
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (!peerIsMe(fd)) {
        ::close(fd);
        if (refused) *refused = true;
        return false;
    }
    client.close();
    client.handle_ = fd;
    return true;
#endif
}

void AiLinkListener::close() {
#if defined(_WIN32)
    if (pending_ != AiLinkConn::kInvalid) CloseHandle(asHandle(pending_));
    if (event_ != AiLinkConn::kInvalid) CloseHandle(asHandle(event_));
    pending_ = event_ = handle_ = AiLinkConn::kInvalid;
#else
    if (handle_ != AiLinkConn::kInvalid) {
        ::close(int(handle_));
        ::unlink(endpoint_.c_str());
    }
    handle_ = AiLinkConn::kInvalid;
#endif
    endpoint_.clear();
}

bool aiLinkConnect(AiLinkConn& conn, const std::string& endpoint, std::string& error) {
    conn.close();
#if defined(_WIN32)
    const HANDLE h = CreateFileW(widen(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = GetLastError() == ERROR_PIPE_BUSY ? "SculptForge is already connected to another AI tool"
                                                  : "SculptForge isn't running, or live mode is off";
        return false;
    }
    if (!serverIsMe(h)) {
        CloseHandle(h);
        error = "the live-mode pipe belongs to another user";
        return false;
    }
    conn.handle_ = fromHandle(h);
    return true;
#else
    sockaddr_un addr;
    if (!fillAddress(endpoint, addr, error)) return false;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(fd);
        error = "SculptForge isn't running, or live mode is off";
        return false;
    }
    if (!peerIsMe(fd)) { // (the folder is ours: only a bug could put another user's socket there)
        ::close(fd);
        error = "the live-mode socket belongs to another user";
        return false;
    }
    conn.handle_ = fd;
    return true;
#endif
}

std::string base64Encode(const uint8_t* data, size_t size) {
    static const char* const kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t n = uint32_t(data[i]) << 16 | (i + 1 < size ? uint32_t(data[i + 1]) << 8 : 0) |
                           (i + 2 < size ? uint32_t(data[i + 2]) : 0);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < size ? kAlphabet[(n >> 6) & 63] : '=';
        out += i + 2 < size ? kAlphabet[n & 63] : '=';
    }
    return out;
}

} // namespace sf::link
