// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Discord's local RPC, outbound only (core/DiscordPresence.h). The security rules it keeps:
//   - Local IPC only (a named pipe or a Unix socket); never a network socket, never listens.
//   - Windows: the pipe is opened with SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION (its server can't act as
//     us), and its server process must run as our user. Unix: the socket must be a socket (lstat: no symlink),
//     owned by our uid, and the peer that accepts must be our uid too.
//   - Sends HANDSHAKE and SET_ACTIVITY only; replies are drained, never interpreted (FrameReader).
//   - The activity holds fixed strings only (Sculpting, Painting, Idle) and the start time: nothing from the scene.
//   - A failed or lost connection is retried no sooner than 30 s later; switching it off ends the thread.
#include "core/DiscordPresence.h"

#include "core/Json.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 // GetNamedPipeServerProcessId
#endif
#include <windows.h>
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

namespace sf::discord {

namespace {

std::atomic<uint64_t> g_connectAttempts{0};

using Clock = std::chrono::steady_clock;

int msLeft(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return int(std::clamp<int64_t>(left, 0, 60000));
}

// --- The connection: a named pipe or a Unix socket ---

#if defined(_WIN32)

bool serverRunsAsUs(HANDLE pipe) {
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid) || pid == 0) return false;
    bool same = false;
    HANDLE ours = nullptr, theirs = nullptr;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc && OpenProcessToken(proc, TOKEN_QUERY, &theirs) && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &ours)) {
        alignas(TOKEN_USER) unsigned char a[256], b[256];
        DWORD na = 0, nb = 0;
        if (GetTokenInformation(ours, TokenUser, a, sizeof a, &na) && GetTokenInformation(theirs, TokenUser, b, sizeof b, &nb))
            same = EqualSid(reinterpret_cast<TOKEN_USER*>(a)->User.Sid, reinterpret_cast<TOKEN_USER*>(b)->User.Sid) != FALSE;
    }
    if (ours) CloseHandle(ours);
    if (theirs) CloseHandle(theirs);
    if (proc) CloseHandle(proc);
    return same;
}

class Conn {
public:
    ~Conn() { close(); }
    bool isOpen() const { return h_ != INVALID_HANDLE_VALUE; }

