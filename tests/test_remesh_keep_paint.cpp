// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tool > Remesh: Voxel Remesh keeps the Part's paint (member report, 0.14.2). Vertex paint (plain, in paint layers, with
// Keep Masked, with Keep Face Groups, on the sparse high-resolution path) follows the closest point of the old surface;
// the texture with its material channels follows with Keep Texture; the Part's shading and the scene's material (Lit
// Sphere) are not touched by a remesh. Measured against a smooth colour field over space.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/PaintLayer.h"
#include "core/Primitives.h"
#include "core/VertexPaint.h"
#include "core/Voxel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

glm::vec3 field(const glm::vec3& p) {
    return glm::vec3(0.5f + 0.4f * std::sin(3.0f * p.x + 0.5f), 0.5f + 0.4f * std::cos(2.5f * p.y),
                     0.5f + 0.4f * std::sin(2.0f * p.z + 1.0f));
}

void paintField(Mesh& m) {
    m.colors.resize(m.positions.size());
    for (size_t v = 0; v < m.positions.size(); ++v) m.colors[v] = field(m.positions[v]);
}

struct Error {
    float mean = 0.0f, worst = 0.0f;
};
// How far each vertex colour is from the field at its own position (a vertex sits within a voxel of the old surface).
Error colourError(const Mesh& m) {
    Error e;
    if (m.colors.size() != m.positions.size() || m.positions.empty()) {
        e.mean = e.worst = 1e9f;
        return e;
    }
    double sum = 0.0;
    for (size_t v = 0; v < m.positions.size(); ++v) {
        const float d = glm::length(m.colors[v] - field(m.positions[v]));
        sum += d;
        e.worst = std::max(e.worst, d);
    }
    e.mean = float(sum / double(m.positions.size()));
    return e;
}

Document paintedSphere() {
    Mesh m = makeIcosphere(5);
    paintField(m);
    return makeDoc(std::move(m));
}

} // namespace

TEST(remesh_vertex_paint_invariant_follows_the_surface) {
    CommandRegistry reg;
    Document doc = paintedSphere();
    const size_t before = doc.mesh.triangleCount();
    const CommandResult r = reg.execute(doc, "remesh", {{"resolution", 96.0}});
    CHECK(r.ok);
    CHECK(doc.mesh.triangleCount() != before);
    CHECK(doc.mesh.hasColors());
    const Error e = colourError(doc.mesh);
    std::printf("  vertex paint after remesh: mean %.4f worst %.4f\n", e.mean, e.worst);
    CHECK(e.mean < 0.03f);
    CHECK(e.worst < 0.12f);
    doc.undo.undo(doc.mesh, doc.bvh); // one undo step brings the old paint back
    CHECK(doc.mesh.triangleCount() == before);
    CHECK(colourError(doc.mesh).worst < 1e-5f);
}

TEST(remesh_vertex_paint_invariant_with_paint_layers) {
    CommandRegistry reg;
    Document doc = paintedSphere();
    const int layer = addPaintLayer(doc.mesh, "Top");
    CHECK(layer >= 0);
    for (uint32_t v = 0; v < doc.mesh.positions.size(); ++v)
        if (doc.mesh.positions[v].y > 0.3f) paintVertex(doc.mesh, v, glm::vec3(0.9f, 0.1f, 0.1f), 1.0f);
    compositePaintAll(doc.mesh);
    const std::vector<glm::vec3> seen = doc.mesh.colors; // the composited look, as the viewport shows it
    const Mesh old = doc.mesh;
    CHECK(reg.execute(doc, "remesh", {{"resolution", 96.0}}).ok);
    CHECK(doc.mesh.hasColors());
    CHECK(doc.mesh.colors.size() == doc.mesh.positions.size());
    // The look stays: a new vertex near the cap is red, one near the equator keeps the field.
    size_t red = 0, redExpected = 0;
    for (size_t v = 0; v < doc.mesh.positions.size(); ++v) {
        const glm::vec3 p = doc.mesh.positions[v];
        if (p.y > 0.45f) {
            ++redExpected;
            red += glm::length(doc.mesh.colors[v] - glm::vec3(0.9f, 0.1f, 0.1f)) < 0.05f;
        }
        if (p.y < 0.1f) CHECK(glm::length(doc.mesh.colors[v] - field(p)) < 0.15f);
    }
    std::printf("  paint layers after remesh: %zu of %zu cap vertices red\n", red, redExpected);
    CHECK(redExpected > 0 && red == redExpected);
    (void)seen;
    (void)old;
}

