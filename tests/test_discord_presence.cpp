// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Show SculptForge on Discord (core/DiscordPresence.h): what is sent (the name only, the optional challenge and
// file name, never a path), sanitising, the frame reader's bounds (malformed frames, oversized lengths, fuzz),
// the socket ownership check (wrong owner, symlink), off = zero connections, and a whole session against a fake
// Discord on a Unix socket (handshake, activity, clear; a malformed reply closes the connection).
#include "TestHarness.h"

#include "core/DiscordPresence.h"
#include "core/Json.h"

#include <chrono>
#include <cstring>
#include <random>
#include <thread>

#if !defined(_WIN32)
#include <cstdlib>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
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
    CHECK(act["assets"]["large_image"].asString() == "logo");
    CHECK(act["assets"]["large_text"].asString() == "SculptForge");
    CHECK(act["timestamps"]["start"].asNumber() == 1793750400.0);
    // Nothing else: no buttons, party, secrets (join / spectate) or small image.
    for (const auto& m : act.members()) CHECK(m.first == "assets" || m.first == "timestamps");
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
#endif
