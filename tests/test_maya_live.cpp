// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Maya Live (#42): the file side's debounce, mesh hash, echo guard and manifest writer id (io/MayaBridge.h), and
// the Live link (link/MayaLink.h): strict frames (round trips, then malformed and random input refused without
// a crash), Points folding, and a real server on a per-user socket that pairs only with its code.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "io/MayaBridge.h"
#include "io/MayaCommands.h"
#include "io/ObjIO.h"
#include "link/MayaLink.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <filesystem>
#include <random>
#include <thread>

using namespace sf;

namespace {

namespace fs = std::filesystem;

link::MayaLinkMesh quadMesh() {
    link::MayaLinkMesh m;
    m.name = "pPlane1";
    m.positions = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 2, 0, 0};
    m.faceSizes = {4, 3};
    m.indices = {0, 1, 2, 3, 1, 4, 2};
    m.uvs = {0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1};
    return m;
}

std::string payloadOf(const std::string& frame) { return frame.substr(link::kMayaLinkHeaderBytes); }

bool waitFor(const std::function<bool()>& f, int ms = 3000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return f();
}

bool readFrame(link::AiLinkConn& c, link::MayaLinkType& type, std::string& payload) {
    char header[link::kMayaLinkHeaderBytes];
    size_t got = 0;
    while (got < sizeof header) {
        const long long n = c.readBytes(header + got, sizeof header - got, 3000);
        if (n <= 0) return false;
        got += size_t(n);
    }
    uint64_t length = 0;
    std::string error;
    if (!link::parseMayaLinkHeader(header, type, length, error)) return false;
    payload.assign(size_t(length), '\0');
    for (size_t at = 0; at < payload.size();) {
        const long long n = c.readBytes(payload.data() + at, payload.size() - at, 3000);
        if (n <= 0) return false;
        at += size_t(n);
    }
    return true;
}

} // namespace

