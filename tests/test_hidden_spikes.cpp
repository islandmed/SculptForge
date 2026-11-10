// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// fix/hidden-spikes (Big Play, Oslen): spikes after Extract / Quad Remesh + subdivision, and how Clay, Flatten and
// Smooth treat them. The spike metric: a vertex whose offset from its neighbours' mean, along its normal, is more
// than a fraction of the local edge length.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/ExtractMirror.h"
#include "core/MeshCheck.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/QRemesh.h"
#include "core/Sculpt.h"
#include "core/Voxel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

constexpr Symmetry kNoSym{false, false, false};

size_t spikes(const Mesh& m, float fraction = 0.6f) {
    size_t n = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        glm::vec3 mean(0);
        float edge = 0;
        int c = 0;
        for (uint32_t w : m.neighbors(v)) {
            mean += m.positions[w];
            edge += glm::distance(m.positions[w], m.positions[v]);
            ++c;
        }
        if (!c) continue;
        mean /= float(c);
        edge /= float(c);
        if (edge > 0 && std::abs(glm::dot(m.positions[v] - mean, m.normals[v])) > fraction * edge) ++n;
    }
    return n;
}

void run(Document& d, BrushType type, glm::vec3 center, float radius, int dabs, bool invert = false) {
    DabParams p;
    p.center = center;
    p.viewDir = -glm::normalize(center);
    p.radius = radius;
    p.strength = 0.6f;
    p.invert = invert;
    d.sculptor.beginStroke(d.mesh, brushName(type));
    for (int i = 0; i < dabs; ++i) d.sculptor.dab(d.mesh, d.bvh, type, p, kNoSym);
    d.undo.push(d.sculptor.endStroke());
    d.mesh.recomputeAllNormals();
}

void strokes(const char* label, Document& d, glm::vec3 center, float radius) {
    std::printf("%s: spikes start %zu", label, spikes(d.mesh));
    for (BrushType t : {BrushType::Clay, BrushType::Flatten, BrushType::Smooth}) {
        run(d, t, center, radius, 6);
        std::printf(", after %s %zu", brushName(t), spikes(d.mesh));
    }
    std::printf("\n"); std::fflush(stdout);
}

} // namespace

TEST(hidden_spikes_remeshed_sphere_with_levels) {
    Mesh src = makeIcosphere(4);
    QRemeshOptions o;
    o.targetQuads = 800;
    Mesh quad;
    std::string err;
    CHECK(qremesh(src, o, quad, err));
    quad.rebuildTopology();
    quad.recomputeAllNormals();
    std::printf("remesh: %s\n", checkMesh(quad).summary().c_str());
    for (int i = 0; i < 5; ++i) CHECK(subdivDivide(quad, true, err, nullptr, 0));
    quad.recomputeAllNormals();
    std::printf("levels: %s\n", checkMesh(quad).summary().c_str());
    Document d = makeDoc(std::move(quad));
    strokes("remesh+5", d, glm::vec3(0, 0, 1), 0.3f);
    CHECK(spikes(d.mesh) == 0);
}

// Extract's two layers (outer first, then inner, one vertex each per patch vertex): how many inner vertices sit at or
// above their outer partner along the outer normal (the sheet has folded through itself), and the thinnest gap.
std::pair<size_t, float> folded(const Mesh& m, float thickness) {
    const uint32_t np = uint32_t(m.vertexCount() / 2);
    size_t n = 0;
    float thin = 1e9f;
    for (uint32_t i = 0; i < np; ++i) {
        const float gap = glm::dot(m.positions[i] - m.positions[i + np], m.normals[i]);
        thin = std::min(thin, gap);
        if (gap < 0.25f * thickness) ++n;
    }
    return {n, thin};
}

