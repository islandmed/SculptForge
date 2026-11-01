// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Keep masked (#43): Voxel Remesh, Decimate and QRemesh leave the masked area's triangles exactly as they were.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/KeepMasked.h"
#include "core/QRemesh.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <tuple>

using namespace sf;

namespace {

using Bits = std::tuple<uint32_t, uint32_t, uint32_t>;
Bits bitsOf(const glm::vec3& p) {
    uint32_t x, y, z;
    std::memcpy(&x, &p.x, 4);
    std::memcpy(&y, &p.y, 4);
    std::memcpy(&z, &p.z, 4);
    return {x, y, z};
}

// A sphere masked where `masked` says, and the positions of the kept triangles' corners.
Document maskedSphere(const std::function<bool(const glm::vec3&)>& masked, std::set<Bits>& keptCorners) {
    Document doc;
    CommandRegistry reg;
    reg.execute(doc, "new_sphere", {{"subdivisions", 4.0}});
    auto& mask = doc.mesh.ensureMask();
    for (size_t v = 0; v < mask.size(); ++v) mask[v] = masked(doc.mesh.positions[v]) ? 1.0f : 0.0f;
    const std::vector<uint8_t> keep = keepMaskedTriangles(doc.mesh);
    for (size_t t = 0; t < keep.size(); ++t)
        if (keep[t])
            for (int c = 0; c < 3; ++c) keptCorners.insert(bitsOf(doc.mesh.positions[doc.mesh.triangles[t][c]]));
    return doc;
}

// Every kept corner is still there, bit for bit, and the mesh is closed and manifold.
bool keptAndClosed(const Mesh& m, const std::set<Bits>& keptCorners) {
    std::set<Bits> now;
    for (const auto& p : m.positions) now.insert(bitsOf(p));
    for (const Bits& b : keptCorners)
        if (!now.count(b)) return false;
    const TopologyReport r = topologyReport(m);
    return r.manifold() && r.boundaryEdges == 0;
}

} // namespace

TEST(keep_masked_triangles_need_every_corner) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CHECK(keepMaskedTriangles(doc.mesh).empty()); // no mask
    auto& mask = doc.mesh.ensureMask();
    mask[doc.mesh.triangles[0].x] = 1.0f;
    mask[doc.mesh.triangles[0].y] = 1.0f;
    const auto partly = keepMaskedTriangles(doc.mesh);
    CHECK(partly.size() == doc.mesh.triangleCount() && partly[0] == 0);
    mask[doc.mesh.triangles[0].z] = 0.6f;
    CHECK(keepMaskedTriangles(doc.mesh)[0] == 1);
}

TEST(keep_masked_voxel_remesh_cap) {
    std::set<Bits> kept;
    Document doc = maskedSphere([](const glm::vec3& p) { return p.y > 0.4f; }, kept);
    CHECK(kept.size() > 50);
    CommandRegistry reg;
    CommandResult r = reg.execute(doc, "remesh", {{"resolution", 48.0}, {"keep_masked", true}});
    CHECK(r.ok);
    if (!r.ok) std::printf("  %s\n", r.message.c_str());
    CHECK(keptAndClosed(doc.mesh, kept));
    // The rest was rebuilt: no original vertex below the cap's edge survives.
    CHECK(reg.execute(doc, "undo", {}).ok);
    std::set<Bits> rest;
    for (const auto& p : doc.mesh.positions)
        if (p.y < 0.0f) rest.insert(bitsOf(p));
    CHECK(reg.execute(doc, "redo", {}).ok);
    size_t survived = 0;
    for (const auto& p : doc.mesh.positions) survived += rest.count(bitsOf(p));
    CHECK(survived == 0);
}

TEST(keep_masked_voxel_remesh_band) {
    // A band round the middle: two edge loops to join.
    std::set<Bits> kept;
    Document doc = maskedSphere([](const glm::vec3& p) { return std::abs(p.y) < 0.35f; }, kept);
    CommandRegistry reg;
    CommandResult r = reg.execute(doc, "remesh", {{"resolution", 48.0}, {"keep_masked", true}});
    CHECK(r.ok);
    if (!r.ok) std::printf("  %s\n", r.message.c_str());
    CHECK(keptAndClosed(doc.mesh, kept));
}

TEST(keep_masked_decimate) {
    std::set<Bits> kept;
    Document doc = maskedSphere([](const glm::vec3& p) { return p.x > 0.3f; }, kept);
    const size_t before = doc.mesh.triangleCount();
    size_t keptTris = 0;
    for (uint8_t k : keepMaskedTriangles(doc.mesh)) keptTris += k;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "decimate", {{"ratio", 0.3}, {"keep_masked", true}}).ok);
    CHECK(doc.mesh.triangleCount() < before);
    CHECK(keptAndClosed(doc.mesh, kept));
    // Every kept triangle is still there.
    size_t keptAfter = 0;
    for (uint8_t k : keepMaskedTriangles(doc.mesh)) keptAfter += k;
    CHECK(keptAfter >= keptTris);
    // Without Keep masked the masked side is decimated too.
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(reg.execute(doc, "decimate", {{"ratio", 0.3}}).ok);
    std::set<Bits> now;
    for (const auto& p : doc.mesh.positions) now.insert(bitsOf(p));
    size_t missing = 0;
    for (const Bits& b : kept) missing += !now.count(b);
    CHECK(missing > 0);
}

TEST(keep_masked_qremesh) {
    std::set<Bits> kept;
    Document doc = maskedSphere([](const glm::vec3& p) { return p.z > 0.5f; }, kept);
    CommandRegistry reg;
    CommandResult r = reg.execute(doc, "qremesh", {{"target", 400.0}, {"keep_masked", true}, {"quality", false}});
    CHECK(r.ok);
    if (!r.ok) std::printf("  %s\n", r.message.c_str());
    CHECK(keptAndClosed(doc.mesh, kept));
    CHECK(topologyReport(doc.mesh).quads > 100); // the rest is quads
}

TEST(keep_masked_edge_cases) {
    CommandRegistry reg;
    // Nothing masked: an ordinary remesh.
    {
        std::set<Bits> kept;
        Document doc = maskedSphere([](const glm::vec3&) { return false; }, kept);
        CHECK(reg.execute(doc, "remesh", {{"resolution", 32.0}, {"keep_masked", true}}).ok);
        CHECK(topologyReport(doc.mesh).manifold());
    }
    // Everything masked: nothing to do, and the mesh is left as it was.
    {
        std::set<Bits> kept;
        Document doc = maskedSphere([](const glm::vec3&) { return true; }, kept);
        const size_t tris = doc.mesh.triangleCount();
        CHECK(!reg.execute(doc, "remesh", {{"resolution", 32.0}, {"keep_masked", true}}).ok);
        CHECK(!reg.execute(doc, "qremesh", {{"target", 200.0}, {"keep_masked", true}}).ok);
        CHECK(doc.mesh.triangleCount() == tris);
    }
}
