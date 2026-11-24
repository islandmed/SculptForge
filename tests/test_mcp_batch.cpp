// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transactions for AI agents (cli/McpBatch.h): `batch` is one undo step and rolls back whole when a command fails
// (the scene is byte-identical afterwards: sceneHash), and checkpoints walk the undo history, across Parts too.
#include "TestHarness.h"

#include "cli/McpBatch.h"
#include "cli/McpServer.h"
#include "core/Commands.h"
#include "core/Json.h"
#include "core/ManagedPolicy.h"
#include "core/UndoBatch.h"

#include <string>

using namespace sf;

namespace {

Json call(const CommandRegistry& reg, Document& d, const std::string& tool, const std::string& arguments = "{}") {
    static int id = 100;
    Json j;
    std::string err;
    const std::string line = R"({"jsonrpc":"2.0","id":)" + std::to_string(++id) + R"(,"method":"tools/call","params":{"name":")" +
                             tool + R"(","arguments":)" + arguments + "}}";
    CHECK(Json::parse(handleMcpMessage(reg, d, line), j, err));
    return j;
}

bool failed(const Json& r) { return r["result"]["isError"].asBool(false); }
const Json& structured(const Json& r) { return r["result"]["structuredContent"]; }
std::string text(const Json& r) { return r["result"]["content"][0]["text"].asString(); }

const char* kStroke = R"({"points":"-0.3,0,2 0.3,0,2","radius":0.4,"strength":0.8})";
const char* kBadStroke = R"({"points":"nonsense"})";

Document sphere(const CommandRegistry& reg) {
    Document d;
    call(reg, d, "new_sphere", R"({"subdivisions":4})");
    return d;
}

std::string batchOf(std::initializer_list<std::pair<const char*, const char*>> commands, const char* extra = "") {
    std::string s = R"({"commands":[)";
    bool first = true;
    for (const auto& [name, args] : commands) {
        s += std::string(first ? "" : ",") + R"({"name":")" + name + R"(","arguments":)" + args + "}";
        first = false;
    }
    return s + "]" + extra + "}";
}

} // namespace

TEST(mcp_batch_is_one_undo_step) {
    CommandRegistry reg;
    Document d = sphere(reg);
    const uint64_t start = sceneHash(d);
    const Json r = call(reg, d, "batch",
                        batchOf({{"stroke", kStroke}, {"mask_all", "{}"}, {"subdivide", "{}"}, {"stroke", kStroke}}));
    CHECK(!failed(r));
    CHECK(structured(r)["ok"].asBool() && structured(r)["steps"].size() == 4 && !structured(r)["rolled_back"].asBool(true));
    const uint64_t done = sceneHash(d);
    CHECK(done != start);
    CHECK(!failed(call(reg, d, "undo")));
    CHECK(sceneHash(d) == start); // all four back with one undo
    CHECK(!failed(call(reg, d, "redo")));
    CHECK(sceneHash(d) == done); // and forward with one redo
    CHECK(failed(call(reg, d, "redo")));
}

TEST(mcp_batch_rolls_back_byte_identical) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "stroke", kStroke))); // some history of its own first
    const uint64_t start = sceneHash(d);
    const size_t depth = d.undo.undoCount();
    const size_t tris = d.mesh.triangleCount();
    const Json r = call(reg, d, "batch",
                        batchOf({{"subdivide", "{}"}, {"mask_all", "{}"}, {"stroke", kStroke}, {"stroke", kBadStroke}, {"subdivide", "{}"}}));
    CHECK(failed(r));
    CHECK(structured(r)["rolled_back"].asBool() && structured(r)["verified"].asBool());
    CHECK(structured(r)["failed_index"].asNumber() == 4);
    CHECK(structured(r)["executed"].asNumber() == 4 && structured(r)["steps"].size() == 4); // the fifth never ran
    CHECK(!structured(r)["steps"][3]["ok"].asBool(true) && structured(r)["steps"][2]["ok"].asBool());
    CHECK(text(r).find("command 4 of 5") != std::string::npos && text(r).find("exactly as before") != std::string::npos);
    CHECK(sceneHash(d) == start);
    CHECK(d.mesh.triangleCount() == tris);
    CHECK(d.undo.undoCount() == depth); // the earlier history is untouched
    CHECK(!failed(call(reg, d, "undo")) && sceneHash(d) != start); // the stroke before the batch is still one step
}

