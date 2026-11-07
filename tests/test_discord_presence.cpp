// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Show SculptForge on Discord (core/DiscordPresence.h): what is sent (the name only, the optional challenge and
// file name, never a path), sanitising, the frame reader's bounds (malformed frames, oversized lengths, fuzz),
// the socket ownership check (wrong owner, symlink), off = zero connections, and a whole session against a fake
// Discord on a Unix socket (handshake, activity, clear; a malformed reply closes the connection; Discord restarting;
// bursts; a killed process), and agent runs never showing.
#include "TestHarness.h"

#include "core/DiscordPresence.h"
#include "core/ExitWatchdog.h"
#include "core/Sha256.h"
#include "core/Json.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <random>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <cstdlib>
#include <sys/socket.h>
#include <sys/stat.h>
#include <csignal>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace sf;
using namespace sf::discord;

namespace {

Json parsed(const std::string& text) {
    Json j;
    std::string err;
    CHECK(Json::parseStrict(text, j, err));
    return j;
}

} // namespace

TEST(discord_app_id_placeholder_hides_it) {
    CHECK(!appIdConfigured("0"));
    CHECK(!appIdConfigured("000"));
    CHECK(!appIdConfigured(""));
    CHECK(!appIdConfigured("12a4"));
    CHECK(!appIdConfigured("123456789012345678901")); // 21 digits
    CHECK(appIdConfigured("1234567890123456789"));
    CHECK(appIdConfigured(kDiscordAppId)); // SculptForge's application
}

TEST(discord_sanitize_strings) {
    CHECK(sanitize("Dragons", 64) == "Dragons");
    CHECK(sanitize("  Sea \t\n monsters  ", 64) == "Sea monsters");
    CHECK(sanitize("a\x01" "b\x1b[31mc\x7f", 64) == "ab[31mc");      // control characters
    CHECK(sanitize("x\xC2\x85y", 64) == "xy");                       // C1 control (NEL)
    CHECK(sanitize("abc\xE2\x80\xAE" "gpj.exe", 64) == "abcgpj.exe"); // right-to-left override
    CHECK(sanitize("a\xE2\x81\xA6" "b", 64) == "ab");                // isolate
    CHECK(sanitize("\xEF\xBB\xBF" "bom", 64) == "bom");
    CHECK(sanitize("bad\xFF\xFE" "utf8", 64) == "badutf8");
    CHECK(sanitize("over\xC0\xAFlong", 64) == "overlong");           // overlong '/'
    CHECK(sanitize("sur\xED\xA0\x80rogate", 64) == "surrogate");
    CHECK(sanitize("cut\xE2\x82", 64) == "cut");                     // truncated sequence
    CHECK(sanitize("caf\xC3\xA9 \xF0\x9F\x90\x89", 64) == "caf\xC3\xA9 \xF0\x9F\x90\x89"); // UTF-8 kept
    CHECK(sanitize(std::string(500, 'x'), 64) == std::string(64, 'x'));
    CHECK(sanitize("\xC3\xA9\xC3\xA9\xC3\xA9", 2) == "\xC3\xA9\xC3\xA9"); // code points, not bytes
    CHECK(sanitize("ab   ", 3) == "ab");
}

TEST(discord_activity_sends_the_name_only) {
    Activity a;
    a.show = true;
    a.start = 1793750400;
    CHECK(activityState(a).empty());
    const Json j = parsed(setActivityJson(4242, &a, 7));
    CHECK(j["cmd"].asString() == "SET_ACTIVITY");
    CHECK(j["nonce"].asString() == "7");
    CHECK(j["args"]["pid"].asNumber() == 4242.0);
    const Json& act = j["args"]["activity"];
    CHECK(act.isObject());
    CHECK(!act.has("details")); // Discord shows the application's name: SculptForge
    CHECK(!act.has("state"));
    // The app icon by URL (Discord's RPC docs: an external https image in large_image, through its media proxy).
    CHECK(act["assets"]["large_image"].asString() == "https://sculptforge.app/press/sculptforge-app-icon-cube-outline-1024.png");
    CHECK(act["assets"]["large_text"].asString() == "SculptForge");
    CHECK(act["timestamps"]["start"].asNumber() == 1793750400.0); // seconds, as in the docs' SET_ACTIVITY example
    CHECK(act["type"].asNumber() == 0.0); // Playing (SET_ACTIVITY takes 0, 2, 3 or 5)
    // Nothing else: no buttons, party, secrets (join / spectate) or small image.
    for (const auto& m : act.members()) CHECK(m.first == "assets" || m.first == "timestamps" || m.first == "type");
    for (const auto& m : act["assets"].members()) CHECK(m.first == "large_image" || m.first == "large_text");
}