    bool open(int timeoutMs, const std::atomic<bool>& stop) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (int n = 0; n < 10 && !stop.load(); ++n) {
            const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(n);
            for (;;) {
                ++g_connectAttempts;
                // SECURITY_IDENTIFICATION: the server may learn who we are, never act as us.
                HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    if (serverRunsAsUs(h)) {
                        h_ = h;
                        return true;
                    }
                    CloseHandle(h); // someone else's pipe: not Discord's
                    break;
                }
                if (GetLastError() != ERROR_PIPE_BUSY || msLeft(deadline) == 0) break;
                WaitNamedPipeW(name.c_str(), DWORD(std::min(250, msLeft(deadline))));
                if (stop.load()) return false;
            }
            if (msLeft(deadline) == 0) break;
        }
        return false;
    }

    bool write(const std::string& data, int timeoutMs) {
        if (!isOpen()) return false;
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;
        DWORD written = 0;
        bool ok = WriteFile(h_, data.data(), DWORD(data.size()), nullptr, &ov) != FALSE;
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, DWORD(timeoutMs)) != WAIT_OBJECT_0) CancelIoEx(h_, &ov);
            ok = true;
        }
        ok = ok && GetOverlappedResult(h_, &ov, &written, TRUE) && written == data.size();
        CloseHandle(ov.hEvent);
        return ok;
    }

    // Reads what's there, waiting up to waitMs for the first byte, at most cap bytes. False: closed or failed.
    bool read(FrameReader& reader, int waitMs, size_t cap) {
        if (!isOpen()) return false;
        const auto deadline = Clock::now() + std::chrono::milliseconds(waitMs);
        size_t total = 0;
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr)) return false; // closed
            if (avail == 0) {
                if (total > 0 || msLeft(deadline) == 0) return true;
                Sleep(20);
                continue;
            }
            char buf[4096];
            const DWORD want = DWORD(std::min<size_t>({sizeof buf, avail, cap - total}));
            OVERLAPPED ov{};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!ov.hEvent) return false;
            DWORD got = 0;
            bool ok = ReadFile(h_, buf, want, nullptr, &ov) != FALSE;
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(ov.hEvent, 1000) != WAIT_OBJECT_0) CancelIoEx(h_, &ov);
                ok = true;
            }
            ok = ok && GetOverlappedResult(h_, &ov, &got, TRUE);
            CloseHandle(ov.hEvent);
            if (!ok || got == 0) return false;
            reader.feed(buf, got);
            total += got;
            if (total >= cap) return true;
        }
    }

    void close() {
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

#else

bool peerIsUs(int fd) {
#if defined(__linux__)
    struct ucred cred{};
    socklen_t len = sizeof cred;
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0 && cred.uid == geteuid();
#else
    uid_t uid = 0;
    gid_t gid = 0;
    return getpeereid(fd, &uid, &gid) == 0 && uid == geteuid();
#endif
}

class Conn {
public:
    ~Conn() { close(); }
    bool isOpen() const { return fd_ >= 0; }

    bool open(int timeoutMs, const std::atomic<bool>& stop) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (const std::string& path : socketCandidates([](const char* n) { return std::getenv(n); })) {
            if (stop.load() || msLeft(deadline) == 0) return false;
            if (!socketOwnedBy(path, unsigned(geteuid()))) continue; // missing, not a socket, a symlink, or not ours
            sockaddr_un addr{};
            if (path.size() >= sizeof addr.sun_path) continue;
            addr.sun_family = AF_UNIX;
            std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
            ++g_connectAttempts;
#ifdef SOCK_CLOEXEC
            // Atomic close-on-exec: a process started meanwhile (an AI worker) never inherits the connection and
            // keeps SculptForge on Discord after SculptForge quits.
            const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
            const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
#endif
            if (fd < 0) return false;
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
            const int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            bool ok = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) == 0;
            if (!ok && (errno == EINPROGRESS || errno == EAGAIN)) {
                pollfd p{fd, POLLOUT, 0};
                int err = 0;
                socklen_t len = sizeof err;
                ok = poll(&p, 1, msLeft(deadline)) == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
            }
            if (ok && peerIsUs(fd)) {
                fd_ = fd;
                return true;
            }
            ::close(fd);
        }
        return false;
    }

    bool write(const std::string& data, int timeoutMs) {
        if (!isOpen()) return false;
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        size_t done = 0;
        while (done < data.size()) {
#ifdef MSG_NOSIGNAL
            const ssize_t n = ::send(fd_, data.data() + done, data.size() - done, MSG_NOSIGNAL);
#else
            const ssize_t n = ::send(fd_, data.data() + done, data.size() - done, 0);
#endif
            if (n > 0) {
                done += size_t(n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                pollfd p{fd_, POLLOUT, 0};
                if (poll(&p, 1, msLeft(deadline)) == 1) continue;
            }
            return false;
        }
        return true;
    }

    bool read(FrameReader& reader, int waitMs, size_t cap) {
        if (!isOpen()) return false;
        pollfd p{fd_, POLLIN, 0};
        if (poll(&p, 1, waitMs) <= 0) return true; // nothing yet
        size_t total = 0;
        char buf[4096];
        while (total < cap) {
            const ssize_t n = ::recv(fd_, buf, std::min(sizeof buf, cap - total), 0);
            if (n > 0) {
                reader.feed(buf, size_t(n));
                total += size_t(n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK); // 0: closed
        }
        return true;
    }

    void close() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    int fd_ = -1;
};

#endif

int64_t processId() {
#if defined(_WIN32)
    return int64_t(GetCurrentProcessId());
#else
    return int64_t(getpid());
#endif
}

} // namespace

bool appIdConfigured(std::string_view id) {
    if (id.empty() || id.size() > 20 || id == "0") return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; }) && id.find_first_not_of('0') != std::string_view::npos;
}

const char* modeName(Mode m) {
    switch (m) {
    case Mode::Sculpting: return "Sculpting";
    case Mode::Painting: return "Painting";
    default: return "Idle";
    }
}

// Only these three words: nothing typed and nothing from the scene ever reaches the state line.
std::string activityState(const Activity& a) { return a.show ? std::string(modeName(a.mode)) : std::string(); }

std::string encodeFrame(uint32_t op, std::string_view json) {
    std::string out(kHeaderSize, '\0');
    const uint32_t len = uint32_t(json.size());
    for (int k = 0; k < 4; ++k) {
        out[k] = char((op >> (8 * k)) & 0xFF);
        out[4 + k] = char((len >> (8 * k)) & 0xFF);
    }
    out.append(json);
    return out;
}

std::string handshakeJson(std::string_view appId) {
    Json j = Json::object();
    j.set("v", 1);
    j.set("client_id", std::string(appId));
    return j.dump();
}