TEST(remesh_vertex_paint_invariant_with_keep_masked_and_groups) {
    CommandRegistry reg;
    Document doc = paintedSphere();
    doc.mesh.mask.assign(doc.mesh.positions.size(), 0.0f);
    for (size_t v = 0; v < doc.mesh.positions.size(); ++v)
        if (doc.mesh.positions[v].x > 0.4f) doc.mesh.mask[v] = 1.0f;
    doc.mesh.ensureFaceGroups(0u);
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t) doc.mesh.facegroups[t] = doc.mesh.positions[doc.mesh.triangles[t].x].z > 0 ? 1u : 2u;
    CHECK(reg.execute(doc, "remesh", {{"resolution", 96.0}, {"keep_masked", true}, {"keep_groups", true}}).ok);
    const Error e = colourError(doc.mesh);
    std::printf("  vertex paint after remesh (Keep Masked, Keep Face Groups): mean %.4f worst %.4f\n", e.mean, e.worst);
    CHECK(e.mean < 0.03f);
    CHECK(e.worst < 0.12f);
}

TEST(remesh_vertex_paint_invariant_on_the_sparse_path) {
    CommandRegistry reg;
    Document doc = paintedSphere();
    CHECK(reg.execute(doc, "remesh", {{"resolution", 640.0}}).ok);
    const Error e = colourError(doc.mesh);
    std::printf("  vertex paint after remesh (resolution 640): mean %.4f worst %.4f, %zu triangles\n", e.mean, e.worst,
                doc.mesh.triangleCount());
    CHECK(e.mean < 0.02f);
    CHECK(e.worst < 0.1f);
}

TEST(remesh_leaves_the_part_shading_alone) {
    CommandRegistry reg;
    Document doc = paintedSphere();
    doc.parts[size_t(doc.activePart)].shading = Shading::Smooth;
    const Shading shading = doc.parts[size_t(doc.activePart)].shading;
    CHECK(reg.execute(doc, "remesh", {{"resolution", 64.0}}).ok);
    CHECK(doc.parts[size_t(doc.activePart)].shading == shading);
}

TEST(remesh_vertex_paint_invariant_smooth_from_a_coarse_source) {
    // A 1280-triangle Part remeshed much denser: the nearest-vertex copy left blocks the size of the old triangles
    // ; the interpolated copy follows the field within about a percent.
    CommandRegistry reg;
    Mesh m = makeIcosphere(3);
    paintField(m);
    Document doc = makeDoc(std::move(m));
    Mesh old = doc.mesh;
    const CommandResult r = reg.execute(doc, "remesh", {{"resolution", 160.0}});
    CHECK(r.ok);
    CHECK(doc.mesh.positions.size() > 4 * old.positions.size());
    const Error e = colourError(doc.mesh);
    std::printf("  coarse source, dense result: mean %.4f worst %.4f\n", e.mean, e.worst);
    CHECK(e.mean < 0.012f);
    CHECK(e.worst < 0.06f);
    // Interpolation never leaves the old paint's range.
    glm::vec3 lo(1.0f), hi(0.0f);
    for (const glm::vec3& c : old.colors) lo = glm::min(lo, c), hi = glm::max(hi, c);
    for (const glm::vec3& c : doc.mesh.colors) CHECK(glm::all(glm::greaterThanEqual(c, lo - 1e-4f)) && glm::all(glm::lessThanEqual(c, hi + 1e-4f)));
    // The command says nothing about paint when the result keeps at least as many vertices.
    CHECK(r.message.find("vertex paint now sits") == std::string::npos);
}

