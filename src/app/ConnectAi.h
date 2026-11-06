// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Help > Connect an AI tool: for this install, the exact command or configuration each MCP client needs to start
// `sculptforge-cli --mcp` (stdio only: the client starts SculptForge as a child process and talks over its standard
// input and output; nothing listens on the network). Each client's format comes from its own documentation
// (docs/guide/scripting.md "AI agents (MCP)" cites them); the window is PanelConnectAi.cpp.
#pragma once

#include <string>
#include <vector>

namespace sf::connect {

// How a client starts the server: command plus arguments, the last one "--mcp".
struct Launch {
    std::string command;
    std::vector<std::string> args;
};

enum class Os { Windows, Mac, Linux };
// The "I have a ... subscription" picker: which clients sign in with that plan. Other = editors, local apps, SDKs.
enum class Plan { Claude, ChatGPT, Gemini, Grok, Other };
inline constexpr int kPlanCount = 5;

// One way to set a client up: a line to paste into a terminal, a file to edit, or fields to fill in.
struct Snippet {
    enum class Kind { Terminal, File, Fields };
    Kind kind = Kind::Terminal;
    std::string file; // Kind::File: where (a path or a menu), shown above the text
    std::string text; // exactly what to paste
};

struct Client {
    const char* id;
    const char* name; // product names stay English
    Plan plan;
    const char* how;  // one line, English (translated with tr() in the window)
    const char* docs; // the vendor's page this format was taken from
    std::vector<Snippet> snippets;
};

// Every client for this launch and OS, in display order (clients that don't run on `os` are left out).
std::vector<Client> clients(const Launch& launch, Os os);
// Notes for a plan: the vendor's apps that only take remote (HTTPS) servers, sign-in caveats. English, for tr().
std::vector<const char*> planNotes(Plan plan);
const char* planName(Plan plan); // "Claude", "ChatGPT", "Gemini", "Grok", "Other tools"

// Where the Claude Desktop bundle for `os` is downloaded (the newest release's .mcpb).
std::string bundleUrl(Os os);

// Quoting, exposed for tests: a JSON string literal (also valid TOML, YAML and Python), and one shell word.
std::string jsonString(const std::string& s);
std::string shellWord(const std::string& s, Os os);

} // namespace sf::connect
