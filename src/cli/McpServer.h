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

#include <iosfwd>
#include <string>

namespace sf {

// Handles one JSON-RPC message; returns the response line, or "" for notifications.
std::string handleMcpMessage(const CommandRegistry& registry, Document& doc, const std::string& line);

// Every command as an MCP tool definition (name, description, inputSchema), as tools/list sends them.
Json mcpToolList(const CommandRegistry& registry);

// Serves until `in` closes. Returns the process exit code.
int runMcpServer(const CommandRegistry& registry, std::istream& in, std::ostream& out);

} // namespace sf
