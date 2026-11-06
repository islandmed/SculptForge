// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ConnectAi.h"

#include <cctype>
#include <cstdio>
#include <string_view>

namespace sf::connect {

namespace {

using K = Snippet::Kind;

// What each snippet is built from (one per client format).
enum class Format {
    ClaudeCode,    // claude mcp add --scope user sculptforge -- CMD ARGS
    McpServers,    // {"mcpServers": {"sculptforge": {"command", "args"}}}: Claude Desktop, Cursor and most others
    VsCode,        // {"servers": {"sculptforge": {"type": "stdio", "command", "args"}}}
    Zed,           // {"context_servers": {"sculptforge": {"command", "args"}}}
    RovoDev,       // McpServers plus "transport": "stdio"
    CodexToml,     // [mcp_servers.sculptforge] command / args (Codex, Grok Build)
    Goose,         // config.yaml extensions entry
    Continue,      // .continue/mcpServers/*.yaml block
    Opencode,      // opencode.json "mcp" entry, command as one array
    Fields,        // a form: name, command, arguments (Jan)
    AgentsSdk,     // OpenAI Agents SDK, MCPServerStdio
};

struct Row {
    const char* id;
    const char* name;
    Plan plan;
    const char* how; // translated in the window (tools/i18n/coverage.py reads kAiClients' 4th column)
    const char* docs;
};

// Display order inside each plan. A client's snippets are built in clients() from its id.
constexpr Row kAiClients[] = {
    {"claude_desktop", "Claude Desktop", Plan::Claude,
     "Add this to its configuration file (Settings > Developer > Edit Config), then restart Claude Desktop.",
     "https://support.claude.com/en/articles/10949351-getting-started-with-local-mcp-servers-on-claude-desktop"},
    {"claude_code", "Claude Code", Plan::Claude, "Run once in a terminal; it signs in with your Claude plan.",
     "https://code.claude.com/docs/en/mcp"},
    {"codex", "Codex CLI", Plan::ChatGPT, "Run once in a terminal, then choose Sign in with ChatGPT when Codex starts.",
     "https://learn.chatgpt.com/docs/extend/mcp"},
    {"antigravity", "Antigravity CLI", Plan::Gemini,
     "Google's CLI for personal Google accounts (free, Google AI Pro and Ultra). Add this to its MCP file, then sign in with Google.",
     "https://antigravity.google/docs/mcp/"},
    {"gemini_cli", "Gemini CLI", Plan::Gemini,
     "For Gemini Code Assist Standard or Enterprise, or a Gemini API key. Run once in a terminal.",
     "https://github.com/google-gemini/gemini-cli/blob/main/docs/tools/mcp-server.md"},
    {"grok_build", "Grok Build", Plan::Grok,
     "xAI's coding agent (the grok command). Run once in a terminal; it signs in with your xAI account in the browser.",
     "https://docs.x.ai/build/features/mcp-servers"},
    {"cursor", "Cursor", Plan::Other, "Add to Cursor's global MCP file (or .cursor/mcp.json in a project).",
     "https://cursor.com/docs/context/mcp"},
    {"vscode", "VS Code (Copilot agent mode)", Plan::Other,
     "Run once in a terminal, or open the command MCP: Open User Configuration and add the entry.",
     "https://code.visualstudio.com/docs/agent-customization/mcp-servers"},
    {"visual_studio", "Visual Studio", Plan::Other, "Add to the global MCP file, then use it from Copilot Chat in Agent mode.",
     "https://learn.microsoft.com/en-us/visualstudio/ide/mcp-servers"},
    {"jetbrains", "JetBrains AI Assistant", Plan::Other,
     "Settings > Tools > AI Assistant > Model Context Protocol (MCP): Add, choose STDIO and paste this.",
     "https://www.jetbrains.com/help/ai-assistant/mcp.html"},
    {"zed", "Zed", Plan::Other, "Add to Zed's settings file (command palette: zed: open settings file).",
     "https://zed.dev/docs/ai/mcp"},
    {"windsurf", "Windsurf (Devin Local)", Plan::Other, "Run once in a terminal, or add to the user MCP file.",
     "https://docs.devin.ai/cli/extensibility/mcp/configuration"},
    {"copilot_cli", "GitHub Copilot CLI", Plan::Other, "Run once in a terminal.",
     "https://docs.github.com/en/copilot/how-tos/copilot-cli/customize-copilot/add-mcp-servers"},
    {"cline", "Cline", Plan::Other, "Run once in a terminal (Cline CLI), or paste into Cline's MCP settings (MCP Servers > Configure).",
     "https://docs.cline.bot/mcp/configuring-mcp-servers"},
    {"continue", "Continue", Plan::Other, "Save as a file in your workspace's .continue/mcpServers folder; tools work in agent mode.",
     "https://docs.continue.dev/customize/deep-dives/mcp"},
    {"kiro", "Kiro", Plan::Other, "Add to Kiro's user MCP file.", "https://kiro.dev/docs/mcp/configuration/"},
    {"amazon_q", "Amazon Q Developer CLI", Plan::Other, "Run once in a terminal.",
     "https://docs.aws.amazon.com/amazonq/latest/qdeveloper-ug/command-line-mcp-config-CLI.html"},
    {"warp", "Warp", Plan::Other, "Settings > Agents > MCP servers > Add, then paste this.",
     "https://docs.warp.dev/knowledge-and-collaboration/mcp"},
    {"goose", "Goose", Plan::Other, "Add under extensions in Goose's configuration file (or goose configure > Add Extension > Command-Line Extension).",
     "https://goose-docs.ai/docs/getting-started/using-extensions"},
    {"opencode", "opencode", Plan::Other, "Add to opencode.json.", "https://opencode.ai/docs/mcp-servers/"},
    {"rovo_dev", "Rovo Dev CLI", Plan::Other, "Add to Rovo Dev's MCP file (acli rovodev mcp opens it).",
     "https://support.atlassian.com/rovo/docs/connect-to-an-mcp-server-in-rovo-dev-cli/"},
    {"lm_studio", "LM Studio", Plan::Other, "Program tab > Install > Edit mcp.json, then paste this.",
     "https://lmstudio.ai/docs/app/mcp"},
    {"jan", "Jan", Plan::Other, "Settings > MCP Servers > +, then fill in these fields.", "https://docs.jan.ai/desktop/mcp"},
    {"agents_sdk", "OpenAI Agents SDK (Python)", Plan::Other, "Your own agent in Python (uses the OpenAI API with an API key).",
     "https://openai.github.io/openai-agents-python/mcp/"},
};

// Under the subscription picker: the vendors' apps that only take remote (HTTPS) servers, sign-in caveats.
// Translated in the window (tools/i18n/coverage.py reads kPlanNotes' 2nd column).
struct Note {
    Plan plan;
    const char* text;
};
constexpr Note kPlanNotes[] = {
    {Plan::Claude, "Claude Code uses your Pro, Max or Ultra plan when you sign in with your Claude account. If ANTHROPIC_API_KEY is set, it uses that key and API billing instead."},
    {Plan::Claude, "Claude on the web and on phones can't start programs on your computer: use Claude Desktop or Claude Code."},
    {Plan::ChatGPT, "The ChatGPT app only connects to remote (HTTPS) servers, and SculptForge doesn't run one: nothing of yours listens on the network. Codex CLI runs on your computer and signs in with your ChatGPT plan."},
    {Plan::Gemini, "Since June 2026 personal Google accounts (free, Google AI Pro, Google AI Ultra) use Antigravity CLI; Gemini CLI is for Gemini Code Assist and API keys."},
    {Plan::Grok, "The Grok app (web, phone, X) can't reach programs on your computer, only public HTTPS servers: use Grok Build. xAI's documentation doesn't say which plans Grok Build's sign-in includes; it also takes an xAI API key."},
    {Plan::Grok, "Your own code with the xAI API: sculptforge-cli --schema gives the tool definitions (examples in the user guide, Scripting > AI agents). API usage is billed by xAI."},
    {Plan::Other, "Rovo in Atlassian's web apps only takes public HTTPS servers added by an admin: use Rovo Dev CLI."},
    {Plan::Other, "Your own code with the OpenAI, Gemini or xAI API: sculptforge-cli --schema prints every tool as JSON (examples in the user guide, Scripting > AI agents)."},
};

std::string jsonArray(const std::vector<std::string>& items) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) out += (i ? ", " : "") + jsonString(items[i]);
    return out + "]";
}