TEST(remesh_says_when_vertex_paint_is_resampled_coarser) {
    CommandRegistry reg;
    Document doc = paintedSphere(); // 10k vertices
    const CommandResult r = reg.execute(doc, "remesh", {{"resolution", 32.0}});
    CHECK(r.ok);
    CHECK(r.message.find("vertex paint now sits on") != std::string::npos);
    CHECK(r.message.find("raise the Resolution") != std::string::npos);
    Document plain = makeDoc(makeIcosphere(5)); // no paint: no note
    CHECK(reg.execute(plain, "remesh", {{"resolution", 32.0}}).message.find("vertex paint") == std::string::npos);
}

TEST(remesh_vertex_paint_invariant_never_changes_the_shape) {
    // Carrying paint is only colour: the remeshed surface is bit for bit the one a Part without paint gets.
    CommandRegistry reg;
    Document painted = paintedSphere();
    Document plain = makeDoc(makeIcosphere(5));
    CHECK(reg.execute(painted, "remesh", {{"resolution", 96.0}}).ok);
    CHECK(reg.execute(plain, "remesh", {{"resolution", 96.0}}).ok);
    CHECK(painted.mesh.positions.size() == plain.mesh.positions.size());
    CHECK(painted.mesh.triangles.size() == plain.mesh.triangles.size());
    size_t moved = 0;
    for (size_t v = 0; v < painted.mesh.positions.size() && v < plain.mesh.positions.size(); ++v)
        moved += painted.mesh.positions[v] != plain.mesh.positions[v];
    CHECK(moved == 0);
}

TEST(remesh_estimate_counts_the_paint_tree_only_when_paint_is_interpolated) {
    // The interpolated copy builds a triangle tree over the old surface (80 bytes a triangle at its peak, measured on 21M
    // triangles: +79% of a 2 GB remesh). The estimate counts it when the result is denser than the Part; a Part denser
    // than its result keeps the nearest-vertex copy, which needs no tree, and the estimate stays as it was.
    Mesh coarse = makeIcosphere(3);
    Mesh dense = makeIcosphere(6);
    const VoxelRemeshEstimate plainCoarse = estimateVoxelRemesh(coarse, 160);
    paintField(coarse);
    const VoxelRemeshEstimate paintedCoarse = estimateVoxelRemesh(coarse, 160);
    CHECK(paintedCoarse.triangles > coarse.triangleCount());
    CHECK(paintedCoarse.bytes == plainCoarse.bytes + size_t(double(coarse.triangleCount()) * kVoxelPaintBvhBytesPerTriangle));
    const VoxelRemeshEstimate plainDense = estimateVoxelRemesh(dense, 32);
    paintField(dense);
    const VoxelRemeshEstimate paintedDense = estimateVoxelRemesh(dense, 32);
    CHECK(paintedDense.triangles < dense.triangleCount());
    CHECK(paintedDense.bytes == plainDense.bytes);
    CHECK(remeshInterpolatesPaint(coarse, paintedCoarse.triangles));
    CHECK(!remeshInterpolatesPaint(dense, paintedDense.triangles));
}

TEST(remesh_vertex_paint_invariant_dense_part_keeps_each_vertex_colour_exactly) {
    // A Part denser than its result: each new vertex takes an old vertex's colour as it was (no blending, no tree).
    CommandRegistry reg;
    Document doc = paintedSphere();
    const Mesh old = doc.mesh;
    CHECK(reg.execute(doc, "remesh", {{"resolution", 32.0}}).ok);
    CHECK(doc.mesh.triangleCount() < old.triangleCount());
    size_t notFromOld = 0;
    for (const glm::vec3& c : doc.mesh.colors) {
        bool found = false;
        for (const glm::vec3& o : old.colors)
            if (c == o) { found = true; break; }
        notFromOld += !found;
        if (notFromOld > 0) break;
    }
    CHECK(notFromOld == 0);
}
