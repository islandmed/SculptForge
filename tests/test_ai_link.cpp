// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Live mode's security (link/AiLink.h, link/AiLinkSession.h): requests before approval are refused, a wrong code is
// refused (and three close the connection), the socket and its folder are this user's only, a disconnect is seen
// at once, the policy asks before files outside the project / overwrites / scripts / network and refuses packs,
// and the message parser survives garbage. Plus the agent tools (core/AgentTools.h).
#include "TestHarness.h"

#include "cli/McpAttach.h"
#include "core/Commands.h"
#include "link/AiLink.h"
#include "link/AiLinkSession.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

using namespace sf;
using namespace sf::link;

namespace {

using Kind = AiLinkSession::Step::Kind;

std::string call(int id, const std::string& name, const std::string& args = "{}") {
    return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + R"(,"method":"tools/call","params":{"name":")" + name +
           R"(","arguments":)" + args + "}}";
}

AiLinkSession paired() {
    AiLinkSession s("123456");
    s.handle(R"({"hello":"sculptforge-live","v":1,"client":"Test"})", 0.0);
    s.handle(R"({"pair":"123456"})", 0.0);
    return s;
}

std::string scratchDir(const char* name) {
    const std::string d = tempPath(name);
    std::error_code ec;
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d, ec);
    return d;
}

} // namespace

TEST(ai_link_refuses_requests_before_approval) {
    AiLinkSession s("482913");
    // No hello: closed.
    AiLinkSession rude("482913");
    CHECK(rude.handle(call(1, "mesh_info"), 0.0).kind == Kind::Close);
    CHECK(rude.state() == AiLinkSession::State::Closed);

    const auto hello = s.handle(R"({"hello":"sculptforge-live","v":1,"client":"Claude Code\u0007\n"})", 0.0);
    CHECK(hello.kind == Kind::ShowApproval);
    CHECK(s.client() == "Claude Code"); // control characters dropped: the dialog shows data, not markup
    // Waiting for approval: tool calls are refused, never run.
    for (const char* tool : {"mesh_info", "save_project", "macro_run"}) {
        const auto st = s.handle(call(2, tool), 0.0);
        CHECK(st.kind == Kind::Reply);
        CHECK(st.reply.find("-32001") != std::string::npos);
    }
    CHECK(s.state() == AiLinkSession::State::Pairing);
}

TEST(ai_link_wrong_code_refused) {
    AiLinkSession s("482913");
    s.handle(R"({"hello":"sculptforge-live","v":1})", 0.0);
    auto st = s.handle(R"({"pair":"000000"})", 0.0);
    CHECK(st.kind == Kind::Reply && st.reply.find("\"paired\":false") != std::string::npos);
    CHECK(s.attemptsLeft() == 2);
    CHECK(s.handle(call(1, "mesh_info"), 0.0).reply.find("-32001") != std::string::npos);
    st = s.handle(R"({"pair":"48291"})", 0.0); // a prefix isn't the code
    CHECK(s.attemptsLeft() == 1);
    st = s.handle(R"({"pair":"999999"})", 0.0);
    CHECK(st.kind == Kind::Close); // three wrong codes: the connection closes
    CHECK(s.state() == AiLinkSession::State::Closed);
    // The right code after that changes nothing.
    CHECK(s.handle(R"({"pair":"482913"})", 0.0).kind == Kind::None);

    AiLinkSession ok("482913");
    ok.handle(R"({"hello":"sculptforge-live","v":1})", 0.0);
    CHECK(ok.handle(R"({"pair":"482 913"})", 0.0).reply == R"({"paired":true})");
    CHECK(ok.state() == AiLinkSession::State::Paired);

    AiLinkSession denied("482913");
    denied.handle(R"({"hello":"sculptforge-live","v":1})", 0.0);
    CHECK(denied.deny().find("declined") != std::string::npos);
    CHECK(denied.handle(call(1, "mesh_info"), 0.0).kind == Kind::None);

    CHECK(sameCode("123456", "123456"));
    CHECK(!sameCode("", "123456"));
    CHECK(!sameCode("1234567", "123456"));
    const std::string a = makePairingCode(), b = makePairingCode();
    CHECK(a.size() == 6 && a.find_first_not_of("0123456789") == std::string::npos);
    CHECK(a != b || makePairingCode() != a); // (one in a million twice in a row)
}