std::string setActivityJson(int64_t pid, const Activity* a, uint64_t nonce) {
    Json args = Json::object();
    args.set("pid", double(pid));
    if (a && a->show) {
        Json activity = Json::object();
        activity.set("type", 0); // Playing: "Playing SculptForge"
        const std::string state = activityState(*a);
        if (!state.empty()) activity.set("state", state);
        if (a->start > 0) {
            Json timestamps = Json::object();
            timestamps.set("start", double(a->start));
            activity.set("timestamps", std::move(timestamps));
        }
        Json assets = Json::object();
        assets.set("large_image", kLargeImageUrl);
        assets.set("large_text", "SculptForge");
        activity.set("assets", std::move(assets));
        args.set("activity", std::move(activity));
    }
    Json j = Json::object();
    j.set("cmd", "SET_ACTIVITY");
    j.set("args", std::move(args));
    j.set("nonce", std::to_string(nonce));
    return j.dump();
}

void FrameReader::feed(const char* data, size_t n) {
    if (bad_) return;
    if (pos_ > 0 && pos_ == buf_.size()) buf_.clear(), pos_ = 0;
    buf_.append(data, n);
}

FrameReader::Result FrameReader::next(uint32_t& op) {
    if (bad_) return Result::Bad;
    if (buf_.size() - pos_ < kHeaderSize) return Result::NeedMore;
    const auto u32 = [&](size_t at) {
        uint32_t v = 0;
        for (int k = 3; k >= 0; --k) v = (v << 8) | uint8_t(buf_[at + size_t(k)]);
        return v;
    };
    op = u32(pos_);
    const uint32_t len = u32(pos_ + 4);
    if (op > OpPong || len > kMaxFrame) {
        bad_ = true;
        buf_.clear();
        pos_ = 0;
        return Result::Bad;
    }
    if (buf_.size() - pos_ - kHeaderSize < len) return Result::NeedMore;
    pos_ += kHeaderSize + len; // the content is never read
    if (pos_ == buf_.size()) buf_.clear(), pos_ = 0;
    return Result::Frame;
}

std::vector<std::string> socketCandidates(const std::function<const char*(const char*)>& getenv) {
    std::vector<std::string> dirs;
    for (const char* name : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        const char* v = getenv(name);
        if (v && *v && v[0] == '/') dirs.emplace_back(v);
    }
    dirs.emplace_back("/tmp");
    std::vector<std::string> out;
    std::vector<std::string> seen;
    for (std::string d : dirs) {
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        if (std::find(seen.begin(), seen.end(), d) != seen.end()) continue;
        seen.push_back(d);
        const std::string base = d == "/" ? "" : d;
        for (const char* sub : {"", "/app/com.discordapp.Discord", "/snap.discord"})
            for (int n = 0; n < 10; ++n) out.push_back(base + sub + "/discord-ipc-" + std::to_string(n));
    }
    return out;
}

bool socketOwnedBy(const std::string& path, unsigned uid) {
#if defined(_WIN32)
    (void)path;
    (void)uid;
    return false;
#else
    struct stat st{};
    return lstat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode) && unsigned(st.st_uid) == uid;
#endif
}

uint64_t connectAttempts() { return g_connectAttempts.load(); }

int retryDelayMs(int failures, int maxMs) {
    const int64_t wait = int64_t(kRetryMinMs) << std::clamp(failures, 0, 1); // 30 s, then 60 s
    return int(std::min<int64_t>(std::max(maxMs, 1), wait));
}

bool agentRun(const std::function<const char*(const char*)>& getenv, const char* videoDriver) {
    const auto is = [](const char* v, const char* want) { return v && std::strcmp(v, want) == 0; };
    const char* noDiscord = getenv("SF_NO_DISCORD");
    return is(getenv("SDL_APP_ID"), "sculptforge-agent") || is(getenv("SDL_VIDEO_DRIVER"), "offscreen") ||
           is(videoDriver, "offscreen") || (noDiscord && *noDiscord && !is(noDiscord, "0"));
}

Client::Client(std::string appId) : appId_(std::move(appId)) {}

Client::~Client() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    abortConnect_ = true; // a connect in progress gives up
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Client::setTimingForTests(int backoffMs, int connectTimeoutMs, int minGapMs) {
    std::lock_guard lock(mutex_);
    backoffMs_ = backoffMs;
    connectTimeoutMs_ = connectTimeoutMs;
    minGapMs_ = minGapMs;
}

void Client::set(const Activity& a) {
    {
        std::lock_guard lock(mutex_);
        abortConnect_ = !a.show; // off: a connect in progress gives up at once
        if (a == desired_) return;
        desired_ = a;
        dirty_ = true;
        if (thread_.joinable() && !running_) thread_.join(); // ended when it was switched off (instant: it has returned)
        // Nothing runs until something is shown with a real application ID (off = no thread, no connection).
        if (!thread_.joinable() && a.show && appIdConfigured(appId_) && !stop_) {
            running_ = true;
            thread_ = std::thread([this] { run(); });
        }
    }
    cv_.notify_all();
}

