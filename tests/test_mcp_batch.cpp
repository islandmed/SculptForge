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
#include "core/SelectionSets.h"
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
    Document d;
    CHECK(!failed(call(reg, d, "new_sphere", R"({"subdivisions":2})")));
    CHECK(d.mesh.triangleCount() == 320);
    const uint64_t atStart = sceneHash(d);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"a"})")));
    CHECK(!failed(call(reg, d, "transform", R"({"translate":"1,0,0"})")));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"b"})")));
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"a"})")));
    CHECK(!failed(call(reg, d, "transform", R"({"translate":"0,0.25,0"})"))); // discards b's redo step
    CHECK(glm::length(d.mesh.bounds().center() - glm::vec3(0, 0.25f, 0)) < 1e-6f);
    const auto positions = d.mesh.positions;
    const uint64_t edited = sceneHash(d), revision = d.undo.revision();
    const uint32_t active = d.activePartInfo().id;
    const size_t undos = d.undo.undoCount(), redos = d.undo.redoCount();
    CHECK(undos == 1 && redos == 0);

    const Json r = call(reg, d, "checkpoint_restore", R"({"name":"b"})");
    CHECK(failed(r) && !text(r).empty());
    CHECK(structured(r)["steps_undone"].asNumber(-1) == 0 && structured(r)["steps_redone"].asNumber(-1) == 0);
    CHECK(d.mesh.positions == positions);
    CHECK(sceneHash(d) == edited);
    CHECK(d.activePartInfo().id == active);
    CHECK(d.undo.undoCount() == undos && d.undo.redoCount() == redos);
    CHECK(d.undo.revision() == revision);
    CHECK(!failed(call(reg, d, "undo")) && sceneHash(d) == atStart);
    CHECK(!failed(call(reg, d, "redo")) && sceneHash(d) == edited);
    // A new scene clears the history: every checkpoint is lost, and says so.
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"c"})")));
    CHECK(!failed(call(reg, d, "new_sphere", R"({"subdivisions":2})")));
    const Json lost = call(reg, d, "checkpoint_restore", R"({"name":"c"})");
    CHECK(failed(lost) && text(lost).find("cleared") != std::string::npos);
    CHECK(call(reg, d, "checkpoint_list")["result"]["content"][0]["text"].asString().find("lost") != std::string::npos);
}

TEST(mcp_checkpoint_rejects_scene_changes_outside_the_history) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"start"})")));
    CHECK(!failed(call(reg, d, "batch", batchOf({{"stroke", kStroke}, {"mask_all", "{}"}}))));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"edited"})")));
    CHECK(!failed(call(reg, d, "part_rename", R"({"to":"Renamed"})"))); // outside the undo history
    d.markClean();
    const uint64_t before = sceneHash(d), revision = d.undo.revision();
    const Json r = call(reg, d, "checkpoint_restore", R"({"name":"start"})");
    CHECK(failed(r) && text(r).find("scene differs") != std::string::npos);
    CHECK(sceneHash(d) == before && d.undo.revision() == revision && !d.dirty());
    CHECK(d.undo.undoCount() == 2 && d.undo.redoCount() == 0);
    CHECK(!failed(call(reg, d, "undo")) && d.undo.undoCount() == 0); // the batch is still one edit
    const uint64_t undone = sceneHash(d), undoRevision = d.undo.revision();
    CHECK(failed(call(reg, d, "checkpoint_restore", R"({"name":"edited"})")));
    CHECK(sceneHash(d) == undone && d.undo.revision() == undoRevision);
    CHECK(d.undo.undoCount() == 0 && d.undo.redoCount() == 2);
    CHECK(!failed(call(reg, d, "redo")) && sceneHash(d) == before);
    CHECK(!failed(call(reg, d, "part_rename", R"({"to":"Sphere"})")));
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"start"})")));
}

TEST(mcp_checkpoint_rejects_a_root_before_the_retained_undo_history) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"root"})")));
    d.undo.byteBudget = 1; // only the newest step is kept
    CHECK(!failed(call(reg, d, "transform", R"({"translate":"1,0,0"})")));
    CHECK(!failed(call(reg, d, "transform", R"({"translate":"0,1,0"})")));
    const uint64_t before = sceneHash(d), revision = d.undo.revision();
    const Json r = call(reg, d, "checkpoint_restore", R"({"name":"root"})");
    CHECK(failed(r));
    CHECK(sceneHash(d) == before && d.undo.revision() == revision);
    CHECK(d.undo.undoCount() == 1 && d.undo.redoCount() == 0);
}

