// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What an agent reads without parsing text, for the headless MCP server (cli/McpServer.h):
//   - structuredContent: every tools/call result carries the numbers of the scene after the call (triangles,
//     vertices, bounds, Parts, undo depth, how long it took); tools/list gives every tool the matching outputSchema;
//   - resources (resources/list, resources/read, resources/templates/list): the scene, its Parts, one Part in
//     detail and the undo history as JSON, read-only (no tool call, no undo step).
// Both leave out the scene's numbers when the studio's policy (core/ManagedPolicy.h) doesn't let AI tools read ("info").
#pragma once

#include "core/Json.h"

#include <cstdint>
#include <string>

namespace sf {

struct Document;

// Whether the policy lets an agent read the scene's numbers (the "info" tool category). `why` says why not.
bool mcpMayReadScene(std::string* why = nullptr);

// The outputSchema every tool shares. `withScene` = the scene's numbers are in it (and required).
Json mcpOutputSchema(bool withScene);

// A tools/call result's structuredContent: {ok, message, ms} and, when the policy allows reading, the scene's numbers
// (triangles, vertices, bounds, parts, activePart, undoDepth, canRedo, scene{...}) as mesh_info and part_list report.
Json mcpStructuredResult(const Document& doc, bool ok, const std::string& message, int64_t ms);

// resources/list, resources/templates/list: {"resources": [...]} / {"resourceTemplates": [...]}.
Json mcpResourceList(const Document& doc);
Json mcpResourceTemplates();
// resources/read: fills `result` ({"contents": [{uri, mimeType, text}]}); false with `error` for an unknown URI.
bool mcpResourceRead(const Document& doc, const std::string& uri, Json& result, std::string& error);

} // namespace sf