void Client::run() {
    Conn conn;
    Activity sent;          // what Discord shows (show = false: nothing)
    bool sentValid = false; // sent is known (reset on every new connection)
    uint64_t nonce = 0;
    Clock::time_point retryAt{}, nextSendAt{};
    int failures = 0; // in a row: 30 s, then 60 s (retryDelayMs, capped at backoffMs)
    FrameReader reader;
    constexpr size_t kDrainCap = 256 * 1024; // per wake-up: a flood can't keep the thread reading
    constexpr int kWriteTimeoutMs = 2000;

    const auto drop = [&](int backoffMs) {
        conn.close();
        connected_ = false;
        sentValid = false;
        reader = FrameReader();
        retryAt = Clock::now() + std::chrono::milliseconds(retryDelayMs(failures++, backoffMs));
    };
    // Drains replies; false (and closed) on a malformed frame, CLOSE, or a closed connection.
    const auto drain = [&](int waitMs) {
        if (!conn.read(reader, waitMs, kDrainCap)) return false;
        uint32_t op = 0;
        for (;;) {
            const FrameReader::Result r = reader.next(op);
            if (r == FrameReader::Result::NeedMore) return true;
            if (r == FrameReader::Result::Bad || op == OpClose) return false;
        }
    };

    std::unique_lock lock(mutex_);
    for (;;) {
        const bool stopping = stop_;
        const Activity want = desired_;
        dirty_ = false;
        const int backoffMs = backoffMs_, connectTimeoutMs = connectTimeoutMs_, minGapMs = minGapMs_;
        lock.unlock();

        if (stopping) {
            if (conn.isOpen() && (!sentValid || sent.show)) conn.write(encodeFrame(OpFrame, setActivityJson(processId(), nullptr, ++nonce)), 500);
            conn.close();
            connected_ = false;
            running_ = false;
            return;
        }

        if (want.show && !conn.isOpen() && Clock::now() >= retryAt) {
            bool ok = conn.open(connectTimeoutMs, abortConnect_) && conn.write(encodeFrame(OpHandshake, handshakeJson(appId_)), kWriteTimeoutMs);
            if (ok) { // Discord answers the handshake (READY) or closes: wait for the first frame
                const auto deadline = Clock::now() + std::chrono::milliseconds(connectTimeoutMs);
                ok = false;
                uint32_t op = 0;
                while (!ok && conn.isOpen()) {
                    if (!conn.read(reader, std::max(1, msLeft(deadline)), kDrainCap)) break;
                    const FrameReader::Result r = reader.next(op);
                    if (r == FrameReader::Result::Frame) {
                        ok = op != OpClose;
                        if (ok) drain(0); // the rest, if any (ignored)
                        break;
                    }
                    if (r == FrameReader::Result::Bad || msLeft(deadline) == 0) break;
                }
            }
            if (ok) connected_ = true, failures = 0;
            else drop(backoffMs);
        }
        // A change to what's shown waits for its gap (Discord drops the 6th in 20 s); clearing never waits.
        const bool due = !want.show || Clock::now() >= nextSendAt;
        if (conn.isOpen() && (!sentValid || !(sent == want)) && due) {
            if (conn.write(encodeFrame(OpFrame, setActivityJson(processId(), &want, ++nonce)), kWriteTimeoutMs)) {
                sent = want;
                sentValid = true;
                ++sent_;
                nextSendAt = Clock::now() + std::chrono::milliseconds(minGapMs);
            } else {
                drop(backoffMs);
            }
        }
        if (conn.isOpen() && !want.show) { // off: cleared above; close until it's on again
            conn.close();
            connected_ = false;
            sentValid = false;
            reader = FrameReader();
        }
        if (conn.isOpen() && !drain(0)) drop(backoffMs);

        lock.lock();
        if (stop_ || dirty_) continue;
        if (!desired_.show && !conn.isOpen()) { // switched off and cleared: no thread is left behind (set() starts a new one)
            running_ = false;
            return;
        }
        if (conn.isOpen() && (!sentValid || !(sent == desired_))) cv_.wait_until(lock, nextSendAt); // the latest, once due
        else if (conn.isOpen()) cv_.wait_for(lock, std::chrono::seconds(5)); // wake up to drain now and then
        else cv_.wait_until(lock, retryAt);
    }
}

} // namespace sf::discord