TEST(mcp_checkpoint_root_cannot_cross_an_evicted_selection_set) {
    CommandRegistry reg;
    Document d;
    CHECK(!failed(call(reg, d, "new_sphere", R"({"subdivisions":2})")));
    CHECK(d.mesh.triangleCount() == 320);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"root"})")));
    d.undo.byteBudget = 1;
    CHECK(!failed(call(reg, d, "selection_set_store", R"({"name":"fixture"})")));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"after set"})")));
    CHECK(!failed(call(reg, d, "transform", R"({"translate":"0,0.25,0"})")));
    CHECK(d.undo.droppedTotal() == 1 && d.undo.undoCount() == 1 && d.undo.redoCount() == 0);
    CHECK(glm::length(d.mesh.bounds().center() - glm::vec3(0, 0.25f, 0)) < 1e-6f);
    CHECK(!failed(call(reg, d, "edge_select", R"({"op":"edge","edge":0})")));
    const auto edges = d.edgeSelection.selected;
    d.uvSelection.faces.assign(d.mesh.triangleCount(), 0);
    d.uvSelection.faces[0] = 1;
    d.markClean();
    const auto faces = d.uvSelection.faces;
    const auto positions = d.mesh.positions;
    const auto triangles = d.mesh.triangles;
    const auto sets = d.mesh.selectionSets;
    CHECK(sets && sets->find("fixture") == 0);
    const auto active = d.activePartInfo().id;
    const uint64_t hash = sceneHash(d), revision = d.undo.revision();

    const Json r = call(reg, d, "checkpoint_restore", R"({"name":"root"})");
    CHECK(failed(r));
    CHECK(structured(r)["steps_undone"].asNumber(-1) == 0 && structured(r)["steps_redone"].asNumber(-1) == 0);
    CHECK(d.mesh.positions == positions && d.mesh.triangles == triangles && sceneHash(d) == hash);
    CHECK(d.mesh.selectionSets == sets && d.activePartInfo().id == active && d.uvSelection.faces == faces);
    CHECK(d.edgeSelection.selected == edges && d.uvSelection.valid(d.mesh));
    CHECK(d.undo.undoCount() == 1 && d.undo.redoCount() == 0 && d.undo.revision() == revision && !d.dirty());
    const Json list = structured(call(reg, d, "checkpoint_list"))["checkpoints"];
    CHECK(!list[0]["valid"].asBool() && list[1]["valid"].asBool());

    // The state immediately after the evicted edit is still the start of the retained history.
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"after set"})")));
    CHECK(d.undo.undoCount() == 0 && d.undo.redoCount() == 1 && d.mesh.selectionSets == sets);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"retained base"})")));
    CHECK(!failed(call(reg, d, "redo")) && sceneHash(d) == hash);
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"retained base"})")));
    CHECK(d.undo.undoCount() == 0 && d.undo.redoCount() == 1 && d.mesh.selectionSets == sets);
}

TEST(mcp_checkpoint_validates_named_sets_and_same_count_splat_edits) {
    CommandRegistry reg;
    Document d = sphere(reg);
    d.splats.resize(1, 0);
    d.splatsChanged();
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"root"})")));
    CHECK(!failed(call(reg, d, "selection_set_store", R"({"name":"fixture"})")));
    const auto sets = d.mesh.selectionSets;
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"set"})")));
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"root"})")) && !d.mesh.selectionSets);
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"set"})")) && d.mesh.selectionSets == sets);
    CHECK(!failed(call(reg, d, "transform", R"({"translate":"0,0.25,0"})")));
    const uint64_t edited = sceneHash(d), revision = d.undo.revision();
    d.mesh.selectionSets.reset(); // an out-of-history replacement at an otherwise reachable cursor
    CHECK(failed(call(reg, d, "checkpoint_restore", R"({"name":"set"})")));
    CHECK(!d.mesh.selectionSets && sceneHash(d) == edited && d.undo.revision() == revision);
    d.mesh.selectionSets = sets;
    CHECK(!failed(call(reg, d, "splat_transform", R"({"rotate_x":45})")));
    const auto placement = d.splats.transform;
    CHECK(failed(call(reg, d, "checkpoint_restore", R"({"name":"set"})")));
    CHECK(d.splats.transform == placement && sceneHash(d) == edited && d.undo.revision() == revision);
    CHECK(!failed(call(reg, d, "splat_transform", R"({"reset":true})")));
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"set"})")) && d.mesh.selectionSets == sets);
}

TEST(mcp_checkpoint_restores_subdivision_and_detects_edits_outside_mesh_history) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"plain"})")));
    const auto plain = d.mesh.positions;
    CHECK(!failed(call(reg, d, "subdiv_divide")));
    CHECK(!failed(call(reg, d, "transform", R"({"translate":"0,0.25,0"})")));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"divided"})")));
    const auto divided = d.mesh.positions;
    const uint64_t revision = d.undo.revision();
    const auto stack = d.mesh.multires;
    CHECK(stack && divided.size() > plain.size());
    const uint32_t folder = d.addPartFolder("Fixture");
    CHECK(failed(call(reg, d, "checkpoint_restore", R"({"name":"plain"})")));
    CHECK(d.partFolderIndex(folder) >= 0 && d.undo.revision() == revision);
    CHECK(d.mesh.multires == stack && d.mesh.positions == divided && d.undo.redoCount() == 0);
    d.removePartFolder(folder);
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"plain"})")));
    CHECK(!d.mesh.multires && d.mesh.positions == plain);
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"divided"})")));
    CHECK(d.mesh.multires && d.mesh.positions == divided);
}

TEST(mcp_checkpoint_restores_uv_seams_and_paint_layers) {
    CommandRegistry reg;
    Document d = sphere(reg);
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"plain"})")));
    CHECK(!failed(call(reg, d, "uv_seam", R"({"op":"path","from":0,"to":17})")));
    const auto flags = d.mesh.uvFlags;
    CHECK(!flags.empty());
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"seams"})")));
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"plain"})")) && d.mesh.uvFlags.empty());
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"seams"})")) && d.mesh.uvFlags == flags);
    CHECK(!failed(call(reg, d, "paint_layer_add", R"({"name":"Fixture"})")));
    CHECK(!failed(call(reg, d, "checkpoint_save", R"({"name":"paint"})")));
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"plain"})")) && d.mesh.paintLayers.empty());
    CHECK(!failed(call(reg, d, "checkpoint_restore", R"({"name":"paint"})")));
    CHECK(d.mesh.uvFlags == flags && d.mesh.paintLayers.size() == 1 && d.mesh.paintLayers[0].name == "Fixture");
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
