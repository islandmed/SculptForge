// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Show SculptForge on Discord (Preferences > Integrations, off by default): a minimal in-house client of Discord's
// documented local RPC. It talks only to the Discord app on this computer, through its local IPC endpoint (Windows:
// the named pipe \\.\pipe\discord-ipc-N; Linux/macOS: the Unix socket discord-ipc-N in the runtime folder). No SDK,
// no network socket, nothing listens. Outbound only: HANDSHAKE, then SET_ACTIVITY (an empty one clears it on
// disable and quit). Whatever Discord sends back is read only to drain the connection, never interpreted: frames
// are bounds-checked (8-byte header, 64 KiB cap) and anything malformed closes it. It runs on its own thread
// (5 s connect timeout, a back-off from 2 s to 60 s after a failure, one activity every 4 s at most) and starts no
// thread or connection until it's switched on. Scripted and agent runs never start it (agentRun).
//
// Sent: the icon, the name SculptForge (the application's name; details stay empty), the elapsed time, and
// optionally "Monthly challenge: <theme>" and the file name (never a path). Every string is sanitised first.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sf::discord {

// The SculptForge application on Discord's developer portal (a public ID, not a secret). "0" hides the feature and
// never connects.
inline constexpr const char* kDiscordAppId = "1567737303210926130";
// The icon: the cube app icon on the site (Discord fetches an external image through its media proxy; the portal's
// "logo" asset was a placeholder cube).
inline constexpr const char* kLargeImageUrl = "https://sculptforge.app/press/sculptforge-icon-1024.png";

// A run made by a script or an agent, never the user at the window: SDL_APP_ID=sculptforge-agent,
// SDL_VIDEO_DRIVER=offscreen (or videoDriver "offscreen") or SF_NO_DISCORD set. It never shows anything on the
// user's Discord, whatever the preferences say. getenv is a parameter for the tests.
bool agentRun(const std::function<const char*(const char*)>& getenv, const char* videoDriver);

// A real application ID: 1-20 digits, not "0".
bool appIdConfigured(std::string_view id);

// Valid UTF-8 only (bad sequences dropped), no control or bidirectional-override characters, whitespace runs
// collapsed and trimmed, at most maxCodepoints code points.
std::string sanitize(std::string_view in, size_t maxCodepoints);

inline constexpr size_t kChallengeMax = 64;  // the theme the user types
inline constexpr size_t kFileNameMax = 64;
inline constexpr size_t kStateMax = 128;     // Discord's limit for the state line

struct Activity {
    bool show = false;          // false: nothing shown (cleared)
    std::string challenge;      // the monthly challenge's theme ("" = not shown)
    std::string fileName;       // the file's name ("" = not shown); a path is cut to its last part
    int64_t start = 0;          // seconds since 1970 for the elapsed time (0 = none)
    bool operator==(const Activity&) const = default;
};

// The state line: "Monthly challenge: <theme>", the file name, or both joined by " · "; "" for none.
std::string activityState(const Activity& a);

// Frames: little-endian opcode and length, then the JSON.
enum Op : uint32_t { OpHandshake = 0, OpFrame = 1, OpClose = 2, OpPing = 3, OpPong = 4 };
inline constexpr size_t kHeaderSize = 8;
inline constexpr size_t kMaxFrame = 64 * 1024; // a longer frame is malformed

std::string encodeFrame(uint32_t op, std::string_view json);
std::string handshakeJson(std::string_view appId);
// SET_ACTIVITY for this process; a = nullptr or !a->show clears it.
std::string setActivityJson(int64_t pid, const Activity* a, uint64_t nonce);

// Reads Discord's replies only to drain them: frames are checked and counted, their content is never looked at.
class FrameReader {
public:
    enum class Result { NeedMore, Frame, Bad };
    // Takes bytes; returns Frame while whole frames are complete (call next() for each), Bad on a malformed one
    // (unknown opcode, length over kMaxFrame); after Bad everything is Bad.
    void feed(const char* data, size_t n);
    Result next(uint32_t& op);
    size_t buffered() const { return buf_.size() - pos_; }

private:
    std::string buf_;
    size_t pos_ = 0;
    bool bad_ = false;
};

// Unix: the sockets to try, in order: $XDG_RUNTIME_DIR, $TMPDIR, $TMP, $TEMP, /tmp, each plain and with the
// Flatpak and Snap subfolders, discord-ipc-0..9. getenv is a parameter for the tests.
std::vector<std::string> socketCandidates(const std::function<const char*(const char*)>& getenv);
// Unix: path is a socket itself (not a symlink: lstat) owned by uid. Always false on Windows.
bool socketOwnedBy(const std::string& path, unsigned uid);

// Every connection attempt this process made (tests: off means zero).
uint64_t connectAttempts();

class Client {
public:
    explicit Client(std::string appId);
    ~Client(); // clears the activity, closes, joins (bounded: every wait is short)
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // What to show. Cheap; the worker thread starts on the first call that shows something with a real app ID.
    void set(const Activity& a);
    bool connected() const { return connected_.load(); }
    uint64_t activitiesSent() const { return sent_.load(); }

    // Tests: the longest back-off after a failure, the connect timeout and the gap between two activities
    // (milliseconds).
    void setTimingForTests(int backoffMs, int connectTimeoutMs, int minGapMs = 0);

private:
    void run();

    std::string appId_;
    std::mutex mutex_;
    std::condition_variable cv_;
    Activity desired_;
    bool dirty_ = false, stop_ = false;
    std::thread thread_;
    std::atomic<bool> abort_{false};
    std::atomic<bool> connected_{false};
    std::atomic<uint64_t> sent_{0};
    // Back-off: 2 s after a connection is lost (Discord restarting), doubling to 60 s while it stays away. Discord
    // takes at most 5 activities in 20 s and drops the rest: one every 4 s at most, always the latest.
    int backoffMs_ = 60000, connectTimeoutMs_ = 5000, minGapMs_ = 4000;
};

} // namespace sf::discord
