// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generate 3D makes its result sculptable with Quad Remesh by default (core/GeneratedRemesh.h): a voxel cleanup
// pass (AI meshes have holes and overlapping, non-manifold pieces Quad Remesh can't take), Quad Remesh, one Divide
// projected back onto the surface. Voxel and Off stay as options; the CLI takes remesh=quad|voxel|false.
#include "TestHarness.h"

#include "ai/AiCommands.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/GeneratedRemesh.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/QRemesh.h"
#include "core/SubTools.h"

#include <glm/glm.hpp>

using namespace sf;

namespace {

// What image-to-3D services hand back: overlapping pieces, a hole, and a non-manifold fin.
Mesh aiLikeMesh() {
    Mesh a = makeIcosphere(4, 1.0f);
    Mesh b = makeIcosphere(3, 0.6f);
    for (auto& p : b.positions) p += glm::vec3(0.0f, 0.9f, 0.0f);
    Mesh m = mergeMeshes({&a, &b});
    m.triangles.erase(m.triangles.begin(), m.triangles.begin() + 12); // a hole
    const uint32_t base = uint32_t(m.positions.size());
    m.positions.push_back(m.positions[m.triangles[0].x]); // a fin hanging off an edge: three faces share it
    m.triangles.push_back({m.triangles[0].x, m.triangles[0].y, base});
    m.positions.back() += glm::vec3(0.3f, 0.3f, 0.3f);
    m.sanitizeAttributes();
    m.recomputeAllNormals();
    return m;
}

} // namespace

TEST(generated_remesh_modes_and_target) {
    GeneratedRemesh mode;
    CHECK(generatedRemeshFromName("quad", mode) && mode == GeneratedRemesh::Quad);
    CHECK(generatedRemeshFromName("true", mode) && mode == GeneratedRemesh::Quad);
    CHECK(generatedRemeshFromName("voxel", mode) && mode == GeneratedRemesh::Voxel);
    CHECK(generatedRemeshFromName("false", mode) && mode == GeneratedRemesh::Off);
    CHECK(!generatedRemeshFromName("dyna", mode));
    // The default Resolution (256) asks for 20-40k quads; the cleanup pass Quad Remesh reads is coarser.
    CHECK(generatedQuadTarget(256) == 32768);
    CHECK(generatedQuadTarget(96) >= 2000 && generatedQuadTarget(400) <= 40000);
    CHECK(generatedCleanupResolution(256) == 179 && generatedCleanupResolution(400) == 200);
}

TEST(generated_remesh_quad_cleans_an_ai_mesh) {
    const Mesh raw = aiLikeMesh();
    CHECK(!topologyReport(raw).manifold() || topologyReport(raw).boundaryEdges > 0);

    Mesh out;
    std::string err;
    GeneratedRemeshStats st;
    CHECK(remeshGenerated(raw, GeneratedRemesh::Quad, 96, 1, out, err, &st));
    const TopologyReport topo = topologyReport(out);
    CHECK(topo.manifold() && topo.boundaryEdges == 0);
    CHECK(topo.quadFraction() > 0.95f);
    CHECK(st.levels == 1 && subdivLevelCount(out) == 2 && subdivCurrentLevel(out) == 1); // the quad cage below
    CHECK(st.quads == topo.faces && st.quads > 3 * size_t(generatedQuadTarget(96)));
    CHECK(generatedRemeshNote(st, GeneratedRemesh::Quad).find("remeshed to " + std::to_string(st.quads) + " quads") == 0);
    // Projected back: the top level sits on the big sphere where nothing else is (its equator, radius 1).
    float maxDev = 0.0f;
    for (const auto& p : out.positions)
        if (std::abs(p.y) < 0.2f && p.x > 0.0f) maxDev = std::max(maxDev, std::abs(glm::length(p) - 1.0f));
    CHECK(maxDev < 0.04f);

    CHECK(remeshGenerated(raw, GeneratedRemesh::Voxel, 96, 1, out, err, &st));
    CHECK(!out.multires && st.levels == 0 && topologyReport(out).boundaryEdges == 0); // watertight
    CHECK(generatedRemeshNote(st, GeneratedRemesh::Voxel) == "remeshed to " + std::to_string(out.triangleCount()) + " triangles");
    CHECK(remeshGenerated(raw, GeneratedRemesh::Off, 96, 1, out, err, &st));
    CHECK(out.triangleCount() == raw.triangleCount() && generatedRemeshNote(st, GeneratedRemesh::Off).empty());

    // Cancel from the progress callback.
    CHECK(!remeshGenerated(raw, GeneratedRemesh::Quad, 96, 1, out, err, nullptr, [](float) { return false; }));
}

TEST(generated_remesh_cli_argument) {
    // remesh= is checked before anything is sent: a bad value fails at once, with no network.
    CommandRegistry reg;
    ai::registerAiCommands(reg, [] { return ai::AiSettings{}; });
    Document doc;
    const CommandResult r = reg.execute(doc, "image_to_3d", {{"path", std::string("missing.png")}, {"remesh", std::string("dyna")}});
    CHECK(!r.ok && r.message.find("quad, voxel or false") != std::string::npos);
    // An offline service isn't swapped for a hosted one behind the user's back.
    const CommandResult t = reg.execute(doc, "text_to_3d", {{"prompt", std::string("an owl")}, {"service", std::string("inflate")}});
    CHECK(!t.ok && t.message.find("service=huggingface") != std::string::npos);
}