TEST(mcp_batch_rolls_back_part_changes) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "mask_all")));
    const uint64_t start = sceneHash(d);
    CHECK(d.partCount() == 1);
    // A copy of the masked Part is added (undoable), the new one is made active and edited, then a command fails.
    const Json r = call(reg, d, "batch",
                        batchOf({{"part_split_masked", R"({"keep":true})"}, {"part_select", R"({"index":1})"}, {"stroke", kStroke},
                                 {"part_select", R"({"index":0})"}, {"subdivide", "{}"}, {"stroke", kBadStroke}}));
    CHECK(failed(r) && structured(r)["verified"].asBool());
    CHECK(d.partCount() == 1);
    CHECK(d.activePart == 0);
    CHECK(sceneHash(d) == start);
    // Without a failure the same batch is one undo step across both Parts.
    const Json ok = call(reg, d, "batch",
                         batchOf({{"part_split_masked", R"({"keep":true})"}, {"part_select", R"({"index":1})"}, {"stroke", kStroke},
                                  {"part_select", R"({"index":0})"}, {"subdivide", "{}"}}));
    CHECK(!failed(ok));
    CHECK(d.partCount() == 2);
    const uint64_t after = sceneHash(d);
    CHECK(!failed(call(reg, d, "undo")));
    CHECK(d.partCount() == 1 && sceneHash(d) == start);
    CHECK(!failed(call(reg, d, "redo")));
    CHECK(d.partCount() == 2 && sceneHash(d) == after);
}

TEST(mcp_batch_stop_on_error_false_keeps_the_rest) {
    CommandRegistry reg;
    Document d = sphere(reg);
    const uint64_t start = sceneHash(d);
    const Json r = call(reg, d, "batch", batchOf({{"stroke", kStroke}, {"stroke", kBadStroke}, {"mask_all", "{}"}}, R"(,"stop_on_error":false)"));
    CHECK(failed(r) && !structured(r)["rolled_back"].asBool(true));
    CHECK(structured(r)["executed"].asNumber() == 3 && structured(r)["failed_index"].asNumber() == 2);
    CHECK(sceneHash(d) != start);
    CHECK(!failed(call(reg, d, "undo")) && sceneHash(d) == start); // still one undo step
}

TEST(mcp_batch_refuses_before_running_anything) {
    CommandRegistry reg;
    Document d = sphere(reg);
    const uint64_t start = sceneHash(d);
    const size_t depth = d.undo.undoCount();
    for (const std::string& bad : {
             batchOf({{"stroke", kStroke}, {"batch", R"({"commands":[]})"}}),         // no nesting
             batchOf({{"stroke", kStroke}, {"undo", "{}"}}),                         // undo inside would undo the user's steps
             batchOf({{"stroke", kStroke}, {"checkpoint_save", R"({"name":"a"})"}}), //
             batchOf({{"stroke", kStroke}, {"viewport_image", "{}"}}),               // pictures are not steps
             batchOf({{"stroke", kStroke}, {"no_such_tool", "{}"}}),
             batchOf({{"stroke", kStroke}, {"pack_install", "{}"}}), // hidden from agents
             std::string(R"({"commands":[]})"),
             std::string(R"({})")}) {
        const Json r = call(reg, d, "batch", bad);
        CHECK(failed(r));
        CHECK(sceneHash(d) == start && d.undo.undoCount() == depth);
    }
    std::string many = R"({"commands":[)";
    for (size_t i = 0; i < kMaxBatchCommands + 1; ++i) many += std::string(i ? "," : "") + R"({"name":"mask_all"})";
    const Json r = call(reg, d, "batch", many + "]}");
    CHECK(failed(r) && text(r).find("at most 64") != std::string::npos);
    CHECK(sceneHash(d) == start && d.undo.undoCount() == depth);
}

TEST(mcp_batch_commands_pass_the_studio_policy_one_by_one) {
    CommandRegistry reg;
    Document d = sphere(reg);
    const uint64_t start = sceneHash(d);
    policy::Policy p;
    std::string error;
    CHECK(policy::parsePolicy(R"({"format": "sculptforge-policy", "version": 1, "mcp": {"categories": ["edit"]}})", "test", p, error));
    policy::setPolicyForTests(p);
    // mask_all is an edit and runs; mesh_info is "info": the policy refuses it exactly as if called alone, so the batch rolls back.
    const Json r = call(reg, d, "batch", batchOf({{"mask_all", "{}"}, {"mesh_info", "{}"}}));
    CHECK(failed(r) && structured(r)["failed_index"].asNumber() == 2);
    CHECK(text(r).find("policy") != std::string::npos);
    CHECK(sceneHash(d) == start);
    CHECK(!failed(call(reg, d, "batch", batchOf({{"mask_all", "{}"}}))));
    policy::resetPolicyForTests();
}

