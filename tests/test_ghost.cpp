// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The predictive stroke preview (render/Ghost.h) against the brush solver it stands in for, the cavity term the
// normals pass hands the renderer (Mesh::curvatureAt), and the pen prediction (core/PenPredictor.h).
#include "TestHarness.h"

#include "core/PenPredictor.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "render/Ghost.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace sf;

namespace {

// The ghost of one dab with the frame the solver used for it (what the preview has once a dab is placed).
GhostStroke ghostOf(BrushType type, const Sculptor::DabFrame& f, const DabParams& p, Symmetry sym) {
    GhostStroke g;
    g.kind = ghostKindForBrush(int(type));
    g.invert = p.invert;
    g.focal = p.focal;
    const std::vector<glm::vec3> mirrors = Sculptor::mirrors(sym);
    for (uint32_t m = 0; m < mirrors.size(); ++m) {
        GhostDab d;
        d.center = f.center * mirrors[m];
        d.radius = p.radius;
        d.normal = f.normal * mirrors[m];
        d.strength = p.strength * p.pressure;
        d.areaCenter = f.areaCenter * mirrors[m];
        d.mirror = m;
        g.dabs.push_back(d);
    }
    return g;
}

// Largest distance between where the solver put each vertex and where the ghost draws it, and the largest move.
struct Error {
    float worst = 0.0f, moved = 0.0f;
};
Error compare(BrushType type, Mesh mesh, const DabParams& p, Symmetry sym) {
    Document doc = makeDoc(std::move(mesh));
    const std::vector<glm::vec3> before = doc.mesh.positions, normals = doc.mesh.normals;
    doc.sculptor.beginStroke(doc.mesh, "test");
    doc.sculptor.dab(doc.mesh, doc.bvh, type, p, sym);
    const GhostStroke g = ghostOf(type, doc.sculptor.lastFrame, p, sym);
    doc.sculptor.endStroke();
    Error e;
    for (size_t v = 0; v < before.size(); ++v) {
        const glm::vec3 ghost = ghostMove(g, before[v], normals[v]);
        e.worst = std::max(e.worst, glm::distance(ghost, doc.mesh.positions[v]));
        e.moved = std::max(e.moved, glm::distance(before[v], doc.mesh.positions[v]));
    }
    return e;
}

} // namespace

TEST(ghost_matches_standard_dab_on_a_plane) {
    // On a plane the area normal is the plane's: the preview is the solver's dab exactly (no jump when it lands).
    DabParams p;
    p.center = glm::vec3(0.1f, 0.0f, -0.05f);
    p.radius = 0.4f;
    p.strength = 0.7f;
    p.viewDir = glm::vec3(0.0f, -1.0f, 0.0f);
    Mesh plane = makePlane(120);
    const Error e = compare(BrushType::Standard, plane, p, Symmetry{false, false, false});
    CHECK(e.moved > 0.01f);
    CHECK(e.worst < e.moved * 1e-3f);
    // Inverted, focal shift, X symmetry: still exact.
    p.invert = true;
    p.focal = 0.4f;
    p.center.x = 0.3f;
    const Error s = compare(BrushType::Standard, plane, p, Symmetry{true, false, false});
    CHECK(s.moved > 0.005f);
    CHECK(s.worst < s.moved * 1e-3f);
}

TEST(ghost_follows_other_kernels_on_a_plane) {
    // Kernels that use only the area frame and the falloff match on a plane up to float noise; Clay and Flatten
    // pull towards a plane through the area centre, which the frame carries.
    Mesh plane = makePlane(120);
    for (BrushType t : {BrushType::Inflate, BrushType::Crease, BrushType::Pinch, BrushType::DamStandard, BrushType::Clay,
                        BrushType::Flatten, BrushType::Trim}) {
        DabParams p;
        p.center = glm::vec3(0.05f, 0.0f, 0.0f);
        p.radius = 0.35f;
        p.strength = 0.6f;
        p.viewDir = glm::vec3(0.0f, -1.0f, 0.0f);
        const Error e = compare(t, plane, p, Symmetry{false, false, false});
        // (Clay, Flatten and Trim leave a flat plane where it is: nothing to compare then.)
        CHECK(e.worst <= std::max(e.moved * 0.02f, 1e-5f));
    }
}

TEST(ghost_estimates_a_curved_surface_closely) {
    // On a sphere the preview uses the solver's frame of the dab; the vertices' own curvature makes the dab's
    // normals fan out a little where the solver's area normal doesn't: the preview stays within a tenth of the move.
    Mesh sphere = makeIcosphere(6);
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.2f, 0.3f, 1.0f));
    p.radius = 0.3f;
    p.strength = 0.8f;
    p.viewDir = -p.center;
    const Error e = compare(BrushType::Standard, sphere, p, Symmetry{true, false, false});
    CHECK(e.moved > 0.005f);
    CHECK(e.worst < e.moved * 0.1f);
}