TEST(discord_activity_challenge_and_file_name) {
    Activity a;
    a.show = true;
    a.challenge = "  Sea\x07 monsters ";
    CHECK(activityState(a) == "Monthly challenge: Sea monsters");
    a.challenge = std::string(200, 'z');
    CHECK(activityState(a) == "Monthly challenge: " + std::string(kChallengeMax, 'z'));
    a.challenge.clear();
    a.fileName = "/home/someone/secret project/kraken.sfproj";
    CHECK(activityState(a) == "kraken.sfproj"); // the name, never the path
    a.fileName = "C:\\Users\\someone\\Desktop\\kraken.sfproj";
    CHECK(activityState(a) == "kraken.sfproj");
    a.challenge = "Dragons";
    CHECK(activityState(a) == "Monthly challenge: Dragons \xC2\xB7 kraken.sfproj");
    const Json j = parsed(setActivityJson(1, &a, 1));
    CHECK(j["args"]["activity"]["state"].asString() == activityState(a));
    CHECK(j.dump().find("someone") == std::string::npos);
    a.fileName = "x"; // shorter than Discord's 2-character minimum on its own
    a.challenge.clear();
    CHECK(activityState(a).empty());
    a.show = false;
    a.challenge = "Dragons";
    CHECK(activityState(a).empty());
}

TEST(discord_clear_has_no_activity) {
    const Json j = parsed(setActivityJson(9, nullptr, 3));
    CHECK(j["cmd"].asString() == "SET_ACTIVITY");
    CHECK(!j["args"].has("activity"));
    Activity off;
    CHECK(!parsed(setActivityJson(9, &off, 3))["args"].has("activity"));
    const Json h = parsed(handshakeJson("1234"));
    CHECK(h["v"].asNumber() == 1.0 && h["client_id"].asString() == "1234");
    CHECK(h.members().size() == 2);
}

// Discord's media proxy caches the image by its URL: the file it names never changes (a new icon, a new name). The
// press icon changed, yet profiles kept the SF mark (@ebarranco).
TEST(discord_icon_url_is_frozen) {
    const std::string url = kLargeImageUrl, site = "https://sculptforge.app/";
    CHECK(url.rfind(site, 0) == 0);
    std::ifstream f(std::string(SF_TEST_DATA_DIR) + "/../../website/" + url.substr(site.size()), std::ios::binary);
    CHECK(f.good());
    std::ostringstream bytes;
    bytes << f.rdbuf();
    // Changed on purpose? Add the new image under a new file name, point kLargeImageUrl at it and pin it here.
    CHECK(sha256Hex(bytes.str()) == "95ce025be2983fb0a6d824fda577e3a0b705dd618102c227420093312261adef");
}

TEST(exit_watchdog_fires_after_its_timeout) {
    std::atomic<bool> fired{false};
    const auto start = std::chrono::steady_clock::now();
    armExitWatchdog(50, 0, [&] { fired = true; });
    for (int i = 0; i < 500 && !fired; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(fired.load());
    CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(50));
}