TEST(maya_live_debounce_and_hash) {
    MayaLiveDebounce d;
    CHECK(!d.pending() && !d.due(1'000'000'000ull, false));
    d.changed(1'000'000'000ull);
    CHECK(d.pending() && !d.due(1'100'000'000ull, false));                    // 100 ms: still waiting
    CHECK(!d.due(1'400'000'000ull, true));                                    // still, but mid-stroke: never
    CHECK(d.due(1'300'000'000ull, false) && d.due(1'400'000'000ull, false)); // 300 ms of stillness
    d.changed(1'350'000'000ull);                                              // another edit restarts the wait
    CHECK(!d.due(1'400'000'000ull, false) && d.due(1'650'000'000ull, false));
    d.clear();
    CHECK(!d.pending());

    Mesh a = makeCube(2);
    a.uvs.assign(a.triangleCount() * 3, glm::vec2(0.5f));
    Mesh b = mayaSendCopy(a);
    CHECK(b.positions == a.positions && b.triangles == a.triangles && b.uvs == a.uvs && b.hasQuads() == a.hasQuads());
    CHECK(b.layers.empty() && !b.hasFaceGroups() && !b.texture);
    CHECK(mayaMeshHash(a) == mayaMeshHash(b));
    b.positions[3].x += 1e-4f;
    CHECK(mayaMeshHash(a) != mayaMeshHash(b));

    // The echo guard: OBJ's rounding is the same mesh; a real move or other faces are not.
    Mesh c = a;
    for (glm::vec3& p : c.positions) p += glm::vec3(3e-7f);
    CHECK(mayaMeshesMatch(a, c));
    c.positions[0].y += 0.01f;
    CHECK(!mayaMeshesMatch(a, c));
    CHECK(!mayaMeshesMatch(a, makeCube(3)));
}

TEST(maya_live_manifest_writer_and_echo) {
    MayaManifest m;
    m.revision = 7;
    m.sender = "sculptforge";
    m.writer = mayaNewWriterId();
    m.live = true;
    CHECK(m.writer.size() == 16 && m.writer != mayaNewWriterId());
    MayaManifest back;
    std::string err;
    CHECK(parseMayaManifest(writeMayaManifest(m), back, err));
    CHECK(back.writer == m.writer && back.live && back.revision == 7);
    // Older manifests (no writer) still read; a writer that isn't a short string is refused.
    CHECK(parseMayaManifest(R"({"format": "sculptforge-maya", "version": 1, "revision": 1, "objects": []})", back, err) &&
          back.writer.empty() && !back.live);
    CHECK(!parseMayaManifest(R"({"format": "sculptforge-maya", "version": 1, "revision": 1, "writer": 5, "objects": []})", back, err));

    // A Live send: writer and live in the manifest, files renamed into place (no .tmp left).
    const fs::path root = fs::temp_directory_path() / "sf_maya_live";
    fs::remove_all(root);
    fs::create_directories(root);
    Mesh cube = makeCube(2);
    std::string msg;
    std::vector<std::string> names;
    CHECK(mayaSend({{"Body", &cube}}, root.string(), msg, &names, nullptr, m.writer, true));
    CHECK(msg.rfind("Live: sent 1 object", 0) == 0);
    CHECK(readMayaManifest(mayaManifestPath(mayaOutbox(root.string())), back, err) && back.writer == m.writer && back.live);
    for (const auto& e : fs::directory_iterator(root / "sculptforge_to_maya"))
        CHECK(e.path().extension() != ".tmp");

    // maya_receive skips what Maya sends back unchanged (no undo step), loads what changed.
    CommandRegistry reg;
    Document doc = makeDoc(makeCube(2));
    doc.bridgeName = "Body";
    const fs::path inbox = root / "maya_to_sculptforge";
    fs::create_directories(inbox);
    CHECK(saveObj((inbox / "r1_000.obj").string(), doc.mesh, err));
    std::ofstream(inbox / "manifest.json") << R"({"format": "sculptforge-maya", "version": 1, "revision": 1,
        "objects": [{"name": "Body", "file": "r1_000.obj"}]})";
    CommandResult r = reg.execute(doc, "maya_receive", {{"folder", root.string()}});
    CHECK(r.ok && r.message.find("unchanged") != std::string::npos && !doc.undo.canUndo());
    Mesh moved = doc.mesh;
    for (glm::vec3& p : moved.positions) p.x += 0.5f;
    CHECK(saveObj((inbox / "r2_000.obj").string(), moved, err));
    std::ofstream(inbox / "manifest.json") << R"({"format": "sculptforge-maya", "version": 1, "revision": 2,
        "objects": [{"name": "Body", "file": "r2_000.obj"}]})";
    r = reg.execute(doc, "maya_receive", {{"folder", root.string()}});
    CHECK(r.ok && r.message.find("Received 1 object") != std::string::npos && doc.undo.canUndo());
    fs::remove_all(root);
}

TEST(maya_link_frames_round_trip) {
    using namespace link;
    const std::string code = mayaLinkNewCode();
    CHECK(code.size() == kMayaLinkCodeChars && code != mayaLinkNewCode());
    const std::string shown = mayaLinkShowCode(code);
    CHECK(shown.size() == 35 && shown[8] == '-');
    CHECK(mayaLinkCodeMatches(shown, code) && mayaLinkCodeMatches(code, code));
    CHECK(!mayaLinkCodeMatches(mayaLinkNewCode(), code) && !mayaLinkCodeMatches("", code) &&
          !mayaLinkCodeMatches(code.substr(0, 31), code) && !mayaLinkCodeMatches(code + "0", code));
    CHECK(mayaLinkPairingText(code, "/run/x/maya-link.sock") == "sflink1 " + shown + " /run/x/maya-link.sock");

    std::string c, client, err;
    CHECK(parseMayaLinkHello(payloadOf(encodeMayaLinkHello(code, "Maya 2026")), c, client, err) && c == code &&
          client == "Maya 2026");

    const MayaLinkMesh m = quadMesh();
    const std::string f = encodeMayaLinkMesh(m);
    MayaLinkType type{};
    uint64_t length = 0;
    CHECK(parseMayaLinkHeader(f.data(), type, length, err) && type == MayaLinkType::Mesh && length == f.size() - 16);
    MayaLinkMesh back;
    CHECK(parseMayaLinkMesh(payloadOf(f), back, err));
    CHECK(back.name == m.name && back.positions == m.positions && back.faceSizes == m.faceSizes &&
          back.indices == m.indices && back.uvs == m.uvs);

    // To a SculptForge mesh and back: a quad stays a quad, the triangle a triangle, UVs per corner.
    Mesh mesh;
    CHECK(mayaLinkMeshTo(back, mesh, err));
    CHECK(mesh.vertexCount() == 5 && mesh.triangleCount() == 3 && mesh.quadCount() == 1 && mesh.uvs.size() == 9);
    const MayaLinkMesh again = mayaLinkMeshFrom("pPlane1", mesh);
    CHECK(again.faceSizes.size() == 2 && again.indices.size() == 7 && again.uvs.size() == 14);

    MayaLinkPoints p = mayaLinkPointsBetween("pPlane1", mesh.positions, mesh.positions);
    CHECK(p.indices.empty() && p.vertexCount == 5);
    std::vector<glm::vec3> now = mesh.positions;
    now[1].z = 0.5f;
    now[4].z = -0.5f;
    p = mayaLinkPointsBetween("pPlane1", mesh.positions, now);
    CHECK((p.indices == std::vector<uint32_t>{1, 4}) && p.positions.size() == 6);
    MayaLinkPoints pb;
    CHECK(parseMayaLinkPoints(payloadOf(encodeMayaLinkPoints(p)), pb, err) && pb.indices == p.indices &&
          pb.positions == p.positions && pb.vertexCount == 5);

    // Folding: the union of the vertices, the later positions winning.
    MayaLinkPoints later = p;
    later.indices = {4, 2};
    later.positions = {9, 9, 9, 7, 7, 7};
    mergeMayaLinkPoints(p, later);
    CHECK((p.indices == std::vector<uint32_t>{1, 4, 2}) && p.positions[3] == 9.0f && p.positions[6] == 7.0f &&
          p.positions[0] == now[1].x);
}

// The shelf script's encoder wrote these (tests/maya/data; tests/maya/test_sculptforge_maya.py checks it still
// writes the same bytes): both sides speak one format.
TEST(maya_link_reads_the_shelf_scripts_frames) {
    using namespace link;
    auto read = [](const char* name) {
        std::ifstream f(fs::path(SF_TEST_DATA_DIR) / ".." / "maya" / "data" / name, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    const std::string mesh = read("link_mesh.bin");
    CHECK(mesh == encodeMayaLinkMesh(quadMesh()));
    MayaLinkType type{};
    uint64_t length = 0;
    std::string err;
    MayaLinkMesh m;
    CHECK(mesh.size() > 16 && parseMayaLinkHeader(mesh.data(), type, length, err) && type == MayaLinkType::Mesh &&
          parseMayaLinkMesh(payloadOf(mesh), m, err) && m.name == "pPlane1" && m.faceSizes.size() == 2);
    const std::string points = read("link_points.bin");
    MayaLinkPoints p;
    CHECK(points.size() > 16 && parseMayaLinkPoints(payloadOf(points), p, err) && p.name == "Body" && p.vertexCount == 5 &&
          (p.indices == std::vector<uint32_t>{1, 4}) && p.positions[0] == 0.5f && p.positions[5] == 4.0f);
}

TEST(maya_link_refuses_malformed) {
    using namespace link;
    std::string err;
    MayaLinkType type{};
    uint64_t length = 0;
    auto header = [](const char* magic, uint16_t version, uint16_t t, uint64_t n) {
        std::string h(magic, 4);
        h += char(version & 0xff);
        h += char(version >> 8);
        h += char(t & 0xff);
        h += char(t >> 8);
        for (int i = 0; i < 8; ++i) h += char((n >> (8 * i)) & 0xff);
        return h;
    };
    CHECK(!parseMayaLinkHeader(header("GET ", 1, 3, 10).data(), type, length, err)); // (HTTP, a browser: refused)
    CHECK(!parseMayaLinkHeader(header("SFML", 2, 3, 10).data(), type, length, err));
    CHECK(!parseMayaLinkHeader(header("SFML", 1, 0, 10).data(), type, length, err));
    CHECK(!parseMayaLinkHeader(header("SFML", 1, 9, 10).data(), type, length, err));
    CHECK(!parseMayaLinkHeader(header("SFML", 1, 3, kMayaLinkMaxPayload + 1).data(), type, length, err));
    CHECK(parseMayaLinkHeader(header("SFML", 1, 3, 10).data(), type, length, err));

    MayaLinkMesh out;
    auto refused = [&](MayaLinkMesh m) { return !parseMayaLinkMesh(payloadOf(encodeMayaLinkMesh(m)), out, err); };
    MayaLinkMesh m = quadMesh();
    m.indices[2] = 5; // out of range
    CHECK(refused(m));
    m = quadMesh();
    m.faceSizes = {2, 5}; // a face of 2
    CHECK(refused(m));
    m = quadMesh();
    m.faceSizes = {4, 4}; // faces that don't add up
    CHECK(refused(m));
    m = quadMesh();
    m.positions[4] = std::nanf("");
    CHECK(refused(m));
    m = quadMesh();
    m.uvs[3] = std::numeric_limits<float>::infinity();
    CHECK(refused(m));
    for (const char* name : {"", "../x", "a b", "1abc", "C:\\x"}) {
        m = quadMesh();
        m.name = name;
        CHECK(refused(m));
    }
    const std::string good = payloadOf(encodeMayaLinkMesh(quadMesh()));
    CHECK(!parseMayaLinkMesh(good + "x", out, err));                 // a byte too many
    CHECK(!parseMayaLinkMesh(good.substr(0, good.size() - 1), out, err)); // one too few
    for (size_t cut = 0; cut < good.size(); ++cut) CHECK(!parseMayaLinkMesh(good.substr(0, cut), out, err));

    // Fuzz: random bytes, and the good payload with random bytes changed, never crash or read out of bounds (run
    // under the sanitizers in CI); whatever parses still holds only in-range indices and finite numbers.
    std::mt19937 rng(42);
    for (int i = 0; i < 20000; ++i) {
        std::string s = i % 2 ? good : std::string(size_t(rng() % 96), '\0');
        if (i % 2 == 0)
            for (char& ch : s) ch = char(rng());
        else
            for (int k = 0; k < 1 + int(rng() % 4); ++k) s[rng() % s.size()] = char(rng());
        MayaLinkMesh fm;
        if (parseMayaLinkMesh(s, fm, err)) {
            for (uint32_t v : fm.indices) CHECK(v < fm.positions.size() / 3);
            for (float x : fm.positions) CHECK(std::isfinite(x));
        }
        MayaLinkPoints fp;
        if (parseMayaLinkPoints(s, fp, err))
            for (uint32_t v : fp.indices) CHECK(v < fp.vertexCount);
        std::string c, cl;
        parseMayaLinkHello(s, c, cl, err);
        if (s.size() >= kMayaLinkHeaderBytes) parseMayaLinkHeader(s.data(), type, length, err);
    }
    // Counts that claim more than the payload holds (a 4 GB allocation attempt) are refused before allocating.
    std::string huge;
    huge += char(1);
    huge += char(0);
    huge += 'a';
    for (int i = 0; i < 3; ++i) huge += std::string("\xff\xff\xff\x03", 4);
    huge += char(0);
    CHECK(!parseMayaLinkMesh(huge, out, err));
}

#ifndef _WIN32
TEST(maya_link_server_pairs_and_streams) {
    using namespace link;
    const fs::path dir = fs::temp_directory_path() / ("sf_maya_link_" + std::to_string(std::random_device{}() % 100000));
    fs::create_directories(dir);
    setenv("SF_AI_LIVE_DIR", dir.string().c_str(), 1);
    std::string err;
    const std::string endpoint = localEndpoint("maya-link", err);
    CHECK(!endpoint.empty() && endpoint.find("maya-link.sock") != std::string::npos);
    MayaLinkServer server;
    CHECK(server.start(endpoint, err));
    CHECK(aiLinkFileMode(endpoint) == 0600);
    // A second server on the same endpoint: refused (another window has it on).
    MayaLinkServer second;
    CHECK(!second.start(endpoint, err));

    // A wrong code: the connection closes, nothing connected.
    {
        AiLinkConn c;
        CHECK(aiLinkConnect(c, endpoint, err));
        const std::string hello = encodeMayaLinkHello(mayaLinkNewCode(), "intruder");
        CHECK(c.writeAll(hello.data(), hello.size()));
        MayaLinkType t{};
        std::string p;
        CHECK(!readFrame(c, t, p) || t == MayaLinkType::Bye);
        CHECK(waitFor([&] { return server.refused() == 1; }) && !server.connected());
    }
    // Something that isn't the link at all (an HTTP request): refused too.
    {
        AiLinkConn c;
        CHECK(aiLinkConnect(c, endpoint, err));
        const std::string http = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        CHECK(c.writeAll(http.data(), http.size()));
        CHECK(waitFor([&] { return server.refused() == 2; }) && !server.connected());
    }
    // The right code: Welcome, then meshes both ways.
    AiLinkConn c;
    CHECK(aiLinkConnect(c, endpoint, err));
    const std::string hello = encodeMayaLinkHello(mayaLinkShowCode(server.code()), "Maya 2026");
    CHECK(c.writeAll(hello.data(), hello.size()));
    MayaLinkType t{};
    std::string p;
    CHECK(readFrame(c, t, p) && t == MayaLinkType::Welcome);
    CHECK(waitFor([&] { return server.connected(); }) && server.clientName() == "Maya 2026");
    std::string e;
    for (int i = 0; i < 4 && e != "connected"; ++i) e = server.takeEvent();
    CHECK(e == "connected");

    const std::string mesh = encodeMayaLinkMesh(quadMesh());
    CHECK(c.writeAll(mesh.data(), mesh.size()));
    std::vector<MayaLinkMesh> got;
    CHECK(waitFor([&] {
        for (auto& m : server.takeReceived()) got.push_back(std::move(m));
        return !got.empty();
    }));
    CHECK(got.size() == 1 && got[0].name == "pPlane1" && got[0].indices.size() == 7);

    // App -> Maya: Points queued together fold into one frame.
    MayaLinkPoints a;
    a.name = "Body";
    a.vertexCount = 100;
    a.indices = {1};
    a.positions = {1, 2, 3};
    server.send(a);
    MayaLinkMesh whole = quadMesh();
    whole.name = "Body2";
    server.send(whole);
    CHECK(readFrame(c, t, p));
    CHECK(t == MayaLinkType::Points || t == MayaLinkType::Mesh);

    // Maya says Bye: the server is free for the next client.
    const std::string bye = mayaLinkFrame(MayaLinkType::Bye, {});
    CHECK(c.writeAll(bye.data(), bye.size()));
    CHECK(waitFor([&] { return !server.connected(); }));
    server.stop();
    CHECK(aiLinkFileMode(endpoint) < 0); // the socket file is gone when the link is off
    unsetenv("SF_AI_LIVE_DIR");
    fs::remove_all(dir);
}
#endif

#ifndef _WIN32
// SF_MAYA_BENCH=1 sculptforge_tests maya_live_bench: what each way costs on a 1M-triangle and a Lucy-size
// (28M-triangle) quad mesh with UVs: the folder (OBJ write, read) and the link (encode, carry through the real
// socket, parse, rebuild), and a stroke's Points (2% of the vertices). Not part of the suite (it times).
TEST(maya_live_bench) {
    if (!std::getenv("SF_MAYA_BENCH")) return;
    using namespace link;
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); };
    for (int res : {289, 1528}) {
        Mesh mesh = makeCube(res);
        mesh.uvs.assign(size_t(mesh.triangleCount()) * 3, glm::vec2(0.5f));
        std::printf("\n%u triangles, %zu vertices\n", mesh.triangleCount(), mesh.positions.size());
        const fs::path root = fs::temp_directory_path() / "sf_maya_bench";
        fs::remove_all(root);
        fs::create_directories(root);
        std::string msg, err;
        auto t = Clock::now();
        Mesh copy = mayaSendCopy(mesh);
        std::printf("  copy for the worker     %8.1f ms\n", ms(t));
        t = Clock::now();
        const uint64_t h = mayaMeshHash(copy);
        std::printf("  hash                    %8.1f ms (%llx)\n", ms(t), (unsigned long long)(h & 0xffff));
        t = Clock::now();
        CHECK(mayaSend({{"Body", &copy}}, root.string(), msg, nullptr, nullptr, "bench", true));
        std::printf("  folder: OBJ write       %8.1f ms (%.0f MB)\n", ms(t),
                    double(fs::file_size(root / "sculptforge_to_maya" / "r1_000.obj")) / 1e6);
        fs::rename(root / "sculptforge_to_maya", root / "maya_to_sculptforge");
        std::vector<MeshObject> back;
        t = Clock::now();
        CHECK(mayaReceive(root.string(), back, nullptr, msg));
        std::printf("  folder: OBJ read        %8.1f ms\n", ms(t));
        t = Clock::now();
        CHECK(back.size() == 1 && mayaMeshesMatch(mesh, back[0].mesh));
        std::printf("  echo check              %8.1f ms\n", ms(t));
        back.clear();
        fs::remove_all(root);

        t = Clock::now();
        const MayaLinkMesh lm = mayaLinkMeshFrom("Body", mesh);
        const std::string frame = encodeMayaLinkMesh(lm);
        std::printf("  link: encode            %8.1f ms (%.0f MB)\n", ms(t), double(frame.size()) / 1e6);
        // Carried through a real link: this test plays Maya, sending to the app's server.
        const fs::path dir = fs::temp_directory_path() / "sf_maya_bench_link";
        fs::create_directories(dir);
        setenv("SF_AI_LIVE_DIR", dir.string().c_str(), 1);
        const std::string endpoint = localEndpoint("maya-link", err);
        MayaLinkServer server;
        CHECK(server.start(endpoint, err));
        AiLinkConn c;
        CHECK(aiLinkConnect(c, endpoint, err));
        const std::string hello = encodeMayaLinkHello(server.code(), "bench");
        c.writeAll(hello.data(), hello.size());
        MayaLinkType type{};
        std::string p;
        CHECK(readFrame(c, type, p) && type == MayaLinkType::Welcome);
        std::vector<MayaLinkMesh> got;
        t = Clock::now();
        std::thread writer([&] { c.writeAll(frame.data(), frame.size()); });
        CHECK(waitFor([&] {
            for (auto& m : server.takeReceived()) got.push_back(std::move(m));
            return !got.empty();
        }, 120000));
        writer.join();
        std::printf("  link: carry + parse     %8.1f ms\n", ms(t));
        t = Clock::now();
        Mesh rebuilt;
        CHECK(!got.empty() && mayaLinkMeshTo(got[0], rebuilt, err));
        std::printf("  link: rebuild mesh      %8.1f ms\n", ms(t));
        got.clear();

        std::vector<glm::vec3> moved = mesh.positions;
        for (size_t i = 0; i < moved.size(); i += 50) moved[i].y += 0.01f;
        t = Clock::now();
        const MayaLinkPoints pts = mayaLinkPointsBetween("Body", mesh.positions, moved);
        std::printf("  stroke: diff (%zu moved) %7.1f ms\n", pts.indices.size(), ms(t));
        t = Clock::now();
        const std::string pf = encodeMayaLinkPoints(pts);
        MayaLinkPoints pb;
        CHECK(parseMayaLinkPoints(payloadOf(pf), pb, err));
        std::printf("  stroke: Points encode+parse %4.1f ms (%.1f MB)\n", ms(t), double(pf.size()) / 1e6);
        server.stop();
        unsetenv("SF_AI_LIVE_DIR");
        fs::remove_all(dir);
    }
}
#endif