TEST(hidden_spikes_extract) {
    // (wider than the cap itself, flattening it carries the near layer through the far one: not what this checks)
    for (float radius : {0.15f, 0.3f})
        for (BrushType type : {BrushType::Clay, BrushType::Flatten, BrushType::Smooth, BrushType::Standard}) {
            Mesh src = makeIcosphere(5);
            src.mask.assign(src.vertexCount(), 0.0f);
            for (uint32_t v = 0; v < src.vertexCount(); ++v)
                if (glm::normalize(src.positions[v]).z > 0.6f) src.mask[v] = 1.0f;
            ExtractOptions eo;
            std::string err;
            Mesh ex = extractShell(src, eo, err);
            CHECK(ex.vertexCount() > 0);
            const float thickness = 2 * 0.015f * 2.0f; // largest side 2
            Document d = makeDoc(std::move(ex));
            const Mesh before = d.mesh;
            // A stroke from the middle out past the rim (z = 0.6 on the unit sphere: 53 degrees).
            for (float a : {0.0f, 0.3f, 0.6f, 0.9f}) {
                const glm::vec3 c(std::sin(a), 0, std::cos(a));
                DabParams p;
                p.center = c;
                p.viewDir = -c;
                p.radius = radius;
                p.strength = 0.6f;
                d.sculptor.beginStroke(d.mesh, brushName(type));
                for (int i = 0; i < 3; ++i) d.sculptor.dab(d.mesh, d.bvh, type, p, kNoSym);
                d.undo.push(d.sculptor.endStroke());
            }
            d.mesh.recomputeAllNormals();
            size_t moved = 0;
            for (uint32_t v = 0; v < before.vertexCount(); ++v)
                if (glm::distance(before.positions[v], d.mesh.positions[v]) > 1e-5f) ++moved;
            const auto [n0, t0] = folded(before, 0.03f);
            const auto [n1, t1] = folded(d.mesh, 0.03f);
            std::printf("extract r=%.1f %-8s moved %zu, folded %zu -> %zu, thinnest gap %.4f -> %.4f, %s\n", radius,
                        brushName(type), moved, n0, n1, t0, t1, checkMesh(d.mesh).summary().c_str());
            std::fflush(stdout);
            (void)thickness;
            CHECK(n1 <= n0 + n0 / 4); // the shell keeps its thickness under every brush, as Smooth and Standard do
        }
}

// phoenix (macOS, 0.12.x): Clay breaks the topology of a fresh Voxel Remesh result. Shapes with a thin part (a horn, a
// cloth-like sheet) as well as a plain sphere and a torus, at a mid resolution.
TEST(hidden_spikes_voxel_remesh) {
    struct Shape { const char* name; Mesh mesh; };
    std::vector<Shape> shapes;
    shapes.push_back({"sphere", makeIcosphere(4)});
    shapes.push_back({"torus", makeTorus(48)});
    shapes.push_back({"cone", makeCone(48)});
    shapes.push_back({"capsule", makeCapsule(48)});
    for (Shape& sh : shapes) {
        sh.mesh.rebuildTopology();
        std::string err;
        Mesh vox = voxelRemesh(sh.mesh, 96, &err);
        CHECK(vox.vertexCount() > 0);
        vox.rebuildTopology();
        vox.recomputeAllNormals();
        Document d = makeDoc(std::move(vox));
        const size_t s0 = spikes(d.mesh);
        std::printf("voxel %-8s %zu verts, %s, spikes %zu", sh.name, d.mesh.vertexCount(), checkMesh(d.mesh).summary().c_str(), s0);
        for (BrushType t : {BrushType::Clay, BrushType::Flatten, BrushType::Smooth, BrushType::Clay}) {
            for (float a : {0.0f, 0.4f, 0.8f}) run(d, t, glm::normalize(glm::vec3(std::sin(a), 0.2f, std::cos(a))) * 0.9f, 0.25f, 3);
            std::printf(", %s %zu", brushName(t), spikes(d.mesh));
        }
        std::printf(", after: %s\n", checkMesh(d.mesh).summary().c_str());
        std::fflush(stdout);
        CHECK(spikes(d.mesh) <= s0 + s0 / 4 + 4);
    }
}