TEST(mcp_checkpoints_walk_the_history_across_parts) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "mask_all")));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"start"})")));
    const uint64_t atStart = sceneHash(d);
    CHECK(!failed(call(reg, d, "stroke", kStroke)));
    CHECK(!failed(call(reg, d, "part_split_masked", R"({"keep":true})"))); // a Part is added
    CHECK(d.partCount() == 2);
    CHECK(!failed(call(reg, d, "part_select", R"({"index":1})")));
    const uint64_t beforePartStroke = sceneHash(d);
    CHECK(!failed(call(reg, d, "stroke", kStroke))); // on the new Part
    CHECK(sceneHash(d) != beforePartStroke);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"two parts"})")));
    const uint64_t atTwo = sceneHash(d);
    CHECK(!failed(call(reg, d, "part_select", R"({"index":0})")));
    CHECK(!failed(call(reg, d, "subdivide")));
    const uint64_t atEnd = sceneHash(d);

    const Json list = call(reg, d, "checkpoint_list");
    CHECK(structured(list)["checkpoints"].size() == 2);
    CHECK(structured(list)["checkpoints"][0]["steps_back"].asNumber() > 0);

    // Back to the start: the added Part is gone again.
    const Json back = call(reg, d, "checkpoint_restore", R"({"name":"start"})");
    if (failed(back) || structured(back)["steps_undone"].asNumber() < 3) std::printf("    %s\n", text(back).c_str());
    CHECK(!failed(back) && structured(back)["steps_undone"].asNumber() >= 3);
    CHECK(d.partCount() == 1 && sceneHash(d) == atStart);
    // Forward again to the later one (nothing was edited in between): the Part comes back, with its stroke.
    const Json fwd = call(reg, d, "checkpoint_restore", R"({"name":"two parts"})");
    CHECK(!failed(fwd) && structured(fwd)["steps_redone"].asNumber() >= 2);
    CHECK(d.partCount() == 2 && sceneHash(d) == atTwo);
    // The whole restore is one undo step, and one redo.
    CHECK(!failed(call(reg, d, "undo")));
    CHECK(d.partCount() == 1 && sceneHash(d) == atStart);
    CHECK(!failed(call(reg, d, "redo")));
    CHECK(d.partCount() == 2 && sceneHash(d) == atTwo);
    // The last edit was undone by the restores, so it is gone for good only after a new edit.
    CHECK(!failed(call(reg, d, "redo")));
    CHECK(sceneHash(d) == atEnd);
    CHECK(text(call(reg, d, "checkpoint_restore", R"({"name":"two parts"})")).find("Restored") == 0);
    CHECK(failed(call(reg, d, "checkpoint_restore", R"({"name":"nope"})")));
    CHECK(text(call(reg, d, "checkpoint_restore", R"({"name":"two parts"})")).find("Already") == 0);
}

TEST(mcp_checkpoint_after_a_new_edit_says_what_was_lost) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"a"})")));
    CHECK(!failed(call(reg, d, "stroke", kStroke)));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"b"})")));
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"a"})")));
    CHECK(!failed(call(reg, d, "mask_all"))); // a new edit: "b"'s step is gone
    const Json r = call(reg, d, "checkpoint_restore", R"({"name":"b"})");
    CHECK(failed(r) && !text(r).empty());
    // A new scene clears the history: every checkpoint is lost, and says so.
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"c"})")));
    CHECK(!failed(call(reg, d, "new_sphere", R"({"subdivisions":2})")));
    const Json lost = call(reg, d, "checkpoint_restore", R"({"name":"c"})");
    CHECK(failed(lost) && text(lost).find("cleared") != std::string::npos);
    CHECK(call(reg, d, "checkpoint_list")["result"]["content"][0]["text"].asString().find("lost") != std::string::npos);
}

TEST(mcp_checkpoints_are_capped_and_named) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(failed(call(reg, d, "checkpoint_save", R"({"name":""})")));
    CHECK(failed(call(reg, d, "checkpoint_save", "{}")));
    for (size_t i = 0; i < kMaxCheckpoints + 3; ++i)
        CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"c)" + std::to_string(i) + R"("})")));
    CHECK(d.checkpoints.size() == kMaxCheckpoints);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"c5"})"))); // the same name replaces
    CHECK(d.checkpoints.size() == kMaxCheckpoints && d.checkpoints.back().first == "c5");
}