TEST(ai_link_paired_requests_and_rate_limit) {
    AiLinkSession s = paired();
    CHECK(s.state() == AiLinkSession::State::Paired);
    auto st = s.handle(R"({"jsonrpc":"2.0","id":7,"method":"tools/list"})", 0.0);
    CHECK(st.kind == Kind::ListTools);
    st = s.handle(call(8, "stroke", R"({"points":"0,0,0","radius":0.1,"invert":true,"nested":{"a":[1,2]}})"), 0.0);
    CHECK(st.kind == Kind::Execute);
    CHECK(st.call.name == "stroke");
    CHECK(std::get<double>(st.call.args.at("radius")) == 0.1);
    CHECK(std::get<bool>(st.call.args.at("invert")));
    CHECK(std::get<std::string>(st.call.args.at("nested")) == R"({"a":[1,2]})"); // data, as text
    CHECK(s.handle(R"({"jsonrpc":"2.0","id":9,"method":"resources/list"})", 0.0).reply.find("-32601") != std::string::npos);
    // A burst past the bucket: refused with "slow down"; time refills it.
    int limited = 0;
    for (int i = 0; i < 40; ++i)
        if (s.handle(call(100 + i, "mesh_info"), 1.0).reply.find("slow down") != std::string::npos) ++limited;
    CHECK(limited > 0);
    CHECK(s.handle(call(200, "mesh_info"), 10.0).kind == Kind::Execute);
}

TEST(ai_link_policy) {
    CommandRegistry reg;
    const std::string project = scratchDir("sf_ai_link_policy");
    const std::string elsewhere = scratchDir("sf_ai_link_elsewhere");
    { std::ofstream(project + "/old.glb") << "x"; }
    AiLinkPolicy p;
    p.projectDir = project;
    auto verdict = [&](const std::string& name, const Args& a) { return p.classify(reg.find(name), name, a).verdict; };

    CHECK(AiLinkPolicy::hidden("pack_install") && AiLinkPolicy::hidden("ai_features"));
    CHECK(verdict("pack_install", {{"path", project + "/p.sfpack"}}) == AiVerdict::Deny);
    CHECK(verdict("mesh_info", {}) == AiVerdict::Allow);
    CHECK(p.classify(nullptr, "viewport_image", {}).verdict == AiVerdict::Allow);
    // Files: inside the project folder fine, a new file there fine, an existing one asks (overwrite).
    CHECK(verdict("export_glb", {{"path", project + "/new.glb"}}) == AiVerdict::Allow);
    CHECK(verdict("export_glb", {{"path", std::string("relative.glb")}}) == AiVerdict::Allow);
    CHECK(verdict("export_glb", {{"path", project + "/old.glb"}}) == AiVerdict::Confirm);
    // Outside it (or escaping it with ..): asks, reading or writing.
    CHECK(verdict("export_glb", {{"path", elsewhere + "/x.glb"}}) == AiVerdict::Confirm);
    CHECK(verdict("open", {{"path", elsewhere + "/x.obj"}}) == AiVerdict::Confirm);
    CHECK(verdict("open", {{"path", project + "/../sf_ai_link_elsewhere/x.obj"}}) == AiVerdict::Confirm);
    // A picked file (or folder) is the user's choice.
    p.addPicked(elsewhere + "/x.obj");
    CHECK(verdict("open", {{"path", elsewhere + "/x.obj"}}) == AiVerdict::Allow);
    CHECK(verdict("open", {{"path", elsewhere + "/y.obj"}}) == AiVerdict::Confirm);
    // Programs and dot-files: always asked, even inside the project.
    CHECK(verdict("export_glb", {{"path", project + "/run.sh"}}) == AiVerdict::Confirm);
    CHECK(verdict("export_glb", {{"path", project + "/.bashrc"}}) == AiVerdict::Confirm);
    // Saving over the project, scripts, network, deleting library files.
    CHECK(verdict("save_project", {}) == AiVerdict::Confirm);
    CHECK(verdict("macro_run", {{"name", std::string("x")}}) == AiVerdict::Confirm);
    CHECK(AiLinkPolicy{}.classify(reg.find("mesh_info"), "image_to_3d", {}).verdict == AiVerdict::Confirm);
    CHECK(verdict("brush_delete", {{"name", std::string("x")}}) == AiVerdict::Confirm);
    CHECK(AiLinkPolicy{}.classify(reg.find("mesh_info"), "anything", {{"url", std::string("http://x")}}).verdict == AiVerdict::Confirm);
    // No project: a relative path can't be placed, so it asks.
    AiLinkPolicy none;
    CHECK(none.classify(reg.find("export_glb"), "export_glb", {{"path", std::string("a.glb")}}).verdict == AiVerdict::Confirm);
}

