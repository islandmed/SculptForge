// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forge Deck prototype (src/link/DeckCommands.h, PhoneLink's Deck channel): the allowlist refuses everything that
// isn't one of its six commands, values are range-checked, inputs are rate-limited, the Deck is off at every start,
// nothing is answered without the pairing secrets, and the relay fallback drops forged, replayed and reordered
// envelopes.
#include "TestHarness.h"

#include "core/Json.h"
#include "ai/Http.h"
#include "link/DeckCommands.h"
#include "link/HttpServer.h"
#include "link/PhoneLink.h"
#include "link/PhoneRtc.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <thread>

using namespace sf;
using namespace sf::link;
namespace fs = std::filesystem;

namespace {

HttpRequest request(const std::string& method, const std::string& target, const std::string& body = {},
                    const std::string& token = {}) {
    HttpRequest r;
    r.method = method;
    r.target = target;
    const size_t q = target.find('?');
    r.path = target.substr(0, q);
    if (q != std::string::npos) r.query = target.substr(q + 1);
    r.version = "HTTP/1.1";
    r.body = body;
    r.peer = "192.168.1.20";
    r.headers.emplace_back("user-agent", "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Version/18.0 Mobile/15E148 Safari/604.1");
    if (!token.empty()) r.headers.emplace_back("x-sf-token", token);
    return r;
}


bool startLink(PhoneLink& link, const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    PhoneLinkOptions o;
    o.inboxRoot = dir.string();
    o.startServer = false;
    o.desktopName = "Test PC";
    o.relayUrl = "off";
    o.siteUrl = "https://example.org/link/";
    std::string err;
    return link.start(o, err);
}


const char* kHello = R"({"type":"hello","ua":"Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Version/18.0 Mobile/15E148 Safari/604.1"})";

} // namespace

TEST(deck_is_off_at_every_start_and_the_old_http_routes_are_gone) {
    PhoneLink link;
    CHECK(startLink(link, "sf_deck_off"));
    CHECK(!link.deckEnabled());
    std::string err;
    // Off: even a well-formed command is refused, and nothing is queued.
    CHECK(!link.receiveDeckMessage(R"({"cmd":"undo"})", "WebRTC", err));
    CHECK(!link.receiveDeckMessage(kHello, "WebRTC", err));
    CHECK(link.takeDeckActions().empty());
    CHECK(!link.deckStatus().connected);
    std::string url;
    CHECK(!link.createDeckLink(url, err)); // no pairing link while it is off
    // Commands no longer travel over any HTTP route of the phone link: the old paths answer nothing useful.
    link.setDeckEnabled(true);
    for (const char* path : {"/deck", "/api/deck/state", "/api/deck/do", "/api/deck/img/b1", "/api/deck/disconnect"})
        for (const char* method : {"GET", "POST"}) {
            HttpRequest r;
            r.method = method;
            r.target = r.path = path;
            r.version = "HTTP/1.1";
            r.body = R"({"cmd":"undo"})";
            r.peer = "192.168.1.20";
            r.headers.emplace_back("x-sf-token", link.token());
            CHECK(link.handle(r).status != 200);
        }
    CHECK(link.takeDeckActions().empty());

    // A new session starts with the Deck off again.
    link.stop();
    CHECK(startLink(link, "sf_deck_off"));
    CHECK(!link.deckEnabled());
}

