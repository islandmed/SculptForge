// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/McpBatch.h"

#include "cli/McpServer.h"
#include "core/AuditLog.h"
#include "core/ManagedPolicy.h"
#include "core/OpControl.h"
#include "core/UndoBatch.h"

#include <algorithm>
#include <chrono>

namespace sf {

namespace {

bool isImageTool(const std::string& n) { return n == "viewport_image" || n == "view_closeup"; }

// Files an export or save wrote stay where they are when a batch is rolled back.
bool writesFiles(const std::string& n) { return n.rfind("export", 0) == 0 || n.rfind("save", 0) == 0 || n == "splat_save"; }

Json prop(const char* type, const char* description) {
    Json p = Json::object();
    p.set("type", type);
    p.set("description", description);
    return p;
}

Json tool(const char* name, const char* description, Json props, Json required) {
    Json schema = Json::object();
    schema.set("type", "object");
    schema.set("properties", std::move(props));
    schema.set("required", std::move(required));
    Json t = Json::object();
    t.set("name", name);
    t.set("description", description);
    t.set("inputSchema", std::move(schema));
    return t;
}

bool allowed(const std::string& name, std::string* why = nullptr) {
    return name == "batch" ? policy::mcpAllowed(why) : policy::mcpToolAllowed(name, {}, why);
}

HistoryToolReply fail(const std::string& message) {
    HistoryToolReply r;
    r.ok = false;
    r.text = message;
    return r;
}

std::string plural(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// ---- batch ----

HistoryToolReply runBatch(const CommandRegistry& registry, Document& doc, const Json& arguments) {
    const Json& commands = arguments["commands"];
    if (!commands.isArray() || commands.size() == 0) return fail("batch needs `commands`: a list of {name, arguments}");
    if (commands.size() > kMaxBatchCommands)
        return fail("A batch holds at most " + std::to_string(kMaxBatchCommands) + " commands (this one has " +
                    std::to_string(commands.size()) + "): split it, or use checkpoint_save before the first part");
    const bool stopOnError = !arguments.has("stop_on_error") || arguments["stop_on_error"].asBool(true);
    // Everything is checked before anything runs: a mistake in command 7 must not leave 1-6 done.
    for (size_t i = 0; i < commands.size(); ++i) {
        const Json& c = commands[i];
        const std::string where = "command " + std::to_string(i + 1);
        if (!c.isObject() || !c["name"].isString() || c["name"].asString().empty()) return fail(where + " needs a `name`");
        if (c.has("arguments") && !c["arguments"].isObject()) return fail(where + ": `arguments` must be an object");
        const std::string& n = c["name"].asString();
        if (isHistoryTool(n)) return fail(where + " (" + n + "): batches don't nest, and checkpoints are made around them");
        if (n == "undo" || n == "redo")
            return fail(where + " (" + n + "): undo and redo can't be inside a batch; a failed batch rolls itself back");
        if (isImageTool(n)) return fail(where + " (" + n + "): pictures can't be part of a batch; call it alone");
        if (!mcpToolCallable(registry, n)) return fail(where + ": unknown tool " + n);
    }

    const HistoryMark before = markHistory(doc, false);
    const uint32_t activeBefore = doc.parts[size_t(doc.activePart)].id;
    Json steps = Json::array();
    std::vector<std::string> wroteFiles;
    int failedAt = -1;
    std::string failedWhy;
    size_t ran = 0, failedCount = 0;
    bool cancelled = false; // (the client cancelled: the batch rolls back whole, whatever stop_on_error says)
    {
        UndoBatchScope scope; // every step the commands push carries this id: one undo step
        for (size_t i = 0; i < commands.size(); ++i) {
            const std::string& n = commands[i]["name"].asString();
            if (opCancelled()) {
                cancelled = true;
                if (failedAt < 0) failedAt = int(i + 1), failedWhy = "cancelled before " + n;
                break;
            }
            int64_t ms = 0;
            CommandResult r;
            {
                OpProgressWindow window(float(i) / float(commands.size()), 1.0f / float(commands.size())); // (each command's slice)
                opProgress(0.0f, "Command " + std::to_string(i + 1) + " of " + std::to_string(commands.size()) + ": " + n);
                r = callRegistryTool(registry, doc, n, commands[i]["arguments"], &ms);
            }
            ++ran;
            if (!r.ok && opCancelled()) cancelled = true;
            Json s = Json::object();
            s.set("index", int(i + 1));
            s.set("name", n);
            s.set("ok", r.ok);
            s.set("message", r.message);
            s.set("ms", double(ms));
            steps.push(std::move(s));
            if (r.ok && writesFiles(n)) wroteFiles.push_back(n);
            if (!r.ok) {
                ++failedCount;
                if (failedAt < 0) failedAt = int(i + 1), failedWhy = n + ": " + r.message;
                if (stopOnError || cancelled) break;
            }
        }
    }

    HistoryToolReply reply;
    Json out = Json::object();
    out.set("total", double(commands.size()));
    out.set("executed", double(ran));
    out.set("steps", steps);
    std::string text;
    const bool rollBack = cancelled || (failedAt >= 0 && stopOnError);
    if (rollBack) {
        const HistoryWalk w = walkToMark(doc, before);
        if (const int i = doc.partIndex(activeBefore); i >= 0) doc.selectPart(i); // (part_select isn't a history step)
        const bool same = w.exact && sceneHash(doc) == before.hash;
        reply.ok = false;
        out.set("ok", false);
        out.set("rolled_back", same);
        out.set("failed_index", failedAt);
        out.set("failed", failedWhy);
        out.set("steps_undone", w.undone);
        out.set("verified", same);
        text = "Batch failed at command " + std::to_string(failedAt) + " of " + std::to_string(commands.size()) + " (" + failedWhy +
               "). ";
        if (same) {
            text += "Rolled back: the scene is exactly as before the batch.";
        } else {
            std::string problem = w.problem.empty() ? "the scene differs from before the batch (something in it was changed "
                                                      "outside the undo history)"
                                                    : w.problem;
            text += "ROLLBACK INCOMPLETE: " + problem + ". Check with mesh_info / viewport_image, or checkpoint_restore.";
            out.set("problem", problem);
        }
        if (!wroteFiles.empty()) {
            Json files = Json::array();
            std::string list;
            for (const std::string& f : wroteFiles) files.push(f), list += (list.empty() ? "" : ", ") + f;
            out.set("files_written", files);
            text += " Files written by " + list + " stay on disk.";
        }
    } else {
        reply.ok = failedAt < 0;
        out.set("ok", reply.ok);
        out.set("rolled_back", false);
        if (failedAt >= 0) {
            out.set("failed_index", failedAt);
            out.set("failed", failedWhy);
        }
        text = failedAt < 0 ? "Batch ok: " + plural(commands.size(), "command", "commands") + ", one undo step."
                            : std::to_string(failedCount) + " of " + std::to_string(commands.size()) +
                                  " commands failed (stop_on_error is off, nothing was rolled back); first: command " +
                                  std::to_string(failedAt) + " (" + failedWhy + "). One undo step.";
    }
    for (const Json& s : steps.items())
        text += "\n" + std::to_string(int(s["index"].asNumber())) + ". " + s["name"].asString() + (s["ok"].asBool() ? ": " : " FAILED: ") +
                s["message"].asString();
    if (rollBack && ran < commands.size())
        text += "\n(" + plural(commands.size() - ran, "command", "commands") + " after it did not run)";
    reply.text = text;
    reply.structured = std::move(out);
    return reply;
}

// ---- checkpoints ----

HistoryToolReply saveCheckpoint(Document& doc, const Json& arguments) {
    const std::string name = arguments["name"].asString();
    if (name.empty() || name.size() > 64 || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
        return fail("checkpoint_save needs a `name` of 1-64 characters");
    auto& list = doc.checkpoints;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& c) { return c.first == name; }), list.end());
    std::string dropped;
    while (list.size() >= kMaxCheckpoints) {
        dropped = list.front().first;
        list.erase(list.begin());
    }
    list.emplace_back(name, markHistory(doc));
    HistoryToolReply r;
    r.text = "Checkpoint '" + name + "' saved (" + plural(list.size(), "checkpoint", "checkpoints") + ")";
    if (!dropped.empty()) r.text += "; the oldest, '" + dropped + "', was dropped (at most " + std::to_string(kMaxCheckpoints) + ")";
    r.structured = Json::object();
    r.structured.set("ok", true);
    r.structured.set("name", name);
    r.structured.set("checkpoints", double(list.size()));
    return r;
}

HistoryToolReply restoreCheckpoint(Document& doc, const Json& arguments) {
    const std::string name = arguments["name"].asString();
    const auto it = std::find_if(doc.checkpoints.begin(), doc.checkpoints.end(), [&](const auto& c) { return c.first == name; });
    if (it == doc.checkpoints.end()) return fail("No checkpoint named '" + name + "' (checkpoint_list shows them)");
    const HistoryWalk w = restoreCheckpointMark(doc, it->second);
    HistoryToolReply r;
    r.ok = w.exact;
    r.structured = Json::object();
    r.structured.set("ok", w.exact);
    r.structured.set("name", name);
    r.structured.set("steps_undone", w.undone);
    r.structured.set("steps_redone", w.redone);
    if (w.exact) {
        r.text = w.undone + w.redone == 0 ? "Already at checkpoint '" + name + "'"
                                          : "Restored checkpoint '" + name + "': " + plural(size_t(w.undone), "step", "steps") +
                                                " undone, " + std::to_string(w.redone) + " redone. undo takes it back";
    } else {
        r.text = "Could not restore checkpoint '" + name + "' exactly: " + w.problem + " (" + std::to_string(w.undone) +
                 " undone, " + std::to_string(w.redone) + " redone)";
        r.structured.set("problem", w.problem);
    }
    return r;
}

HistoryToolReply listCheckpoints(const Document& doc) {
    HistoryToolReply r;
    Json list = Json::array();
    std::string text;
    for (const auto& [name, mark] : doc.checkpoints) {
        const HistoryDistance d = distanceToMark(doc, mark);
        std::string where = !d.valid ? "lost (the checkpoint is outside the retained history)"
                            : d.back == 0 && d.ahead == 0
                                ? "here"
                                : (d.back ? plural(size_t(d.back), "step", "steps") + " back" : "") +
                                      (d.back && d.ahead ? ", " : "") +
                                      (d.ahead ? plural(size_t(d.ahead), "step", "steps") + " ahead" : "");
        Json c = Json::object();
        c.set("name", name);
        c.set("valid", d.valid);
        c.set("steps_back", d.back);
        c.set("steps_ahead", d.ahead);
        list.push(std::move(c));
        text += (text.empty() ? "" : "\n") + name + ": " + where;
    }
    r.text = text.empty() ? "No checkpoints" : text;
    r.structured = Json::object();
    r.structured.set("ok", true);
    r.structured.set("checkpoints", std::move(list));
    return r;
}

} // namespace