TEST(discord_frame_reader_bounds) {
    const std::string f = encodeFrame(OpFrame, "{\"evt\":\"READY\"}");
    CHECK(f.size() == kHeaderSize + 15);
    CHECK(f[0] == 1 && f[4] == 15 && f[1] == 0 && f[5] == 0);
    // Byte by byte, and two frames in one read.
    FrameReader r;
    uint32_t op = 99;
    const std::string two = f + encodeFrame(OpPing, "{}");
    for (size_t i = 0; i + 1 < f.size(); ++i) {
        r.feed(&two[i], 1);
        CHECK(r.next(op) == FrameReader::Result::NeedMore);
    }
    r.feed(two.data() + f.size() - 1, two.size() - f.size() + 1);
    CHECK(r.next(op) == FrameReader::Result::Frame && op == OpFrame);
    CHECK(r.next(op) == FrameReader::Result::Frame && op == OpPing);
    CHECK(r.next(op) == FrameReader::Result::NeedMore);
    CHECK(r.buffered() == 0);
    // An empty body is a frame; a length at the cap is accepted, one past it is malformed (not waited for).
    FrameReader e;
    const std::string empty = encodeFrame(OpClose, "");
    e.feed(empty.data(), empty.size());
    CHECK(e.next(op) == FrameReader::Result::Frame && op == OpClose);
    FrameReader cap;
    std::string atCap = encodeFrame(OpFrame, std::string(kMaxFrame, ' '));
    cap.feed(atCap.data(), atCap.size());
    CHECK(cap.next(op) == FrameReader::Result::Frame);
    for (uint32_t len : {uint32_t(kMaxFrame + 1), 0x7FFFFFFFu, 0xFFFFFFFFu}) {
        FrameReader big;
        const char hdr[8] = {1, 0, 0, 0, char(len & 0xFF), char((len >> 8) & 0xFF), char((len >> 16) & 0xFF), char(len >> 24)};
        big.feed(hdr, 8);
        CHECK(big.next(op) == FrameReader::Result::Bad);
        big.feed(f.data(), f.size()); // stays bad
        CHECK(big.next(op) == FrameReader::Result::Bad);
        CHECK(big.buffered() == 0);
    }
    FrameReader unknown;
    const std::string u = encodeFrame(5, "{}");
    unknown.feed(u.data(), u.size());
    CHECK(unknown.next(op) == FrameReader::Result::Bad);
}

TEST(discord_frame_reader_fuzz) {
    std::mt19937 rng(20261105);
    for (int round = 0; round < 2000; ++round) {
        FrameReader r;
        std::string data;
        const int pieces = 1 + int(rng() % 6);
        for (int p = 0; p < pieces; ++p) {
            if (rng() % 2) data += encodeFrame(rng() % 6, std::string(rng() % 64, 'j'));
            else for (int k = int(rng() % 40); k > 0; --k) data += char(rng() & 0xFF);
        }
        size_t at = 0;
        int frames = 0;
        bool bad = false;
        while (at < data.size() && !bad) {
            const size_t n = std::min<size_t>(1 + rng() % 17, data.size() - at);
            r.feed(data.data() + at, n);
            at += n;
            uint32_t op = 0;
            for (;;) {
                const FrameReader::Result res = r.next(op);
                if (res == FrameReader::Result::Bad) { bad = true; break; }
                if (res == FrameReader::Result::NeedMore) break;
                CHECK(op <= OpPong);
                ++frames;
            }
            CHECK(r.buffered() <= data.size()); // never holds more than it was given
        }
        CHECK(frames <= int(data.size() / kHeaderSize));
    }
}

TEST(discord_socket_candidates) {
    const auto env = [](const char* n) -> const char* {
        if (!std::strcmp(n, "XDG_RUNTIME_DIR")) return "/run/user/1000/";
        if (!std::strcmp(n, "TMPDIR")) return "relative/ignored";
        if (!std::strcmp(n, "TMP")) return "/tmp";
        return nullptr;
    };
    const std::vector<std::string> c = socketCandidates(env);
    CHECK(c.size() == 2 * 3 * 10); // /run/user/1000 and /tmp (once), each plain, Flatpak and Snap, 0..9
    CHECK(c.front() == "/run/user/1000/discord-ipc-0");
    CHECK(std::find(c.begin(), c.end(), "/run/user/1000/app/com.discordapp.Discord/discord-ipc-0") != c.end());
    CHECK(std::find(c.begin(), c.end(), "/run/user/1000/snap.discord/discord-ipc-9") != c.end());
    CHECK(c.back() == "/tmp/snap.discord/discord-ipc-9");
    for (const std::string& p : c) CHECK(p.find("relative") == std::string::npos && p[0] == '/');
}