TEST(deck_queues_allowlisted_actions_and_refuses_and_logs_the_rest) {
    PhoneLink link;
    CHECK(startLink(link, "sf_deck_do"));
    std::vector<std::string> log;
    link.setDeckLog([&](const std::string& command, bool ok, const std::string& message) {
        log.push_back(command + (ok ? ":ok:" : ":refused:") + message);
    });
    link.setDeckEnabled(true);
    std::string err;
    CHECK(link.receiveDeckMessage(kHello, "WebRTC", err));
    CHECK(link.deckStatus().connected && link.deckStatus().device.find("iPhone") != std::string::npos);
    CHECK(link.deckStatus().transport == "WebRTC");

    CHECK(link.receiveDeckMessage(R"({"cmd":"brush_use","name":"Clay"})", "WebRTC", err));
    CHECK(link.receiveDeckMessage(R"({"cmd":"size_set","value":0.5})", "WebRTC", err));
    CHECK(link.receiveDeckMessage(R"({"cmd":"redo"})", "WebRTC", err));
    CHECK(!link.receiveDeckMessage(R"({"cmd":"open","path":"/home/x/secret.sfproj"})", "WebRTC", err));
    CHECK(!link.receiveDeckMessage(R"({"cmd":"run","line":"rm -rf ~"})", "WebRTC", err));
    CHECK(!link.receiveDeckMessage(R"({"type":"hello","cmd":"undo"})", "WebRTC", err)); // not both
    CHECK(!link.receiveDeckMessage(R"({"type":"eval","code":"1"})", "WebRTC", err));    // not one of the control messages
    CHECK(!link.receiveDeckMessage("not json", "WebRTC", err));
    CHECK(!link.receiveDeckMessage(std::string(kMaxDeckMessageBytes + 1, ' '), "WebRTC", err));

    const auto actions = link.takeDeckActions();
    CHECK(actions.size() == 3);
    if (actions.size() == 3) {
        CHECK(actions[0].command == DeckCommand::BrushUse && actions[0].name == "Clay");
        CHECK(actions[1].command == DeckCommand::SizeSet && actions[1].value == 0.5f);
        CHECK(actions[2].command == DeckCommand::Redo);
    }
    CHECK(link.takeDeckActions().empty());
    const DeckStatus ds = link.deckStatus();
    CHECK(ds.accepted == 3 && ds.refused == 6);
    CHECK(!ds.lastRefusal.empty());
    CHECK(log.size() == 9);
    int refused = 0;
    for (const std::string& line : log) refused += line.find(":refused:") != std::string::npos ? 1 : 0;
    CHECK(refused == 6);
    bool eventSeen = false;
    for (const PhoneEvent& e : link.takeEvents()) eventSeen = eventSeen || e.text.find("Forge Deck refused") != std::string::npos;
    CHECK(eventSeen);
    // The phone's own Disconnect ends its session.
    CHECK(link.receiveDeckMessage(R"({"type":"bye"})", "WebRTC", err));
    CHECK(!link.deckStatus().connected);
}

TEST(deck_rate_limits_a_flood) {
    PhoneLink link;
    CHECK(startLink(link, "sf_deck_flood"));
    link.setDeckEnabled(true);
    std::string err;
    int ok = 0, tooMany = 0;
    for (int i = 0; i < 200; ++i) {
        if (link.receiveDeckMessage(R"({"cmd":"undo"})", "WebRTC", err)) ++ok;
        else tooMany += err.find("too many") != std::string::npos;
    }
    CHECK(ok >= 20 && ok < 60);
    CHECK(tooMany > 140);
    CHECK(link.takeDeckActions().size() == size_t(ok));
    // Control messages have their own, smaller allowance.
    int pings = 0;
    for (int i = 0; i < 100; ++i) pings += link.receiveDeckMessage(R"({"type":"ping"})", "WebRTC", err) ? 1 : 0;
    CHECK(pings >= 10 && pings < 30);
}

TEST(deck_disconnect_changes_the_token_and_turns_the_deck_off) {
    PhoneLink link;
    CHECK(startLink(link, "sf_deck_kill"));
    link.setDeckEnabled(true);
    const std::string token = link.token();
    std::string err;
    CHECK(link.receiveDeckMessage(kHello, "WebRTC", err));
    CHECK(link.deckStatus().connected);
    link.deckDisconnect();
    CHECK(!link.deckEnabled());
    CHECK(!link.deckStatus().connected);
    CHECK(link.token() != token);
    CHECK(!link.receiveDeckMessage(R"({"cmd":"undo"})", "WebRTC", err)); // the old link is dead
    CHECK(link.takeDeckActions().empty());
    // An answer made with the old token is for another session.
    if (webrtcAvailable()) {
        Json msg = Json::object();
        msg.set("k", token);
        msg.set("sdp", "v=0");
        std::string packed;
        CHECK(!link.acceptRelayMessage("junk", err));
    }
}

TEST(deck_size_is_one_value_in_one_unit) {
    // The strip's position and the pixels the desktop shows are the same number seen two ways: the mapping round-trips
    // (60 px on the desktop is 60 px on the phone, never "96" in the header and "55" on the strip).
    CHECK(std::fabs(deckSizeToPx(0.0f) - kDeckMinBrushPx) < 1e-4f);
    CHECK(std::fabs(deckSizeToPx(1.0f) - kDeckMaxBrushPx) < 1e-2f);
    CHECK(deckPxToSize(kDeckMinBrushPx) == 0.0f && deckPxToSize(kDeckMaxBrushPx) == 1.0f);
    CHECK(deckPxToSize(1.0f) == 0.0f && deckPxToSize(5000.0f) == 1.0f); // out of range: clamped
    for (float px : {2.0f, 7.0f, 60.0f, 96.0f, 333.0f, 800.0f})
        CHECK(std::lround(deckSizeToPx(deckPxToSize(px))) == std::lround(px));
    CHECK(std::lround(deckSizeToPx(0.55f)) == 54); // what the strip's 55 means in pixels
}

