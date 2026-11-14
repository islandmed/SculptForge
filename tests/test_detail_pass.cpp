// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fine detail for AI agents (core/DetailPass.h): detail_pass keeps its promises on the dense sphere
// (tests/GeomInvariants.h): pores and wrinkles only carve, scales build with their rims cut in, nothing moves outside the region,
// no NaN or flipped triangles, the X mirror holds, one undo step, the same seed the same surface; a coarse mesh is
// refused with the subdivide that would do. Pores are an even field of round pits (no spikes in their rings), scars all
// land without folding a triangle (artist-qa, 11-12). The detail presets work as stroke brushes without touching the user's
// alpha, and view_closeup (cli/McpTools.h) frames a spot along its normal and draws the same picture every time.
#include "GeomInvariants.h"

#include "cli/McpServer.h"
#include "cli/McpTools.h"
#include "core/Commands.h"
#include "core/DetailPass.h"

#include <cstring>

using namespace sf;
using namespace sf::geom;

namespace {

uint64_t hashPositions(const Mesh& m) {
    uint64_t h = 1469598103934665603ull;
    for (const glm::vec3& p : m.positions) {
        uint32_t bits[3];
        std::memcpy(bits, &p, sizeof bits);
        for (const uint32_t b : bits) h = (h ^ b) * 1099511628211ull;
    }
    return h;
}

struct Pass {
    Document doc = denseSphere();
    std::vector<glm::vec3> before = doc.mesh.positions, normals = doc.mesh.normals;
    CommandResult result;
};

Pass run(const Args& args) {
    Pass p;
    CommandRegistry reg;
    p.result = reg.execute(p.doc, "detail_pass", args);
    return p;
}

} // namespace

TEST(detail_pass_pores_carve_only_inside_the_region_without_spikes) {
    // Pores as large as the 1.3M sphere holds (its edges are ~0.0047; a pore needs 2.5 edges across its radius).
    Document d = denseSphere();
    Pass p;
    DetailPassOptions o;
    o.kind = "pores";
    o.whole = false;
    o.center = glm::vec3(0, 0, 1);
    o.radius = 0.4f;
    o.seed = 3;
    o.size = 0.006f;
    std::vector<glm::vec3> placed;
    std::string err;
    p.result.ok = detailPass(p.doc, o, p.result.message, err, &placed) > 0;
    std::printf("  %s%s\n", p.result.message.c_str(), err.c_str());
    CHECK(p.result.ok);
    const float edge = meanEdge(p.doc.mesh);
    const float rise = maxRise(p.before, p.doc.mesh, p.normals), sink = maxSink(p.before, p.doc.mesh, p.normals);
    const float r = 1.25f * 0.006f * glm::length(p.doc.mesh.bounds().extent()); // (the biggest pore)
    const EdgeReport e = edgeOutliers(p.doc.mesh, p.before, placed, r, edge);
    std::printf("  ring: %d spikes, worst %.2f edges over %d vertices; %zu pores\n", e.spikes, e.worst, e.ring, placed.size());
    CHECK(e.spikes == 0);
    CHECK(placed.size() > 100); // an even field, not a few stamps
    std::printf("  rise %.2g sink %.2g (edge %.2g), outside %.2g\n", rise, sink, edge, maxMoveOutside(p.before, p.doc.mesh, {{0, 0, 1}}, 0.4f + 2.0f * r));
    CHECK(sink > 0.05f * edge);        // pores went in
    CHECK(rise < 0.02f * edge);        // and nothing came out
    CHECK(maxMoveOutside(p.before, p.doc.mesh, {{0, 0, 1}}, 0.4f + 2.0f * r) == 0.0f);
    CHECK(nonFinite(p.doc.mesh) == 0 && flippedFaces(p.before, p.doc.mesh) == 0);
    // Even: no two pores closer than 1.4 radii (density 0.5: 2 radii).
    float closest = 1e30f;
    for (size_t i = 0; i < placed.size(); ++i)
        for (size_t j = i + 1; j < placed.size(); ++j) closest = std::min(closest, glm::distance(placed[i], placed[j]));
    CHECK(closest > 1.4f * 0.006f * glm::length(p.doc.mesh.bounds().extent()));
}

