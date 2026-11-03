// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// sculptforge-cli: run SculptForge commands headless, for build farms, asset
// pipelines and AI agents.
#include "ai/AiCommands.h"
#include "ai/AiPolicy.h"
#include "ai/SplatTrainer.h"
#include "cli/McpServer.h"
#include "core/Benchmark.h"
#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/Pack.h"
#include "core/Script.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace {

void usage() {
    std::puts(
        "Usage: sculptforge-cli [options]\n"
        "\n"
        "  -c, --command \"<script>\"   Run commands, separated by ';'\n"
        "  -f, --file <script.txt>    Run commands from a file ('-' reads stdin)\n"
        "      --set NAME=VALUE       Define a variable; script values written $NAME are replaced\n"
        "  -l, --list                 List available commands\n"
        "      --schema               Print commands as LLM tool definitions (JSON)\n"
        "      --mcp                  Run as an MCP server on stdio (Claude Code, Claude Desktop, any MCP client)\n"
        "  -q, --quiet                Only print errors\n"
        "      --no-ai                Leave the AI commands out (as SF_AI=off; Preferences > AI turns them off for good)\n"
        "      --no-packs             Safe mode: leave every installed pack out (pack_install still works)\n"
        "      --bench                Run the performance benchmark (CPU side; the app's --bench adds the GPU)\n"
        "      --bench-sizes 4,16,32  Benchmark sizes in millions of triangles\n"
        "      --bench-dabs N         Dabs per benchmark stroke (default 40)\n"
        "      --bench-brushes TEXT   Only the benchmark brushes whose name contains TEXT (e.g. large)\n"
        "      --bench-no-project     Skip the project save / open (it needs twice the memory)\n"
        "      --bench-view           Benchmark only what the viewport costs (uploads, orbit)\n"
        "      --bench-dyntopo        Benchmark dynamic topology strokes (e.g. --bench-sizes 0.25,1,4)\n"
        "      --bench-micro          Benchmark micro-displacement levels (sizes = micro triangles, e.g. 33,100,300)\n"
        "      --bench-micro-level N  Micro level for --bench-micro (default 4: 256 per base triangle)\n"
        "      --bench-out FILE       Write the benchmark report (Markdown) to FILE\n"
        "  -h, --help                 Show this help\n"
        "\n"
        "Connect Claude Code:  claude mcp add sculptforge -- sculptforge-cli --mcp\n"
        "\n"
        "Example (game LOD pipeline):\n"
        "  sculptforge-cli -c 'open_obj path=rock.obj; export_lods path=out/rock.glb levels=4 ratio=0.5'");
}

} // namespace

int main(int argc, char** argv) {
    sf::CommandRegistry registry;
    // AI commands use the same settings file as the app (keys can also come from the environment), and only
    // while AI is on there (Preferences > AI), SF_AI allows it and --no-ai isn't given (ai/AiPolicy.h).
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--no-ai") sf::ai::setAiForcedOff(true);
    sf::ai::setAiEnabled(sf::ai::aiFeaturesAllowed(sf::ai::AiSettings::load(sf::ai::defaultAiSettingsPath()).enabled));
    sf::ai::registerAiCommands(registry, [] { return sf::ai::AiSettings::load(sf::ai::defaultAiSettingsPath()); });
    // splat_train: trains in the foreground with the app's tool settings (ai/SplatTrainer.h).
    sf::ai::registerSplatTrainCommands(registry, [] { return sf::ai::SplatTrainSettings::load(sf::ai::defaultSplatTrainSettingsPath()); });
    std::string script;
    bool quiet = false;
    std::map<std::string, std::string> vars;
    sf::BenchCommandLine bench;

    if (argc < 2) {
        usage();
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-c" || a == "--command") && i + 1 < argc) {
            script += argv[++i];
            script += "\n";
        } else if ((a == "-f" || a == "--file") && i + 1 < argc) {
            std::string path = argv[++i];
            std::stringstream ss;
            if (path == "-") {
                ss << std::cin.rdbuf();
            } else {
                std::ifstream f(path);
                if (!f) {
                    std::fprintf(stderr, "error: cannot read %s\n", path.c_str());
                    return 1;
                }
                ss << f.rdbuf();
            }
            script += ss.str();
            script += "\n";
        } else if (a == "--set" && i + 1 < argc) {
            std::string kv = argv[++i];
            size_t eq = kv.find('=');
            if (eq == std::string::npos || eq == 0) {
                std::fprintf(stderr, "error: --set expects NAME=VALUE\n");
                return 1;
            }
            vars[kv.substr(0, eq)] = kv.substr(eq + 1);
        } else if (a == "-l" || a == "--list") {
            for (const auto& c : registry.all()) {
                std::printf("%-18s %s\n", c.name.c_str(), c.description.c_str());
                for (const auto& p : c.params)
                    std::printf("    %s%s  %s\n", p.name.c_str(), p.required ? " (required)" : "", p.description.c_str());
            }
            return 0;
        } else if (a == "--mcp") {
            return sf::runMcpServer(registry, std::cin, std::cout);
        } else if (a == "--schema") {
            std::puts(registry.toolSchemaJson().c_str());
            return 0;
        } else if (sf::parseBenchArg(argc, argv, i, bench)) {
            // handled after the loop
        } else if (a == "--no-ai") {
            // read before the commands were registered
        } else if (a == "--no-packs") {
            sf::setPacksSafeMode(true);
        } else if (a == "-q" || a == "--quiet") {
            quiet = true;
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown option %s\n", a.c_str());
            usage();
            return 1;
        }
    }

    if (bench.run) return sf::runBenchCommandLine(bench, "## CPU only (sculptforge-cli --bench)\n\n");

    std::vector<sf::ScriptStatement> statements;
    std::string error;
    if (!sf::parseScript(script, statements, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    // Substitute $NAME variables (whole values only, so paths with spaces stay intact).
    for (auto& st : statements) {
        for (auto& [key, value] : st.args) {
            auto* s = std::get_if<std::string>(&value);
            if (!s || s->size() < 2 || (*s)[0] != '$') continue;
            auto it = vars.find(s->substr(1));
            if (it == vars.end()) {
                std::fprintf(stderr, "error (line %d): undefined variable %s\n", st.line, s->c_str());
                return 1;
            }
            *s = it->second;
        }
    }

    sf::Document doc;
    for (const auto& message : sf::loadBrushLibrary(doc)) std::fprintf(stderr, "warning: %s\n", message.c_str());
    for (const auto& st : statements) {
        sf::CommandResult r = registry.execute(doc, st.command, st.args);
        if (!r.ok) {
            std::fprintf(stderr, "error (line %d, %s): %s\n", st.line, st.command.c_str(), r.message.c_str());
            return 2;
        }
        if (!quiet) std::printf("%s: %s\n", st.command.c_str(), r.message.c_str());
    }
    return 0;
}