TEST(ai_link_message_parser_fuzz) {
    std::mt19937 rng(1234);
    const std::string seeds[] = {call(1, "mesh_info"), R"({"pair":"123456"})", R"({"hello":"sculptforge-live"})",
                                 R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"};
    const std::string alphabet = "{}[]\":,0123456789abcdefnulltruefalse\\u \t\n\x01\xff";
    for (int round = 0; round < 3000; ++round) {
        AiLinkSession s = round % 2 ? paired() : AiLinkSession("123456");
        std::string m = seeds[size_t(round) % 4];
        const int edits = 1 + int(rng() % 8);
        for (int e = 0; e < edits; ++e) {
            const size_t at = m.empty() ? 0 : rng() % m.size();
            switch (rng() % 3) {
            case 0: m.insert(at, 1, alphabet[rng() % alphabet.size()]); break;
            case 1: if (!m.empty()) m.erase(at, 1); break;
            default: if (!m.empty()) m[at] = alphabet[rng() % alphabet.size()]; break;
            }
        }
        const auto st = s.handle(m, 0.0); // must not crash or hang
        if (round % 2 == 0) CHECK(st.kind != Kind::Execute && st.kind != Kind::ListTools); // never before approval
    }
    // Deep nesting and huge numbers of arguments are refused, not followed.
    AiLinkSession s = paired();
    CHECK(s.handle(std::string(100000, '[') + std::string(100000, ']'), 0.0).kind == Kind::Reply);
    std::string many = "{";
    for (int i = 0; i < 100; ++i) many += (i ? "," : "") + std::string("\"k") + std::to_string(i) + "\":1";
    many += "}";
    CHECK(s.handle(call(1, "mesh_info", many), 0.0).reply.find("Bad tool call") != std::string::npos);
    // A flood of garbage closes the connection.
    AiLinkSession flood("123456");
    Kind last = Kind::None;
    for (int i = 0; i < AiLinkSession::kMaxRejected; ++i) last = flood.handle("garbage", 0.0).kind;
    CHECK(last == Kind::Close);
}

#if !defined(_WIN32)
TEST(ai_link_socket_permissions_and_disconnect) {
    // A runtime folder of our own for the test (the user's real one may have the app listening).
    const std::string run = scratchDir("sf_ai_link_run");
    const char* old = std::getenv("XDG_RUNTIME_DIR");
    const std::string saved = old ? old : "";
    setenv("XDG_RUNTIME_DIR", run.c_str(), 1);
    std::string err;
    const std::string endpoint = aiLinkEndpoint(err);
    CHECK(!endpoint.empty());
    CHECK(aiLinkFileMode(std::filesystem::path(endpoint).parent_path().string()) == 0700);

    AiLinkListener listener;
    CHECK(listener.open(endpoint, err));
    CHECK(aiLinkFileMode(endpoint) == 0600);
    // A second listener on the same endpoint (another window) is refused while the first one lives.
    AiLinkListener second;
    CHECK(!second.open(endpoint, err));
    { // (its probe connection, already closed, waits in the first one's queue)
        AiLinkConn probe;
        CHECK(listener.accept(probe, 2000));
        std::string none;
        CHECK(probe.readLine(none, 2000) == -1);
    }

    AiLinkConn client, server;
    CHECK(aiLinkConnect(client, endpoint, err));
    bool refused = true;
    CHECK(listener.accept(server, 2000, &refused));
    CHECK(!refused); // same user
    CHECK(client.writeLine(R"({"hello":"sculptforge-live"})"));
    std::string line;
    CHECK(server.readLine(line, 2000) == 1 && line == R"({"hello":"sculptforge-live"})");
    // A line longer than the limit closes the connection.
    std::thread big([&] { client.writeLine(std::string(kAiLinkMaxLine + 10, 'x')); }); // (fails once the app closes)
    CHECK(server.readLine(line, 2000) == -1);
    big.join();
    CHECK(!server.valid());
    // Disconnect: the other side sees it at once.
    AiLinkConn c2, s2;
    CHECK(aiLinkConnect(c2, endpoint, err));
    CHECK(listener.accept(s2, 2000));
    s2.shutdown();
    s2.close();
    CHECK(c2.readLine(line, 2000) == -1);

    // A folder opened to others is closed again; one that isn't a folder is refused.
    std::filesystem::permissions(run + "/sculptforge", std::filesystem::perms::all);
    listener.close();
    CHECK(!aiLinkEndpoint(err).empty());
    CHECK(aiLinkFileMode(run + "/sculptforge") == 0700);
    std::filesystem::remove_all(run + "/sculptforge");
    { std::ofstream(run + "/sculptforge") << "x"; }
    CHECK(aiLinkEndpoint(err).empty());
    std::filesystem::remove(run + "/sculptforge");
    // The socket file is gone after close; nobody's listening: the CLI says so.
    CHECK(aiLinkFileMode(endpoint) == -1);
    AiLinkConn c3;
    CHECK(!aiLinkConnect(c3, endpoint, err));
    if (old) setenv("XDG_RUNTIME_DIR", saved.c_str(), 1);
    else unsetenv("XDG_RUNTIME_DIR");
}

TEST(ai_link_long_runtime_folder_still_listens) {
    // macOS: $TMPDIR (and so test folders) is long, and a socket path must fit sun_path (104 bytes there).
    const std::string run = scratchDir("sf_ai_link_long_runtime_folder_padding_padding_padding_padding_padding_padding");
    const char* old = std::getenv("XDG_RUNTIME_DIR");
    const std::string saved = old ? old : "";
    setenv("XDG_RUNTIME_DIR", run.c_str(), 1);
    std::string err;
    const std::string endpoint = aiLinkEndpoint(err);
    CHECK(!endpoint.empty() && endpoint.size() < 100);
    CHECK(aiLinkFileMode(std::filesystem::path(endpoint).parent_path().string()) == 0700);
    AiLinkListener listener;
    CHECK(listener.open(endpoint, err));
    AiLinkConn client, server;
    CHECK(aiLinkConnect(client, endpoint, err));
    CHECK(listener.accept(server, 2000));
    listener.close();
    std::filesystem::remove(std::filesystem::path(endpoint).parent_path());
    if (old) setenv("XDG_RUNTIME_DIR", saved.c_str(), 1);
    else unsetenv("XDG_RUNTIME_DIR");
}

TEST(ai_link_attach_end_to_end) {
    // sculptforge-cli --mcp --attach against a stand-in app: live_connect waits, the code pairs, tools are forwarded.
    const std::string run = scratchDir("sf_ai_link_attach");
    const std::string endpoint = run + "/live.sock";
    AiLinkListener listener;
    std::string err;
    CHECK(listener.open(endpoint, err));
    std::thread app([&] {
        AiLinkConn conn;
        if (!listener.accept(conn, 5000)) return;
        AiLinkSession s("314159");
        std::string line;
        while (conn.readLine(line, 5000) == 1) {
            const auto st = s.handle(line, 0.0);
            if (st.kind == Kind::ListTools) conn.writeLine(AiLinkSession::listReply(st.call.id, Json::array()));
            else if (st.kind == Kind::Execute) conn.writeLine(AiLinkSession::resultReply(st.call.id, true, "ran " + st.call.name));
            else if (!st.reply.empty()) conn.writeLine(st.reply);
            if (st.kind == Kind::Close) break;
        }
    });
    std::stringstream in, out;
    in << R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"clientInfo":{"name":"Tester"}}})" << "\n"
       << call(2, "mesh_info") << "\n"                                   // not connected yet
       << call(3, "live_connect", R"({"code":"000000"})") << "\n"         // wrong code
       << call(4, "live_connect", R"({"code":"314159"})") << "\n"         // right code
       << call(5, "mesh_info") << "\n"
       << call(6, "live_disconnect") << "\n"
       << call(7, "mesh_info") << "\n";
    runMcpAttach(in, out, endpoint);
    app.join();
    const std::string o = out.str();
    CHECK(o.find("Not connected to SculptForge: call live_connect first") != std::string::npos);
    CHECK(o.find("wrong code") != std::string::npos);
    CHECK(o.find("Connected to SculptForge") != std::string::npos);
    CHECK(o.find("ran mesh_info") != std::string::npos);
    CHECK(o.find("notifications/tools/list_changed") != std::string::npos);
    CHECK(o.rfind("Not connected to SculptForge") > o.find("ran mesh_info")); // after Disconnect, refused again
}
#endif

