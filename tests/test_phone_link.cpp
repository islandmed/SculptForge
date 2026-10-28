// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The phone link (src/link): the HTTP request parser (with a fuzz pass), the server on a real socket, pairing,
// resumable uploads into the Captures inbox, the face message parser and takes, the WebRTC face link (when
// built with it), the QR code and the AR export (GLB and USDZ validity).
#include "TestHarness.h"

#include "core/Json.h"
#include "core/Primitives.h"
#include "ai/Http.h"
#include "core/Texture.h"
#include "io/ArExport.h"
#include "io/GltfIO.h"
#include "io/Zip.h"
#include "link/FaceFeed.h"
#include "link/HttpServer.h"
#include "link/PhoneLink.h"
#include "link/PhoneRtc.h"
#include "link/Qr.h"
#include "link/Socket.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <thread>

using namespace sf;
using namespace sf::link;
namespace fs = std::filesystem;

namespace {

HttpRequestParser::State parseAll(HttpRequestParser& p, const std::string& text) { return p.feed(text.data(), text.size()); }

std::string readText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

fs::path freshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

fs::path upath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string str(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

HttpRequest request(const std::string& method, const std::string& target, const std::string& token = {},
                    const std::string& body = {}) {
    HttpRequest r;
    r.method = method;
    r.target = target;
    const size_t q = target.find('?');
    r.path = target.substr(0, q);
    r.query = q == std::string::npos ? std::string() : target.substr(q + 1);
    r.version = "HTTP/1.1";
    if (!token.empty()) r.headers.emplace_back("x-sf-token", token);
    r.headers.emplace_back("user-agent", "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Version/18.0 Mobile/15E148 Safari/604.1");
    r.body = body;
    r.peer = "192.168.1.33";
    return r;
}

Json parseJson(const std::string& text) {
    Json j;
    std::string err;
    Json::parse(text, j, err);
    return j;
}

PhoneLinkOptions testOptions(const fs::path& inbox) {
    PhoneLinkOptions o;
    o.inboxRoot = str(inbox);
    o.startServer = false;
    o.desktopName = "Test PC";
    o.version = "0.0.0";
    o.relayUrl = "off";
    o.siteUrl = "https://example.org/link/";
    return o;
}

std::string faceJson(float jaw, float t = 0) {
    return "{\"type\":\"face\",\"t\":" + std::to_string(t) + ",\"weights\":{\"jawOpen\":" + std::to_string(jaw) +
           ",\"eyeBlinkLeft\":0.25,\"_neutral\":0.1},\"head\":[0,0,0,1]}";
}

} // namespace

// --- HTTP parser ---

TEST(http_parser_reads_a_request) {
    HttpRequestParser p;
    CHECK(parseAll(p, "GET /api/status?k=ab%20c&x=1+2 HTTP/1.1\r\nHost: 10.0.0.2:8765\r\nUser-Agent: Phone\r\n\r\n") ==
          HttpRequestParser::State::Done);
    const HttpRequest& r = p.request();
    CHECK(r.method == "GET");
    CHECK(r.path == "/api/status");
    CHECK(r.param("k") == "ab c");
    CHECK(r.param("x") == "1 2");
    CHECK(r.param("missing", "none") == "none");
    CHECK(r.header("user-agent") == "Phone");
    CHECK(r.header("host") == "10.0.0.2:8765");
    CHECK(r.keepAlive());
    CHECK(r.body.empty());
}

TEST(http_parser_handles_split_bodies_and_pipelining) {
    const std::string two = "PUT /api/file/ab12?offset=0 HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello"
                            "POST /api/pair HTTP/1.1\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
    // Byte by byte gives the same as all at once.
    HttpRequestParser p;
    size_t i = 0;
    while (i < two.size() && !p.done()) p.feed(&two[i++], 1);
    CHECK(p.done());
    CHECK(p.request().method == "PUT");
    CHECK(p.request().body == "hello");
    // The rest of the stream is the next request.
    p.feed(two.data() + i, two.size() - i);
    CHECK(p.next() == HttpRequestParser::State::Done);
    CHECK(p.request().method == "POST");
    CHECK(p.request().body == "{}");
    CHECK(!p.request().keepAlive());
}

TEST(http_parser_rejects_bad_requests) {
    struct Case {
        std::string text;
        int status;
    };
    const std::string bigHeader = "GET / HTTP/1.1\r\nX: " + std::string(20000, 'a') + "\r\n\r\n";
    const Case cases[] = {
        {"GET /\r\n\r\n", 400},                                                    // no version
        {"GET / HTTP/2.0\r\n\r\n", 505},                                           // unsupported version
        {"GET http://evil/ HTTP/1.1\r\n\r\n", 400},                                // absolute form
        {"G(T / HTTP/1.1\r\n\r\n", 400},                                           // bad method
        {"GET / HTTP/1.1\r\nBad Header: x\r\n\r\n", 400},                          // space in a name
        {"GET / HTTP/1.1\r\nA: b\r\n folded\r\n\r\n", 400},                        // obsolete folding
        {"POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n", 400},
        {"POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n", 400}, // smuggling attempt
        {"POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", 501},
        {"POST / HTTP/1.1\r\nContent-Length: 999999999999\r\n\r\n", 413},
        {bigHeader, 431},
        {"GET /a\x01 HTTP/1.1\r\n\r\n", 400},
    };
    for (const Case& c : cases) {
        HttpRequestParser p;
        p.feed(c.text.data(), c.text.size());
        CHECK(p.failed());
        CHECK(p.errorStatus() == c.status);
    }
    // An endless head without a blank line fails once it passes the limit.
    HttpRequestParser p;
    const std::string line = "X-Fill: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n";
    p.feed("GET / HTTP/1.1\r\n", 16);
    for (int i = 0; i < 1000 && !p.failed(); ++i) p.feed(line.data(), line.size());
    CHECK(p.failed() && p.errorStatus() == 431);
}

TEST(http_parser_survives_fuzzing) {
    // Mutated valid requests and random bytes, fed in random pieces: the parser must end in a defined state,
    // never read past what it was given, and agree with itself whatever the split.
    const std::string seeds[] = {
        "GET /api/status?k=0123 HTTP/1.1\r\nHost: x\r\n\r\n",
        "PUT /api/file/00ff?offset=4194304 HTTP/1.1\r\nContent-Length: 8\r\nX-SF-Token: abc\r\n\r\n\x00\x01\x02\x03\x04\x05\x06\x07",
        "POST /api/capture HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 16\r\n\r\n{\"kind\":\"splat\"}",
    };
    std::mt19937 rng(1234);
    int done = 0, failed = 0;
    for (int iter = 0; iter < 4000; ++iter) {
        std::string text = seeds[iter % 3];
        if (iter % 3 == 2 && iter % 7 == 0) text.resize(rng() % text.size());
        const int mutations = int(rng() % 6);
        for (int m = 0; m < mutations && !text.empty(); ++m) {
            const size_t at = rng() % text.size();
            switch (rng() % 4) {
            case 0: text[at] = char(rng() & 0xFF); break;
            case 1: text.erase(at, 1); break;
            case 2: text.insert(at, 1, char(rng() & 0xFF)); break;
            default: text.insert(at, "\r\n"); break;
            }
        }
        if (iter % 50 == 0) { // pure noise
            text.resize(rng() % 300);
            for (char& c : text) c = char(rng() & 0xFF);
        }
        HttpLimits limits;
        limits.maxHeadBytes = 512;
        limits.maxBodyBytes = 1024;
        HttpRequestParser whole(limits), pieces(limits);
        whole.feed(text.data(), text.size());
        size_t pos = 0;
        while (pos < text.size()) {
            const size_t n = std::min(text.size() - pos, size_t(1 + rng() % 17));
            pieces.feed(text.data() + pos, n);
            pos += n;
        }
        CHECK(whole.state() == pieces.state());
        if (whole.done()) {
            ++done;
            CHECK(whole.request().method == pieces.request().method);
            CHECK(whole.request().body == pieces.request().body);
            CHECK(whole.request().body.size() == whole.contentLength());
        }
        if (whole.failed()) {
            ++failed;
            CHECK(whole.errorStatus() >= 400 && whole.errorStatus() < 600);
        }
    }
    CHECK(done > 100);
    CHECK(failed > 100);
}

TEST(http_helpers) {
    CHECK(percentDecode("a%2Fb%zz%4") == "a/b%zz%4");
    CHECK(percentDecode("a+b", true) == "a b");
    CHECK(percentEncode("http://1.2.3.4:5/?a=b c") == "http%3A%2F%2F1.2.3.4%3A5%2F%3Fa%3Db%20c");
    CHECK(queryParameter("a=1&b=&c", "b", "x").empty());
    CHECK(queryParameter("a=1&b=&c", "c", "x").empty());
    CHECK(queryParameter("a=1&b=&c", "d", "x") == "x");
    CHECK(isLocalNetworkAddress("192.168.1.20"));
    CHECK(isLocalNetworkAddress("10.0.0.117"));
    CHECK(isLocalNetworkAddress("172.31.255.1"));
    CHECK(!isLocalNetworkAddress("172.32.0.1"));
    CHECK(isLocalNetworkAddress("127.0.0.1"));
    CHECK(isLocalNetworkAddress("100.99.23.78"));
    CHECK(!isLocalNetworkAddress("8.8.8.8"));
    CHECK(!isLocalNetworkAddress("192.168.1"));
    CHECK(!isLocalNetworkAddress("192.168.1.256"));
    CHECK(!isLocalNetworkAddress("::1"));
    const std::string head = httpResponseHead(HttpResponse::json(200, "{}"), true);
    CHECK(head.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(head.find("Content-Length: 2\r\n") != std::string::npos);
    CHECK(head.find("Connection: keep-alive") != std::string::npos);
}

TEST(http_server_answers_on_a_socket) {
    HttpServer server;
    HttpServer::Options o;
    o.port = 0;
    o.bindAddress = "127.0.0.1";
    std::string err;
    CHECK(server.start(o, [](const HttpRequest& r) { return HttpResponse::text(200, r.method + " " + r.path + " " + r.body); }, err));
    CHECK(server.port() != 0);
    // Two requests on separate connections, one of them with a body.
    HttpClientResponse a = httpLocalRequest("127.0.0.1", server.port(), "GET", "/hello?x=1");
    CHECK(a.status == 200);
    CHECK(a.body == "GET /hello ");
    HttpClientResponse b = httpLocalRequest("127.0.0.1", server.port(), "POST", "/data", std::string(100000, 'z'));
    CHECK(b.status == 200);
    CHECK(b.body.size() == std::string("POST /data ").size() + 100000);
    // Keep-alive: two requests on one connection.
    Socket s;
    CHECK(s.connect("127.0.0.1", server.port(), err));
    CHECK(s.sendAll("GET /one HTTP/1.1\r\n\r\nGET /two HTTP/1.1\r\nConnection: close\r\n\r\n", err));
    std::string all;
    char buf[4096];
    for (int i = 0; i < 50; ++i) {
        const long long n = s.readSome(buf, sizeof buf, 100);
        if (n < 0) break;
        all.append(buf, size_t(std::max<long long>(n, 0)));
    }
    CHECK(all.find("GET /one") != std::string::npos);
    CHECK(all.find("GET /two") != std::string::npos);
    // A malformed request gets its error status and the connection closes.
    Socket bad;
    CHECK(bad.connect("127.0.0.1", server.port(), err));
    CHECK(bad.sendAll("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", err));
    std::string reply;
    for (int i = 0; i < 50; ++i) {
        const long long n = bad.readSome(buf, sizeof buf, 100);
        if (n < 0) break;
        reply.append(buf, size_t(std::max<long long>(n, 0)));
    }
    CHECK(reply.rfind("HTTP/1.1 501", 0) == 0);
    server.stop();
    CHECK(!server.running());
}

// --- Session: pairing, uploads, the inbox ---

TEST(phone_upload_names_are_safe) {
    CHECK(sanitizeUploadName("IMG_0001.JPG") == "IMG_0001.JPG");
    CHECK(sanitizeUploadName("../../etc/passwd") == "passwd");
    CHECK(sanitizeUploadName("..\\..\\Windows\\scan.ply") == "scan.ply");
    CHECK(sanitizeUploadName(".hidden.ply") == "hidden.ply");
    CHECK(sanitizeUploadName("my scan (1).mov") == "my_scan__1_.mov");
    CHECK(sanitizeUploadName("CON.ply") == "file_CON.ply");
    CHECK(sanitizeUploadName("capture.json") == "capture_file.json");
    CHECK(sanitizeUploadName("....") == "");
    CHECK(sanitizeUploadName("") == "");
    CHECK(sanitizeUploadName("caf\xC3\xA9.jpg") == "caf.jpg");
    CHECK(sanitizeUploadName(std::string(300, 'a') + ".mp4").size() <= 80);
    CHECK(sanitizeUploadName(std::string(300, 'a') + ".mp4").find(".mp4") != std::string::npos);
    CHECK(captureExtensionAllowed(CaptureKind::Splat, ".mov"));
    CHECK(captureExtensionAllowed(CaptureKind::Splat, ".jpg"));
    CHECK(!captureExtensionAllowed(CaptureKind::Photo, ".mov"));
    CHECK(captureExtensionAllowed(CaptureKind::Ply, ".ply"));
    CHECK(!captureExtensionAllowed(CaptureKind::Ply, ".exe"));
    CHECK(describeDevice("Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Version/18.0 Mobile/15E148 Safari/604.1") ==
          "iPhone \xC2\xB7 Safari");
    CHECK(describeDevice("Mozilla/5.0 (Linux; Android 15; Pixel 9) AppleWebKit/537.36 Chrome/131.0 Mobile Safari/537.36") ==
          "Android \xC2\xB7 Chrome");
}

TEST(phone_pairing_and_tokens) {
    const fs::path inbox = freshDir("sf_phone_pair");
    PhoneLink link;
    std::string err;
    CHECK(link.start(testOptions(inbox), err));
    // Without the token only the page itself answers.
    CHECK(link.handle(request("GET", "/")).status == 200);
    CHECK(link.handle(request("GET", "/")).body.find("SculptForge capture") != std::string::npos);
    CHECK(link.handle(request("GET", "/api/status")).status == 403);
    CHECK(link.handle(request("GET", "/api/status", "0123456789abcdef0123456789abcdef")).status == 403);
    CHECK(link.handle(request("GET", "/ar/model.glb?k=nope")).status == 403);
    CHECK(link.handle(request("GET", "/etc/passwd")).status == 404);
    // The QR code's URL carries the token.
    CHECK(link.url().find("/?k=" + link.token()) != std::string::npos);
    CHECK(link.handle(request("GET", "/api/status?k=" + link.token())).status == 200);
    const Json st = parseJson(link.handle(request("GET", "/api/status", link.token())).body);
    CHECK(st["desktop"].asString() == "Test PC");
    CHECK(st["device"].asString() == "iPhone \xC2\xB7 Safari");
    // Typing the code gives the token; a wrong code doesn't.
    std::this_thread::sleep_for(std::chrono::milliseconds(850));
    CHECK(link.handle(request("POST", "/api/pair", {}, "{\"code\":\"999999x\"}")).status == 403 || link.code() == "999999");
    std::this_thread::sleep_for(std::chrono::milliseconds(850));
    HttpResponse ok = link.handle(request("POST", "/api/pair", {}, "{\"code\":\"" + link.code() + "\"}"));
    CHECK(ok.status == 200);
    CHECK(parseJson(ok.body)["token"].asString() == link.token());
    // Too fast: refused without counting.
    CHECK(link.handle(request("POST", "/api/pair", {}, "{\"code\":\"" + link.code() + "\"}")).status == 429);
    bool connected = false;
    for (const PhoneEvent& e : link.takeEvents()) connected = connected || e.type == PhoneEvent::Type::DeviceConnected;
    CHECK(connected);
    link.stop();
}

TEST(phone_upload_resumes_and_lands_in_the_inbox) {
    const fs::path inbox = freshDir("sf_phone_upload");
    PhoneLink link;
    std::string err;
    CHECK(link.start(testOptions(inbox), err));
    const std::string k = link.token();
    // A bad kind and a bad file are refused.
    CHECK(link.handle(request("POST", "/api/capture", k, "{\"kind\":\"virus\"}")).status == 400);
    const Json cap = parseJson(link.handle(request("POST", "/api/capture", k, "{\"kind\":\"splat\"}")).body);
    const std::string cid = cap["id"].asString();
    CHECK(!cid.empty());
    CHECK(link.handle(request("POST", "/api/capture/" + cid + "/file", k, "{\"name\":\"x.exe\",\"size\":10}")).status == 400);
    CHECK(link.handle(request("POST", "/api/capture/" + cid + "/file", k, "{\"name\":\"x.mov\",\"size\":9e12}")).status == 413);
    CHECK(link.handle(request("POST", "/api/capture/" + cid + "/finish", k)).status == 400); // nothing yet

    std::string data(3 * 1000 + 17, '\0');
    for (size_t i = 0; i < data.size(); ++i) data[i] = char((i * 131) & 0xFF);
    const Json file = parseJson(link.handle(request("POST", "/api/capture/" + cid + "/file", k,
                                                    "{\"name\":\"../scan video.mov\",\"size\":" + std::to_string(data.size()) + "}")).body);
    const std::string fid = file["id"].asString();
    CHECK(file["name"].asString() == "scan_video.mov");
    // First chunk; then a chunk from the future (a lost one in between) is refused with what arrived.
    CHECK(link.handle(request("PUT", "/api/file/" + fid + "?offset=0", k, data.substr(0, 1000))).status == 200);
    HttpResponse gap = link.handle(request("PUT", "/api/file/" + fid + "?offset=2000", k, data.substr(2000, 1000)));
    CHECK(gap.status == 409);
    CHECK(parseJson(gap.body)["received"].asNumber() == 1000);
    // Finishing early lists the incomplete file.
    CHECK(link.handle(request("POST", "/api/capture/" + cid + "/finish", k)).status == 409);
    // A resent chunk that overlaps what arrived (the phone didn't see the answer) only adds the new part.
    CHECK(link.handle(request("PUT", "/api/file/" + fid + "?offset=500", k, data.substr(500, 1500))).status == 200);
    CHECK(parseJson(link.handle(request("GET", "/api/file/" + fid, k)).body)["received"].asNumber() == 2000);
    CHECK(link.handle(request("PUT", "/api/file/" + fid + "?offset=2000", k, data.substr(2000) + "extra")).status == 400);
    CHECK(link.handle(request("PUT", "/api/file/" + fid + "?offset=2000", k, data.substr(2000))).status == 200);
    // Status shows the upload.
    const PhoneLinkStatus st = link.status();
    CHECK(st.uploads.size() == 1);
    CHECK(st.uploads[0].received == data.size());
    HttpResponse fin = link.handle(request("POST", "/api/capture/" + cid + "/finish", k));
    CHECK(fin.status == 200);
    const fs::path folder = upath(parseJson(fin.body)["folder"].asString());
    CHECK(folder.parent_path() == inbox);
    CHECK(folder.filename().string().size() == std::string("2026-10-26_195803-splat").size());
    CHECK(folder.filename().string().find("-splat") != std::string::npos);
    CHECK(readText(folder / "scan_video.mov") == data);
    const Json info = parseJson(readText(folder / "capture.json"));
    CHECK(info["kind"].asString() == "splat");
    CHECK(info["device"].asString() == "iPhone \xC2\xB7 Safari");
    CHECK(info["files"].size() == 1);
    CHECK(info["files"][size_t(0)]["name"].asString() == "scan_video.mov");
    CHECK(info["files"][size_t(0)]["size"].asNumber() == double(data.size()));
    // Nothing is left in the staging folder, and the app hears about it.
    CHECK(fs::is_empty(inbox / ".incoming"));
    bool ready = false;
    for (const PhoneEvent& e : link.takeEvents())
        if (e.type == PhoneEvent::Type::CaptureReady) {
            ready = true;
            CHECK(e.kind == CaptureKind::Splat);
            CHECK(e.files.size() == 1);
            CHECK(upath(e.files[0]).filename() == "scan_video.mov");
        }
    CHECK(ready);
    // Finishing again just answers with the folder; more chunks aren't taken.
    CHECK(link.handle(request("POST", "/api/capture/" + cid + "/finish", k)).status == 200);

    // A second capture in the same second gets its own folder.
    const std::string cid2 = parseJson(link.handle(request("POST", "/api/capture", k, "{\"kind\":\"ply\"}")).body)["id"].asString();
    const std::string fid2 = parseJson(link.handle(request("POST", "/api/capture/" + cid2 + "/file", k, "{\"name\":\"s.ply\",\"size\":3}")).body)["id"].asString();
    CHECK(link.handle(request("PUT", "/api/file/" + fid2 + "?offset=0", k, "ply")).status == 200);
    const fs::path folder2 = upath(parseJson(link.handle(request("POST", "/api/capture/" + cid2 + "/finish", k)).body)["folder"].asString());
    CHECK(fs::exists(folder2 / "s.ply"));
    CHECK(folder2 != folder);

    // An unfinished capture is removed when the link stops.
    const std::string cid3 = parseJson(link.handle(request("POST", "/api/capture", k, "{\"kind\":\"photo\"}")).body)["id"].asString();
    link.handle(request("POST", "/api/capture/" + cid3 + "/file", k, "{\"name\":\"p.jpg\",\"size\":30}"));
    CHECK(fs::exists(inbox / ".incoming" / cid3));
    link.stop();
    CHECK(!fs::exists(inbox / ".incoming" / cid3));
}

// --- Face ---

TEST(face_messages_parse) {
    FaceFrame f;
    std::string err;
    CHECK(parseFaceMessage(faceJson(0.5f, 1234.5f), f, err));
    CHECK(std::abs(f.t - 1234.5) < 1e-6);
    CHECK(std::abs(f.weight("jawOpen") - 0.5f) < 1e-6f);
    CHECK(std::abs(f.weight("eyeBlinkLeft") - 0.25f) < 1e-6f);
    CHECK(f.presentCount() == 2); // _neutral is MediaPipe's, not ARKit's: ignored without counting as unknown
    CHECK(f.unknownNames == 0);
    CHECK(f.hasHead);
    CHECK(std::abs(f.head.w - 1.0f) < 1e-6f);
    // Clamped, normalised, unknown names counted.
    CHECK(parseFaceMessage(R"({"type":"face","weights":{"jawOpen":1.7,"mouthSmileLeft":-1,"wiggle":0.3},"head":[0,0,2,0]})", f, err));
    CHECK(f.weight("jawOpen") == 1.0f);
    CHECK(f.weight("mouthSmileLeft") == 0.0f);
    CHECK(f.unknownNames == 1);
    CHECK(std::abs(f.head.z - 1.0f) < 1e-6f);
    // Rejected.
    CHECK(!parseFaceMessage("not json", f, err));
    CHECK(!parseFaceMessage(R"({"type":"hand","weights":{}})", f, err));
    CHECK(!parseFaceMessage(R"({"type":"face"})", f, err));
    CHECK(!parseFaceMessage(R"({"type":"face","weights":{"jawOpen":"wide"}})", f, err));
    CHECK(!parseFaceMessage(R"({"type":"face","weights":{},"head":[1,2,3]})", f, err));
    CHECK(!parseFaceMessage(R"({"type":"face","weights":{},"head":[0,0,0,0]})", f, err));
    CHECK(!parseFaceMessage(R"({"type":"face","t":"now","weights":{}})", f, err));
    CHECK(!parseFaceMessage("{\"type\":\"face\",\"weights\":{\"jawOpen\":" + std::string(20000, '1') + "}}", f, err));
    // All 52 names, each mirror pair, and the weights round trip.
    CHECK(arkitShapeNames().size() == 52);
    CHECK(arkitShapeIndex("tongueOut") == 51);
    CHECK(arkitShapeIndex("_neutral") < 0);
    for (int i = 0; i < kArkitShapeCount; ++i) CHECK(arkitMirrorIndex(arkitMirrorIndex(i)) == i);
    CHECK(arkitShapeNames()[size_t(arkitMirrorIndex(arkitShapeIndex("eyeBlinkLeft")))] == std::string("eyeBlinkRight"));
    CHECK(arkitShapeNames()[size_t(arkitMirrorIndex(arkitShapeIndex("jawLeft")))] == std::string("jawRight"));
    CHECK(arkitMirrorIndex(arkitShapeIndex("jawOpen")) == arkitShapeIndex("jawOpen"));
    CHECK(parseFaceMessage(faceJson(0.3125f), f, err));
    const FaceFrame m = f.mirrored();
    CHECK(m.weight("eyeBlinkRight") == 0.25f);
    CHECK(m.weight("eyeBlinkLeft") == 0.0f);
    CHECK(f.weightsJson() == "{\"eyeBlinkLeft\":0.25,\"jawOpen\":0.3125}");
    FaceFrame back;
    CHECK(parseFaceMessage(f.messageJson(), back, err));
    CHECK(back.weightsJson() == f.weightsJson());
}

TEST(face_feed_over_http_and_takes) {
    const fs::path inbox = freshDir("sf_phone_face");
    PhoneLink link;
    std::string err;
    CHECK(link.start(testOptions(inbox), err));
    const std::string k = link.token();
    FaceFrame f;
    uint64_t serial = 0;
    CHECK(!link.latestFace(f, serial));
    CHECK(link.handle(request("POST", "/api/face", k, faceJson(0.1f, 0))).status == 200);
    CHECK(link.latestFace(f, serial));
    CHECK(std::abs(f.weight("jawOpen") - 0.1f) < 1e-5f);
    CHECK(!link.latestFace(f, serial)); // nothing newer
    CHECK(link.status().faceConnected);
    CHECK(link.status().faceTransport == "Wi-Fi (HTTP)");
    CHECK(link.handle(request("POST", "/api/face", k, "{\"type\":\"face\",\"weights\":5}")).status == 400);
    CHECK(link.handle(request("POST", "/api/face", "", faceJson(0.2f))).status == 403);
    // A take records every frame, batched or not.
    std::string folder;
    CHECK(link.startTake(folder, err));
    CHECK(fs::path(upath(folder)).filename().string().find("-face") != std::string::npos);
    CHECK(link.handle(request("POST", "/api/face", k, faceJson(0.2f, 1000) + "\n" + faceJson(0.3f, 1033))).status == 200);
    CHECK(link.receiveFaceMessage(faceJson(0.4f, 1066), "WebRTC", err));
    CHECK(link.status().recording);
    CHECK(link.status().takeFrames == 3);
    std::string summary;
    CHECK(link.stopTake(&summary) == folder);
    CHECK(summary.find("3 frames") != std::string::npos);
    std::vector<FaceFrame> frames;
    CHECK(readFaceTake(str(upath(folder) / "take.jsonl"), frames, err));
    CHECK(frames.size() == 3);
    CHECK(std::abs(frames[2].weight("jawOpen") - 0.4f) < 1e-4f);
    CHECK(std::abs(frames[1].t - 1033) < 1e-3);
    const Json info = parseJson(readText(upath(folder) / "capture.json"));
    CHECK(info["kind"].asString() == "face");
    CHECK(info["frames"].asNumber() == 3);
    CHECK(std::abs(info["seconds"].asNumber() - 0.066) < 1e-6);
    CHECK(link.stopTake().empty()); // not recording any more
    link.stop();
}

TEST(face_link_over_webrtc) {
    if (!webrtcAvailable()) {
        PhoneLink link;
        std::string url, err;
        CHECK(!link.createFaceLink(url, err));
        CHECK(err.find("without WebRTC") != std::string::npos);
        return;
    }
    // base64url and AES-GCM as the face page uses them.
    CHECK(base64UrlEncode("\xff\xfe") == "__4");
    std::string bytes;
    CHECK(base64UrlDecode("__4", bytes) && bytes == "\xff\xfe");
    const std::string key = randomBytes(32);
    std::string packed, plain, err;
    CHECK(aesGcmEncrypt(key, "hello phone", packed, err));
    CHECK(aesGcmDecrypt(key, packed, plain, err) && plain == "hello phone");
    packed[20] = packed[20] == 'A' ? 'B' : 'A';
    CHECK(!aesGcmDecrypt(key, packed, plain, err)); // tampered

    const fs::path inbox = freshDir("sf_phone_rtc");
    PhoneLink link;
    CHECK(link.start(testOptions(inbox), err));
    // The capture page asks for the face link: a URL on the website with everything in the fragment.
    const Json linkJson = parseJson(link.handle(request("GET", "/api/face-link", link.token())).body);
    const std::string url = linkJson["url"].asString();
    CHECK(url.rfind("https://example.org/link/#v=1&", 0) == 0);
    const std::string fragment = url.substr(url.find('#') + 1);
    std::string offer, faceKey;
    CHECK(base64UrlDecode(queryParameter(fragment, "o"), offer));
    CHECK(offer.find("webrtc-datachannel") != std::string::npos);
    CHECK(base64UrlDecode(queryParameter(fragment, "x"), faceKey) && faceKey.size() == 32);
    CHECK(queryParameter(fragment, "k") == link.token());
    // The phone (a second peer here) answers; the answer comes back encrypted through the relay.
    FaceRtc phone;
    std::string answer;
    CHECK(phone.answerForTest(offer, answer, err));
    std::string wrongToken;
    CHECK(aesGcmEncrypt(faceKey, "{\"k\":\"nope\",\"sdp\":\"v=0\"}", wrongToken, err));
    CHECK(!link.acceptRelayMessage(wrongToken, err));
    Json msg = Json::object();
    msg.set("k", link.token());
    msg.set("sdp", answer);
    std::string relayed;
    CHECK(aesGcmEncrypt(faceKey, msg.dump(), relayed, err));
    CHECK(link.acceptRelayMessage(relayed, err));
    // Frames flow over the data channel.
    FaceFrame f;
    uint64_t serial = 0;
    bool got = false;
    for (int i = 0; i < 100 && !got; ++i) {
        phone.sendFromTestPeer(faceJson(0.75f));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        got = link.latestFace(f, serial);
    }
    CHECK(got);
    CHECK(std::abs(f.weight("jawOpen") - 0.75f) < 1e-5f);
    CHECK(link.status().faceConnected);
    CHECK(link.status().faceTransport == "WebRTC");
    phone.close();
    link.stop();
}

// Android AR: Scene Viewer can't load a model from the desktop's plain-HTTP address, so the website's AR page gets
// it over the WebRTC link (reliable channel): the page asks, the app exports, the GLB arrives in chunks, intact.
TEST(ar_link_sends_the_model_over_webrtc) {
    if (!webrtcAvailable()) return;
    const fs::path inbox = freshDir("sf_phone_ar_rtc");
    PhoneLink link;
    std::string err;
    CHECK(link.start(testOptions(inbox), err));
    HttpRequest r = request("GET", "/api/face-link", link.token());
    r.query = "mode=ar&scope=scene&size=35";
    const std::string url = parseJson(link.handle(r).body)["url"].asString();
    CHECK(url.rfind("https://example.org/link/ar.html?v=4#v=1&", 0) == 0);
    const std::string fragment = url.substr(url.find('#') + 1);
    CHECK(queryParameter(fragment, "m") == "ar");
    CHECK(queryParameter(fragment, "s") == "scene");
    CHECK(queryParameter(fragment, "z") == "35");
    std::string offer;
    CHECK(base64UrlDecode(queryParameter(fragment, "o"), offer));

    std::mutex m;
    std::vector<std::pair<std::string, bool>> received;
    FaceRtc phone(FaceRtc::Mode::Ar);
    phone.onTestMessage = [&](const std::string& data, bool binary) {
        std::lock_guard lock(m);
        received.emplace_back(data, binary);
    };
    std::string answer;
    CHECK(phone.answerForTest(offer, answer, err));
    CHECK(link.acceptFaceAnswer(answer, err));
    // The page asks for the model (as soon as its channel is open).
    bool asked = false;
    for (int i = 0; i < 100 && !asked; ++i) {
        phone.sendFromTestPeer("{\"type\":\"ar\",\"scope\":\"scene\",\"size\":35}");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        for (const PhoneEvent& e : link.takeEvents())
            if (e.type == PhoneEvent::Type::ArRequested) {
                asked = true;
                CHECK(e.wholeScene);
                CHECK(std::abs(e.sizeCm - 35.0f) < 1e-4f);
            }
    }
    CHECK(asked);
    CHECK(link.secondsSinceArViewed() >= 0);
    // The app publishes a model: it goes down the channel.
    std::string glb = "glTF";
    for (int i = 0; glb.size() < 300000; ++i) glb += char(i * 131 % 251);
    link.publishArModel(glb, "usdz", "1,000 triangles");
    std::string got, header, end;
    for (int i = 0; i < 200 && end.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        std::lock_guard lock(m);
        got.clear();
        header.clear();
        for (const auto& [data, binary] : received) {
            if (binary) got += data;
            else if (data.find("\"model\"") != std::string::npos) header = data;
            else if (data.find("\"end\"") != std::string::npos) end = data;
        }
    }
    CHECK(!end.empty());
    CHECK(parseJson(header)["bytes"].asNumber() == double(glb.size()));
    CHECK(parseJson(header)["summary"].asString() == "1,000 triangles");
    CHECK(got == glb);
    // An export error reaches the page too.
    link.setArError("Nothing to show");
    bool error = false;
    for (int i = 0; i < 100 && !error; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        std::lock_guard lock(m);
        for (const auto& [data, binary] : received) error = error || (!binary && data.find("Nothing to show") != std::string::npos);
    }
    CHECK(error);
    CHECK(!link.status().faceConnected); // an AR link is not a face feed
    phone.close();
    link.stop();
}

// Android Chrome may not reach the computer at all: the AR link also carries a relay URL, and each model goes
// there encrypted with the link's key (only the page can read it).
TEST(ar_link_uploads_the_model_encrypted_to_the_relay) {
    if (!webrtcAvailable()) return;
    std::mutex m;
    std::vector<std::pair<std::string, std::string>> posted; // query, body
    HttpServer relay;
    HttpServer::Options ro;
    ro.port = 0;
    ro.bindAddress = "127.0.0.1";
    std::string err;
    CHECK(relay.start(ro, [&](const HttpRequest& r) {
        std::lock_guard lock(m);
        if (r.method == "POST" && r.path.rfind("/blob/sculptforge-", 0) == 0) posted.emplace_back(r.query, r.body);
        return HttpResponse::json(200, "{}");
    }, err));
    if (ai::httpRequest("GET", "http://127.0.0.1:" + std::to_string(relay.port()) + "/blob/x", {}, {}, 5).status != 200) {
        relay.stop(); // no HTTP client in this build
        return;
    }
    const fs::path inbox = freshDir("sf_phone_ar_blob");
    PhoneLinkOptions o = testOptions(inbox);
    o.relayUrl = "http://127.0.0.1:" + std::to_string(relay.port()) + "/relay/";
    PhoneLink link;
    CHECK(link.start(o, err));
    std::string url;
    CHECK(link.createFaceLink(url, err, true, "subtool", 20.0f));
    const std::string fragment = url.substr(url.find('#') + 1);
    const std::string blobUrl = percentDecode(queryParameter(fragment, "b"));
    CHECK(blobUrl.rfind("http://127.0.0.1:" + std::to_string(relay.port()) + "/blob/sculptforge-", 0) == 0);
    std::string key;
    CHECK(base64UrlDecode(queryParameter(fragment, "x"), key));
    bool asked = false;
    for (const PhoneEvent& e : link.takeEvents()) asked = asked || e.type == PhoneEvent::Type::ArRequested;
    CHECK(asked); // the app makes a fresh model for the page
    std::string glb = "glTF";
    for (int i = 0; glb.size() < 200000; ++i) glb += char(i * 37 % 253);
    link.publishArModel(glb, "usdz", "summary");
    std::string query, body;
    for (int i = 0; i < 200 && body.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        std::lock_guard lock(m);
        if (!posted.empty()) std::tie(query, body) = posted.front();
    }
    CHECK(query == "v=1");
    std::string plain;
    CHECK(aesGcmDecrypt(key, base64UrlEncode(body), plain, err));
    CHECK(plain == glb);
    link.stop();
    relay.stop();
}

// The phone's answer through the relays (services/link-worker's protocol, which ntfy.sh also speaks): the first
// relay is down, the second, a fake one here, carries the message; the desktop polls both and connects.
TEST(face_link_answer_through_the_relays) {
    if (!webrtcAvailable()) return;
    std::mutex relayMutex;
    std::vector<std::pair<std::string, std::string>> posted; // topic, message
    HttpServer relay;
    HttpServer::Options ro;
    ro.port = 0;
    ro.bindAddress = "127.0.0.1";
    std::string err;
    CHECK(relay.start(ro, [&](const HttpRequest& r) {
        std::lock_guard lock(relayMutex);
        const std::string prefix = "/relay/";
        if (r.path.rfind(prefix, 0) != 0) return HttpResponse::text(404, "");
        std::string topic = r.path.substr(prefix.size());
        if (r.method == "POST") {
            posted.emplace_back(topic, r.body);
            return HttpResponse::json(200, "{}");
        }
        if (topic.size() < 5 || topic.substr(topic.size() - 5) != "/json" || r.param("poll") != "1")
            return HttpResponse::text(400, "");
        topic.resize(topic.size() - 5);
        std::string lines;
        for (size_t i = 0; i < posted.size(); ++i)
            if (posted[i].first == topic)
                lines += "{\"id\":\"m" + std::to_string(i) + "\",\"event\":\"message\",\"message\":\"" + posted[i].second + "\"}\n";
        return HttpResponse::bytes("application/x-ndjson", lines);
    }, err));
    // Without an HTTP client in this build (no curl), the desktop can't poll: nothing to test.
    if (ai::httpRequest("GET", "http://127.0.0.1:" + std::to_string(relay.port()) + "/relay/x/json?poll=1", {}, {}, 5).status != 200) {
        relay.stop();
        return;
    }

    const fs::path inbox = freshDir("sf_phone_relay");
    PhoneLinkOptions o = testOptions(inbox);
    const std::string relayBase = "http://127.0.0.1:" + std::to_string(relay.port()) + "/relay/";
    o.relayUrl = "http://127.0.0.1:1/relay/ " + relayBase; // port 1: refused at once
    PhoneLink link;
    CHECK(link.start(o, err));
    std::string url;
    CHECK(link.createFaceLink(url, err));
    const std::string fragment = url.substr(url.find('#') + 1);
    // Both relays are in the link, in order (the page tries them so).
    const size_t first = fragment.find("&r=" + percentEncode("http://127.0.0.1:1/relay/sculptforge-"));
    const size_t second = fragment.find("&r=" + percentEncode(relayBase + "sculptforge-"));
    CHECK(first != std::string::npos && second != std::string::npos && first < second);
    const std::string topicUrl = percentDecode(fragment.substr(second + 3, fragment.find('&', second + 3) - second - 3));
    const std::string topic = topicUrl.substr(relayBase.size());
    std::string offer, faceKey;
    CHECK(base64UrlDecode(queryParameter(fragment, "o"), offer));
    CHECK(base64UrlDecode(queryParameter(fragment, "x"), faceKey));
    FaceRtc phone;
    std::string answer;
    CHECK(phone.answerForTest(offer, answer, err));
    Json msg = Json::object();
    msg.set("k", link.token());
    msg.set("sdp", answer);
    std::string packed;
    CHECK(aesGcmEncrypt(faceKey, msg.dump(), packed, err));
    {
        std::lock_guard lock(relayMutex);
        posted.emplace_back("sculptforge-0000", "junk"); // another topic: ignored
        posted.emplace_back(topic, "not-for-us");        // undecryptable: skipped
        posted.emplace_back(topic, packed);
    }
    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        connected = link.status().faceConnected;
        if (!connected) phone.sendFromTestPeer(faceJson(0.5f));
    }
    CHECK(connected);
    phone.close();
    link.stop();
    relay.stop();
}

// --- QR code ---

TEST(qr_code_encodes_the_pairing_url) {
    QrCode qr;
    std::string err;
    CHECK(makeQrCode("http://192.168.1.20:8765/?k=0123456789abcdef0123456789abcdef", qr, err));
    CHECK(qr.size >= 29 && qr.size <= 45);
    CHECK(qr.modules.size() == size_t(qr.size * qr.size));
    // Finder patterns in three corners: dark outer ring, light ring, dark centre.
    for (auto [x, y] : {std::pair{0, 0}, std::pair{qr.size - 7, 0}, std::pair{0, qr.size - 7}}) {
        CHECK(qr.dark(x, y) && qr.dark(x + 6, y + 6) && qr.dark(x + 3, y + 3));
        CHECK(!qr.dark(x + 1, y + 1) && !qr.dark(x + 5, y + 5));
    }
    CHECK(!qr.dark(-1, 0));
    CHECK(!makeQrCode(std::string(5000, 'x'), qr, err));
}

// --- AR export ---

TEST(ar_export_writes_valid_glb_and_usdz) {
    Mesh sphere = makeIcosphere(5); // 20,480 triangles
    // Polypaint: red top, blue bottom.
    sphere.ensureColors();
    for (size_t v = 0; v < sphere.vertexCount(); ++v)
        sphere.colors[v] = sphere.positions[v].y > 0 ? glm::vec3(0.9f, 0.05f, 0.05f) : glm::vec3(0.05f, 0.05f, 0.9f);
    ArExportOptions o;
    o.maxTriangles = 4000;
    o.sizeMeters = 0.2f;
    o.textureSize = 256;
    ArExportResult r;
    std::string err;
    CHECK(exportArModel({&sphere}, o, r, err));
    CHECK(r.colored);
    CHECK(r.triangles > 2000 && r.triangles <= 4000);
    CHECK(r.summary.find("in colour") != std::string::npos);
    // GLB: header, a JSON chunk naming an image and a texture, total length matching.
    CHECK(r.glb.size() > 20);
    CHECK(r.glb.compare(0, 4, "glTF") == 0);
    uint32_t version = 0, length = 0, jsonLength = 0;
    std::memcpy(&version, r.glb.data() + 4, 4);
    std::memcpy(&length, r.glb.data() + 8, 4);
    std::memcpy(&jsonLength, r.glb.data() + 12, 4);
    CHECK(version == 2);
    CHECK(length == r.glb.size());
    CHECK(r.glb.compare(16, 4, "JSON") == 0);
    const Json gltf = parseJson(r.glb.substr(20, jsonLength));
    CHECK(gltf["images"].size() == 1);
    CHECK(gltf["materials"].size() >= 1);
    // It loads back at desk size: 20 cm across, standing on y = 0.
    const fs::path dir = freshDir("sf_phone_ar");
    {
        std::ofstream(dir / "m.glb", std::ios::binary) << r.glb;
        std::ofstream(dir / "m.usdz", std::ios::binary) << r.usdz;
    }
    Mesh back;
    CHECK(loadGltf(str(dir / "m.glb"), back, err));
    const AABB box = back.bounds();
    const glm::vec3 e = box.extent();
    CHECK(std::abs(std::max({e.x, e.y, e.z}) - 0.2f) < 1e-3f);
    CHECK(std::abs(box.min.y) < 1e-4f);
    CHECK(std::abs(box.center().x) < 1e-3f && std::abs(box.center().z) < 1e-3f);
    CHECK(back.hasUvs()); // (the glTF reader doesn't load textures; the material must name one)
    CHECK(gltf["materials"][size_t(0)]["pbrMetallicRoughness"].has("baseColorTexture"));
    // USDZ: a stored zip whose first file is the USD layer, with the texture inside, 64-byte aligned.
    std::vector<ZipFileEntry> entries;
    CHECK(readStoredZipIndex(str(dir / "m.usdz"), entries, err));
    CHECK(!entries.empty());
    CHECK(entries[0].name.size() > 5 && entries[0].name.substr(entries[0].name.size() - 5) == ".usda");
    bool png = false;
    for (const auto& en : entries) {
        CHECK(en.offset % 64 == 0);
        png = png || en.name.find(".png") != std::string::npos;
    }
    CHECK(png);
    // Without colour there is no texture; hidden triangles are left out; an empty scene is an error.
    Mesh plain = makeIcosphere(3);
    CHECK(exportArModel({&plain}, o, r, err));
    CHECK(!r.colored);
    CHECK(parseJson(r.glb.substr(20, [&] { uint32_t n; std::memcpy(&n, r.glb.data() + 12, 4); return n; }()))["images"].size() == 0);
    Mesh empty;
    CHECK(!exportArModel({&empty, nullptr}, o, r, err));
    plain.ensureHidden();
    std::fill(plain.hidden.begin(), plain.hidden.end(), uint8_t(1));
    CHECK(!exportArModel({&plain}, o, r, err));
}

TEST(ar_export_bakes_corner_colours) {
    // One triangle covering the lower-left half of the texture, red; the padding spills a few texels over.
    const std::vector<glm::uvec3> tris = {{0, 1, 2}};
    const std::vector<glm::vec2> uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    const std::vector<glm::vec3> cols(3, glm::vec3(1.0f, 0.0f, 0.0f));
    const std::vector<uint8_t> rgba = bakeCornerColors(tris, uvs, cols, 32);
    CHECK(rgba.size() == 32 * 32 * 4);
    auto px = [&](int x, int y) { return &rgba[(size_t(y) * 32 + size_t(x)) * 4]; };
    CHECK(px(2, 2)[0] == 255 && px(2, 2)[1] == 0);   // inside
    CHECK(px(31, 31)[0] == 255 && px(31, 31)[1] == 255); // far outside: white
}
