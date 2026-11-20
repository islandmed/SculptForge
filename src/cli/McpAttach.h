// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// `sculptforge-cli --mcp --attach`: an MCP server on stdio that drives the OPEN app (live mode, link/AiLink.h)
// instead of a headless scene. tools/list is the full set from the first call (some clients never refresh it):
// every command, the viewport image, the camera and screen-space strokes, live_connect and live_disconnect. A tool
// called before the user has approved the connection in the app is answered with "call live_connect"; once approved
// the app's own list replaces ours. The app decides everything: what's asked first, what's refused. This side
// only carries messages.
#pragma once

#include "core/Json.h"

#include <iosfwd>
#include <string>

namespace sf {

class CommandRegistry;

// The tools live mode offers, from the commands `registry` holds: what the app lists once paired (minus what its own
// registry or a studio's policy changes), what `--attach` lists before. Not live_connect / live_disconnect.
Json mcpAppToolList(const CommandRegistry& registry);

// Serves until `in` closes. `endpoint` "" = this user's (link::aiLinkEndpoint). `registry` (null: only the window
// tools) is what the tool list shows before the app's own arrives.
int runMcpAttach(std::istream& in, std::ostream& out, const std::string& endpoint = {}, const CommandRegistry* registry = nullptr);

} // namespace sf