// "CMD ARG ARG" for a terminal.
std::string commandLine(const Launch& l, Os os) {
    std::string out = shellWord(l.command, os);
    for (const std::string& a : l.args) out += " " + shellWord(a, os);
    return out;
}

// One field per line, so an entry fits the window (and reads like the vendors' examples).
std::string serverEntry(const Launch& l, const char* extra = nullptr) {
    return std::string("    ") + jsonString(l.name) + ": {\n" + (extra ? std::string("      ") + extra + ",\n" : std::string()) +
           "      \"command\": " + jsonString(l.command) + ",\n      \"args\": " + jsonArray(l.args) + "\n    }";
}

std::string wrapped(const char* key, const std::string& entry) {
    return std::string("{\n  \"") + key + "\": {\n" + entry + "\n  }\n}";
}

std::string home(Os os, const char* posix, const char* windows) { return os == Os::Windows ? windows : posix; }

Snippet terminal(std::string text) { return {K::Terminal, {}, std::move(text)}; }
Snippet file(std::string where, std::string text) { return {K::File, std::move(where), std::move(text)}; }

std::vector<Snippet> snippetsFor(const std::string& id, const Launch& l, Os os) {
    const std::string cmd = commandLine(l, os);
    const std::string mcpServers = wrapped("mcpServers", serverEntry(l));
    if (id == "claude_desktop") {
        const std::string where = os == Os::Windows ? "%APPDATA%\\Claude\\claude_desktop_config.json"
                                  : os == Os::Mac   ? "~/Library/Application Support/Claude/claude_desktop_config.json"
                                                    : "~/.config/Claude/claude_desktop_config.json";
        return {file(where, mcpServers)};
    }
    if (id == "claude_code") return {terminal("claude mcp add --scope user " + l.name + " -- " + cmd)};
    if (id == "codex")
        return {terminal("codex mcp add " + l.name + " -- " + cmd),
                file(home(os, "~/.codex/config.toml", "%USERPROFILE%\\.codex\\config.toml"),
                     "[mcp_servers." + l.name + "]\ncommand = " + jsonString(l.command) + "\nargs = " + jsonArray(l.args))};
    if (id == "antigravity")
        return {file(home(os, "~/.gemini/config/mcp_config.json", "%USERPROFILE%\\.gemini\\config\\mcp_config.json"), mcpServers)};
    if (id == "gemini_cli") {
        std::string args;
        for (const std::string& a : l.args) args += " " + shellWord(a, os);
        return {terminal("gemini mcp add --scope user " + l.name + " " + shellWord(l.command, os) + args)};
    }
    if (id == "grok_build")
        return {terminal("grok mcp add " + l.name + " -- " + cmd),
                file(home(os, "~/.grok/config.toml", "%USERPROFILE%\\.grok\\config.toml"),
                     "[mcp_servers." + l.name + "]\ncommand = " + jsonString(l.command) + "\nargs = " + jsonArray(l.args))};
    if (id == "cursor") return {file(home(os, "~/.cursor/mcp.json", "%USERPROFILE%\\.cursor\\mcp.json"), mcpServers)};
    if (id == "vscode") {
        const std::string entry = "{\"name\": " + jsonString(l.name) + ", \"type\": \"stdio\", \"command\": " + jsonString(l.command) +
                                  ", \"args\": " + jsonArray(l.args) + "}";
        std::vector<Snippet> out;
        if (os != Os::Windows) out.push_back(terminal("code --add-mcp " + shellWord(entry, os))); // (cmd.exe quoting of JSON is not worth it)
        out.push_back(file("MCP: Open User Configuration", wrapped("servers", serverEntry(l, "\"type\": \"stdio\""))));
        return out;
    }
    if (id == "visual_studio") return {file("%USERPROFILE%\\.mcp.json", wrapped("servers", serverEntry(l, "\"type\": \"stdio\"")))};
    if (id == "jetbrains") return {file("Model Context Protocol (MCP) > Add > STDIO", mcpServers)};
    if (id == "zed")
        return {file(home(os, "~/.config/zed/settings.json", "zed: open settings file"),
                     wrapped("context_servers", serverEntry(l)))};
    if (id == "windsurf")
        return {terminal("devin mcp add -s user " + l.name + " -- " + cmd),
                file(home(os, "~/.config/devin/mcp_config.json", "%APPDATA%\\devin\\mcp_config.json"), mcpServers)};
    if (id == "copilot_cli") return {terminal("copilot mcp add " + l.name + " -- " + cmd)};
    if (id == "cline")
        return {terminal("cline mcp add " + l.name + " --yes -- " + cmd), file("MCP Servers > Configure", mcpServers)};
    if (id == "continue") {
        std::string yaml = "name: SculptForge\nversion: 0.0.1\nschema: v1\nmcpServers:\n  - name: " + l.name + "\n    type: stdio\n"
                           "    command: " + jsonString(l.command) + "\n    args:\n";
        for (const std::string& a : l.args) yaml += "      - " + jsonString(a) + "\n";
        yaml.pop_back();
        return {file(".continue/mcpServers/" + l.name + ".yaml", yaml)};
    }
    if (id == "kiro") return {file(home(os, "~/.kiro/settings/mcp.json", "%USERPROFILE%\\.kiro\\settings\\mcp.json"), mcpServers)};
    if (id == "amazon_q")
        return {terminal("q mcp add --name " + l.name + " --command " + shellWord(l.command, os) + " --args " +
                         shellWord(jsonArray(l.args), os))};
    if (id == "warp") return {file("Settings > Agents > MCP servers > Add", mcpServers)};
    if (id == "goose") {
        std::string yaml = "extensions:\n  " + l.name + ":\n    name: SculptForge\n    type: stdio\n    cmd: " +
                           jsonString(l.command) + "\n    args: " + jsonArray(l.args) + "\n    enabled: true\n    timeout: 300";
        return {file(home(os, "~/.config/goose/config.yaml", "Goose's config.yaml (goose configure)"), yaml)};
    }
    if (id == "opencode") {
        std::vector<std::string> all{l.command};
        all.insert(all.end(), l.args.begin(), l.args.end());
        return {file("opencode.json", "{\n  \"$schema\": \"https://opencode.ai/config.json\",\n  \"mcp\": {\n    " + jsonString(l.name) + ": "
                                      "{\"type\": \"local\", \"command\": " + jsonArray(all) + ", \"enabled\": true}\n  }\n}")};
    }
    if (id == "rovo_dev")
        return {file(home(os, "~/.rovodev/mcp.json", "%USERPROFILE%\\.rovodev\\mcp.json"),
                     wrapped("mcpServers", serverEntry(l, "\"transport\": \"stdio\"")))};
    if (id == "lm_studio") return {file("mcp.json", mcpServers)};
    if (id == "jan") {
        std::string args;
        for (const std::string& a : l.args) args += (args.empty() ? "" : " ") + a;
        return {{K::Fields, {}, "Server Name: " + l.name + "\nCommand: " + l.command + "\nArguments: " + args}};
    }
    if (id == "agents_sdk")
        return {file("agent.py",
                     "import asyncio\n"
                     "from agents import Agent, Runner\n"
                     "from agents.mcp import MCPServerStdio\n\n"
                     "async def main():\n"
                     "    async with MCPServerStdio(name=" + jsonString(l.name) + ", client_session_timeout_seconds=60,\n"
                     "                              params={\"command\": " + jsonString(l.command) + ", \"args\": " +
                         jsonArray(l.args) + "}) as sf:\n"
                     "        agent = Agent(name=\"Sculptor\", instructions=\"Use SculptForge's tools.\", mcp_servers=[sf])\n"
                     "        result = await Runner.run(agent, \"Make a sphere, remesh it at 128 and export ball.glb\")\n"
                     "        print(result.final_output)\n\n"
                     "asyncio.run(main())")};
    return {};
}

} // namespace

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else out += ch;
    }
    return out + "\"";
}

