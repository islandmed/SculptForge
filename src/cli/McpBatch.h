// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transactions for AI agents (headless MCP server, cli/McpServer.h): `batch` runs several commands as one undo step and
// rolls everything back when one fails; `checkpoint_save` / `checkpoint_restore` / `checkpoint_list` name places in the
// undo history and walk back or forward to them. Nothing is copied: both reuse the history (core/UndoBatch.h).
#pragma once

#include "core/Commands.h"
#include "core/Json.h"

#include <string>

namespace sf {

constexpr size_t kMaxBatchCommands = 64; // commands in one batch
constexpr size_t kMaxCheckpoints = 32;   // named checkpoints kept (saving more drops the oldest)

// True for batch and checkpoint_*: the tools handled here.
bool isHistoryTool(const std::string& name);
// Their tool definitions for tools/list; each is left out when the studio's policy doesn't allow it.
Json historyToolList();

struct HistoryToolReply {
    bool ok = true;
    std::string text;    // what the agent reads
    Json structured;     // the same, machine readable (null when there is none)
};
// Runs one of them. Every command inside a batch goes through the studio policy and the audit log as if called alone.
HistoryToolReply callHistoryTool(const CommandRegistry& registry, Document& doc, const std::string& name, const Json& arguments);

} // namespace sf