TEST(discord_off_never_connects) {
    const uint64_t before = connectAttempts();
    {
        Client c("1234567890"); // a real-looking ID, but switched off
        Activity a;
        a.challenge = "Dragons";
        for (int i = 0; i < 5; ++i) {
            a.start = i;
            c.set(a);
        }
        CHECK(!c.connected());
    }
    {
        Client placeholder("0"); // no application: never connects
        Activity a;
        a.show = true;
        placeholder.set(a);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(!placeholder.connected());
    }
    CHECK(connectAttempts() == before);
}

TEST(discord_agent_runs_never_show) {
    const auto env = [](std::initializer_list<std::pair<const char*, const char*>> vars) {
        std::vector<std::pair<std::string, std::string>> v;
        for (const auto& p : vars) v.emplace_back(p.first, p.second);
        return [v](const char* n) -> const char* {
            for (const auto& p : v)
                if (p.first == n) return p.second.c_str();
            return nullptr;
        };
    };
    CHECK(!agentRun(env({}), "x11"));
    CHECK(!agentRun(env({{"SDL_APP_ID", "sculptforge"}}), "wayland"));
    CHECK(!agentRun(env({{"SDL_APP_ID", "sculptforge"}}), nullptr));
    CHECK(agentRun(env({{"SDL_APP_ID", "sculptforge-agent"}}), "x11"));
    CHECK(agentRun(env({{"SDL_VIDEO_DRIVER", "offscreen"}}), nullptr));
    CHECK(agentRun(env({}), "offscreen")); // the driver SDL picked
    CHECK(agentRun(env({{"SF_NO_DISCORD", "1"}}), "windows"));
    CHECK(!agentRun(env({{"SF_NO_DISCORD", "0"}}), "windows"));
    CHECK(!agentRun(env({{"SF_NO_DISCORD", ""}}), "windows"));
}

#if !defined(_WIN32)