std::string shellWord(const std::string& s, Os os) {
    bool plain = !s.empty();
    for (const char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || std::string_view("_-./:=@%+,").find(c) != std::string_view::npos ||
              (os == Os::Windows && c == '\\')))
            plain = false;
    if (plain) return s;
    if (os == Os::Windows) { // Command Prompt and PowerShell both take "..." (paths never hold a double quote)
        std::string out = "\"";
        for (const char c : s) out += c == '"' ? std::string("\\\"") : std::string(1, c);
        return out + "\"";
    }
    std::string out = "'";
    for (const char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::vector<Client> clients(const Launch& launch, Os os) {
    std::vector<Client> out;
    for (const Row& r : kAiClients) {
        if (std::string_view(r.id) == "visual_studio" && os != Os::Windows) continue; // Windows only
        // (its --args takes JSON, which Command Prompt and PowerShell quote differently: macOS and Linux only here)
        if (std::string_view(r.id) == "amazon_q" && os == Os::Windows) continue;
        out.push_back({r.id, r.name, r.plan, r.how, r.docs, snippetsFor(r.id, launch, os)});
    }
    return out;
}

std::vector<const char*> planNotes(Plan plan) {
    std::vector<const char*> out;
    for (const Note& n : kPlanNotes)
        if (n.plan == plan) out.push_back(n.text);
    return out;
}

const char* planName(Plan plan) {
    switch (plan) {
    case Plan::Claude: return "Claude";
    case Plan::ChatGPT: return "ChatGPT";
    case Plan::Gemini: return "Gemini";
    case Plan::Grok: return "Grok";
    case Plan::Other: return "Other tools";
    }
    return "";
}

std::string bundleUrl(Os os) {
    const char* name = os == Os::Windows ? "SculptForge-windows-x86_64.mcpb"
                       : os == Os::Mac   ? "SculptForge-macos.mcpb"
#if defined(__aarch64__)
                                         : "SculptForge-linux-arm64.mcpb";
#else
                                         : "SculptForge-linux-x86_64.mcpb";
#endif
    return std::string("https://github.com/islandmed/SculptForge/releases/latest/download/") + name;
}

} // namespace sf::connect