bool isHistoryTool(const std::string& name) { return name == "batch" || name.rfind("checkpoint_", 0) == 0; }

Json historyToolList() {
    Json tools = Json::array();
    if (allowed("batch")) {
        Json commandProps = Json::object();
        commandProps.set("name", prop("string", "The tool to run, as listed (not batch, checkpoint_*, undo, redo or the picture tools)."));
        commandProps.set("arguments", prop("object", "That tool's arguments."));
        Json commandRequired = Json::array();
        commandRequired.push("name");
        Json item = Json::object();
        item.set("type", "object");
        item.set("properties", commandProps);
        item.set("required", commandRequired);
        Json commands = prop("array", "The commands to run in order (at most 64): [{name, arguments}].");
        commands.set("items", item);
        Json props = Json::object();
        props.set("commands", commands);
        props.set("stop_on_error", prop("boolean", "Default true: at the first error everything the batch did is rolled back "
                                                    "and the reply says which command failed and why. False: run them all, keep "
                                                    "what worked, list the failures."));
        Json required = Json::array();
        required.push("commands");
        tools.push(tool("batch",
                        "Run several commands as ONE undo step, all or nothing: if one fails, the scene is put back exactly as "
                        "it was before the batch. The reply has each command's result. Each command is checked by the studio "
                        "policy and the audit log as if called alone. Use it for a sequence that only makes sense whole "
                        "(remesh, then subdivide, then a detail pass).",
                        props, required));
    }
    auto nameOnly = [](const char* what) {
        Json props = Json::object();
        props.set("name", prop("string", what));
        Json required = Json::array();
        required.push("name");
        return std::make_pair(props, required);
    };
    if (allowed("checkpoint_save")) {
        auto [props, required] = nameOnly("A name for this moment (1-64 characters; the same name again replaces it).");
        tools.push(tool("checkpoint_save",
                        "Name this moment in the undo history, to come back to with checkpoint_restore. Costs no memory: "
                        "it is a mark on the history, not a copy of the scene (at most 32 are kept).",
                        props, required));
    }
    if (allowed("checkpoint_restore")) {
        auto [props, required] = nameOnly("The checkpoint to go back (or forward) to.");
        tools.push(tool("checkpoint_restore",
                        "Put the scene back to a checkpoint by undoing (or redoing) the steps in between, in one go; undo takes "
                        "the restore back. Steps undone stay redoable until the next edit. Fails, saying why, if the history "
                        "was cleared since or the checkpoint's steps were dropped to save memory.",
                        props, required));
    }
    if (allowed("checkpoint_list"))
        tools.push(tool("checkpoint_list", "List the checkpoints and how far each is from now (steps back / ahead).", Json::object(),
                        Json::array()));
    return tools;
}