TEST(mcp_history_tools_are_listed) {
    CommandRegistry reg;
    Document d;
    Json list;
    std::string err;
    CHECK(Json::parse(handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"), list, err));
    int found = 0;
    for (const Json& t : list["result"]["tools"].items()) {
        const std::string& n = t["name"].asString();
        if (n == "batch" || n == "checkpoint_save" || n == "checkpoint_restore" || n == "checkpoint_list") ++found;
    }
    CHECK(found == 4);
}

namespace {
// What a failed restore must leave alone: the scene, the history's depth both ways, and which Part is active.
struct Snapshot {
    uint64_t hash;
    size_t undoDepth, redoDepth;
    int active, parts;
    bool operator==(const Snapshot&) const = default;
};
Snapshot snapshotOf(const Document& d) {
    return {sceneHash(d), d.undo.undoCount(), d.undo.redoCount(), d.activePart, d.partCount()};
}
} // namespace

TEST(mcp_failed_checkpoint_restore_changes_nothing) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "stroke", kStroke)));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"old"})")));
    CHECK(!failed(call(reg, d, "stroke", kStroke)));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"now"})")));
    // The memory budget drops every step but the newest: "old" is no longer in the undo history.
    d.undo.byteBudget = 1;
    CHECK(!failed(call(reg, d, "stroke", kStroke)));
    CHECK(d.undo.undoCount() == 1);
    const Snapshot before = snapshotOf(d);
    const uint64_t revision = d.undo.revision();

    for (int attempt = 0; attempt < 3; ++attempt) { // repeated failures don't drift
        const Json r = call(reg, d, "checkpoint_restore", R"({"name":"old"})");
        CHECK(failed(r));
        CHECK(text(r).find("checkpoint no longer in undo history") != std::string::npos);
        CHECK(text(r).find("not changed") != std::string::npos);
        CHECK(structured(r)["scene_unchanged"].asBool(false));
        CHECK(snapshotOf(d) == before);
        CHECK(d.undo.revision() == revision); // nothing was undone or redone, not even to be put back
    }
    // The newest step is still ordinary: one undo takes it back.
    CHECK(!failed(call(reg, d, "undo")));
    CHECK(sceneHash(d) != before.hash);
}

TEST(mcp_checkpoint_restore_that_ends_off_the_mark_puts_the_scene_back) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"start"})"))); // before any step: the walk goes to the bottom
    d.undo.byteBudget = 1;
    CHECK(!failed(call(reg, d, "stroke", kStroke)));
    CHECK(!failed(call(reg, d, "stroke", kStroke))); // the first stroke was dropped: the bottom is not "start"
    CHECK(d.undo.undoCount() == 1);
    const Snapshot before = snapshotOf(d);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const Json r = call(reg, d, "checkpoint_restore", R"({"name":"start"})");
        CHECK(failed(r));
        CHECK(text(r).find("checkpoint no longer in undo history") != std::string::npos);
        CHECK(snapshotOf(d) == before);
    }
    CHECK(!failed(call(reg, d, "undo"))); // and the history still works
}

TEST(mcp_checkpoint_replaced_by_a_new_edit_leaves_the_scene_alone) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"a"})")));
    CHECK(!failed(call(reg, d, "stroke", kStroke)));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"b"})")));
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"a"})")));
    CHECK(!failed(call(reg, d, "mask_all"))); // "b"'s step is gone for good
    const Snapshot before = snapshotOf(d);
    const Json r = call(reg, d, "checkpoint_restore", R"({"name":"b"})");
    CHECK(failed(r) && text(r).find("checkpoint no longer in undo history") != std::string::npos);
    CHECK(snapshotOf(d) == before);
    // A cleared history (a new scene) is the same: the error, and the new scene untouched.
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"c"})")));
    CHECK(!failed(call(reg, d, "new_sphere", R"({"subdivisions":2})")));
    CHECK(!failed(call(reg, d, "stroke", kStroke)));
    const Snapshot fresh = snapshotOf(d);
    const Json lost = call(reg, d, "checkpoint_restore", R"({"name":"c"})");
    CHECK(failed(lost) && text(lost).find("cleared") != std::string::npos);
    CHECK(snapshotOf(d) == fresh);
    // A valid restore still works afterwards, and keeps batch grouping of earlier steps intact.
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"d"})")));
    CHECK(!failed(call(reg, d, "stroke", kStroke)));
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"d"})")));
    CHECK(sceneHash(d) == fresh.hash);
}
