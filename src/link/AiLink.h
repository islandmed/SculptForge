// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Live mode's channel (Preferences > AI > Let AI tools control this window): `sculptforge-cli --mcp --attach`
// talks to the open app through a local endpoint only this user can open. No TCP or UDP port, no network
// listener of any kind:
//   Linux / macOS: a Unix domain socket "ai-live.sock" (0600) in a folder only this user can enter (0700):
//                  $XDG_RUNTIME_DIR/sculptforge, else $TMPDIR/sculptforge-<uid> ($SF_AI_LIVE_DIR/sculptforge first: tests
//                  and isolated runs). The folder is refused when another
//                  user owns it or anyone else can enter it; every connection's peer must be this user (SO_PEERCRED /
//                  getpeereid).
//   Windows:       the named pipe \\.\pipe\sculptforge-ai-live-<user>, its DACL granting the current user only,
//                  created as the first instance (nobody can squat the name first) with remote clients rejected.
// Messages are newline-delimited JSON, at most kAiLinkMaxLine bytes a line (longer: the connection closes).
#pragma once

#include <cstdint>
#include <string>

namespace sf::link {

inline constexpr size_t kAiLinkMaxLine = 1u << 20;        // client -> app (requests are small)
inline constexpr size_t kAiLinkMaxReplyLine = 64u << 20;  // app -> client (viewport images)

// The endpoint this user's app listens on ("" with `error` set when the folder can't be made safe).
std::string aiLinkEndpoint(std::string& error);
// The same kind of endpoint under another name: "<folder>/<name>.sock", or \\.\pipe\sculptforge-<name>-<user>
// (the Maya link, link/MayaLink.h: "maya-link").
std::string localEndpoint(const std::string& name, std::string& error);

// One connection: a socket (POSIX) or pipe handle (Windows).
class AiLinkConn {
public:
    AiLinkConn() = default;
    ~AiLinkConn() { close(); }
    AiLinkConn(const AiLinkConn&) = delete;
    AiLinkConn& operator=(const AiLinkConn&) = delete;
    AiLinkConn(AiLinkConn&& o) noexcept;
    AiLinkConn& operator=(AiLinkConn&& o) noexcept;

    // 1: a line in `line` (without its newline); 0: none within `timeoutMs`; -1: closed, error or a line longer
    // than `maxLine` (the connection is then closed).
    int readLine(std::string& line, int timeoutMs, size_t maxLine = kAiLinkMaxLine);
    bool writeLine(const std::string& line);
    // Binary use (the Maya link): up to `size` bytes, waiting at most `timeoutMs` for the first: the count, 0 on
    // timeout, -1 when closed. writeAll sends every byte (false: the peer is gone).
    long long readBytes(char* buffer, size_t size, int timeoutMs);
    bool writeAll(const char* data, size_t size);
    bool valid() const { return handle_ != kInvalid; }
    void close();
    // Unblocks a readLine waiting on another thread (the connection is closed afterwards by its owner).
    void shutdown();

    static constexpr intptr_t kInvalid = -1;
    intptr_t handle_ = kInvalid;
    bool server_ = false; // Windows: the app's end of the pipe (disconnect before closing)

private:
    long long readChunk(char* chunk, size_t size, int timeoutMs); // one read from the handle (no buffer_)
    std::string buffer_;
};

class AiLinkListener {
public:
    AiLinkListener() = default;
    ~AiLinkListener() { close(); }
    AiLinkListener(const AiLinkListener&) = delete;
    AiLinkListener& operator=(const AiLinkListener&) = delete;

    // Starts listening on `endpoint` (aiLinkEndpoint). A stale socket file left by a crashed app is replaced; a
    // live one (another SculptForge window of this user) is not: false.
    bool open(const std::string& endpoint, std::string& error);
    // Waits up to `timeoutMs` for a client. False on timeout or error. On POSIX a peer that isn't this user is
    // refused here (closed, false, `refused` set).
    bool accept(AiLinkConn& client, int timeoutMs, bool* refused = nullptr);
    void close();
    bool valid() const { return handle_ != AiLinkConn::kInvalid; }
    const std::string& endpoint() const { return endpoint_; }

private:
    intptr_t handle_ = AiLinkConn::kInvalid;
    intptr_t pending_ = AiLinkConn::kInvalid; // Windows: the pipe instance waiting for a client
    intptr_t event_ = AiLinkConn::kInvalid;   // Windows: its overlapped connect event
    std::string endpoint_;
};

// The CLI's side: connects to the app's endpoint. False with `error` (the app isn't running, or live mode is off).
bool aiLinkConnect(AiLinkConn& conn, const std::string& endpoint, std::string& error);

// POSIX: the permission bits of `path` (tests check 0600 / 0700), -1 if it doesn't exist. Windows: -1.
int aiLinkFileMode(const std::string& path);

std::string base64Encode(const uint8_t* data, size_t size);

} // namespace sf::link