HistoryToolReply callHistoryTool(const CommandRegistry& registry, Document& doc, const std::string& name, const Json& arguments) {
    audit::Entry entry;
    entry.tool = "mcp";
    entry.command = name;
    Args summary;
    if (name == "batch") summary["commands"] = double(arguments["commands"].size());
    else if (arguments["name"].isString()) summary["name"] = arguments["name"].asString();
    entry.args = audit::summarizeArgs(summary);
    const auto start = std::chrono::steady_clock::now();
    HistoryToolReply r;
    std::string why;
    if (name != "batch" && name != "checkpoint_save" && name != "checkpoint_restore" && name != "checkpoint_list")
        r = fail("Unknown tool: " + name);
    else if (!allowed(name, &why)) r = fail(why);
    else if (audit::required() && !audit::writable(&why)) r = fail(why);
    else if (name == "batch") r = runBatch(registry, doc, arguments);
    else if (name == "checkpoint_save") r = saveCheckpoint(doc, arguments);
    else if (name == "checkpoint_restore") r = restoreCheckpoint(doc, arguments);
    else r = listCheckpoints(doc);
    entry.ok = r.ok;
    entry.message = r.text.substr(0, r.text.find('\n'));
    entry.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    audit::record(entry);
    return r;
}

} // namespace sf