TEST(ghost_kinds_cover_the_displacement_brushes) {
    CHECK(ghostKindForBrush(int(BrushType::Standard)) == GhostKind::Standard);
    CHECK(ghostKindForBrush(int(BrushType::Clay)) == GhostKind::Clay);
    CHECK(ghostKindForBrush(int(BrushType::ClayBuildup)) == GhostKind::ClayBuildup);
    CHECK(ghostKindForBrush(int(BrushType::Trim)) == GhostKind::Trim);
    CHECK(ghostKindForBrush(int(BrushType::Smooth)) == GhostKind::None);
    CHECK(ghostKindForBrush(int(BrushType::Layer)) == GhostKind::None);
    CHECK(ghostKindForBrush(int(BrushType::Move)) == GhostKind::None);
    CHECK(ghostKindForBrush(int(BrushType::Vdm)) == GhostKind::None);
    CHECK(ghostFalloff(0.0f, 1.0f, 0.0f) == brushFalloff(0.0f, 1.0f, 0.0f));
    for (float f : {-0.7f, 0.0f, 0.5f})
        for (float d : {0.1f, 0.5f, 0.9f}) CHECK(std::abs(ghostFalloff(d, 1.0f, f) - brushFalloff(d, 1.0f, f)) < 1e-6f);
}

TEST(curvature_from_triangles_matches_the_ring_average) {
    // Mesh::curvatureAt (the renderer's cavity term) from each vertex's triangles is the old average over its
    // ring on a closed manifold; the normals pass that writes it alongside gives the same numbers and normals.
    Mesh m = makeIcosphere(4);
    for (size_t v = 0; v < m.positions.size(); ++v)
        m.positions[v] *= 1.0f + 0.05f * std::sin(float(v) * 1.7f);
    m.recomputeAllNormals();
    float worst = 0.0f;
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        glm::vec3 avg(0.0f);
        float edge = 0.0f;
        const auto ring = m.neighbors(v);
        for (uint32_t n : ring) {
            avg += m.positions[n];
            edge += glm::distance(m.positions[n], m.positions[v]);
        }
        avg /= float(ring.size());
        edge /= float(ring.size());
        const float ringCurvature = glm::dot(m.normals[v], avg - m.positions[v]) / edge;
        worst = std::max(worst, std::abs(ringCurvature - m.curvatureAt(v)));
    }
    CHECK(worst < 1e-4f);

    Mesh a = m, b = m;
    std::vector<uint32_t> moved;
    for (uint32_t v = 0; v < a.vertexCount(); v += 7) {
        a.positions[v] *= 1.02f;
        b.positions[v] *= 1.02f;
        moved.push_back(v);
    }
    std::vector<float> curvature(a.vertexCount(), -99.0f);
    a.updateNormals(moved, curvature.data());
    b.updateNormals(moved);
    CHECK(a.normals == b.normals);
    float off = 0.0f;
    size_t written = 0;
    for (uint32_t v = 0; v < a.vertexCount(); ++v) {
        if (curvature[v] == -99.0f) continue;
        ++written;
        off = std::max(off, std::abs(curvature[v] - a.curvatureAt(v)));
    }
    CHECK(written >= moved.size());
    CHECK(off < 1e-5f);
}

TEST(pen_predictor_extrapolates_steady_motion) {
    PenPredictor pp;
    // 200 Hz samples on a straight line at 800 px/s: a frame (16 ms) ahead lands on the line, 12.8 px on.
    pp.reset(glm::vec2(100.0f, 100.0f), 0.0);
    glm::vec2 last;
    for (int i = 1; i <= 40; ++i) {
        const double t = i * 0.005;
        last = glm::vec2(100.0f + 800.0f * float(t), 100.0f);
        pp.add(last, t);
    }
    const glm::vec2 at = pp.predict(0.2 + 0.016, last);
    CHECK(std::abs(at.x - (last.x + 12.8f)) < 0.5f);
    CHECK(std::abs(at.y - 100.0f) < 0.1f);
    // Stopped: no new sample for a while predicts where it is.
    CHECK(glm::distance(pp.predict(0.2 + 0.2, last), last) < 1e-4f);
}

TEST(pen_predictor_tracks_a_circle_better_than_holding_still) {
    // The --measure-stroke circle (176 px, two turns in 3 s), sampled at 120 Hz: predicting a frame ahead must be
    // much closer to where the pointer is a frame later than the last sample is.
    PenPredictor pp;
    const float r = 176.0f;
    auto at = [&](double t) {
        const double a = t / 3.0 * 4.0 * 3.14159265358979;
        return glm::vec2(r * float(std::cos(a)), r * float(std::sin(a)));
    };
    pp.reset(at(0.0), 0.0);
    double predicted = 0.0, held = 0.0;
    int n = 0;
    for (int i = 1; i < 300; ++i) {
        const double t = i / 120.0;
        pp.add(at(t), t);
        if (i < 10) continue;
        const glm::vec2 truth = at(t + 1.0 / 120.0);
        predicted += glm::distance(pp.predict(t + 1.0 / 120.0, at(t)), truth);
        held += glm::distance(at(t), truth);
        ++n;
    }
    CHECK(predicted / n < held / n * 0.25);
}