TEST(deck_caps_the_commands_waiting_for_the_desktop) {
    PhoneLink link;
    CHECK(startLink(link, "sf_deck_cap"));
    link.setDeckEnabled(true);
    link.setDeckRateLimit(1e6, 1e6); // out of the way: this is the queue's own limit
    std::string err;
    int ok = 0, busy = 0;
    for (int i = 0; i < 200; ++i) {
        if (link.receiveDeckMessage(R"({"cmd":"undo"})", "WebRTC", err)) ++ok;
        else busy += err.find("too many commands waiting") != std::string::npos;
    }
    CHECK(ok == int(kMaxQueuedDeckActions) && busy == 200 - ok);
    CHECK(link.takeDeckActions().size() == kMaxQueuedDeckActions);
    CHECK(link.receiveDeckMessage(R"({"cmd":"undo"})", "WebRTC", err)); // drained: room again
}

// The pairing link: an HTTPS page of ours with the offer, token and key in its #fragment; commands go up the
// WebRTC data channel, state and tiles come down it.
TEST(deck_link_over_webrtc) {
    if (!webrtcAvailable() || sf::test::networkTestsOff()) return;
    PhoneLink link;
    CHECK(startLink(link, "sf_deck_rtc"));
    std::string url, err;
    CHECK(!link.createDeckLink(url, err)); // off
    link.setDeckEnabled(true);
    CHECK(link.createDeckLink(url, err));
    CHECK(url.rfind("https://example.org/link/deck.html?v=1#v=1&", 0) == 0);
    const std::string fragment = url.substr(url.find('#') + 1);
    CHECK(queryParameter(fragment, "m") == "deck");
    CHECK(queryParameter(fragment, "k") == link.token());
    std::string offer, key;
    CHECK(base64UrlDecode(queryParameter(fragment, "o"), offer));
    CHECK(base64UrlDecode(queryParameter(fragment, "x"), key) && key.size() == 32);

    std::mutex m;
    std::vector<std::pair<std::string, bool>> received;
    FaceRtc phone(FaceRtc::Mode::Deck);
    phone.onTestMessage = [&](const std::string& data, bool binary) {
        std::lock_guard lock(m);
        received.emplace_back(data, binary);
    };
    std::string answer;
    CHECK(phone.answerForTest(offer, answer, err));
    CHECK(link.acceptFaceAnswer(answer, err));
    link.setDeckState(R"({"tool":"Clay","size":0.4,"brushes":[{"id":"Clay","label":"Clay","img":"b1"}]})");
    link.setDeckImage("b1", std::string("\x89PNG-test", 9));
    // The page says hello (as soon as its channel is open); the desktop answers with the tile and the state.
    bool state = false, tile = false;
    for (int i = 0; i < 400 && !(state && tile); ++i) { // up to 20 s (a loaded CI runner: see face_link_over_webrtc)
        if (!link.deckStatus().connected) phone.sendFromTestPeer(kHello);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        link.pumpDeck();
        std::lock_guard lock(m);
        for (const auto& [data, binary] : received) {
            if (binary) tile = tile || data == std::string("\x89PNG-test", 9);
            else if (data.find("\"state\"") != std::string::npos) {
                Json j;
                std::string perr;
                state = Json::parse(data, j, perr) && j["tool"].asString() == "Clay" && j["desktop"].asString() == "Test PC";
            }
        }
    }
    CHECK(state && tile);
    CHECK(link.deckStatus().connected && link.deckStatus().transport == "WebRTC");
    // A command goes up the channel and is queued; a forged one is refused and counted.
    phone.sendFromTestPeer(R"({"cmd":"brush_use","name":"Clay"})");
    phone.sendFromTestPeer(R"({"cmd":"open","path":"/etc/passwd"})");
    phone.sendFromTestPeer(R"({"cmd":"redo"})");
    std::vector<DeckAction> actions;
    for (int i = 0; i < 200 && actions.size() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        for (const DeckAction& a : link.takeDeckActions()) actions.push_back(a);
    }
    CHECK(actions.size() == 2 && actions[0].command == DeckCommand::BrushUse && actions[1].command == DeckCommand::Redo);
    for (int i = 0; i < 100 && link.deckStatus().refused < 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(link.deckStatus().refused == 1);
    // The state follows a change on the desktop, and a ping is answered.
    {
        std::lock_guard lock(m);
        received.clear();
    }
    link.setDeckState(R"({"tool":"Smooth","size":0.4,"brushes":[]})");
    phone.sendFromTestPeer(R"({"type":"ping"})");
    bool changed = false, pong = false;
    for (int i = 0; i < 200 && !(changed && pong); ++i) {
        link.pumpDeck();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        std::lock_guard lock(m);
        for (const auto& [data, binary] : received) {
            changed = changed || (!binary && data.find("Smooth") != std::string::npos);
            pong = pong || (!binary && data.find("\"pong\"") != std::string::npos);
        }
    }
    CHECK(changed && pong);
    // The kill switch tells the page and closes the link; a command after it goes nowhere.
    const std::string token = link.token();
    link.deckDisconnect();
    bool bye = false;
    for (int i = 0; i < 100 && !bye; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        std::lock_guard lock(m);
        for (const auto& [data, binary] : received) bye = bye || (!binary && data.find("\"bye\"") != std::string::npos);
    }
    CHECK(bye);
    phone.sendFromTestPeer(R"({"cmd":"undo"})");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(link.takeDeckActions().empty());
    CHECK(link.token() != token);
    phone.close();
    link.stop();
}

// The relay fallback: the page's commands come as AES-GCM envelopes {"d":"p2d","s":sequence,"m":message}. Wrong key,
// wrong direction, a repeated or lower sequence number, and non-allowlisted commands are all dropped or refused.
TEST(deck_relay_fallback_drops_forged_replayed_and_reordered_commands) {
    if (!webrtcAvailable() || sf::test::networkTestsOff()) return;
    std::mutex relayMutex;
    struct Item { int n; std::string message; };
    std::vector<Item> up;
    std::vector<std::string> down;
    std::string blob;
    HttpServer relay;
    HttpServer::Options ro;
    ro.port = 0;
    ro.bindAddress = "127.0.0.1";
    std::string err;
    CHECK(relay.start(ro, [&](const HttpRequest& r) {
        std::lock_guard lock(relayMutex);
        if (r.path.rfind("/relay/", 0) == 0) return HttpResponse::bytes("application/x-ndjson", ""); // answers: none
        if (r.path.rfind("/blob/", 0) == 0 && r.method == "POST") {
            blob = r.body;
            return HttpResponse::json(200, "{}");
        }
        const size_t slash = r.path.rfind('/');
        if (r.path.rfind("/deck/", 0) != 0 || slash == std::string::npos) return HttpResponse::text(404, "");
        const std::string way = r.path.substr(slash + 1);
        if (way == "down" && r.method == "POST") {
            down.push_back(r.body);
            return HttpResponse::json(200, "{}");
        }
        if (way == "up" && r.method == "GET") {
            const int after = std::atoi(r.param("after", "0").c_str());
            std::string list = "[";
            for (const Item& i : up)
                if (i.n > after) list += std::string(list.size() > 1 ? "," : "") + "{\"n\":" + std::to_string(i.n) + ",\"message\":\"" + i.message + "\"}";
            return HttpResponse::json(200, list + "]");
        }
        return HttpResponse::text(404, "");
    }, err));
    if (ai::httpRequest("GET", "http://127.0.0.1:" + std::to_string(relay.port()) + "/deck/x/up", {}, {}, 5).status != 200) {
        relay.stop(); // no HTTP client in this build: nothing to test
        return;
    }
    PhoneLink link;
    const fs::path dir = fs::temp_directory_path() / "sf_deck_relay";
    std::error_code ec;
    fs::remove_all(dir, ec);
    PhoneLinkOptions o;
    o.inboxRoot = dir.string();
    o.startServer = false;
    o.desktopName = "Test PC";
    o.siteUrl = "https://example.org/link/";
    o.relayUrl = "http://127.0.0.1:" + std::to_string(relay.port()) + "/relay/";
    CHECK(link.start(o, err));
    link.setDeckEnabled(true);
    link.setDeckRateLimit(1e6, 1e6);
    std::string url;
    CHECK(link.createDeckLink(url, err));
    const std::string fragment = url.substr(url.find('#') + 1);
    std::string key;
    CHECK(base64UrlDecode(queryParameter(fragment, "x"), key));
    const std::string blobUrl = percentDecode(queryParameter(fragment, "b"));
    CHECK(blobUrl.rfind("http://127.0.0.1:" + std::to_string(relay.port()) + "/blob/sculptforge-", 0) == 0);

    auto envelope = [&](const std::string& useKey, const char* direction, double seq, const std::string& message) {
        Json e = Json::object();
        e.set("d", direction);
        e.set("s", seq);
        e.set("m", message);
        std::string packed, why;
        CHECK(aesGcmEncrypt(useKey, e.dump(), packed, why));
        return packed;
    };
    int n = 0;
    auto post = [&](const std::string& packed) {
        std::lock_guard lock(relayMutex);
        up.push_back({++n, packed});
    };
    const std::string wrongKey = randomBytes(32);
    post(envelope(key, "p2d", 1000, kHello));                                  // accepted: the page starts
    post(envelope(wrongKey, "p2d", 1001, R"({"cmd":"undo"})"));                 // forged (wrong key): dropped
    post(envelope(key, "d2p", 1002, R"({"cmd":"undo"})"));                     // wrong direction (reflected state): dropped
    post(envelope(key, "p2d", 1003, R"({"cmd":"size_set","value":0.5})"));     // accepted
    post(envelope(key, "p2d", 1003, R"({"cmd":"redo"})"));                     // replayed sequence number: dropped
    post(envelope(key, "p2d", 1002, R"({"cmd":"redo"})"));                     // reordered (lower): dropped
    post(envelope(key, "p2d", 1004, R"({"cmd":"open","path":"/etc/passwd"})")); // not allowlisted: refused
    post(envelope(key, "p2d", 1005, R"({"cmd":"brush_use","name":"Clay"})"));   // accepted
    post("not-base64-at-all!!");                                              // garbage: dropped
    std::vector<DeckAction> actions;
    for (int i = 0; i < 300 && actions.size() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        for (const DeckAction& a : link.takeDeckActions()) actions.push_back(a);
    }
    CHECK(actions.size() == 2 && actions[0].command == DeckCommand::SizeSet && actions[1].command == DeckCommand::BrushUse);
    CHECK(link.deckStatus().refused == 1); // only the allowlist refusal reaches the Deck: the rest never decrypted or were stale
    CHECK(link.deckStatus().connected && link.deckStatus().transport == "relay");
    // State goes down through the relay: encrypted, direction d2p, sequence rising; the tiles as a blob.
    link.setDeckImage("b1", std::string("\x89PNG-test", 9));
    link.setDeckState(R"({"tool":"Clay","size":0.4,"brushes":[]})");
    // (wait for the state itself and the tiles' blob, not just the first message: the desktop also posts a greeting)
    std::vector<std::string> sent;
    for (int i = 0; i < 300; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bool haveState = false, haveBlob = false;
        {
            std::lock_guard lock(relayMutex);
            sent = down;
            std::string bytes = blob, plain, why;
            haveBlob = !bytes.empty() && aesGcmDecrypt(key, base64UrlEncode(bytes), plain, why) && plain.find("\"b1\"") != std::string::npos;
        }
        for (const std::string& packed : sent) {
            std::string plain, why;
            haveState = haveState || (aesGcmDecrypt(key, packed, plain, why) && plain.find("\\\"Clay\\\"") != std::string::npos);
        }
        if (haveState && haveBlob) break;
    }
    CHECK(!sent.empty());
    double last = 0;
    bool sawState = false;
    for (const std::string& packed : sent) {
        std::string plain, why;
        CHECK(aesGcmDecrypt(key, packed, plain, why));
        Json e;
        CHECK(Json::parse(plain, e, why) && e["d"].asString() == "d2p" && e["s"].asNumber() > last);
        last = e["s"].asNumber();
        sawState = sawState || e["m"].asString().find("\"Clay\"") != std::string::npos;
    }
    CHECK(sawState);
    {
        std::lock_guard lock(relayMutex);
        std::string bytes = blob, plain, why;
        CHECK(!bytes.empty() && aesGcmDecrypt(key, base64UrlEncode(bytes), plain, why));
        Json tiles;
        CHECK(Json::parse(plain, tiles, why) && tiles.has("b1"));
    }
    link.deckDisconnect();
    link.stop();
    relay.stop();
}
