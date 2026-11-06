// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the headless MCP server (cli/McpServer.h) and live mode (sculptforge-cli --attach) share, so an agent
// sees one tool set either way:
//   - the recipes: MCP prompts (prompts/list, prompts/get) that walk an agent through a whole job;
//   - viewport_image: a picture of the scene. Live mode shows the open viewport; the headless server draws the
//     visible Parts itself on the CPU (no window, no GPU), same name and schema.
#pragma once

#include "core/Json.h"

#include <string>

namespace sf {

struct Document;

// prompts/list's result: {"prompts": [{name, title, description, arguments}]}.
Json mcpPromptList();
// prompts/get: fills `result` ({description, messages}) or `error` (unknown recipe, a required argument missing).
bool mcpPromptGet(const std::string& name, const Json& args, Json& result, std::string& error);

// The viewport_image tool definition ({name, description, inputSchema}), for tools/list.
Json viewportImageTool();
// Headless viewport_image: draws doc's visible Parts (args: size, yaw, elevation, frame "all" | "active" | a Part's
// name, preset, zoom; always orthographic) and fills `content` with an MCP content array (the PNG as image content, then a line about the view).
bool renderViewportImage(const Document& doc, const Json& args, Json& content, std::string& error);

} // namespace sf
