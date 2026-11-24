// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Model Context Protocol server over stdio (newline-delimited JSON-RPC 2.0).
// Every SculptForge command becomes an MCP tool acting on one live document,
// so an agent (Claude Code, Claude Desktop, Cursor, ...) can build, sculpt,
// remesh, convert and export assets conversationally.
#pragma once

#include "core/Commands.h"
#include "core/Json.h"

#include <cstdint>
#include <iosfwd>
#include <string>

namespace sf {

// Handles one JSON-RPC message; returns the response line, or "" for notifications.
std::string handleMcpMessage(const CommandRegistry& registry, Document& doc, const std::string& line);

// Every command as an MCP tool definition (name, description, inputSchema), as tools/list sends them.
Json mcpToolList(const CommandRegistry& registry);

// One registry command through the studio policy and the audit log, exactly as if the agent called it alone (tools/call,
// and each command of a batch, cli/McpBatch.h). `arguments` is the call's JSON arguments object.
CommandResult callRegistryTool(const CommandRegistry& registry, Document& doc, const std::string& name, const Json& arguments,
                              int64_t* ms = nullptr); // (ms: how long it took)
// Whether the tool exists for agents (packs and plugins stay the user's own decision).
bool mcpToolCallable(const CommandRegistry& registry, const std::string& name);

// Serves until `in` closes. Returns the process exit code.
int runMcpServer(const CommandRegistry& registry, std::istream& in, std::ostream& out);

} // namespace sf
