// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help > Connect an AI tool (app/ConnectAi.h): every client's snippet for this install, quoted right for paths with
// spaces and backslashes; and what the headless MCP server shares with live mode (cli/McpTools.h): the recipes
// (prompts) and viewport_image.
#include "TestHarness.h"

#include "app/ConnectAi.h"
#include "cli/McpServer.h"
#include "cli/McpTools.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Image.h"
#include "core/Json.h"

#include <cstdio>
#include <set>
#include <string>

using namespace sf;

namespace {

int checkClients(const connect::Launch& launch, connect::Os os) {
    int problems = 0;
    std::set<int> plans;
    for (const connect::Client& c : connect::clients(launch, os)) {
        plans.insert(int(c.plan));
        if (c.snippets.empty() || std::string(c.docs).rfind("https://", 0) != 0 || !*c.how) {
            std::printf("  %s: no snippet, how or docs link\n", c.id);
            ++problems;
        }
        for (const connect::Snippet& s : c.snippets) {
            // The path must appear exactly as typed in a terminal, or escaped in a file.
            const bool hasPath = s.text.find(s.kind == connect::Snippet::Kind::Terminal ? connect::shellWord(launch.command, os)
                                             : s.kind == connect::Snippet::Kind::Fields ? launch.command
                                                                                        : connect::jsonString(launch.command)) !=
                                 std::string::npos;
            if (!hasPath || s.text.find("--mcp") == std::string::npos) {
                std::printf("  %s: the command or --mcp is missing:\n%s\n", c.id, s.text.c_str());
                ++problems;
            }
            if (s.kind == connect::Snippet::Kind::File && s.text.front() == '{') { // JSON files must parse
                Json j;
                std::string err;
                if (!Json::parseStrict(s.text, j, err)) {
                    std::printf("  %s: invalid JSON (%s):\n%s\n", c.id, err.c_str(), s.text.c_str());
                    ++problems;
                }
            }
        }
    }
    if (plans.size() != size_t(connect::kPlanCount)) ++problems; // every subscription has at least one client
    return problems;
}

} // namespace

TEST(connect_ai_quoting) {
    CHECK(connect::jsonString("C:\\Program Files\\SculptForge\\a\"b") == "\"C:\\\\Program Files\\\\SculptForge\\\\a\\\"b\"");
    CHECK(connect::shellWord("/usr/bin/sculptforge-cli", connect::Os::Linux) == "/usr/bin/sculptforge-cli");
    CHECK(connect::shellWord("/Applications/Sculpt Forge/it's", connect::Os::Mac) == "'/Applications/Sculpt Forge/it'\\''s'");
    CHECK(connect::shellWord("C:\\Program Files\\SculptForge\\sculptforge-cli.exe", connect::Os::Windows) ==
          "\"C:\\Program Files\\SculptForge\\sculptforge-cli.exe\"");
    CHECK(connect::shellWord("C:\\SF\\sculptforge-cli.exe", connect::Os::Windows) == "C:\\SF\\sculptforge-cli.exe");
}

TEST(connect_ai_every_client_has_a_working_snippet) {
    CHECK(checkClients({"C:\\Program Files\\SculptForge\\sculptforge-cli.exe", {"--mcp"}}, connect::Os::Windows) == 0);
    CHECK(checkClients({"/Applications/SculptForge.app/Contents/MacOS/sculptforge-cli", {"--mcp"}}, connect::Os::Mac) == 0);
    CHECK(checkClients({"/home/me/Apps/Sculpt Forge.AppImage", {"--cli", "--mcp"}}, connect::Os::Linux) == 0);
    CHECK(checkClients({"flatpak", {"run", "--command=sculptforge-cli", "org.sculptforge.SculptForge", "--mcp"}},
                       connect::Os::Linux) == 0);
    // Visual Studio is Windows only; the subscriptions lead to the vendors' own local tools.
    bool vsOnMac = false, codex = false, antigravity = false, grok = false;
    for (const connect::Client& c : connect::clients({"/x/sculptforge-cli", {"--mcp"}}, connect::Os::Mac)) {
        vsOnMac = vsOnMac || std::string(c.id) == "visual_studio";
        codex = codex || (std::string(c.id) == "codex" && c.plan == connect::Plan::ChatGPT);
        antigravity = antigravity || (std::string(c.id) == "antigravity" && c.plan == connect::Plan::Gemini);
        grok = grok || (std::string(c.id) == "grok_build" && c.plan == connect::Plan::Grok);
    }
    CHECK(!vsOnMac && codex && antigravity && grok);
    CHECK(connect::bundleUrl(connect::Os::Mac).find(".mcpb") != std::string::npos);
}

TEST(mcp_recipes_are_prompts) {
    CommandRegistry reg;
    Document d;
    Json init, j;
    std::string err;
    Json::parse(handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"), init, err);
    CHECK(init["result"]["capabilities"]["prompts"].isObject());
    CHECK(init["result"]["serverInfo"]["title"].asString() == "SculptForge");
    Json::parse(handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":2,"method":"prompts/list"})"), j, err);
    std::set<std::string> names;
    for (const Json& p : j["result"]["prompts"].items()) names.insert(p["name"].asString());
    CHECK(names.count("creature_head") && names.count("game_ready") && names.count("rock_variations") &&
          names.count("stylised_bust") && names.count("teach_me"));
    Json got;
    Json::parse(handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":3,"method":"prompts/get","params":{"name":"rock_variations","arguments":{"folder":"/tmp/rocks"}}})"),
                got, err);
    const std::string text = got["result"]["messages"][0]["content"]["text"].asString();
    CHECK(text.find("20 distinct rocks") != std::string::npos && text.find("/tmp/rocks") != std::string::npos &&
          text.find("viewport_image") != std::string::npos);
    Json missing;
    Json::parse(handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":4,"method":"prompts/get","params":{"name":"creature_head"}})"),
                missing, err);
    CHECK(missing.has("error")); // its description is required
}

TEST(mcp_viewport_image_headless) {
    CommandRegistry reg;
    Document d;
    Json j, empty;
    std::string err;
    CHECK(!renderViewportImage(d, Json::object(), empty, err) && !err.empty()); // nothing to show yet
    handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":0,"method":"tools/call","params":{"name":"new_sphere","arguments":{"subdivisions":3}}})");
    Json::parse(handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"viewport_image","arguments":{"size":256,"preset":"front"}}})"),
                j, err);
    const Json& image = j["result"]["content"][0];
    CHECK(!j["result"]["isError"].asBool(true) && image["type"].asString() == "image" &&
          image["mimeType"].asString() == "image/png" && image["data"].asString().size() > 1000);
    Json content; // the defaults: 768 px, three-quarter view
    CHECK(renderViewportImage(d, Json::object(), content, err));
    CHECK(content[1]["text"].asString().find("768 px") != std::string::npos);
    Json bad;
    Json::parse(handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"viewport_image","arguments":{"frame":"Nope"}}})"),
                bad, err);
    CHECK(bad["result"]["isError"].asBool(false));
}
