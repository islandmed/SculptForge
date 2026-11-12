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
        d.up = p.alphaUp * mirrors[m]; // (the square tip of Clay Block)
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
    // Inverted, falloff, X symmetry: still exact.
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
    for (BrushType t : {BrushType::Inflate, BrushType::Crease, BrushType::Pinch, BrushType::SharpCrease, BrushType::Clay,
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

TEST(ghost_matches_solver_per_vertex_at_every_intensity) {
    // GEOMETRIC INVARIANT (11-12): for every brush the preview models, the vertex the ghost draws is the vertex the
    // solver places, at Intensity 0.1 / 0.35 / 1.0, with and without pen pressure, on a bumpy seeded sphere (a bump
    // puts surface on both sides of every plane brush's plane). Clay and Clay Block drew 1.43x too much at 0.35
    // (the preview skipped clayPull). Clay Block's square tip is measured in the brush's frame, as the solver does.
    Mesh bumpy = makeIcosphere(6);
    for (size_t v = 0; v < bumpy.positions.size(); ++v) {
        const glm::vec3 q = bumpy.positions[v];
        bumpy.positions[v] *= 1.0f + 0.03f * std::sin(9.0f * q.x + 2.0f * q.y) * std::cos(7.0f * q.z - q.x);
    }
    bumpy.recomputeAllNormals();
    struct Case { BrushType type; const char* name; };
    const Case cases[] = {{BrushType::Standard, "Standard"}, {BrushType::Inflate, "Inflate"}, {BrushType::Clay, "Clay"},
                          {BrushType::ClayBlock, "Clay Block"}, {BrushType::Flatten, "Flatten"},
                          {BrushType::Crease, "Crease"}, {BrushType::Pinch, "Pinch"},
                          {BrushType::SharpCrease, "Sharp Crease"}, {BrushType::Trim, "Trim"}};
    for (const Case& c : cases) {
        for (float intensity : {0.1f, 0.35f, 1.0f}) {
            for (float pressure : {1.0f, 0.6f}) {
                DabParams p;
                p.center = glm::normalize(glm::vec3(0.25f, 0.35f, 1.0f)) * 1.01f;
                p.radius = 0.35f;
                p.strength = intensity;
                p.pressure = pressure;
                p.viewDir = glm::vec3(0.0f, 0.0f, -1.0f);
                p.alphaUp = glm::normalize(glm::vec3(0.6f, 0.8f, 0.1f)); // (a turned square tip: Clay Block)
                Document doc = makeDoc(bumpy);
                const std::vector<glm::vec3> before = doc.mesh.positions, normals = doc.mesh.normals;
                doc.sculptor.beginStroke(doc.mesh, "test");
                doc.sculptor.dab(doc.mesh, doc.bvh, c.type, p, Symmetry{false, false, false});
                const GhostStroke g = ghostOf(c.type, doc.sculptor.lastFrame, p, Symmetry{false, false, false});
                doc.sculptor.endStroke();
                float worst = 0.0f, moved = 0.0f;
                for (size_t v = 0; v < before.size(); ++v) {
                    const float dv = glm::distance(ghostMove(g, before[v], normals[v]), doc.mesh.positions[v]);
                    worst = std::max(worst, dv);
                    moved = std::max(moved, glm::distance(before[v], doc.mesh.positions[v]));
                }
                const bool ok = worst <= std::max(moved * 0.01f, 1e-6f);
                if (!ok)
                    std::printf("ghost vs solver: %s intensity %.2f pressure %.1f: worst %.6f of a move up to %.6f\n", c.name,
                                intensity, pressure, worst, moved);
                CHECK(ok);
            }
        }
    }
}

TEST(ghost_matches_a_stroke_of_overlapping_dabs) {
    // The preview of several dabs the solver hasn't placed yet (each with the frame it will use) is the solver's result
    // for them in turn: the composition holds for the plane brushes, Clay Block's slabs among them.
    Mesh bumpy = makeIcosphere(6);
    for (size_t v = 0; v < bumpy.positions.size(); ++v) {
        const glm::vec3 q = bumpy.positions[v];
        bumpy.positions[v] *= 1.0f + 0.03f * std::sin(9.0f * q.x + 2.0f * q.y) * std::cos(7.0f * q.z - q.x);
    }
    bumpy.recomputeAllNormals();
    for (BrushType t : {BrushType::Clay, BrushType::ClayBlock}) {
        Document doc = makeDoc(bumpy);
        const std::vector<glm::vec3> before = doc.mesh.positions, normals = doc.mesh.normals;
        GhostStroke g;
        g.kind = ghostKindForBrush(int(t));
        doc.sculptor.beginStroke(doc.mesh, "test");
        for (int i = 0; i < 8; ++i) {
            DabParams p;
            const float a = 0.5f + 0.12f * float(i);
            p.center = glm::vec3(std::sin(a), 0.2f, std::cos(a)) * 1.01f;
            p.center = glm::normalize(p.center) * 1.01f;
            p.radius = 0.3f;
            p.strength = 0.35f;
            p.viewDir = -glm::normalize(p.center);
            p.alphaUp = glm::vec3(0.0f, 1.0f, 0.0f);
            doc.sculptor.dab(doc.mesh, doc.bvh, t, p, Symmetry{false, false, false});
            const GhostStroke one = ghostOf(t, doc.sculptor.lastFrame, p, Symmetry{false, false, false});
            g.dabs.push_back(one.dabs[0]);
        }
        doc.sculptor.endStroke();
        float worst = 0.0f, moved = 0.0f;
        for (size_t v = 0; v < before.size(); ++v) {
            worst = std::max(worst, glm::distance(ghostMove(g, before[v], normals[v]), doc.mesh.positions[v]));
            moved = std::max(moved, glm::distance(before[v], doc.mesh.positions[v]));
        }
        std::printf("ghost stroke vs solver: brush %d worst %.6f of a move up to %.6f\n", int(t), worst, moved);
        CHECK(moved > 0.005f);
        CHECK(worst <= moved * 0.02f);
    }
}

TEST(ghost_kinds_cover_the_displacement_brushes) {
    CHECK(ghostKindForBrush(int(BrushType::Standard)) == GhostKind::Standard);
    CHECK(ghostKindForBrush(int(BrushType::Clay)) == GhostKind::Clay);
    CHECK(ghostKindForBrush(int(BrushType::ClayBlock)) == GhostKind::ClayBlock);
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

TEST(live_move_matches_the_grab) {
    // Live Move (SF_LIVE_MOVE, research prototype): the preview of a whole Move drag (GhostKind::Move, from the
    // grab's start positions) is where the grab puts every vertex, and one grab with the whole move lands where a
    // grab per frame does (up to float rounding), with X symmetry.
    Mesh sphere = makeIcosphere(6);
    DabParams p;
    p.center = glm::normalize(glm::vec3(0.3f, 0.2f, 1.0f));
    p.radius = 0.45f;
    p.strength = 0.4f;
    p.focal = 0.3f;
    p.viewDir = -p.center;
    const Symmetry sym{true, false, false};
    const std::vector<glm::vec3> steps = {{0.01f, 0.02f, 0.0f}, {0.03f, -0.01f, 0.02f}, {0.02f, 0.02f, 0.01f}};
    glm::vec3 total(0.0f);
    for (const glm::vec3& s : steps) total += s;

    Document framed = makeDoc(sphere), once = makeDoc(sphere);
    const std::vector<glm::vec3> before = framed.mesh.positions, normals = framed.mesh.normals;
    framed.sculptor.beginStroke(framed.mesh, "test");
    framed.sculptor.beginGrab(framed.mesh, framed.bvh, p, sym);
    for (const glm::vec3& s : steps) framed.sculptor.grab(framed.mesh, framed.bvh, s);
    framed.sculptor.endStroke();
    once.sculptor.beginStroke(once.mesh, "test");
    once.sculptor.beginGrab(once.mesh, once.bvh, p, sym);
    once.sculptor.grab(once.mesh, once.bvh, total);
    once.sculptor.endStroke();

    GhostStroke g;
    g.kind = GhostKind::Move;
    g.focal = p.focal;
    const std::vector<SymCopy> copies = Sculptor::copies(sym);
    for (uint32_t m = 0; m < copies.size(); ++m) {
        GhostDab d;
        d.center = copies[m].point(p.center);
        d.radius = p.radius;
        d.normal = copies[m].m * total;
        d.strength = std::clamp(p.strength * 2.0f, 0.0f, 1.0f);
        d.mirror = m;
        g.dabs.push_back(d);
    }
    float moved = 0.0f, single = 0.0f, ghost = 0.0f;
    for (size_t v = 0; v < before.size(); ++v) {
        moved = std::max(moved, glm::distance(before[v], framed.mesh.positions[v]));
        single = std::max(single, glm::distance(once.mesh.positions[v], framed.mesh.positions[v]));
        ghost = std::max(ghost, glm::distance(ghostMove(g, before[v], normals[v]), framed.mesh.positions[v]));
    }
    CHECK(moved > 0.02f);
    CHECK(single < 1e-6f);
    CHECK(ghost < 1e-5f);
}