TEST(detail_pass_scars_all_land_without_folds) {
    const Pass s = run({{"kind", std::string("scars")}, {"center", std::string("0,-0.3,0.95")}, {"radius", 0.35}, {"density", 1.0}});
    std::printf("  %s\n", s.result.message.c_str());
    CHECK(s.result.ok && s.result.message.find("5 strokes") != std::string::npos);
    CHECK(maxSink(s.before, s.doc.mesh, s.normals) > 0.3f * meanEdge(s.doc.mesh));
    CHECK(foldedFaces(s.before, s.doc.mesh) == 0 && invertedFaces(s.before, s.doc.mesh) == 0);
}

TEST(detail_pass_wrinkles_carve_and_scales_build_with_rims) {
    const Pass w = run({{"kind", std::string("wrinkles")}, {"center", std::string("0,0.3,0.95")}, {"radius", 0.3}});
    CHECK(w.result.ok);
    const float edge = meanEdge(w.doc.mesh);
    std::printf("  wrinkles: rise %.2g sink %.2g; %s\n", maxRise(w.before, w.doc.mesh, w.normals),
                maxSink(w.before, w.doc.mesh, w.normals), w.result.message.c_str());
    CHECK(maxSink(w.before, w.doc.mesh, w.normals) > 0.05f * edge);
    CHECK(invertedFaces(w.before, w.doc.mesh) == 0 && nonFinite(w.doc.mesh) == 0);

    const Pass s = run({{"kind", std::string("scales")}, {"center", std::string("1,0,0")}, {"radius", 0.4}, {"size", 0.02}});
    CHECK(s.result.ok);
    std::printf("  scales: rise %.2g sink %.2g\n", maxRise(s.before, s.doc.mesh, s.normals), maxSink(s.before, s.doc.mesh, s.normals));
    CHECK(maxRise(s.before, s.doc.mesh, s.normals) > 0.05f * edge);           // raised plates
    CHECK(maxSink(s.before, s.doc.mesh, s.normals) > 0.02f * edge);           // with their rims cut in
    CHECK(flippedFaces(s.before, s.doc.mesh) == 0 && foldedFaces(s.before, s.doc.mesh) == 0);
}

TEST(detail_pass_is_deterministic_one_undo_step_and_mirrors) {
    const Args args{{"kind", std::string("pores")}, {"center", std::string("0.4,0.2,0.9")}, {"radius", 0.3}, {"seed", 7.0},
                    {"size", 0.006}, {"symmetry", std::string("x")}};
    Pass a = run(args);
    const Pass b = run(args);
    CHECK(a.result.ok && b.result.ok);
    CHECK(hashPositions(a.doc.mesh) == hashPositions(b.doc.mesh)); // the same seed, the same surface
    Args other = args;
    other["seed"] = 8.0;
    CHECK(hashPositions(run(other).doc.mesh) != hashPositions(a.doc.mesh));
    // The mirror holds on the sphere (mirror-symmetric in X).
    static const std::vector<uint32_t> map = mirrorMapX(denseSphereFixture().mesh);
    const float sym = symmetryError(a.doc.mesh, map);
    std::printf("  mirror error %.2g (edge %.2g)\n", sym, meanEdge(a.doc.mesh));
    CHECK(sym < 0.01f * meanEdge(a.doc.mesh));
    // One undo step takes all of it back.
    CommandRegistry reg;
    CHECK(reg.execute(a.doc, "undo").ok);
    CHECK(hashPositions(a.doc.mesh) == hashPositions(denseSphereFixture().mesh));
}