TEST(agent_tools_blockout_measure_overview) {
    CommandRegistry reg;
    Document doc;
    CommandResult r = reg.execute(doc, "blockout",
                                  {{"spheres", std::string("0,0,0,0.6,-1 | 0,0.9,0.1,0.4,0 | 0.5,0.2,0,0.25,0")},
                                   {"name", std::string("Body")},
                                   {"resolution", 64.0}});
    CHECK(r.ok);
    CHECK(doc.findPart("Body") >= 0);
    CHECK(doc.mesh.triangleCount() > 0); // the skinned Part is active
    r = reg.execute(doc, "measure", {});
    CHECK(r.ok && r.message.find("proportions") != std::string::npos);
    r = reg.execute(doc, "measure", {{"compare", std::string("Body")}});
    CHECK(r.ok && r.message.find("ratio") != std::string::npos);
    r = reg.execute(doc, "measure", {{"from", std::string("0,0,0")}, {"to", std::string("3,4,0")}});
    CHECK(r.ok && r.message.find("Distance 5") == 0);
    r = reg.execute(doc, "scene_overview", {});
    CHECK(r.ok && r.message.find("Body") != std::string::npos && r.message.find("active") != std::string::npos);
    CHECK(!reg.execute(doc, "blockout", {{"spheres", std::string("0,0,0,-1")}}).ok);
    CHECK(!reg.execute(doc, "blockout", {{"spheres", std::string("0,0,0,1 | 1,0,0,1,5")}}).ok);
    CHECK(!reg.execute(doc, "measure", {{"part", std::string("Nope")}}).ok);
}
