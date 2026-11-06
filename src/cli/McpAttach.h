// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// `sculptforge-cli --mcp --attach`: an MCP server on stdio that drives the OPEN app (live mode, link/AiLink.h)
// instead of a headless scene. Until the user approves the connection in the app it offers one tool,
// live_connect; once approved, the app's own tools (every command, plus the viewport image, the camera and
// screen-space strokes) and live_disconnect. The app decides everything: what's offered, what's asked first,
// what's refused. This side only carries messages.
#pragma once

#include <iosfwd>
#include <string>

namespace sf {

// Serves until `in` closes. `endpoint` "" = this user's (link::aiLinkEndpoint).
int runMcpAttach(std::istream& in, std::ostream& out, const std::string& endpoint = {});

} // namespace sf
