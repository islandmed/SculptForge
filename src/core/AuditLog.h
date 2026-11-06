// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The audit log: one JSON line per AI or MCP action (an AI tool calling a command, Brush Lab, Generate 3D), so a
// studio can see what AI did on which machine. Local, append-only, rotated; nothing is sent anywhere.
//
//   {"time":"2026-11-05T14:03:22.120Z","user":"ana","host":"ws042","client":"mcp:claude-code 2.1","tool":"mcp",
//    "command":"remesh","args":{"resolution":256},"result":"ok","message":"Remeshed: 812k triangles","ms":930}
//
// What is never written: artwork (meshes, images, textures), secrets (any argument named like a key, token,
// password or secret), and long text (strings are cut to 120 characters; a prompt's length is kept, not its
// words, unless it is short). File paths are kept: they say what was read or written.
//
// Path: the policy's audit.path ({user}, {host}, {date} filled in; core/ManagedPolicy.h), e.g. a network share,
// else <settings folder>/audit/ai-audit.jsonl. Rotation: past audit.maxMB (10) the file becomes .1, .1 becomes .2,
// ... up to audit.keep (5) old files. With audit.required, an action whose line can't be written is refused.
#pragma once

#include "core/Commands.h"
#include "core/Json.h"

#include <cstdint>
#include <string>

namespace sf::audit {

struct Entry {
    std::string client;   // "app", "cli", "python", "mcp:<client name> <version>"; "" = the process's (setClient)
    std::string tool;     // the surface: "mcp", "mcp-live", "ai"
    std::string command;  // "remesh", "brush_ai", "tools/list", ...
    Json args = Json::object(); // already summarized (summarizeArgs)
    bool ok = true;
    std::string message;  // the result's message (cut to 200 characters)
    int64_t ms = -1;      // how long it took; -1 = not timed
    std::string endpoint; // AI requests: the host they went to
};

// The process's client ("app", "cli", "python"; the MCP server sets "mcp:<name>" from the agent's initialize).
void setClient(const std::string& client);
std::string client();
// "mcp:" + the agent's clientInfo name and version from an initialize request's params ("mcp:unknown" if none),
// made safe for a log (printable, at most 64 characters).
std::string clientFromInitialize(const Json& params);

// The arguments with secrets, artwork and long text left out (see above).
Json summarizeArgs(const Args& args);

// Appends one line. False when it couldn't be written (MESSAGE in `error`). Thread-safe.
bool record(const Entry& e, std::string* error = nullptr);
// True when the policy requires the log: callers refuse the action if record() of its start fails.
bool required();
// Whether a line could be written now (with audit.required, checked before the action runs).
bool writable(std::string* error = nullptr);
// Where lines go now.
std::string logPath();

// Tests: write here instead (empty: back to the policy / default).
void setPathForTests(const std::string& path);

} // namespace sf::audit