namespace {

struct TempSocketDir {
    std::string dir;
    TempSocketDir() {
        char tmpl[] = "/tmp/sfdiscordXXXXXX"; // short: Unix socket paths are limited to ~100 bytes
        if (mkdtemp(tmpl)) dir = tmpl;
    }
    ~TempSocketDir() {
        for (int n = 0; n < 10; ++n) unlink((dir + "/discord-ipc-" + std::to_string(n)).c_str());
        unlink((dir + "/link").c_str());
        unlink((dir + "/file").c_str());
        rmdir(dir.c_str());
    }
};

int listenAt(const std::string& path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(fd, 4) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// Reads one frame (blocking, with a receive timeout); false on timeout or close.
bool readFrame(int fd, uint32_t& op, std::string& body) {
    char hdr[8];
    size_t got = 0;
    while (got < 8) {
        const ssize_t n = recv(fd, hdr + got, 8 - got, 0);
        if (n <= 0) return false;
        got += size_t(n);
    }
    op = uint32_t(uint8_t(hdr[0])) | uint32_t(uint8_t(hdr[1])) << 8;
    const uint32_t len = uint32_t(uint8_t(hdr[4])) | uint32_t(uint8_t(hdr[5])) << 8 | uint32_t(uint8_t(hdr[6])) << 16;
    body.assign(len, '\0');
    got = 0;
    while (got < len) {
        const ssize_t n = recv(fd, body.data() + got, len - got, 0);
        if (n <= 0) return false;
        got += size_t(n);
    }
    return true;
}

int acceptWithTimeout(int listener) {
    timeval tv{5, 0};
    setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    const int fd = accept(listener, nullptr, nullptr);
    if (fd >= 0) setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

struct EnvVar {
    std::string name, saved;
    bool had = false;
    EnvVar(const char* n, const std::string& value) : name(n) {
        if (const char* v = std::getenv(n)) saved = v, had = true;
        setenv(n, value.c_str(), 1);
    }
    ~EnvVar() {
        if (had) setenv(name.c_str(), saved.c_str(), 1);
        else unsetenv(name.c_str());
    }
};

// Discord's side of one connection: the handshake, READY, then the first activity. -1 on failure.
int acceptSession(int listener, std::string& activity) {
    const int c = acceptWithTimeout(listener);
    uint32_t op = 0;
    std::string hs;
    const std::string ready = encodeFrame(OpFrame, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\"}");
    if (c < 0 || !readFrame(c, op, hs) || op != OpHandshake || send(c, ready.data(), ready.size(), 0) != ssize_t(ready.size()) ||
        !readFrame(c, op, activity) || op != OpFrame) {
        if (c >= 0) close(c);
        return -1;
    }
    return c;
}

std::string stateOf(const std::string& frame) {
    Json j;
    std::string err;
    return Json::parseStrict(frame, j, err) ? j["args"]["activity"]["state"].asString() : std::string("?");
}

} // namespace

TEST(discord_socket_owner_check) {
    TempSocketDir t;
    CHECK(!t.dir.empty());
    const std::string sock = t.dir + "/discord-ipc-0";
    const int fd = listenAt(sock);
    CHECK(fd >= 0);
    CHECK(socketOwnedBy(sock, unsigned(geteuid())));
    CHECK(!socketOwnedBy(sock, unsigned(geteuid()) + 1)); // someone else's socket: refused
    CHECK(symlink(sock.c_str(), (t.dir + "/link").c_str()) == 0);
    CHECK(!socketOwnedBy(t.dir + "/link", unsigned(geteuid()))); // a symlink is never followed
    FILE* f = std::fopen((t.dir + "/file").c_str(), "w");
    if (f) std::fclose(f);
    CHECK(!socketOwnedBy(t.dir + "/file", unsigned(geteuid()))); // not a socket
    CHECK(!socketOwnedBy(t.dir + "/missing", unsigned(geteuid())));
    close(fd);
}

TEST(discord_session_with_a_fake_discord) {
    TempSocketDir t;
    const int listener = listenAt(t.dir + "/discord-ipc-0");
    CHECK(listener >= 0);
    if (listener < 0) return;
    EnvVar xdg("XDG_RUNTIME_DIR", t.dir);

    uint32_t op = 0;
    std::string body;
    std::string handshake, activity, clear;
    bool ok = true;
    std::thread discord([&] { // Discord's side: READY after the handshake, then reads the activity and the clear
        const int c = acceptWithTimeout(listener);
        ok = c >= 0 && readFrame(c, op, handshake) && op == OpHandshake;
        const std::string ready = encodeFrame(OpFrame, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\"}");
        ok = ok && send(c, ready.data(), ready.size(), 0) == ssize_t(ready.size());
        ok = ok && readFrame(c, op, activity) && op == OpFrame;
        // Commands Discord might send are ignored: an AUTHORIZE request and an event dispatch.
        const std::string noise = encodeFrame(OpFrame, "{\"cmd\":\"AUTHORIZE\"}") + encodeFrame(OpPing, "{}");
        ok = ok && send(c, noise.data(), noise.size(), 0) == ssize_t(noise.size());
        ok = ok && readFrame(c, op, clear) && op == OpFrame;
        if (c >= 0) close(c);
    });
    {
        Client client("1234567890");
        Activity a;
        a.show = true;
        a.start = 1793750400;
        a.fileName = "/somewhere/private/kraken.sfproj";
        client.set(a);
        for (int i = 0; i < 400 && client.activitiesSent() < 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(client.connected());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        a.show = false; // switched off: cleared
        client.set(a);
        for (int i = 0; i < 400 && client.activitiesSent() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    discord.join();
    close(listener);
    CHECK(ok);
    CHECK(parsed(handshake)["client_id"].asString() == "1234567890");
    const Json act = parsed(activity);
    CHECK(act["cmd"].asString() == "SET_ACTIVITY");
    CHECK(act["args"]["activity"]["state"].asString() == "kraken.sfproj");
    CHECK(activity.find("somewhere") == std::string::npos);
    const Json cl = parsed(clear);
    CHECK(cl["cmd"].asString() == "SET_ACTIVITY" && !cl["args"].has("activity"));
}

TEST(discord_malformed_reply_closes) {
    TempSocketDir t;
    const int listener = listenAt(t.dir + "/discord-ipc-0");
    CHECK(listener >= 0);
    if (listener < 0) return;
    EnvVar xdg("XDG_RUNTIME_DIR", t.dir);

    int accepted = 0;
    bool closedByClient = false;
    std::thread discord([&] {
        // First connection: an oversized length in reply to the handshake. The client must hang up.
        int c = acceptWithTimeout(listener);
        if (c < 0) return;
        ++accepted;
        uint32_t op = 0;
        std::string body;
        readFrame(c, op, body);
        const char hdr[8] = {1, 0, 0, 0, char(0xFF), char(0xFF), char(0xFF), char(0x7F)};
        send(c, hdr, 8, 0);
        char b;
        closedByClient = recv(c, &b, 1, 0) == 0; // EOF: closed, nothing sent after the bad frame
        close(c);
        // After the back-off it tries again.
        c = acceptWithTimeout(listener);
        if (c >= 0) ++accepted, close(c);
    });
    {
        Client client("1234567890");
        client.setTimingForTests(200, 1000);
        Activity a;
        a.show = true;
        client.set(a);
        for (int i = 0; i < 300 && accepted < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(client.activitiesSent() == 0);
    }
    discord.join();
    close(listener);
    CHECK(closedByClient);
    CHECK(accepted == 2);
}
// Discord quits and starts again while SculptForge runs: the client finds it again within seconds (2 s, not the
// 60 s back-off), sends what's shown again, and later changes still go out (Tony 11-05: "it doesn't update anymore").
TEST(discord_reconnects_after_discord_restarts) {
    TempSocketDir t;
    const std::string path = t.dir + "/discord-ipc-0";
    int listener = listenAt(path);
    CHECK(listener >= 0);
    if (listener < 0) return;
    EnvVar xdg("XDG_RUNTIME_DIR", t.dir);

    Client client("1234567890");
    client.setTimingForTests(60000, 1000, 0);
    Activity a;
    a.show = true;
    a.fileName = "kraken.sfproj";
    client.set(a);
    std::string first;
    int c = acceptSession(listener, first);
    CHECK(c >= 0 && stateOf(first) == "kraken.sfproj");
    // Discord quits: its socket goes away.
    if (c >= 0) close(c);
    close(listener);
    unlink(path.c_str());
    for (int i = 0; i < 700 && client.connected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(!client.connected()); // noticed at the next drain (every 5 s)
    // It starts again.
    listener = listenAt(path);
    CHECK(listener >= 0);
    const auto t0 = std::chrono::steady_clock::now();
    std::string again;
    c = acceptSession(listener, again);
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(c >= 0 && stateOf(again) == "kraken.sfproj"); // shown again without any change
    CHECK(waited < 4.5);
    // A change after the reconnect goes out.
    a.fileName = "dragon.sfproj";
    client.set(a);
    uint32_t op = 0;
    std::string changed;
    CHECK(c >= 0 && readFrame(c, op, changed) && stateOf(changed) == "dragon.sfproj");
    if (c >= 0) close(c);
    close(listener);
}

// Discord takes 5 activities in 20 s and drops the rest: changes in a burst come out spaced, and the last one
// (what the window shows now) is always sent.
TEST(discord_bursts_send_the_latest) {
    TempSocketDir t;
    const int listener = listenAt(t.dir + "/discord-ipc-0");
    CHECK(listener >= 0);
    if (listener < 0) return;
    EnvVar xdg("XDG_RUNTIME_DIR", t.dir);
    Client client("1234567890");
    client.setTimingForTests(60000, 1000, 400);
    Activity a;
    a.show = true;
    a.fileName = "a0.sfproj";
    client.set(a);
    std::string frame;
    const int c = acceptSession(listener, frame);
    CHECK(c >= 0 && stateOf(frame) == "a0.sfproj");
    if (c < 0) return;
    for (int i = 1; i <= 8; ++i) {
        a.fileName = "a" + std::to_string(i) + ".sfproj";
        client.set(a);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::vector<std::string> got;
    uint32_t op = 0;
    while (got.empty() || got.back() != "a8.sfproj") {
        if (!readFrame(c, op, frame)) break;
        got.push_back(stateOf(frame));
    }
    CHECK(!got.empty() && got.back() == "a8.sfproj");
    CHECK(got.size() <= 2); // not 8
    close(c);
    close(listener);
}

// Quit, a crash or a kill: the connection closes with the process (Discord then clears what it showed), even
// with a child process still running that was started after the connection (close-on-exec).
TEST(discord_killed_process_closes_the_connection) {
    TempSocketDir t;
    const int listener = listenAt(t.dir + "/discord-ipc-0");
    CHECK(listener >= 0);
    if (listener < 0) return;
    EnvVar xdg("XDG_RUNTIME_DIR", t.dir);
    const pid_t pid = fork();
    if (pid == 0) {
        Client* client = new Client("1234567890"); // never destroyed: the kill ends it
        Activity a;
        a.show = true;
        client->set(a);
        for (int i = 0; i < 300 && client->activitiesSent() < 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (fork() == 0) { // a worker started after the connection, outliving this process
            execl("/bin/sleep", "sleep", "3", static_cast<char*>(nullptr));
            _exit(127);
        }
        kill(getpid(), SIGKILL);
        _exit(1);
    }
    std::string activity;
    const int c = acceptSession(listener, activity);
    CHECK(c >= 0);
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status));
    if (c >= 0) {
        timeval tv{1, 0}; // well before the worker's 3 s are up
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char b;
        CHECK(recv(c, &b, 1, 0) == 0); // EOF: closed
        close(c);
    }
    close(listener);
}
// Quitting with a stuck teardown (a driver or a thread that never ends): the watchdog ends the process with its exit
// code, and the Discord connection closes with it, so the status goes (@ebarranco: "Playing SculptForge" stayed).
TEST(exit_watchdog_ends_a_stuck_process) {
    TempSocketDir t;
    const int listener = listenAt(t.dir + "/discord-ipc-0");
    CHECK(listener >= 0);
    if (listener < 0) return;
    EnvVar xdg("XDG_RUNTIME_DIR", t.dir);
    const pid_t pid = fork();
    if (pid == 0) {
        Client* client = new Client("1234567890"); // never destroyed: the teardown is stuck
        Activity a;
        a.show = true;
        client->set(a);
        for (int i = 0; i < 300 && client->activitiesSent() < 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        armExitWatchdog(200, 7);
        std::thread([] { for (;;) std::this_thread::sleep_for(std::chrono::seconds(1)); }).join(); // never returns
        _exit(1);
    }
    std::string activity;
    const int c = acceptSession(listener, activity);
    CHECK(c >= 0);
    int status = 0;
    bool exited = false;
    for (int i = 0; i < 500 && !exited; ++i) { // 5 s at most
        exited = waitpid(pid, &status, WNOHANG) == pid;
        if (!exited) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!exited) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    CHECK(exited);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 7);
    if (c >= 0) {
        timeval tv{1, 0};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char b;
        CHECK(recv(c, &b, 1, 0) == 0); // EOF: closed
        close(c);
    }
    close(listener);
}
#endif