TEST(detail_pass_refuses_a_coarse_mesh_with_the_subdivide_that_would_do) {
    CommandRegistry reg;
    Document d;
    CHECK(reg.execute(d, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const std::vector<glm::vec3> before = d.mesh.positions;
    const size_t steps = d.undo.undoCount();
    const CommandResult r = reg.execute(d, "detail_pass", {{"kind", std::string("scales")}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(!r.ok && r.message.find("subdivide levels=") != std::string::npos);
    CHECK(d.mesh.positions == before && d.undo.undoCount() == steps); // nothing done, nothing to undo
    // Following the advice works.
    const size_t at = r.message.find("levels=") + 7;
    const int levels = std::atoi(r.message.c_str() + at);
    CHECK(levels >= 1 && levels <= 4);
    CHECK(reg.execute(d, "subdivide", {{"levels", double(levels)}}).ok);
    CHECK(reg.execute(d, "detail_pass", {{"kind", std::string("scales")}, {"density", 0.2}}).ok);
    CHECK(!reg.execute(d, "detail_pass", {{"kind", std::string("feathers")}}).ok);
    CHECK(!reg.execute(d, "detail_pass", {{"kind", std::string("pores")}, {"center", std::string("9,9,9")}, {"radius", 0.1}}).ok);
}

TEST(detail_presets_stroke_without_touching_the_users_alpha) {
    CHECK(findDetailPreset("detail_crease") && findDetailPreset("Detail Pores") && !findDetailPreset("Clay"));
    Document d = denseSphere();
    CommandRegistry reg;
    CHECK(reg.execute(d, "brush_set_alpha", {{"name", std::string("cracks")}}).ok);
    const std::vector<glm::vec3> before = d.mesh.positions;
    const CommandResult r = reg.execute(d, "stroke", {{"points", std::string("-0.3,0.1,1 0,0.15,1 0.3,0.1,1")},
                                                      {"brush", std::string("Detail Crease")}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok && r.message.find("Detail Crease") != std::string::npos);
    const float sink = maxSink(before, d.mesh, denseSphereFixture().mesh.normals), rise = maxRise(before, d.mesh, denseSphereFixture().mesh.normals);
    std::printf("  crease: sink %.2g rise %.2g\n", sink, rise);
    CHECK(sink > 2.0f * rise); // a crease is cut in
    CHECK(d.brushAlpha.name == "Cracks");
    // The preset's own size: a crease, not a 4% blob.
    const float diag = glm::length(d.mesh.bounds().extent());
    CHECK(maxMoveOutside(before, d.mesh, {{-0.3f, 0.1f, 1}, {0, 0.15f, 1}, {0.3f, 0.1f, 1}, {-0.15f, 0.13f, 1}, {0.15f, 0.13f, 1}},
                         0.15f + 0.01f * diag) == 0.0f);
}

TEST(view_closeup_frames_a_spot_along_its_normal_and_draws_the_same_picture) {
    Document d = denseSphere();
    Closeup c;
    std::string err;
    CHECK(closeupFraming(d, glm::vec3(0.0f, 0.0f, 1.3f), 0.2f, glm::vec3(0.0f), c, err));
    CHECK(glm::distance(c.point, glm::vec3(0, 0, 1)) < 0.01f && c.eye.z > 0.99f && c.triangles > 1000 && c.edge > 0.0f);
    CHECK(closeupText(c, 0.2f).find("triangles in view") != std::string::npos);

    Json args;
    std::string perr;
    CHECK(Json::parse(R"({"point":"0,0,1","span":0.2,"size":256})", args, perr));
    Json first, second;
    CHECK(renderCloseup(d, args, first, err) && renderCloseup(d, args, second, err));
    CHECK(first[0]["type"].asString() == "image" && first[0]["data"].asString() == second[0]["data"].asString());
    CHECK(first[1]["text"].asString().find("Close-up of") != std::string::npos);
    CHECK(Json::parse(R"({"point":"nowhere"})", args, perr) && !renderCloseup(d, args, first, err));
    CHECK(Json::parse(R"({"point":"0,0,1","span":0})", args, perr) && !renderCloseup(d, args, first, err));

    // The tool's schema, and both in the headless server's list.
    const Json tool = viewCloseupTool();
    CHECK(tool["name"].asString() == "view_closeup" && tool["inputSchema"]["required"][0].asString() == "point");
    CommandRegistry reg;
    const std::string list = handleMcpMessage(reg, d, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
    CHECK(list.find("\"view_closeup\"") != std::string::npos && list.find("\"detail_pass\"") != std::string::npos);
    const std::string init = handleMcpMessage(
        reg, d, R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{}}})");
    CHECK(init.find("3-6M triangles") != std::string::npos && init.find("view_closeup") != std::string::npos);
    const std::string call = handleMcpMessage(
        reg, d, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"view_closeup","arguments":{"point":"0,0,1","size":256}}})");
    CHECK(call.find("image/png") != std::string::npos && call.find("\"isError\":false") != std::string::npos);
}
